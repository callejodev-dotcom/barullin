#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Saca las ideas de Barullín al ordenador: un MIDI por idea y un proyecto de Ableton Live con una escena por idea.

    python3 tools/barullin_export.py                 lee las ideas del FM-1 por USB (Barullín encendido, parado)
    python3 tools/barullin_export.py --desde FILE    o de una copia de seguridad ya hecha (FILE.barullin)
    opciones:  --salida CARPETA (de serie: "Barullin Export")   --vueltas N (cuántas veces se repite cada idea en el
               MIDI, de serie 1)   --port NOMBRE (el puerto MIDI del FM-1)

Lo que sale, en la carpeta:
    Barullin.barullin        la copia de seguridad (la misma que base/tools/fm1_backup.py restore devuelve al FM-1)
    Idea 1 - ... .mid        cada idea en su MIDI (tipo 1): ACORDES, BAJO, ARPEGIO y BATERIA, cada uno en su pista y su
                             canal (2, 3, 4 y 10, como los manda Barullín), con su tempo y su compás
    Idea N - ... Project/    un proyecto de Ableton Live (12) por idea: una pista por parte, la idea en la primera
                             escena, con su tempo y su compás. Las pistas van sin instrumento: arrastra el que quieras.
                             (--todas: además, uno con todas, una escena por idea)

Lo que se calcula es lo que toca el FM-1 con PLAY: los acordes (con el mismo motor de acordes, app/core/chords.c,
compilado aquí con el compilador del ordenador), el bajo (sostenido o pulsado), el arpegiador y el ritmo, con los
ajustes que cada idea guardó con SAVE. El rasgueo es en directo y no se guarda.

