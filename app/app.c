/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Barullín: un instrumento de acordes para el M-VAVE FM-1, sobre fm1-base.
 *
 *   Teclas blancas 1-7   grados I..vii de la tonalidad y el modo
 *   Teclas blancas 8-16  placa de rasgueo: cada tecla, una nota del acorde
 *   Teclas negras        modificadores, mientras se mantienen:
 *                          m/M  7  maj7 | sus2 sus4 | 9  6  dim/aug | INV1 INV2 | HOLD (pulsar)
 *   K1 tonalidad  K2 modo  K3 octava  K4 brillo      SAVE guarda
 *
 * Forma parte del unity build de fm1-base: gfx.c, announce.c y touch están a la vista. */
#include "fastmath.h"
#include "core/chords.h"

#define SR 44100.0f

/* ------------------------------------------------------------- ajustes --- */
#define SET_MAGIC 0x42524C34u            /* "BRL4" */
#define LOOP_MAX 64                      /* cambios de acorde en una idea */
#define LOOP_STEPS 128                   /* 16 compases de corcheas */
typedef struct { uint8_t step; int8_t degree; uint8_t mods; uint8_t inv; } loop_ev_t;
typedef struct {
    uint32_t magic;
    uint8_t key;                         /* 0 = C .. 11 = B */
    uint8_t mode;                        /* enum bl_mode */
    int8_t octave;                       /* -2 .. +2 */
    uint8_t bright;                      /* 0..20: el corte del filtro del pad */
    uint8_t lead;                        /* voice leading */
    uint8_t hold;                        /* el acorde sigue al soltar (por defecto, sí) */
    uint8_t arp;                         /* arpegiador */
    uint8_t arp_mode;                    /* 0 sube, 1 baja, 2 sube-baja, 3 azar */
    uint8_t arp_div;                     /* 0 corcheas, 1 tresillos, 2 semicorcheas */
    uint8_t arp_oct;                     /* 1..3 octavas */
    uint8_t arp_len;                     /* 1..9: duración de cada nota, en décimas del paso */
    uint8_t tempo;                       /* 40..220 BPM */
    uint8_t loop_len;                    /* la idea grabada: corcheas (0: no hay), compases enteros */
    uint8_t loop_n;                      /* sus cambios de acorde */
    loop_ev_t loop_ev[LOOP_MAX];
} settings_t;
static settings_t set;
static uint8_t set_dirty;

static const char *const MODE_NAME[BL_MODE_COUNT] = {"MAYOR", "DORICO", "FRIGIO", "LIDIO",
                                                     "MIXOLIDIO", "MENOR", "LOCRIO"};
/* las teclas negras, de izquierda a derecha */
enum { BK_FLIP, BK_7, BK_MAJ7, BK_SUS2, BK_SUS4, BK_9, BK_6, BK_DIMAUG, BK_INV1, BK_INV2, BK_HOLD, NBK };
static const char *const BK_LABEL[NBK] = {"m/M", "7", "M7", "s2", "s4", "9", "6", "o/+", "I1", "I2", "STOP"};
static const unsigned BK_MOD[8] = {BL_MOD_FLIP, BL_MOD_7, BL_MOD_MAJ7, BL_MOD_SUS2,
                                   BL_MOD_SUS4, BL_MOD_9, BL_MOD_6, BL_MOD_DIMAUG};
static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
static const char *const ARP_MODE_NAME[4] = {"SUBE", "BAJA", "S-B", "AZAR"};
static const char *const ARP_DIV_NAME[3] = {"1/8", "1/8T", "1/16"};
#define NDEG 7
#define NSTRUM 9

/* ---------------------------------------------- del UI al audio --- */
/* El UI publica el acorde en uno de dos buffers y luego sube seq: el audio nunca lee uno a medio escribir. */
typedef struct {
    uint8_t n, gate, bass;
    uint8_t notes[BL_MAX_NOTES];
} snd_chord;
static snd_chord snd_buf[2];
static volatile uint32_t snd_seq;
static volatile float snd_cut = 0.2f;    /* coeficiente del filtro (lo calcula el UI) */

#define NPLQ 16u                         /* cola de rasgueos */
static volatile uint8_t plq_note[NPLQ], plq_vel[NPLQ];
static volatile uint32_t plq_w;

static void publish(const bl_chord *c, int gate)
{
    snd_chord *b = &snd_buf[(snd_seq + 1u) & 1u];
    int i;
    b->n = c->n;
    b->gate = (uint8_t)gate;
    b->bass = c->bass;
    for (i = 0; i < c->n; i++)
        b->notes[i] = c->notes[i];
    snd_seq = snd_seq + 1u;
}

static void pluck(int note, int vel)
{
    uint32_t w = plq_w;
    plq_note[w % NPLQ] = (uint8_t)note;
    plq_vel[w % NPLQ] = (uint8_t)vel;
    plq_w = w + 1u;
}

