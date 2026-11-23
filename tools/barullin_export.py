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
    Barullin Project/Barullin.als   el proyecto de Ableton Live (12): una pista por parte y una escena por idea, con su
                             tempo y su compás. Las pistas van sin instrumento: arrastra el que quieras a cada una.

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
    if inf[II_VOL_ARP]:
        pb = [2, 3, 4][inf[II_ARP_DIV] % 3]
        step = 1.0 / pb
        rng = [0x1234567]
        mode_a, octs = inf[II_ARP_MODE], max(1, inf[II_ARP_OCT])
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
            parts["ARPEGIO"].append((t, step * inf[II_ARP_LEN] / 10.0, max(0, min(127, note)), vel(inf[II_VOL_ARP])))

    # el ritmo (drum_step): bombo en el pulso, caja en el 2 y el 4, charles a corcheas
    if not inf[II_DRM_MUTE] and (inf[II_DRM_KICK] or inf[II_DRM_SNARE] or inf[II_DRM_HAT]):
        bar8, beats = meter[1], meter[5]
        hat = 46 if inf[II_DRM_HOPEN] >= 5 else 42  # abierto o cerrado
        for s8 in range(total8):
            s = s8 % bar8
            beat = beats >> s & 1
            nbeat = sum(beats >> k & 1 for k in range(s))
            t = s8 / 2.0
            if beat and inf[II_DRM_KICK]:
                parts["BATERIA"].append((t, 0.25, 36, vel(inf[II_DRM_KICK])))
            if beat and nbeat & 1 and inf[II_DRM_SNARE]:
                parts["BATERIA"].append((t, 0.25, 38, vel(inf[II_DRM_SNARE])))
            if inf[II_DRM_HAT]:
                parts["BATERIA"].append((t, 0.25, hat, max(1, int(vel(inf[II_DRM_HAT]) * (1.0 if beat else 0.8)))))
    return parts, total8 / 2.0


def idea_title(i, idea, L):
    inf = idea["info"]
    key = L.bl_note_name(inf[II_KEY], inf[II_KEY]).decode()
    return f"Idea {i + 1} - {key} {MODE_NAME[inf[II_MODE] % 7].lower()} {inf[II_TEMPO]}"


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
        if not notes:
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
PART_COLOR = {"ACORDES": 24, "BAJO": 13, "ARPEGIO": 9, "BATERIA": 1}
POINTEE_TAGS = ("AutomationTarget", "ModulationTarget", "Pointee")


def is_pointee(tag):
    return tag in POINTEE_TAGS or tag.endswith("ModulationTarget") or tag.startswith("ControllerTargets.")


def write_als(path, ideas_out):
    """ideas_out: lista de 8 (None o (título, tempo, compás, partes, pulsos))"""
    tpl = os.path.join(HERE, "ableton", "plantilla.als")
    root = ET.fromstring(gzip.open(tpl).read())
    ls = root.find("LiveSet")
    tracks = ls.find("Tracks")
    proto = copy.deepcopy(tracks.find("MidiTrack"))
    clip_proto = copy.deepcopy(proto.find(".//MidiClip"))
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
    for slot in proto.findall("DeviceChain/MainSequencer/ClipSlotList/ClipSlot"):
        v = slot.find("ClipSlot/Value")
        for e in list(v):
            v.remove(e)
    next_id = int(ls.find("NextPointeeId").get("Value"))
    used_track_ids = {int(t.get("Id")) for t in tracks}
    tid = max(used_track_ids | {0}) + 10
    scenes = ls.find("Scenes")
    nslots = len(proto.findall("DeviceChain/MainSequencer/ClipSlotList/ClipSlot"))
    first = next((x for x in ideas_out if x), None)
    for pi, part in enumerate(["ACORDES", "BAJO", "ARPEGIO", "BATERIA"]):
        if not any(x and x[3][part] for x in ideas_out):
            continue
        t = copy.deepcopy(proto)
        t.set("Id", str(tid))
        tid += 1
        for e in t.iter():
            if e.get("Id") is not None and is_pointee(e.tag):
                e.set("Id", str(next_id))
                next_id += 1
        t.find("Name/EffectiveName").set("Value", part.capitalize())
        t.find("Name/UserName").set("Value", part.capitalize())
        t.find("Color").set("Value", str(PART_COLOR[part]))
        slots = t.findall("DeviceChain/MainSequencer/ClipSlotList/ClipSlot")
        for si, x in enumerate(ideas_out[:nslots]):
            if not x or not x[3][part]:
                continue
            title, tempo, ts, parts, beats = x
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
        tracks.insert(pi, t)
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
    xml = b'<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding="utf-8")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with gzip.open(path, "wb") as f:
        f.write(xml)


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
    a = ap.parse_args()
    os.makedirs(a.salida, exist_ok=True)
    bk = a.desde or os.path.join(a.salida, "Barullin.barullin")
    if not a.desde:
        print("leyendo las ideas del FM-1...")
        backup_from_fm1(bk, a.port)
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
        p1, b1 = render_idea(idea, L, 1) if a.vueltas != 1 else (parts, beats)
        out.append((title, tempo, ts, p1, b1))
        print(f"  {title}.mid  ({b1:g} pulsos, " + ", ".join(k.lower() for k, v in parts.items() if v) + ")")
    if not any(out):
        raise SystemExit("no hay ideas guardadas (graba con REC y guarda con SAVE)")
    als = os.path.join(a.salida, "Barullin Project", "Barullin.als")
    write_als(als, out)
    print(f"  {als}  (una escena por idea; arrastra un instrumento a cada pista)")
    print("hecho: " + os.path.abspath(a.salida))


if __name__ == "__main__":
    main()