Necesita mido y python-rtmidi solo para leer del FM-1 (como base/tools/fm1_install.py), y un compilador de C (cc).
"""
import argparse
import copy
import ctypes
import gzip
import os
import shutil
import re
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BASE_TOOLS = os.path.join(ROOT, "base", "tools")

# ---------------------------------------------------------------- el formato de las ideas (app/app.c ideas_t)
IDEAS_MAGIC = b"BRID"
IDEAS_VERSION = 1
NBANK, LOOP_MAX, II_N = 8, 64, 32
OBJ_IDEAS = 3                                     # app/app.h: OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_IDEAS
BACKUP_MAGIC = b"BRLN"
(II_KEY, II_MODE, II_OCTAVE, II_TEMPO, II_METER, II_LEAD, II_SOUND, II_VOL_PAD, II_VOL_BASS, II_VOL_ARP,
 II_ARP_MODE, II_ARP_DIV, II_ARP_OCT, II_ARP_LEN, II_ARP_FREE, II_BP_ON, II_BP_DIV, II_BP_PAT, II_BP_LEN, II_BP_OCT,
 II_DRM_KICK, II_DRM_SNARE, II_DRM_HAT, II_DRM_HOPEN, II_DRM_MUTE) = range(25)

# los compases (app/app.c METER): nombre, corcheas por compás, corcheas por pulso, grupos del arpegio en corcheas y en
# semicorcheas, las corcheas donde empieza un pulso (bits), y el compás para el MIDI y Ableton (numerador, denominador)
METER = [
    ("4/4", 8, 2, [4, 4], [4, 4, 4, 4], 0x55, (4, 4)),
    ("3/4", 6, 2, [3, 3], [4, 4, 4], 0x15, (3, 4)),
    ("6/8", 6, 3, [3, 3], [3, 3, 3, 3], 0x09, (6, 8)),
    ("5/4", 10, 2, [4, 3, 3], [4, 4, 4, 4, 4], 0x155, (5, 4)),
    ("7/8", 7, 1, [4, 3], [4, 4, 3, 3], 0x15, (7, 8)),
]
MODE_NAME = ["MAYOR", "DORICO", "FRIGIO", "LIDIO", "MIXOLIDIO", "MENOR", "LOCRIO"]
VOL_MAX = 20


def parse_backup(data):
    """los objetos de una copia de seguridad (base/tools/fm1_backup.py: el formato): {id: bytes}"""
    if len(data) < 20:
        raise SystemExit("la copia de seguridad está vacía o rota")
    magic, version, count, flags, hcrc = struct.unpack_from("<4sHHII", data, 0)
    if magic != BACKUP_MAGIC:
        raise SystemExit(f"no es una copia de Barullín (magic {magic!r})")
    if zlib.crc32(data[:12]) & 0xFFFFFFFF != hcrc:
        raise SystemExit("la copia de seguridad está dañada (la cabecera)")
    objs, p = {}, 16
    for _ in range(count):
        oid, n, crc = struct.unpack_from("<HHI", data, p)
        p += 8
        payload = data[p:p + n]
        p += n
        if zlib.crc32(payload) & 0xFFFFFFFF != crc:
            raise SystemExit(f"la copia de seguridad está dañada (el objeto {oid})")
        objs[oid] = payload
    return objs


def parse_ideas(b):
    """las ideas (app/app.c ideas_t): lista de 8 (None si el banco está vacío) con 'events', 'len' y 'info'"""
    if len(b) < 8 or b[:4] != IDEAS_MAGIC:
        raise SystemExit("la copia no trae ideas (¿guardaste con SAVE en esta versión de Barullín?)")
    version, nbank = b[4], b[5]
    if version != IDEAS_VERSION or nbank != NBANK:
        raise SystemExit(f"las ideas vienen en otro formato (versión {version}): actualiza este programa")
    out, p, size = [], 8, 2 + LOOP_MAX * 4 + II_N
    for _ in range(NBANK):
        rec = b[p:p + size]
        p += size
        length, n = rec[0], rec[1]
        evs = []
        for k in range(min(n, LOOP_MAX)):
            step, degree, mods, inv = struct.unpack_from("<BbBB", rec, 2 + 4 * k)
            evs.append((step, degree, mods, inv))
        info = list(rec[2 + LOOP_MAX * 4:2 + LOOP_MAX * 4 + II_N])
        out.append({"len": length, "events": evs, "info": info} if length and evs else None)
    return out


# ---------------------------------------------------------------- el motor de acordes, el mismo del FM-1
class BlChord(ctypes.Structure):
    _fields_ = [("n", ctypes.c_uint8), ("notes", ctypes.c_uint8 * 6), ("bass", ctypes.c_uint8),
                ("root", ctypes.c_uint8), ("name", ctypes.c_char * 16)]


def load_chords():
    src = os.path.join(ROOT, "app", "core", "chords.c")
    ext = ".dylib" if sys.platform == "darwin" else ".so"
    lib = os.path.join(tempfile.gettempdir(), "barullin_chords" + ext)
    if not os.path.exists(lib) or os.path.getmtime(lib) < os.path.getmtime(src):
        cc = os.environ.get("CC", "cc")
        r = subprocess.run([cc, "-O2", "-shared", "-fPIC", "-o", lib, src], capture_output=True, text=True)
        if r.returncode:
            raise SystemExit("no pude compilar el motor de acordes (hace falta cc: en el Mac, "
                             "xcode-select --install)\n" + r.stderr)
    L = ctypes.CDLL(lib)
    L.bl_chord_build.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_int, ctypes.c_int,
                                 ctypes.POINTER(BlChord)]
    L.bl_chord_lead.argtypes = [ctypes.POINTER(BlChord), ctypes.POINTER(BlChord), ctypes.c_int, ctypes.c_int]
    L.bl_note_name.restype = ctypes.c_char_p
    L.bl_degree_name.argtypes = [ctypes.POINTER(BlChord), ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    return L


# ---------------------------------------------------------------- lo que toca cada idea (app/app.c, en Python)
def render_idea(idea, L, loops=1):
    """las notas de cada parte: {'ACORDES': [(inicio, duración, nota, velocidad)], ...}, en pulsos (negras)"""
    inf = idea["info"]
    key, mode, octave = inf[II_KEY], inf[II_MODE], (inf[II_OCTAVE] ^ 0x80) - 0x80
    meter = METER[inf[II_METER] % len(METER)]
    length8 = idea["len"]
    evs = sorted(idea["events"])
    base = 60 + 12 * octave
    total8 = length8 * loops

    # los acordes de dos vueltas (la segunda, con el voice leading de la idea sonando en bucle)
    def build(prev, ev):
        _, degree, mods, inv = ev
        c = BlChord()
        L.bl_chord_build(key, mode, degree, mods, inv, base, ctypes.byref(c))
        if inf[II_LEAD] and inv == 0 and prev is not None:
            L.bl_chord_lead(ctypes.byref(prev), ctypes.byref(c), base - 8, base + 19)
        return c
    prev = None
    for ev in evs:
        prev = build(prev, ev)
    chords = []
    for ev in evs:
        prev = build(prev, ev)
        chords.append(prev)
    spans = []                                    # (desde, hasta) en corcheas, dentro de una vuelta
    for i, ev in enumerate(evs):
        start = ev[0] if i else 0                 # (la primera idea suena desde el principio)
        end = evs[i + 1][0] if i + 1 < len(evs) else length8
        spans.append((start, end))

    def chord_at(t8):                             # el acorde que suena en la corchea t8 (de toda la idea)
        t = t8 % length8
        for i, (a, b) in enumerate(spans):
            if a <= t < b:
                return chords[i]
        return chords[-1]

    parts = {"ACORDES": [], "BAJO": [], "ARPEGIO": [], "BATERIA": []}
    mudas = set()
    vel = lambda v: max(1, min(127, round(40 + 87 * v / 16)))   # el volumen de la capa, a velocidad
    for lp in range(loops):
        for i, (a, b) in enumerate(spans):
            c = chords[i]
            t0, d = (lp * length8 + a) / 2.0, (b - a) / 2.0
            for k in range(c.n):
                parts["ACORDES"].append((t0, d, c.notes[k], vel(inf[II_VOL_PAD] or 12)))
            if not inf[II_BP_ON]:
                parts["BAJO"].append((t0, d, c.bass, vel(inf[II_VOL_BASS] or 12)))

    # el bajo pulsado (bp_fire): pasos de 1, 2, 3 o 4 por pulso
    if inf[II_BP_ON]:
        pb = [1, 2, 3, 4][inf[II_BP_DIV] % 4]
        step = 1.0 / pb
        for idx in range(int(total8 / 2 * pb)):
            t = idx * step
            c = chord_at(int(t * 2))
            note = c.bass
            if inf[II_BP_PAT] == 3:               # acorde: sus notas en el grave, de 4 en 4 (3 en tresillos)
                iv = sorted({(c.notes[k] - c.bass) % 12 for k in range(c.n)})
                G = 3 if inf[II_BP_DIV] == 2 else 4
                pos = idx % G
                if iv:
                    note += iv[pos % len(iv)] + 12 * (pos // len(iv))
            elif idx & 1:
                note += 7 if inf[II_BP_PAT] == 1 else 12 if inf[II_BP_PAT] == 2 else 0
            note += 12 * ((inf[II_BP_OCT] ^ 0x80) - 0x80)
            parts["BAJO"].append((t, step * inf[II_BP_LEN] / 10.0, max(0, min(127, note)), vel(inf[II_VOL_BASS] or 12)))

    # el arpegiador (arp_fire): en el compás (arp_note_bar) o libre (arp_note), con su azar
    # (todas las partes salen siempre: lo que en el FM-1 estaba a 0 o apagado va con un sonido de partida, y su
    # pista de Ableton, sin volumen: ahí está si se quiere meter)
    if True:
        pb = [2, 3, 4][inf[II_ARP_DIV] % 3]
        step = 1.0 / pb
        rng = [0x1234567]
        mode_a, octs = inf[II_ARP_MODE], max(1, inf[II_ARP_OCT])
        alen = inf[II_ARP_LEN] or 5
        arp_i, last = 0, None

        def rnd():
            rng[0] = (rng[0] * 1664525 + 1013904223) & 0xFFFFFFFF
            return rng[0] >> 16

        for idx in range(int(total8 / 2 * pb)):
            t = idx * step
            c = chord_at(int(t * 2))
            if c is not last:
                arp_i, last = 0, c                # acorde nuevo: el patrón desde el principio
            n = c.n
            if inf[II_ARP_FREE]:
                ln = n * octs
                if mode_a == 1:
                    k = ln - 1 - arp_i % ln
                elif mode_a == 2:
                    per = 2 * ln - 2 if ln > 1 else 1
                    k = arp_i % per
                    if k >= ln:
                        k = per - k
                elif mode_a == 3:
                    k = rnd() % ln
                else:
                    k = arp_i % ln
                note = c.notes[k % n] + 12 * (k // n)
            else:
                if inf[II_ARP_DIV] == 1:
                    G, pos, oc = 3, idx % 3, (idx // 3) % octs
                else:
                    six = inf[II_ARP_DIV] == 2
                    gs = meter[4] if six else meter[3]
                    bar = 2 * meter[1] if six else meter[1]
                    s_in, g = idx % bar, 0
                    while g < len(gs) - 1 and s_in >= gs[g]:
                        s_in -= gs[g]
                        g += 1
                    G, pos, oc = gs[g], s_in, ((idx // bar) * len(gs) + g) % octs
                if mode_a == 1:
                    k = G - 1 - pos
                elif mode_a == 2:
                    k = [0, 1, 2, 1][pos] if G == 4 else [0, 2, 1][pos]
                elif mode_a == 3:
                    k = rnd() % n
                else:
                    k = pos
                note = c.notes[k % n] + 12 * (k // n) + 12 * oc
            arp_i += 1
            parts["ARPEGIO"].append((t, step * alen / 10.0, max(0, min(127, note)), vel(inf[II_VOL_ARP] or 12)))

    # el ritmo (drum_step): bombo en el pulso, caja en el 2 y el 4, charles a corcheas
    if True:
        dm = inf[II_DRM_MUTE]
        kick = inf[II_DRM_KICK] if inf[II_DRM_KICK] and not dm else 0
        snare = inf[II_DRM_SNARE] if inf[II_DRM_SNARE] and not dm else 0
        hatv = inf[II_DRM_HAT] if inf[II_DRM_HAT] and not dm else 0
        if not kick:
            mudas.add("KICK")
        if not snare and not hatv:
            mudas.add("CAJAS")
        if not (kick or snare or hatv):           # todo apagado: el ritmo de partida entero
            kick, snare, hatv = 12, 12, 8
        elif not snare and not hatv:
            snare, hatv = 12, 8
        elif not kick:
            kick = 12
        bar8, beats = meter[1], meter[5]
        hat = 46 if inf[II_DRM_HOPEN] >= 5 else 42  # abierto o cerrado
        for s8 in range(total8):
            s = s8 % bar8
            beat = beats >> s & 1
            nbeat = sum(beats >> k & 1 for k in range(s))
            t = s8 / 2.0
            if beat and kick:
                parts["BATERIA"].append((t, 0.25, 36, vel(kick)))
            if beat and nbeat & 1 and snare:
                parts["BATERIA"].append((t, 0.25, 38, vel(snare)))
            if hatv:
                parts["BATERIA"].append((t, 0.25, hat, max(1, int(vel(hatv) * (1.0 if beat else 0.8)))))
    if not inf[II_VOL_PAD]:
        mudas.add("ACORDES")
    if not inf[II_VOL_BASS]:
        mudas.add("BAJO")
    if not inf[II_VOL_ARP]:
        mudas.add("ARPEGIO")
    parts["MUDAS"] = sorted(mudas)                # (las que en el FM-1 no sonaban: en Ableton, sin volumen)
    return parts, total8 / 2.0


def idea_title(i, idea, L):
    inf = idea["info"]
    key = L.bl_note_name(inf[II_KEY], inf[II_KEY]).decode()
    return f"Idea {i + 1} - {key} {MODE_NAME[inf[II_MODE] % 7].lower()} {inf[II_TEMPO]}"


# ---------------------------------------------------------------- los acordes, en texto
def write_chords_txt(path, title, idea, L):
    """los acordes de la idea: uno por línea (cuándo entra, cuánto dura, el grado y sus notas) y la idea en una
    línea, compás a compás"""
    inf = idea["info"]
    key, mode = inf[II_KEY], inf[II_MODE] % 7
    meter = METER[inf[II_METER] % len(METER)]
    bar8 = meter[1]
    eighths = meter[6][1] == 8                    # (en 6/8 y 7/8 se cuenta en corcheas)
    length8 = idea["len"]
    evs = sorted(idea["events"])
    base = 60 + 12 * ((inf[II_OCTAVE] ^ 0x80) - 0x80)
    rows = []
    for i, (t8, degree, mods, inv) in enumerate(evs):
        c = BlChord()
        L.bl_chord_build(key, mode, degree, mods, inv, base, ctypes.byref(c))
        rom = ctypes.create_string_buffer(16)
        L.bl_degree_name(ctypes.byref(c), degree, rom, 16)
        start = t8 if i else 0
        end = evs[i + 1][0] if i + 1 < len(evs) else length8
        notes = " ".join(L.bl_note_name(c.notes[k] % 12, key).decode() for k in range(c.n))
        rows.append((start, end, c.name.decode(), rom.value.decode(), notes))

    def where(t8):                                # "3.2": compás 3, pulso 2 ("3.2y": la corchea de después)
        b, r = divmod(t8, bar8)
        if eighths:
            return f"{b + 1}.{r + 1}"
        return f"{b + 1}.{r // 2 + 1}" + ("y" if r % 2 else "")

    def dur(n8):
        bars, r = divmod(n8, bar8)
        out = []
        if bars:
            out.append(f"{bars} compás" if bars == 1 else f"{bars} compases")
        if r:
            n = r if eighths else r / 2
            unit = "corchea" if eighths else "pulso"
            out.append(f"{n:g} {unit}" + ("" if n == 1 else "s"))
        return " y ".join(out)

    nbars = -(-length8 // bar8)
    lines = [title, "",
             f"{L.bl_note_name(key, key).decode()} {MODE_NAME[mode].lower()}   {inf[II_TEMPO]} BPM   {meter[0]}   "
             f"{nbars} {'compás' if nbars == 1 else 'compases'}", "",
             f"{'ENTRA':<7}{'ACORDE':<10}{'GRADO':<8}{'DURA':<22}NOTAS"]
    for a, b, name, rom, notes in rows:
        lines.append(f"{where(a):<7}{name:<10}{rom:<8}{dur(b - a):<22}{notes}")
    bars = []                                     # compás a compás: el que suena al empezar y los que entran luego
    for bi in range(nbars):
        lo, hi = bi * bar8, (bi + 1) * bar8
        names = [n for a, b, n, _, _ in rows if a < hi and b > lo]
        bars.append(" ".join(names) if names else "%")
    lines += ["", "| " + " | ".join(bars) + " |", ""]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


# ---------------------------------------------------------------- MIDI (tipo 1)
CHANNEL = {"ACORDES": 1, "BAJO": 2, "ARPEGIO": 3, "BATERIA": 9}   # (0 = canal 1)
PPQ = 480


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


def chunk(tag, body):
    return tag + struct.pack(">I", len(body)) + body


def track(events, name, end=0):
    """events: (tick, orden, bytes); end: el tick donde acaba la pista (el final de la idea, para el bucle)"""
    body = b"\x00\xff\x03" + vlq(len(name.encode())) + name.encode()
    now = 0
    for tick, _, data in sorted(events, key=lambda e: (e[0], e[1])):
        body += vlq(tick - now) + data
        now = tick
    return chunk(b"MTrk", body + vlq(max(0, end - now)) + b"\xff\x2f\x00")


def write_midi(path, title, tempo, ts, parts, beats):
    num, den = ts
    meta = [(0, 0, b"\xff\x51\x03" + (60000000 // tempo).to_bytes(3, "big")),
            (0, 0, b"\xff\x58\x04" + bytes([num, {4: 2, 8: 3}[den], 24, 8]))]
    end = round(beats * PPQ)
    tracks = [track(meta, title, end)]
    for name, notes in parts.items():
        if not notes or name not in CHANNEL:
            continue
        ch, ev = CHANNEL[name], []
        for t, d, n, v in notes:
            a, b = round(t * PPQ), round((t + d) * PPQ)
            ev.append((a, 1, bytes([0x90 | ch, n, v])))
            ev.append((max(a + 1, b), 0, bytes([0x80 | ch, n, 0])))
        tracks.append(track(ev, name, end))
    with open(path, "wb") as f:
        f.write(chunk(b"MThd", struct.pack(">HHH", 1, len(tracks), PPQ)) + b"".join(tracks))


# ---------------------------------------------------------------- Ableton Live (12): de una plantilla
TS_ID = lambda num, den: (num - 1) + 99 * {1: 0, 2: 1, 4: 2, 8: 3, 16: 4}[den]
PART_COLOR = {"ACORDES": 24, "BAJO": 13, "ARPEGIO": 9, "BATERIA": 1, "BOMBO": 2}
PARTS = ["ACORDES", "BAJO", "ARPEGIO", "BOMBO", "BATERIA"]

# ---------------------------------------------------------------- los sonidos de Barullo (tools/ableton/barullo.als)
# El molde trae Barullo Xuno, 7FM y Bombo cargados y un kit de Ableton (y Verb y Ley en los retornos). Cada parte sale
# con su instrumento y el sonido que más se parece al del FM-1: el estado del plugin se reescribe (el XML de JUCE dentro
# de ProcessorState) con el son elegido del banco del plugin.
SOUND_NAME = ["CUERDAS", "NEBOA", "METAIS", "PIANO EL", "ORGANO", "SINTE 80", "CAMPANA", "CRISTAL"]
FM7_FOR_SOUND = {3: 2, 6: 26, 7: 6}              # piano eléctrico: P.ELEC 1; campana: CAMPAS TUB; cristal: CELESTA
XUNO_FOR_SOUND = {0: 8, 1: 88, 2: 0, 4: 24, 5: 72}   # Cordas, Néboa, Metais, Órgano, Fondo 1984
XUNO_BASS, XUNO_ARP = 96, 73                      # Baixo Barullo, Arpexo
XUNO_SLIDERS = ["lfoRate", "lfoDelay", "dcoLfo", "dcoPwm", "dcoNoise", "vcfFreq", "vcfRes", "vcfEnv", "vcfLfo", "vcfKybd",
                "vcaLevel", "envA", "envD", "envS", "envR", "dcoSub"]


def xuno_bank():
    """los 128 sons de Barullo Xuno (o seu Banco.h, aquí copiado): [18 bytes do Juno-106], (portaOn, porta, modo)"""
    path = os.path.join(HERE, "ableton", "xuno_banco.txt")
    out = []
    for line in open(path, encoding="utf-8"):
        if line.strip() and not line.startswith("#"):
            v = [int(x) for x in line.split("#")[0].split()]
            out.append((v[:18], v[18:21] if len(v) >= 21 else None))
    return out


def xuno_params(p):
    """o panel de Xuno para un son (PluginProcessor.cpp setPanel): {id: valor}"""
    sw1, sw2 = p[16], p[17] & 0x1f
    rng = 2 if sw1 & 4 else 1 if sw1 & 2 else 0
    v = {XUNO_SLIDERS[i]: p[i] for i in range(16)}
    v.update(range=rng, pulse=1 if sw1 & 0x08 else 0, saw=1 if sw1 & 0x10 else 0,
             chorus=(2 if not (sw1 & 0x40) else 1) if not (sw1 & 0x20) else 0,
             pwmMode=sw2 & 1, envPol=(sw2 >> 1) & 1, vcaMode=(sw2 >> 2) & 1, hpf=3 - ((sw2 >> 3) & 3))
    return v, sw1 & 0x40


XUNO_NORM = dict({k: 127 for k in XUNO_SLIDERS}, range=2, pulse=1, saw=1, chorus=2, pwmMode=1, envPol=1, vcaMode=1, hpf=3,
                 porta=255, portaOn=1, poly=2)
XUNO_INDEX = {k: i for i, k in enumerate(XUNO_SLIDERS + ["range", "pulse", "saw", "chorus", "pwmMode", "envPol", "vcaMode",
                                                          "hpf", "volume", "tune", "porta", "portaOn", "bendDco",
                                                          "bendVcf", "lfoMod", "poly"])}


def plugin_state(track):
    """(o elemento ProcessorState, a raíz XML do estado de JUCE, o que vai detrás) ou None"""
    e = track.find("DeviceChain/DeviceChain/Devices/PluginDevice//ProcessorState")
    if e is None or not (e.text or "").strip():
        return None
    b = bytes.fromhex(re.sub(r"\s", "", e.text))
    if b[:4] != b"VC2!":
        return None
    n = struct.unpack("<I", b[4:8])[0]
    try:
        root = ET.fromstring(b[8:8 + n].rstrip(b"\x00").decode("utf-8"))
    except ET.ParseError:
        return None
    return e, root, b[8 + n:]


def put_plugin_state(e, root, tail):
    xml = ('<?xml version="1.0" encoding="UTF-8"?>\n\n' + ET.tostring(root, encoding="unicode")).encode("utf-8")
    hx = (b"VC2!" + struct.pack("<I", len(xml)) + xml + tail).hex().upper()
    old = e.text
    head = old[:len(old) - len(old.lstrip())]
    foot = old[len(old.rstrip()):]
    first = old.strip().split()[0]
    w = len(first) or 80
    indent = head.split("\n")[-1]
    e.text = head + ("\n" + indent).join(hx[i:i + w] for i in range(0, len(hx), w)) + foot


def set_xuno(track, patch):
    st = plugin_state(track)
    if not st:
        return
    e, root, tail = st
    p, play = xuno_bank()[patch]
    vals, level = xuno_params(p)
    if play:
        vals.update(portaOn=play[0], porta=play[1], poly=play[2])
    for par in root.findall("PARAM"):
        if par.get("id") in vals:
            par.set("value", f"{float(vals[par.get('id')]):.1f}")
    m = root.find("Memoria")
    if m is not None:
        m.set("patch", str(patch))
        m.set("manual", "0")
        m.set("chorusLevel", str(64 if level else 0))
    put_plugin_state(e, root, tail)
    for fp in track.findall(".//ParameterList/PluginFloatParameter"):   # (y lo que Live recuerda de cada parámetro)
        vi = fp.find("VisualIndex")
        idx = int(vi.get("Value")) if vi is not None else -1
        for k, i in XUNO_INDEX.items():
            if i == idx and k in vals:
                fp.find("ParameterValue/Manual").set("Value", f"{vals[k] / XUNO_NORM[k]:.10g}")


def set_7fm(track, voice):
    st = plugin_state(track)
    if not st:
        return
    e, root, tail = st
    m = root.find("Memoria")
    if m is None:
        return
    m.set("source", "int")                        # a memoria interna (os CLASICOS), a voz elixida
    m.set("voice", str(voice))
    if "edit" in m.attrib:
        del m.attrib["edit"]
    put_plugin_state(e, root, tail)


def barullo_tracks(mtracks, sound):
    """las pistas de Barullo de la plantilla, por parte, ya con su sonido: {parte: pista}"""
    kinds = {}
    for t in mtracks:
        st = plugin_state(t)
        if st:
            kinds.setdefault(st[1].tag, t)
        elif t.find("DeviceChain/DeviceChain/Devices/DrumGroupDevice") is not None:
            kinds.setdefault("KIT", t)
    if "BarulloXuno" not in kinds and "Barullo7FM" not in kinds:
        return {}
    out = {}
    if sound in FM7_FOR_SOUND and "Barullo7FM" in kinds:
        out["ACORDES"] = copy.deepcopy(kinds["Barullo7FM"])
        set_7fm(out["ACORDES"], FM7_FOR_SOUND[sound])
    elif "BarulloXuno" in kinds:
        out["ACORDES"] = copy.deepcopy(kinds["BarulloXuno"])
        set_xuno(out["ACORDES"], XUNO_FOR_SOUND.get(sound, 8))
    if "BarulloXuno" in kinds:
        out["BAJO"] = copy.deepcopy(kinds["BarulloXuno"])
        set_xuno(out["BAJO"], XUNO_BASS)
        out["ARPEGIO"] = copy.deepcopy(kinds["BarulloXuno"])
        set_xuno(out["ARPEGIO"], XUNO_ARP)
    if "BarulloBombo" in kinds:
        out["BOMBO"] = copy.deepcopy(kinds["BarulloBombo"])
    if "KIT" in kinds:
        out["BATERIA"] = copy.deepcopy(kinds["KIT"])
    return out
POINTEE_TAGS = ("AutomationTarget", "ModulationTarget", "Pointee")


def is_pointee(tag):
    return tag in POINTEE_TAGS or tag.endswith("ModulationTarget") or tag.startswith("ControllerTargets.")


def live_crc(path):
    """el OriginalCrc que guarda Live junto a cada archivo: CRC-16/UMTS (0x8005, sin reflejar, de 0) de los
    primeros 16384 bytes (abletoolz/samples/crc.py)"""
    crc = 0
    for b in open(path, "rb").read(16384):
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x8005) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def save_als(root, path):
    """como lo escribe Live: los valores con comillas (el JSON de ViewData) entre comillas simples y sin &quot;"""
    txt = ET.tostring(root, encoding="unicode")
    txt = re.sub(r'="([^"]*&quot;[^"]*)"', lambda m: "='" + m.group(1).replace("&quot;", '"') + "'", txt)
    xml = ('<?xml version="1.0" encoding="UTF-8"?>\n' + txt + "\n").encode("utf-8")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # (sin esta carpeta Live no lo ve como proyecto: al guardar pide "guardar como" y el audio queda fuera)
    os.makedirs(os.path.join(os.path.dirname(path), "Ableton Project Info"), exist_ok=True)
    with gzip.open(path, "wb") as f:
        f.write(xml)


def write_als(path, ideas_out, tpl=None, stage=4):
    """ideas_out: lista de 8 (None o (título, tempo, compás, partes, pulsos)); tpl: un Live Set de molde (de serie,
    tools/ableton/plantilla.als; mejor uno vacío guardado con tu propio Live: --plantilla)"""
    if not tpl:                                   # de serie: la de los plugins de Barullo, si está; si no, la vacía
        b = os.path.join(HERE, "ableton", "barullo.als")
        tpl = b if os.path.exists(b) else os.path.join(HERE, "ableton", "plantilla.als")
    raw = open(tpl, "rb").read()
    root = ET.fromstring(gzip.decompress(raw) if raw[:2] == b"\x1f\x8b" else raw)
    if stage == 1:                                # (diagnóstico: la plantilla tal cual, pasada por aquí)
        return save_als(root, path)
    ls = root.find("LiveSet")
    tracks = ls.find("Tracks")
    mtracks = tracks.findall("MidiTrack")
    if not mtracks:
        raise SystemExit("la plantilla de Ableton necesita al menos una pista MIDI")
    # de molde: la pista MIDI sin aparatos, si hay; el clip, el de la plantilla o el nuestro (tools/ableton/clip.xml)
    clean = [t for t in mtracks if not len(t.find("DeviceChain/DeviceChain/Devices"))]
    proto = copy.deepcopy((clean or mtracks)[0])
    # las pistas con nombre de parte (Acordes, Bajo, Arpegio, Batería) van de molde tal cual, con sus instrumentos y
    # efectos: tu plantilla de sonidos (Barullo Xuno, 7FM, Bombo...)
    named = {}
    for t in mtracks:
        nm = (t.find("Name/UserName").get("Value") or t.find("Name/EffectiveName").get("Value") or "")
        key = nm.strip().upper().replace("Í", "I").replace("í", "I")
        if key in ("ACORDES", "BAJO", "ARPEGIO", "BATERIA") and key not in named:
            named[key] = copy.deepcopy(t)
    first_idea = next((x for x in ideas_out if x), None)
    if not named and first_idea:                  # sin pistas con nombre: los plugins de Barullo, si los trae
        named = barullo_tracks(mtracks, first_idea[5] if len(first_idea) > 5 else 0)
    if "BOMBO" in named:                          # con Barullo Bombo: él lleva el bombo, el kit lo demás
        fixed = []
        for x in ideas_out:
            if x:
                ps = dict(x[3])
                ps["BOMBO"] = [nt for nt in ps["BATERIA"] if nt[2] == 36]
                ps["BATERIA"] = [nt for nt in ps["BATERIA"] if nt[2] != 36]
                x = x[:3] + (ps,) + x[4:]
            fixed.append(x)
        ideas_out = fixed
    atracks = tracks.findall("AudioTrack")
    aproto = copy.deepcopy(atracks[0]) if atracks else None
    c = proto.find(".//MidiClip")
    clip_proto = copy.deepcopy(c) if c is not None else ET.fromstring(open(os.path.join(HERE, "ableton", "clip.xml"), "rb").read())
    gs = clip_proto.find("GrooveSettings/GrooveId")
    if gs is not None:
        gs.set("Value", "-1")
    for t in list(tracks):
        if t.tag != "ReturnTrack":
            tracks.remove(t)
    # la pista de muestra, limpia: sin aparatos, sin automatizaciones, sin clips
    devs = proto.find("DeviceChain/DeviceChain/Devices")
    for d in list(devs):
        devs.remove(d)
    for env in proto.iter("Envelopes"):
        for e in list(env):
            env.remove(e)
    for ev in proto.iter("Events"):
        for e in list(ev):
            ev.remove(e)
    for tr in [proto] + list(named.values()):   # sin clips (ni en la sesión ni en el arreglo)
        for slot in tr.findall("DeviceChain/MainSequencer/ClipSlotList/ClipSlot"):
            v = slot.find("ClipSlot/Value")
            for e in list(v):
                v.remove(e)
        for ev in tr.findall("DeviceChain/MainSequencer/Sample/ArrangerAutomation/Events"):
            for e in list(ev):
                ev.remove(e)
    next_id = int(ls.find("NextPointeeId").get("Value"))
    used_track_ids = {int(t.get("Id")) for t in tracks}
    tid = max(used_track_ids | {0}) + 10
    scenes = ls.find("Scenes")
    nslots = len(proto.findall("DeviceChain/MainSequencer/ClipSlotList/ClipSlot"))
    first = next((x for x in ideas_out if x), None)
    pos = 0                                       # (las pistas van seguidas, antes de las de retorno)
    for pi, part in enumerate(PARTS):
        if not any(x and x[3].get(part) for x in ideas_out):
            continue
        t = copy.deepcopy(named.get(part, proto))
        t.set("Id", str(tid))
        tid += 1
        remap = {}
        for e in t.iter():                        # ids nuevos para todo lo que se puede automatizar o señalar...
            if e.get("Id") is not None and is_pointee(e.tag):
                remap[e.get("Id")] = str(next_id)
                e.set("Id", str(next_id))
                next_id += 1
        for e in t.iter("PointeeId"):             # ...y lo que lo señala (automatizaciones, mapeos), con ellos
            if e.get("Value") in remap:
                e.set("Value", remap[e.get("Value")])
        t.find("Name/EffectiveName").set("Value", part.capitalize())
        t.find("Name/UserName").set("Value", part.capitalize())
        if part not in named:
            t.find("Color").set("Value", str(PART_COLOR[part]))
        mud = set(first[3].get("MUDAS", [])) if first else set()
        silent = (("KICK" in mud) if part == "BOMBO" else
                  ("CAJAS" in mud and ("KICK" in mud or "BOMBO" in named)) if part == "BATERIA" else part in mud)
        if silent and len([x for x in ideas_out if x]) == 1:   # en el FM-1 no sonaba: sin volumen, pero ahí está
            t.find("DeviceChain/Mixer/Volume/Manual").set("Value", "0.0003162277571")
        slots = t.findall("DeviceChain/MainSequencer/ClipSlotList/ClipSlot")
        for si, x in enumerate(ideas_out[:nslots]):
            if stage < 3 or (stage == 3 and (si or pi)):   # (diagnóstico: sin clips, o uno solo)
                continue
            if not x or not x[3].get(part):
                continue
            title, tempo, ts, parts, beats = x[:5]
            clip = copy.deepcopy(clip_proto)
            clip.set("Id", "0")
            clip.set("Time", "0")
            for tag in ("CurrentEnd",):
                clip.find(tag).set("Value", f"{beats:g}")
            lp = clip.find("Loop")
            lp.find("LoopStart").set("Value", "0")
            lp.find("LoopEnd").set("Value", f"{beats:g}")
            lp.find("OutMarker").set("Value", f"{beats:g}")
            lp.find("HiddenLoopStart").set("Value", "0")
            lp.find("HiddenLoopEnd").set("Value", f"{beats:g}")
            lp.find("LoopOn").set("Value", "true")
            clip.find("CurrentStart").set("Value", "0")
            clip.find("Name").set("Value", title)
            clip.find("Color").set("Value", str(PART_COLOR[part]))
            rts = clip.find("TimeSignature/TimeSignatures/RemoteableTimeSignature")
            rts.find("Numerator").set("Value", str(ts[0]))
            rts.find("Denominator").set("Value", str(ts[1]))
            for tagp in ("ScrollerTimePreserver/RightTime",):
                e = clip.find(tagp)
                if e is not None:
                    e.set("Value", f"{beats:g}")
            kts = clip.find("Notes/KeyTracks")
            for e in list(kts):
                kts.remove(e)
            by_key, nid = {}, 1
            for st, d, n, v in parts[part]:
                by_key.setdefault(n, []).append((st, d, v))
            for ki, (n, notes) in enumerate(sorted(by_key.items())):
                kt = ET.SubElement(kts, "KeyTrack", Id=str(ki))
                ns = ET.SubElement(kt, "Notes")
                for st, d, v in sorted(notes):
                    ET.SubElement(ns, "MidiNoteEvent", Time=f"{st:g}", Duration=f"{d:g}", Velocity=str(v),
                                  OffVelocity="64", NoteId=str(nid))
                    nid += 1
                ET.SubElement(kt, "MidiKey", Value=str(n))
            clip.find("Notes/NoteIdGenerator/NextId").set("Value", str(nid))
            slots[si].find("ClipSlot/Value").append(clip)
        tracks.insert(pos, t)
        pos += 1
    if stage < 4:
        ls.find("NextPointeeId").set("Value", str(next_id))
        return save_als(root, path)
    # el sonido original del FM-1: una pista de audio, arriba del todo, con su vuelta en la escena de la idea
    if aproto is not None and any(x and len(x) > 6 for x in ideas_out):
        t = aproto
        for d in list(t.find("DeviceChain/DeviceChain/Devices")):
            t.find("DeviceChain/DeviceChain/Devices").remove(d)
        for env in t.findall("AutomationEnvelopes/Envelopes"):
            for e in list(env):
                env.remove(e)
        t.set("Id", str(tid))
        tid += 1
        remap = {}
        for e in t.iter():
            if e.get("Id") is not None and is_pointee(e.tag):
                remap[e.get("Id")] = str(next_id)
                e.set("Id", str(next_id))
                next_id += 1
        for e in t.iter("PointeeId"):
            if e.get("Value") in remap:
                e.set("Value", remap[e.get("Value")])
        t.find("Name/EffectiveName").set("Value", "FM-1 (original)")
        t.find("Name/UserName").set("Value", "FM-1 (original)")
        t.find("Color").set("Value", "14")
        aslots = t.findall("DeviceChain/MainSequencer/ClipSlotList/ClipSlot")
        for sl in aslots:
            v = sl.find("ClipSlot/Value")
            for e in list(v):
                v.remove(e)
        ac_proto = ET.fromstring(open(os.path.join(HERE, "ableton", "audioclip.xml"), "rb").read())
        for si, x in enumerate(ideas_out[:len(aslots)]):
            if not x or len(x) < 7 or not os.path.exists(x[6]):
                continue
            wav, beats, tempo = x[6], x[4], x[1]
            import wave
            with wave.open(wav) as w:
                nfr, rate = w.getnframes(), w.getframerate()
            secs = nfr / float(rate)
            ac = copy.deepcopy(ac_proto)
            ac.set("Id", "0")
            ac.set("Time", "0")
            ac.find("Name").set("Value", "FM-1 - " + x[0])
            ac.find("CurrentStart").set("Value", "0")
            ac.find("CurrentEnd").set("Value", f"{beats:g}")
            lp = ac.find("Loop")
            for tag, val in (("LoopStart", 0), ("LoopEnd", beats), ("StartRelative", 0), ("OutMarker", beats),
                             ("HiddenLoopStart", 0), ("HiddenLoopEnd", beats)):
                lp.find(tag).set("Value", f"{val:g}")
            lp.find("LoopOn").set("Value", "true")
            ac.find("IsWarped").set("Value", "true")
            ac.find("WarpMode").set("Value", "3")       # re-pitch: al mismo tempo, el audio tal cual
            wm = ac.find("WarpMarkers")
            for e in list(wm):
                wm.remove(e)
            ET.SubElement(wm, "WarpMarker", Id="0", SecTime="0", BeatTime="0")
            ET.SubElement(wm, "WarpMarker", Id="1", SecTime=f"{secs:.10g}", BeatTime=f"{beats:g}")
            rts = ac.find("TimeSignature/TimeSignatures/RemoteableTimeSignature")
            rts.find("Numerator").set("Value", str(x[2][0]))
            rts.find("Denominator").set("Value", str(x[2][1]))
            fr = ac.find("SampleRef/FileRef")
            proj = os.path.dirname(path)
            fr.find("RelativePathType").set("Value", "3")
            fr.find("RelativePath").set("Value", os.path.relpath(wav, proj).replace(os.sep, "/"))
            fr.find("Path").set("Value", os.path.abspath(wav).replace(os.sep, "/"))
            fr.find("OriginalFileSize").set("Value", str(os.path.getsize(wav)))
            fr.find("OriginalCrc").set("Value", str(live_crc(wav)))   # (con 0, Live da el audio por perdido)
            sr = ac.find("SampleRef")
            sr.find("LastModDate").set("Value", str(int(os.path.getmtime(wav))))
            sr.find("DefaultDuration").set("Value", str(nfr))
            sr.find("DefaultSampleRate").set("Value", str(rate))
            aslots[si].find("ClipSlot/Value").append(ac)
        tracks.insert(0, t)
    # las escenas: una por idea, con su nombre, su tempo y su compás
    for si, sc in enumerate(scenes):
        x = ideas_out[si] if si < len(ideas_out) else None
        sc.find("Name").set("Value", x[0] if x else "")
        if x:
            sc.find("Tempo").set("Value", str(x[1]))
            sc.find("IsTempoEnabled").set("Value", "true")
            sc.find("TimeSignatureId").set("Value", str(TS_ID(*x[2])))
            sc.find("IsTimeSignatureEnabled").set("Value", "true")
    if first:                                     # el tempo y el compás del proyecto: los de la primera idea
        main = ls.find("MainTrack")
        mix = main.find("DeviceChain/Mixer")
        mix.find("Tempo/Manual").set("Value", str(first[1]))
        mix.find("TimeSignature/Manual").set("Value", str(TS_ID(*first[2])))
        want = {mix.find("Tempo/AutomationTarget").get("Id"): str(first[1]),
                mix.find("TimeSignature/AutomationTarget").get("Id"): str(TS_ID(*first[2]))}
        for env in main.findall("AutomationEnvelopes/Envelopes/AutomationEnvelope"):   # (Live lee de aquí el valor)
            v = want.get(env.find("EnvelopeTarget/PointeeId").get("Value"))
            if v is not None:
                for e in env.find("Automation/Events"):
                    e.set("Value", v)
    ls.find("NextPointeeId").set("Value", str(next_id))
    save_als(root, path)


# ---------------------------------------------------------------- el sonido original: el FM-1, en el simulador
# El mismo firmware corre en el ordenador (base/tools/build.py --host): se le da la copia, toca cada idea con PLAY y se
# graba. De la grabación sale una vuelta entera (la segunda: con las colas de la primera, como suena en bucle).
def host_sim():
    exe = os.path.join(ROOT, "app", "build", "host", "barullin_host")
    src = os.path.join(ROOT, "app", "app.c")
    if not os.path.exists(exe) or os.path.getmtime(exe) < os.path.getmtime(src):
        print("preparando el simulador de Barullín (la primera vez tarda un poco)...")
        r = subprocess.run([sys.executable, os.path.join(HERE, "host_build.py")], capture_output=True, text=True)
        if r.returncode or not os.path.exists(exe):
            print("  (no pude preparar el simulador: los proyectos salen sin el sonido original)\n" + r.stderr[-800:])
            return None
    return exe


def render_original(backup, cur_bank, i, beats, tempo, outwav):
    """toca la idea i en el simulador y deja en outwav una vuelta (la segunda). True si salió"""
    import wave
    exe = host_sim()
    if not exe:
        return False
    loop_ms = beats * 60000.0 / tempo
    work = tempfile.mkdtemp(prefix="barullin_")
    script = os.path.join(work, "toca.txt")
    shutil.copy(backup, os.path.join(work, "copia.barullin"))
    seq = (i - cur_bank) % NBANK
    with open(script, "w") as f:
        f.write("wait 1500\nrestore out:copia.barullin\nwait 300\nexpect restore_rc 0\n")   # (por nombre: el guion parte en los espacios)
        f.write("tap SEQ\n" * seq + "wait 100\n")
        f.write("turn K4 -20\nwait 50\n")                  # (el rasgueo callado: la tecla de abajo es la marca de tiempo)
        f.write("wav toca.wav\nkey 14 down\nwait 20\nkey 14 up\ntap PLAY\n")
        f.write("wait %d\nwavstop\ntap PLAY\nwait 50\n" % int(2 * loop_ms + 1500))
    r = subprocess.run([exe, script, work], capture_output=True, text=True)
    wpath, mlog = os.path.join(work, "toca.wav"), os.path.join(work, "midi_out.txt")
    if not os.path.exists(wpath) or not os.path.exists(mlog):
        return False
    mark = start = None
    for line in open(mlog):
        m = re.match(r"\s*(\d+) ms: ([0-9A-F]{2}) ([0-9A-F]{2}) ([0-9A-F]{2})", line)
        if not m:
            continue
        t, st = int(m.group(1)), int(m.group(2), 16)
        if st == 0x90 and mark is None:
            mark = t                              # la marca: aquí empezó la grabación
        elif st & 0xF0 == 0x90 and st != 0x90 and mark is not None and start is None:
            start = t                             # la primera nota de la idea: aquí empieza la vuelta
    if mark is None or start is None:
        return False
    w = wave.open(wpath)
    rate, ch, sw = w.getframerate(), w.getnchannels(), w.getsampwidth()
    frames = w.readframes(w.getnframes())
    w.close()
    first = int(round((start - mark) / 1000.0 * rate)) + int(round(loop_ms / 1000.0 * rate))
    n = int(round(loop_ms / 1000.0 * rate))
    chunk_ = frames[first * ch * sw:(first + n) * ch * sw]
    if len(chunk_) < n * ch * sw:
        return False
    if sw == 2 and not any(chunk_[k] | chunk_[k + 1] for k in range(0, len(chunk_), 64 * sw)):
        return False                              # (en silencio: en el FM-1 no sonaba nada, todo a 0)
    os.makedirs(os.path.dirname(outwav), exist_ok=True)
    o = wave.open(outwav, "wb")
    o.setnchannels(ch)
    o.setsampwidth(sw)
    o.setframerate(rate)
    o.writeframes(chunk_)
    o.close()
    return True


# ---------------------------------------------------------------- leer del FM-1
def backup_from_fm1(path, port):
    sys.path.insert(0, BASE_TOOLS)
    try:
        import fm1_backup as B
    except ImportError as e:
        raise SystemExit(f"falta {e.name}: pip install mido python-rtmidi")
    args = ["backup", path, "--magic", BACKUP_MAGIC.decode()]
    if port:
        args += ["--port", port]
    try:
        rc = B.main(args)
    except SystemExit as e:
        rc = e.code
    if rc not in (0, None):
        raise SystemExit(f"no pude leer las ideas del FM-1 (código {rc}): ¿está encendido con Barullín, conectado "
                         "por USB y parado (sin PLAY ni REC)?")


def main():
    ap = argparse.ArgumentParser(description="Saca las ideas de Barullín a MIDI y a Ableton Live")
    ap.add_argument("--desde", help="una copia de seguridad ya hecha (.barullin), en vez de leer el FM-1")
    ap.add_argument("--salida", default="Barullin Export")
    ap.add_argument("--vueltas", type=int, default=1, help="cuántas veces se repite cada idea en el MIDI")
    ap.add_argument("--port")
    ap.add_argument("--sin-original", action="store_true", help="sin la pista con el sonido del FM-1")
    ap.add_argument("--todas", action="store_true", help="además, un proyecto con todas las ideas (una escena cada una)")
    ap.add_argument("--diagnostico", action="store_true", help="cuatro proyectos de prueba, para ver qué no le gusta a Live")
    ap.add_argument("--plantilla", help="(se recuerda para la próxima) " +"un Live Set vacío guardado con tu Ableton (con una pista MIDI), de molde")
    a = ap.parse_args()
    remember = os.path.join(ROOT, ".plantilla_ableton")   # la última --plantilla, para no repetirla
    if a.plantilla:
        a.plantilla = os.path.abspath(os.path.expanduser(a.plantilla))
        with open(remember, "w") as f:
            f.write(a.plantilla)
    elif os.path.exists(remember):
        p = open(remember).read().strip()
        if os.path.exists(p):
            a.plantilla = p
            print("plantilla de Ableton: " + p)
    os.makedirs(a.salida, exist_ok=True)
    bk = a.desde or os.path.join(a.salida, "Barullin.barullin")
    if not a.desde:
        print("leyendo las ideas del FM-1...")
        backup_from_fm1(bk, a.port)
    if not os.path.exists(bk):
        raise SystemExit(f"no encuentro {bk}: sin --desde, las ideas se leen del FM-1 (conectado, con Barullín, parado)")
    data = open(bk, "rb").read()
    objs = parse_backup(data)
    if OBJ_IDEAS not in objs:
        raise SystemExit("la copia no trae ideas: guarda con SAVE en el FM-1 y vuelve a probar")
    ideas = parse_ideas(objs[OBJ_IDEAS])
    L = load_chords()
    out = []
    for i, idea in enumerate(ideas):
        if not idea:
            out.append(None)
            continue
        inf = idea["info"]
        tempo = inf[II_TEMPO] or 100
        ts = METER[inf[II_METER] % len(METER)][6]
        title = idea_title(i, idea, L)
        parts, beats = render_idea(idea, L, max(1, a.vueltas))
        write_midi(os.path.join(a.salida, title + ".mid"), title, tempo, ts, parts, beats)
        os.makedirs(os.path.join(a.salida, title + " Project"), exist_ok=True)   # (los acordes, dentro del proyecto)
        write_chords_txt(os.path.join(a.salida, title + " Project", title + " - acordes.txt"), title, idea, L)
        p1, b1 = render_idea(idea, L, 1) if a.vueltas != 1 else (parts, beats)
        out.append((title, tempo, ts, p1, b1, inf[II_SOUND]))
        print(f"  {title}.mid  ({b1:g} pulsos, " + ", ".join(k.lower() + (" (a 0)" if k in parts["MUDAS"] else "")
                                                   for k, v in parts.items() if v and k in CHANNEL) + ")")
    if not any(out):
        raise SystemExit("no hay ideas guardadas (graba con REC y guarda con SAVE)")
    # un proyecto de Ableton por idea: su carpeta, su tempo y su compás, sus pistas (y la idea en la primera escena),
    # y el sonido original del FM-1 en una pista de audio
    cur_bank = objs.get(0, b"\0" * 8)[7] if len(objs.get(0, b"")) > 7 else 0
    for i, x in enumerate(out):
        if not x:
            continue
        als = os.path.join(a.salida, x[0] + " Project", x[0] + ".als")
        if not a.sin_original:
            wav = os.path.join(os.path.dirname(als), "Samples", "Imported", "FM-1 - " + x[0] + ".wav")
            print(f"  grabando el sonido original de {x[0]}...")
            if render_original(bk, cur_bank, i, x[4], x[1], wav):
                x = x[:6] + (wav,)
                out[i] = x
        write_als(als, [x], a.plantilla)
        print(f"  {als}")
    if a.todas:
        als = os.path.join(a.salida, "Todas Project", "Todas.als")
        write_als(als, out, a.plantilla)
        print(f"  {als}  (todas juntas: una escena por idea)")
    if a.diagnostico:
        for st, nm in ((1, "1 plantilla tal cual"), (2, "2 pistas sin clips"), (3, "3 un clip"), (4, "4 todo")):
            write_als(os.path.join(a.salida, "Diagnostico", nm, nm + ".als"), out, a.plantilla, st)
        print("  " + os.path.join(a.salida, "Diagnostico") + ": ábrelos en orden y dime el primero que tumba Live")
    print("  (las pistas sin instrumento: arrastra uno; o usa tu plantilla de sonidos con --plantilla)")
    print("hecho: " + os.path.abspath(a.salida))


if __name__ == "__main__":
    main()