/* ---------------------------------------------------------------- audio --- */
#define NPAD 6
#define NPLK 8
typedef struct { float p1, p2, i1, i2, env; int note, gate; } pad_v;
typedef struct {
    float ph, inc, env, amp;
    int32_t left;                        /* arpegio: muestras que le quedan sonando; -1 un rasgueo (se apaga solo) */
    uint8_t note;
} plk_v;
static pad_v pad[NPAD];
static plk_v plk[NPLK];
static float bass_ph, bass_inc, bass_env;
static int bass_gate;
static float lp1[2], lp2[2], gain_now;
static uint32_t snd_seen, plq_r, plk_next;
static snd_chord achord;                 /* el acorde según el audio (para el arpegio) */
static int arp_was;                      /* el arpegiador sonaba en el bloque anterior */
static float arp_cnt, clk_cnt;           /* muestras hasta el siguiente paso / pulso de reloj MIDI */
static uint32_t arp_i, arp_rng = 0x1234567u;
static volatile uint32_t arp_steps;      /* pasos tocados (para las pruebas) */

static float note_inc(int note) { return 440.0f * fm_exp2f((float)(note - 69) / 12.0f) / SR; }

/* el acorde nuevo: las notas que ya suenan siguen sin cortarse; las demás se sueltan o se asignan */
static void take_chord(const snd_chord *c)
{
    int i, j, used[NPAD] = {0};
    int pad_gate = c->gate && !set.arp;  /* con arpegio, el pad calla: el acorde va nota a nota */
    if (c != &achord) achord = *c;
    for (j = 0; j < NPAD; j++) {
        int keep = 0;
        if (pad_gate && pad[j].gate)
            for (i = 0; i < c->n; i++)
                if (pad[j].note == c->notes[i]) keep = 1;
        if (!keep) pad[j].gate = 0;
        else used[j] = 1;
    }
    if (pad_gate)
        for (i = 0; i < c->n; i++) {
            int have = 0, best = -1;
            for (j = 0; j < NPAD; j++)
                if (used[j] && pad[j].note == c->notes[i]) have = 1;
            if (have) continue;
            for (j = 0; j < NPAD; j++)       /* la voz libre más callada */
                if (!used[j] && !pad[j].gate && (best < 0 || pad[j].env < pad[best].env)) best = j;
            if (best < 0) continue;
            pad[best].note = c->notes[i];
            pad[best].gate = 1;
            pad[best].i1 = note_inc(c->notes[i]) * 1.0040f;   /* +7 cents */
            pad[best].i2 = note_inc(c->notes[i]) * 0.9960f;   /* -7 cents */
            used[best] = 1;
        }
    bass_gate = c->gate;
    if (c->gate) bass_inc = note_inc(c->bass);
}

static void take_plucks(void)
{
    uint32_t w = plq_w;
    while (plq_r != w) {
        plk_v *p = &plk[plk_next++ % NPLK];
        p->ph = 0.0f;
        p->inc = note_inc(plq_note[plq_r % NPLQ]);
        p->amp = 0.12f + 0.20f * (float)plq_vel[plq_r % NPLQ] / 127.0f;
        p->env = 1.0f;
        p->left = -1;
        plq_r++;
    }
}

/* ----------------------------------------------------------- arpegio --- */
static void midi_rt(uint32_t b) { plat_midi_send(0x0Fu | b << 8, TO_USB | TO_BLE); }   /* reloj, start, stop */
static void midi_ch(uint32_t st_, uint32_t ch, uint32_t d1, uint32_t d2)
{
    plat_midi_send((st_ >> 4) | (st_ | (ch - 1u)) << 8 | d1 << 16 | d2 << 24, TO_USB | TO_BLE);
}

static float arp_step_len(void)
{
    static const float PER_BEAT[3] = {2.0f, 3.0f, 4.0f};
    return SR * 60.0f / (float)set.tempo / PER_BEAT[set.arp_div % 3u];
}

/* la nota del paso i: las notas del acorde, repetidas en arp_oct octavas, en el orden del modo */
static int arp_note(uint32_t i)
{
    int n = achord.n, len = n * (int)set.arp_oct, k, period;
    if (n == 0) return -1;
    switch (set.arp_mode) {
    case 1: k = len - 1 - (int)(i % (uint32_t)len); break;
    case 2:
        period = len > 1 ? 2 * len - 2 : 1;
        k = (int)(i % (uint32_t)period);
        if (k >= len) k = period - k;
        break;
    case 3:
        arp_rng = arp_rng * 1664525u + 1013904223u;
        k = (int)((arp_rng >> 16) % (uint32_t)len);
        break;
    default: k = (int)(i % (uint32_t)len); break;
    }
    return clamp(achord.notes[k % n] + 12 * (k / n), 0, 127);
}

static void arp_fire(float step)
{
    int note = arp_note(arp_i++);
    plk_v *p = &plk[plk_next++ % NPLK];
    if (note < 0) return;
    if (p->left > 0) midi_ch(0x80u, 4, p->note, 0);
    p->ph = 0.0f;
    p->inc = note_inc(note);
    p->amp = 0.26f;
    p->env = 1.0f;
    p->note = (uint8_t)note;
    p->left = (int32_t)(step * (float)set.arp_len / 10.0f);
    if (p->left < 1) p->left = 1;
    midi_ch(0x90u, 4, (uint32_t)note, 100);
    arp_steps++;
}

static void arp_release_all(void)
{
    int j;
    for (j = 0; j < NPLK; j++)
        if (plk[j].left > 0) plk[j].left = 1;   /* se sueltan en la siguiente muestra (y su note off) */
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, seq = snd_seq;
    int j;
    float cut = snd_cut;
    float gain = (float)master_q12 / 4096.0f * 7000000.0f;
    if (seq != snd_seen) {
        snd_seen = seq;
        take_chord(&snd_buf[seq & 1u]);
    }
    take_plucks();
    {   /* el arpegiador encendido o apagado: el pad entra o sale, el reloj MIDI arranca o para */
        int on = set.arp != 0;
        if (on != arp_was) {
            arp_was = on;
            take_chord(&achord);
            if (on) { midi_rt(0xFAu); clk_cnt = 0.0f; arp_cnt = 0.0f; arp_i = 0; }
            else { midi_rt(0xFCu); arp_release_all(); }
        }
        if (!on || !achord.gate) {
            if (achord.gate == 0) arp_release_all();
            arp_cnt = 0.0f;                  /* el siguiente acorde arranca el arpegio en el acto */
            arp_i = 0;
        }
    }
    { uint32_t pkt; int port; while (plat_midi_in(&pkt, &port)) {} }   /* (la entrada MIDI llegará más adelante) */
    for (i = 0; i < n; i++) {
        float l = 0.0f, r = 0.0f, b, s;
        if (set.arp) {
            clk_cnt -= 1.0f;
            if (clk_cnt <= 0.0f) { clk_cnt += SR * 60.0f / (float)set.tempo / 24.0f; midi_rt(0xF8u); }
            if (achord.gate) {
                arp_cnt -= 1.0f;
                if (arp_cnt <= 0.0f) { float step = arp_step_len(); arp_cnt += step; arp_fire(step); }
            }
        }
        for (j = 0; j < NPAD; j++) {
            pad_v *v = &pad[j];
            if (v->gate) { v->env += 1.0f / (0.08f * SR); if (v->env > 1.0f) v->env = 1.0f; }
            else if (v->env > 0.0f) { v->env *= 0.99993f; if (v->env < 0.0001f) v->env = 0.0f; }
            if (v->env == 0.0f) continue;
            v->p1 += v->i1; if (v->p1 >= 1.0f) v->p1 -= 1.0f;
            v->p2 += v->i2; if (v->p2 >= 1.0f) v->p2 -= 1.0f;
            l += (2.0f * v->p1 - 1.0f) * v->env * 0.11f;
            r += (2.0f * v->p2 - 1.0f) * v->env * 0.11f;
        }
        /* filtro paso bajo de dos polos por canal: el pad, tibio */
        lp1[0] += (l - lp1[0]) * cut; lp2[0] += (lp1[0] - lp2[0]) * cut;
        lp1[1] += (r - lp1[1]) * cut; lp2[1] += (lp1[1] - lp2[1]) * cut;
        l = lp2[0];
        r = lp2[1];
        /* bajo: un seno */
        if (bass_gate) { bass_env += 1.0f / (0.02f * SR); if (bass_env > 1.0f) bass_env = 1.0f; }
        else bass_env *= 0.9997f;
        if (bass_env > 0.0001f) {
            bass_ph += bass_inc; if (bass_ph >= 1.0f) bass_ph -= 1.0f;
            b = fm_sinf(bass_ph * 6.2831853f) * bass_env * 0.30f;
            l += b; r += b;
        }
        /* rasgueos: seno con algo de segundo armónico, que se apaga solo */
        for (j = 0; j < NPLK; j++) {
            plk_v *p = &plk[j];
            if (p->env < 0.0005f) continue;
            p->ph += p->inc; if (p->ph >= 1.0f) p->ph -= 1.0f;
            s = (fm_sinf(p->ph * 6.2831853f) + 0.35f * fm_sinf(p->ph * 12.566371f)) * p->env * p->amp;
            if (p->left < 0) p->env *= 0.99990f;            /* rasgueo: se apaga solo */
            else if (p->left > 0) {                           /* arpegio: suena su parte del paso */
                p->env *= 0.99997f;
                if (--p->left == 0) midi_ch(0x80u, 4, p->note, 0);
            } else p->env *= 0.9990f;                         /* y se suelta rápido */
            l += s * 1.1f; r += s * 0.9f;
        }
        gain_now += (gain - gain_now) * 0.004f;
        lr[2 * i] = (int32_t)(fm_tanhf(l) * gain_now);
        lr[2 * i + 1] = (int32_t)(fm_tanhf(r) * gain_now);
    }
}

void app_panic(void)
{
    int j;
    for (j = 0; j < NPAD; j++) pad[j].gate = 0, pad[j].env = 0.0f;
    for (j = 0; j < NPLK; j++) plk[j].env = 0.0f, plk[j].left = -1;
    bass_gate = 0;
    bass_env = 0.0f;
}

/* ------------------------------------------------------------- estado --- */
static struct {
    bl_chord cur, prev;                  /* el acorde que suena (o el último) y el de antes */
    int degree;                          /* el grado que suena, -1 ninguno */
    unsigned mods;
    int inv;
    int gate;
    uint8_t deg_order[NDEG];             /* qué grado se pulsó antes: manda el último */
    uint8_t deg_stamp;
    uint8_t strum_note[NSTRUM];          /* la nota que dio cada tecla de rasgueo (para su note off) */
    uint32_t sig;                        /* para saber si algo cambió */
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint8_t redraw;
    uint8_t gate_hold_off;
    uint8_t arp_turned;
    uint8_t page;
    uint8_t rec;                         /* 0 no, 1 armado (espera el primer acorde), 2 grabando */
    uint8_t playing;
    float rec_acc, play_acc;             /* corcheas desde que empezó la grabación / dentro del bucle */
    uint32_t rec_sig, last_ms, shown_pos;                        /* de quién son K1..K4: 0 el acorde, 1 el arpegiador */                  /* con ARP mantenido se giró un knob: al soltar no se conmuta */
    uint32_t stop_t;                     /* la tecla STOP bajó (0: no está pulsada, o ya actuó)
                                          * corta: para el acorde; un segundo: HOLD sí / no */
} st;

static void cut_update(void)
{
    float fc = 300.0f * fm_exp2f((float)set.bright * 0.25f);   /* 300 Hz .. ~9,6 kHz */
    snd_cut = 1.0f - fm_expf(-6.2831853f * fc / SR);
}

static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.key = 0;
    set.mode = BL_IONIAN;
    set.octave = 0;
    set.bright = 10;
    set.lead = 1;
    set.hold = 1;
    set.arp = 0;
    set.arp_mode = 0;
    set.arp_div = 0;
    set.arp_oct = 1;
    set.arp_len = 5;
    set.tempo = 100;
    set.loop_len = 0;
    set.loop_n = 0;
}

static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.key > 11u || set.mode >= BL_MODE_COUNT || set.octave < -2 || set.octave > 2
        || set.bright > 20u || set.lead > 1u || set.hold > 1u
        || set.arp > 1u || set.arp_mode > 3u || set.arp_div > 2u || set.arp_oct < 1u || set.arp_oct > 3u
        || set.arp_len < 1u || set.arp_len > 9u || set.tempo < 40u || set.tempo > 220u
        || set.loop_len > LOOP_STEPS || set.loop_n > LOOP_MAX || (set.loop_len && !set.loop_n))
        settings_defaults();
}

void app_init(void)
{
    settings_load();
    cut_update();
    st.degree = -1;
    st.gate = 0;
    st.sig = 0;
    st.rec = 0;
    st.playing = 0;
    st.page = 0;
    st.gate_hold_off = 0;
    bl_chord_build(set.key, set.mode, 0, 0, 0, 60 + 12 * set.octave, &st.cur);
    st.prev = st.cur;
}

/* --------------------------------------------------------------- MIDI --- */
static void midi(uint32_t st_, uint32_t ch, uint32_t d1, uint32_t d2)
{
    uint32_t cin = st_ >> 4;
    plat_midi_send(cin | (st_ | (ch - 1u)) << 8 | d1 << 16 | d2 << 24, TO_USB | TO_BLE);
}

/* ------------------------------------------------------------- texto --- */
static void text_cat(char *d, const char *s)
{
    while (*d) d++;
    while (*s) *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do t[n++] = (char)('0' + u % 10u); while ((u /= 10u) && n < 11);
    if (v < 0) *b++ = '-';
    while (n) *b++ = t[--n];
    *b = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++) st.msg[0][i] = a[i];
    st.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++) st.msg[1][i] = b[i];
    st.msg[1][i] = 0;
    st.msg_until = plat_ms() + 1200u;
    st.redraw = 1;
    if (ann_on()) {
        char t[48] = "";
        text_cat(t, st.msg[0]);
        text_cat(t, " ");
        text_cat(t, st.msg[1]);
        ann_say(t, 0, ANN_NOW);
    }
}

static void key_text(char *t)
{
    t[0] = 0;
    text_cat(t, bl_note_name(set.key, set.key));
    text_cat(t, " ");
    text_cat(t, MODE_NAME[set.mode]);
}

/* ----------------------------------------------------------- acordes --- */
static void chord_notes_off(void)
{
    int i;
    for (i = 0; i < st.cur.n; i++) midi(0x80u, 2, st.cur.notes[i], 0);
    midi(0x80u, 3, st.cur.bass, 0);
}

static void chord_notes_on(void)
{
    int i;
    for (i = 0; i < st.cur.n; i++) midi(0x90u, 2, st.cur.notes[i], 100);
    midi(0x90u, 3, st.cur.bass, 100);
}

/* El acorde según lo que se tiene pulsado. Se llama en cada pasada; solo actúa si algo cambió. */
static void chord_update(int degree, unsigned mods, int inv)
{
    int gate = degree >= 0;
    uint32_t sig = (uint32_t)(degree + 1) | mods << 4 | (uint32_t)inv << 12 | (uint32_t)set.key << 14
                   | (uint32_t)set.mode << 18 | (uint32_t)(set.octave + 2) << 21 | (uint32_t)gate << 24;
    if (sig == st.sig) return;
    st.sig = sig;
    if (st.gate) chord_notes_off();
    if (gate) {
        bl_chord c;
        int base = 60 + 12 * set.octave;
        bl_chord_build(set.key, set.mode, degree, mods, inv, base, &c);
        if (set.lead && inv == 0 && st.gate)  /* encadena con el acorde que sonaba */
            bl_chord_lead(&st.cur, &c, base - 8, base + 19);
        if (st.gate) st.prev = st.cur;
        st.cur = c;
        st.degree = degree;
        st.mods = mods;
        st.inv = inv;
    }
    st.gate = gate;
    if (gate) chord_notes_on();
    publish(&st.cur, gate);
    st.redraw = 1;
}

/* La tecla de rasgueo i (0..8): las notas del acorde, de grave a agudo, desde la fundamental. */
static int strum_note(int i)
{
    int iv[BL_MAX_NOTES], n = 0, k, j, root;
    for (k = 0; k < st.cur.n; k++) {     /* intervalos únicos sobre la fundamental, ordenados */
        int d = ((st.cur.notes[k] - st.cur.root) % 12 + 12) % 12, dup = 0;
        for (j = 0; j < n; j++) if (iv[j] == d) dup = 1;
        if (dup) continue;
        for (j = n; j > 0 && iv[j - 1] > d; j--) iv[j] = iv[j - 1];
        iv[j] = d;
        n++;
    }
    root = 53 + 12 * set.octave;
    root += ((st.cur.root - root) % 12 + 12) % 12;
    return clamp(root + 12 * (i / n) + iv[i % n], 0, 127);
}

/* ------------------------------------------------------------ entrada --- */
static void set_changed(const char *a, const char *b)
{
    set_dirty = 1;
    app_say(a, b);
}

/* ------------------------------------------------------ ideas: REC / PLAY --- */
/* Se graban los cambios de acorde (grado, modificadores, inversión) en corcheas al tempo; el bucle dura
 * compases enteros. Las teclas en directo mandan sobre lo grabado mientras se mantienen. */
static float eighth_ms(void) { return 30000.0f / (float)set.tempo; }

static void seq_tick(void)
{
    uint32_t now = plat_ms();
    float d = (float)(now - st.last_ms) / eighth_ms();
    st.last_ms = now;
    if (st.rec == 2) st.rec_acc += d;
    if (st.playing && set.loop_len) {
        st.play_acc += d;
        while (st.play_acc >= (float)set.loop_len) st.play_acc -= (float)set.loop_len;
    }
}

static void seq_current(int *degree, unsigned *mods, int *inv)
{
    int i, k = set.loop_n - 1;
    uint32_t pos = (uint32_t)st.play_acc;
    for (i = 0; i < set.loop_n; i++)
        if (set.loop_ev[i].step <= pos) k = i;
    *degree = set.loop_ev[k].degree;
    *mods = set.loop_ev[k].mods;
    *inv = set.loop_ev[k].inv;
}

static void seq_play(int on)
{
    st.playing = (uint8_t)(on && set.loop_len);
    st.play_acc = 0.0f;
    if (!st.playing) st.gate_hold_off = 1;   /* al parar, el acorde calla */
    st.redraw = 1;
}

static void seq_finish(void)
{
    int len = ((int)(st.rec_acc + 4.0f) / 8) * 8;   /* al compás más cercano */
    st.rec = 0;
    if (!set.loop_n) { app_say("REC", "NADA"); return; }
    if (len < 8) len = 8;
    while (len <= set.loop_ev[set.loop_n - 1].step) len += 8;
    if (len > LOOP_STEPS) len = LOOP_STEPS;
    set.loop_len = (uint8_t)len;
    set_dirty = 1;
    {
        char t[12];
        int_text(len / 8, t);
        text_cat(t, len == 8 ? " COMPAS" : " COMPASES");
        app_say("IDEA", t);
    }
    seq_play(1);
}

static void seq_record(int degree, unsigned mods, int inv, int live)
{
    uint32_t sig = (uint32_t)(degree + 1) | mods << 4 | (uint32_t)inv << 12;
    int step;
    if (st.rec == 1 && live) {               /* el primer acorde que se toca: empieza */
        st.rec = 2;
        st.rec_acc = 0.0f;
        set.loop_n = 0;
        set.loop_len = 0;
        st.rec_sig = ~0u;
    }
    if (st.rec != 2 || sig == st.rec_sig) return;
    st.rec_sig = sig;
    step = (int)(st.rec_acc + 0.5f);
    if (step >= LOOP_STEPS) { seq_finish(); return; }
    if (set.loop_n && set.loop_ev[set.loop_n - 1].step == step) set.loop_n--;   /* en la misma corchea: el último */
    if (set.loop_n < LOOP_MAX) {
        loop_ev_t *ev = &set.loop_ev[set.loop_n++];
        ev->step = (uint8_t)step;
        ev->degree = (int8_t)degree;
        ev->mods = (uint8_t)mods;
        ev->inv = (uint8_t)inv;
    }
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k;
    unsigned mods = 0;
    int inv = 0, degree = -1, live_degree, i;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    seq_tick();

    /* grados: el último pulsado manda */
    for (i = 0; i < NDEG; i++) {
        uint32_t bit = 1u << WK[i];
        if ((keys & bit) && !(st.keys & bit)) st.deg_order[i] = ++st.deg_stamp;
        if (!(keys & bit)) st.deg_order[i] = 0;
    }
    {
        int best = 0;
        for (i = 0; i < NDEG; i++)
            if (st.deg_order[i] && (uint8_t)(st.deg_order[i] - st.deg_stamp + 128u) > best) {
                best = (uint8_t)(st.deg_order[i] - st.deg_stamp + 128u);
                degree = i;
            }
    }
    live_degree = degree;
    /* modificadores, inversiones y HOLD */
    for (i = 0; i < 8; i++) if (keys >> BK[i] & 1u) mods |= BK_MOD[i];
    if (keys >> BK[BK_INV1] & 1u) inv = 1;
    if (keys >> BK[BK_INV2] & 1u) inv = 2;
    {
        int down = (int)(keys >> BK[BK_HOLD] & 1u), was = (int)(st.keys >> BK[BK_HOLD] & 1u);
        if (down && !was) st.stop_t = plat_ms() | 1u;
        if (down && st.stop_t && plat_ms() - st.stop_t >= 1000u) {   /* mantenida: HOLD sí / no */
            st.stop_t = 0;
            set.hold = (uint8_t)!set.hold;
            set_changed("HOLD", set.hold ? "SI" : "NO");
        }
        if (!down && was && st.stop_t) {   /* pulsación corta: STOP (y la idea deja de sonar) */
            st.stop_t = 0;
            if (st.playing) seq_play(0);
            if (degree < 0) st.gate_hold_off = 1;
        }
    }
    if (st.playing && degree < 0 && !mods && !inv) {   /* la idea suena: lo grabado, salvo lo que se toque */
        seq_current(&degree, &mods, &inv);
        st.gate_hold_off = 0;
    } else if (st.gate_hold_off) {                /* parado: hasta el siguiente grado */
        if (degree >= 0) st.gate_hold_off = 0;
    } else if (degree < 0 && set.hold && st.gate) {   /* HOLD: el acorde sigue; los modificadores aún lo cambian */
        degree = st.degree;
        if (!mods) mods = st.mods;
    }
    seq_record(degree, mods, inv, live_degree >= 0);
    chord_update(degree, mods, inv);

    /* rasgueo */
    for (i = 0; i < NSTRUM; i++) {
        uint32_t bit = 1u << WK[NDEG + i];
        if ((keys & bit) && !(st.keys & bit)) {
            int note = strum_note(i), vel = (int)touch_vel(WK[NDEG + i]);
            if (vel < 1) vel = 100;
            st.strum_note[i] = (uint8_t)note;
            pluck(note, vel);
            midi(0x90u, 1, (uint32_t)note, (uint32_t)vel);
        } else if (!(keys & bit) && (st.keys & bit)) {
            midi(0x80u, 1, st.strum_note[i], 0);
        }
    }

    /* botones */
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (st.btn & bit)) continue;
        if ((k == B_OCTDN && set.octave > -2) || (k == B_OCTUP && set.octave < 2)) {
            char t[8];
            set.octave = (int8_t)(set.octave + (k == B_OCTUP ? 1 : -1));
            int_text(set.octave, t);
            set_changed("OCTAVA", t);
        } else if (k == B_REC) {
            if (st.rec == 0) {
                if (st.playing) seq_play(0);
                st.rec = 1;
                st.gate_hold_off = 1;        /* lo que sonaba calla: la idea empieza con lo que toques */
                app_say("REC", "TOCA");
            } else if (st.rec == 1) {
                st.rec = 0;
                app_say("REC", "NO");
            } else {
                seq_finish();
            }
        } else if (k == B_PLAY) {
            if (st.rec == 2) seq_finish();
            else if (!set.loop_len) app_say("PLAY", "GRABA CON REC");
            else seq_play(!st.playing);
        } else if (k == B_HOME) {
            st.page = 0;                     /* los knobs vuelven al acorde (el arpegio sigue) */
        } else if (k == B_ARP) {
            st.arp_turned = 0;               /* conmuta al soltar, si no se tocó ningún knob */
        } else if (k == B_SAVE) {
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) { set_dirty = 0; app_say("GUARDADO", ""); }
            else app_say("GUARDAR", "FALLO");
        }
    }

    if (!(btn >> B_ARP & 1u) && (st.btn >> B_ARP & 1u) && !st.arp_turned) {
        set.arp = (uint8_t)!set.arp;
        st.page = set.arp;                   /* encendido: los knobs son suyos; apagado: vuelven */
        set_changed("ARPEGIO", set.arp ? "SI" : "NO");
    }

    /* potenciómetros (con ARP mantenido, K1..K4 son del arpegiador) */
    for (k = 0; k < NE; k++) {
        char t[24];
        if ((e = plat_enc((int)k)) == 0) continue;
        if (((btn >> B_ARP & 1u) || st.page == 1) && k >= EN_K1) {
            st.arp_turned = 1;
            if (k == EN_K1) {
                set.arp_mode = (uint8_t)clamp((int32_t)set.arp_mode + e, 0, 3);
                set_changed("ARP MODO", ARP_MODE_NAME[set.arp_mode]);
            } else if (k == EN_K2) {
                set.arp_div = (uint8_t)clamp((int32_t)set.arp_div + e, 0, 2);
                set_changed("ARP PASO", ARP_DIV_NAME[set.arp_div]);
            } else if (k == EN_K3) {
                set.arp_oct = (uint8_t)clamp((int32_t)set.arp_oct + e, 1, 3);
                int_text(set.arp_oct, t);
                set_changed("ARP OCTAVAS", t);
            } else {
                set.arp_len = (uint8_t)clamp((int32_t)set.arp_len + e, 1, 9);
                int_text(set.arp_len * 10, t);
                text_cat(t, "%");
                set_changed("ARP LARGO", t);
            }
        } else if (k == EN_SELECT) {
            set.tempo = (uint8_t)clamp((int32_t)set.tempo + e, 40, 220);
            int_text(set.tempo, t);
            set_changed("TEMPO", t);
        } else if (k == EN_K1) {
            set.key = (uint8_t)(((int)set.key + e % 12 + 12) % 12);
            key_text(t);
            set_changed("TONO", t);
        } else if (k == EN_K2) {
            set.mode = (uint8_t)clamp((int32_t)set.mode + e, 0, BL_MODE_COUNT - 1);
            key_text(t);
            set_changed("MODO", t);
        } else if (k == EN_K3) {
            set.octave = (int8_t)clamp(set.octave + e, -2, 2);
            int_text(set.octave, t);
            set_changed("OCTAVA", t);
        } else if (k == EN_K4) {
            set.bright = (uint8_t)clamp((int32_t)set.bright + e, 0, 20);
            cut_update();
            int_text(set.bright * 5, t);
            set_changed("BRILLO", t);
        } else if (k == EN_PRESET) {
            set.lead ^= 1u;
            set_changed("ENCADENAR", set.lead ? "SI" : "NO");
        }
    }

    if (keys != st.keys || btn != st.btn) st.redraw = 1;
    st.keys = keys;
    st.btn = btn;
    plat_glow(1);
    {
        uint32_t lit = btn | (set_dirty ? 1u << B_SAVE : 0u) | (set.arp ? 1u << B_ARP : 0u);
        if (st.playing || st.rec == 2) lit |= 1u << B_PLAY;
        if (st.rec == 2 || (st.rec == 1 && (plat_ms() / 250u) & 1u)) lit |= 1u << B_REC;   /* armado: parpadea */
        plat_play_red(st.rec == 2);
        plat_leds(lit, keys);
    }
    {   /* la posición en pantalla, al cambiar de pulso */
        uint32_t pos = st.playing ? (uint32_t)st.play_acc / 2u + 1u : st.rec == 2 ? (uint32_t)st.rec_acc / 2u + 1000u : 0u;
        if (pos != st.shown_pos) { st.shown_pos = pos; st.redraw = 1; }
    }
}

void app_input_only(void) { input(); }

/* ----------------------------------------------------------- pantalla --- */
static void chip(int32_t x, int32_t y, int32_t w, const char *s, int on)
{
    cv_rect(x, y, w, 16, on ? C_HI : C_LINE);
    cv_text(x + (w - text_w(&FONT_XS, s)) / 2, y + 2, &FONT_XS, s, on ? C_BLACK : C_GRAY);
}

static void draw(void)
{
    char t[24];
    int i;
    uint32_t now = plat_ms();

    /* cabecera: el nombre y la tonalidad, o un mensaje un momento */
    cv_begin(240, 30, C_BLACK);
    if (now < st.msg_until) {
        int32_t x = cv_text(8, 7, &FONT_B, st.msg[0], C_WHITE);
        cv_text(x + 8, 7, &FONT_B, st.msg[1], C_HI);
    } else {
        int32_t x = cv_text(8, 7, &FONT_B, "BARULLIN", C_WHITE);
        if (set.hold) x = cv_text(x + 8, 9, &FONT_XS, "HOLD", C_AMB);
        if (set.arp) {
            char b[12] = "ARP ";
            int_text(set.tempo, t);
            text_cat(b, t);
            cv_text(x + 8, 9, &FONT_XS, b, C_HI);
        }
        key_text(t);
        cv_text(232 - text_w(&FONT_B, t), 7, &FONT_B, t, C_HI);
    }
    cv_rect(0, 29, 240, 1, C_LINE);
    cv_blit(0, 0);

    /* centro: el acorde en grande, su grado, sus notas */
    cv_begin(240, 110, C_BLACK);
    {
        const bl_chord *c = &st.cur;
        uint16_t col = st.gate ? C_HI : C_DIM;
        int32_t w = text_w(&FONT_L, c->name);
        char rom[8];
        cv_text((240 - w) / 2, 6, &FONT_L, c->name, col);
        if (st.rec || st.playing) {          /* REC / PLAY y compás.pulso */
            char b[16] = "";
            uint32_t p = (uint32_t)(st.playing ? st.play_acc : st.rec_acc);
            if (st.rec == 1) text_cat(b, "REC ...");
            else {
                char n[8];
                text_cat(b, st.rec == 2 ? "REC " : "PLAY ");
                int_text((int32_t)(p / 8u + 1u), n); text_cat(b, n);
                text_cat(b, ".");
                int_text((int32_t)(p % 8u / 2u + 1u), n); text_cat(b, n);
                if (st.playing) { text_cat(b, "/"); int_text(set.loop_len / 8, n); text_cat(b, n); }
            }
            cv_text(6, 4, &FONT_XS, b, st.rec ? C_WHITE : C_HI);
        }
        bl_degree_name(c, st.degree < 0 ? 0 : st.degree, rom, sizeof rom);
        if (st.degree >= 0) cv_text((240 - text_w(&FONT_B, rom)) / 2, 44, &FONT_B, rom, st.gate ? C_AMB : C_DIM);
        /* las notas, nombradas */
        t[0] = 0;
        for (i = 0; i < c->n; i++) {
            if (i) text_cat(t, " ");
            text_cat(t, bl_note_name(c->notes[i] % 12, set.key));
        }
        cv_text((240 - text_w(&FONT_XS, t)) / 2, 64, &FONT_XS, t, st.gate ? C_GRAY : C_DIM);
        /* un teclado de dos octavas con las notas que suenan */
        {
            int lo = 48 + 12 * set.octave;
            static const uint8_t WHITE[7] = {0, 2, 4, 5, 7, 9, 11};
            static const uint8_t BLACK_AT[5] = {1, 3, 6, 8, 10}, BLACK_X[5] = {0, 1, 3, 4, 5};
            int o, wk;
            for (o = 0; o < 3; o++)
                for (wk = 0; wk < 7; wk++) {
                    int note = lo + 12 * o + WHITE[wk], on = 0, x = 15 + (o * 7 + wk) * 10;
                    for (i = 0; i < c->n; i++) if (c->notes[i] == note) on = 1;
                    cv_rect(x, 82, 9, 26, on ? (st.gate ? C_HI : C_DIM) : C_GRAY);
                }
            for (o = 0; o < 3; o++)
                for (wk = 0; wk < 5; wk++) {
                    int note = lo + 12 * o + BLACK_AT[wk], on = 0, x = 15 + (o * 7 + BLACK_X[wk]) * 10 + 6;
                    for (i = 0; i < c->n; i++) if (c->notes[i] == note) on = 1;
                    cv_rect(x, 82, 7, 15, on ? (st.gate ? C_HI : C_DIM) : C_LINE);
                }
        }
    }
    cv_blit(0, 32);

    /* abajo: los siete grados de la tonalidad y los modificadores */
    cv_begin(240, 96, C_BLACK);
    for (i = 0; i < NDEG; i++) {
        bl_chord d;
        bl_chord_build(set.key, set.mode, i, 0, 0, 60, &d);
        chip(2 + i * 34, 0, 32, d.name, st.gate && st.degree == i);
    }
    for (i = 0; i < NBK; i++) {
        int on = (int)(st.keys >> BK[i] & 1u);
        if (i == BK_HOLD) on = (int)(st.keys >> BK[i] & 1u);
        chip(i < 6 ? 2 + i * 40 : 2 + (i - 6) * 48, i < 6 ? 22 : 42, i < 6 ? 38 : 46, BK_LABEL[i], on);
    }
    cv_rect(0, 64, 240, 1, C_LINE);
    if ((st.btn >> B_ARP & 1u) || st.page == 1) {   /* los knobs del arpegiador */
        static const char *const LBL[4] = {"K1 MODO", "K2 PASO", "K3 OCTS", "K4 LARGO"};
        char v[4][12];
        v[0][0] = 0; text_cat(v[0], ARP_MODE_NAME[set.arp_mode]);
        v[1][0] = 0; text_cat(v[1], ARP_DIV_NAME[set.arp_div]);
        int_text(set.arp_oct, v[2]);
        int_text(set.arp_len * 10, v[3]); text_cat(v[3], "%");
        for (i = 0; i < 4; i++) {
            cv_text(4 + i * 60, 68, &FONT_XS, LBL[i], C_AMB);
            cv_text(4 + i * 60, 81, &FONT_XS, v[i], C_WHITE);
        }
    } else {
        static const char *const LBL[4] = {"K1 TONO", "K2 MODO", "K3 OCT", "K4 BRILLO"};
        char v[4][12];
        v[0][0] = 0; text_cat(v[0], bl_note_name(set.key, set.key));
        v[1][0] = 0; text_cat(v[1], MODE_NAME[set.mode]);
        int_text(set.octave, v[2]);
        int_text(set.bright * 5, v[3]);
        for (i = 0; i < 4; i++) {
            cv_text(4 + i * 60, 68, &FONT_XS, LBL[i], C_GRAY);
            cv_text(4 + i * 60, 81, &FONT_XS, v[i], C_HI);
        }
    }
    cv_blit(0, 144);
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    st.redraw = 1;
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    if (ann_on() && ann_where()) {
        char t[48] = "";
        text_cat(t, "BARULLIN. ");
        text_cat(t, st.cur.name);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(now);
    if (st.msg_until && now >= st.msg_until) {
        st.msg_until = 0;
        st.redraw = 1;
    }
    if (st.redraw) {
        st.redraw = 0;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "degree")) *got = st.gate ? st.degree : -1;
    else if (!strcmp(what, "nnotes")) *got = st.cur.n;
    else if (!strncmp(what, "note", 4)) *got = st.cur.notes[atoi(what + 4) % BL_MAX_NOTES];
    else if (!strcmp(what, "mods")) *got = (int)st.mods;
    else if (!strcmp(what, "key")) *got = set.key;
    else if (!strcmp(what, "mode")) *got = set.mode;
    else if (!strcmp(what, "hold")) *got = set.hold;
    else if (!strcmp(what, "dirty")) *got = set_dirty;
    else if (!strcmp(what, "arp")) *got = set.arp;
    else if (!strcmp(what, "page")) *got = st.page;
    else if (!strcmp(what, "rec")) *got = st.rec;
    else if (!strcmp(what, "playing")) *got = st.playing;
    else if (!strcmp(what, "loop_len")) *got = set.loop_len;
    else if (!strcmp(what, "loop_n")) *got = set.loop_n;
    else if (!strcmp(what, "arp_mode")) *got = set.arp_mode;
    else if (!strcmp(what, "tempo")) *got = set.tempo;
    else if (!strcmp(what, "arp_moving")) *got = arp_steps >= 4u;   /* al menos cuatro pasos tocados */
    else return 0;
    return 1;
}
#endif
