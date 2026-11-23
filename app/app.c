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
#define SET_MAGIC 0x42524C3Eu            /* "BRL>" */
#define LOOP_MAX 64                      /* cambios de acorde en una idea */
#define LOOP_STEPS 128                   /* 16 compases de corcheas */
typedef struct { uint8_t step; int8_t degree; uint8_t mods; uint8_t inv; } loop_ev_t;
typedef struct {
    uint8_t dest;                        /* 0 no, 1 filtro (pad) / brillo (arpegio, bajo), 2 volumen, 3 afinación, 4 paneo */
    uint8_t rate;                        /* 0..15: 0,1 .. 10 Hz */
    uint8_t depth;                       /* 0..10 */
    uint8_t shape;                       /* 0 seno, 1 triángulo, 2 cuadrada, 3 azar */
} lfo_t;
enum { LFO_PAD, LFO_ARP, LFO_BASS };
#define NBANK 8                          /* bancos de ideas (SEQ pasa de uno a otro) */
typedef struct {
    uint8_t len;                         /* corcheas (0: vacío), compases enteros */
    uint8_t n;                           /* sus cambios de acorde */
    loop_ev_t ev[LOOP_MAX];
} loop_t;
typedef struct {
    uint32_t magic;
    uint8_t key;                         /* 0 = C .. 11 = B */
    uint8_t mode;                        /* enum bl_mode */
    int8_t octave;                       /* -2 .. +2 */
    uint8_t bright;                      /* 0..20: el corte del filtro del pad */
    uint8_t lead;                        /* voice leading */
    uint8_t hold;                        /* el acorde sigue al soltar (por defecto, sí) */
    uint8_t arp;                         /* arpegiador: 0 no, 1 solo, 2 con el pad */
    uint8_t arp_mode;                    /* 0 sube, 1 baja, 2 sube-baja, 3 azar */
    uint8_t arp_div;                     /* 0 corcheas, 1 tresillos, 2 semicorcheas */
    uint8_t arp_oct;                     /* 1..3 octavas */
    uint8_t arp_len;                     /* 1..9: duración de cada nota, en décimas del paso */
    uint8_t tempo;                       /* 40..220 BPM */
    uint8_t dly_mix;                     /* 0..10: cuánto delay */
    uint8_t dly_div;                     /* 0..5: 1/16 1/8 1/8. 1/4 1/4. 1/2 */
    uint8_t dly_fb;                      /* 0..9: repeticiones */
    uint8_t rev_mix;                     /* 0..10: cuánta reverb */
    uint8_t dly_on, rev_on;
    uint8_t rev_size;                    /* 0..9: tamaño de la sala */
    uint8_t rev_tone;                    /* 0..9: oscura .. brillante */
    uint8_t env_a, env_d, env_s, env_r;  /* la envolvente del pad: ataque, caída (0..9), sostenido (0..10), release (0..9) */
    uint8_t aenv_a, aenv_d, aenv_s, aenv_r;   /* la envolvente del arpegio, igual */
    uint8_t benv_a, benv_d, benv_s, benv_r;   /* la del bajo (el de siempre y el arpegiado) */
    uint8_t bp_on;                       /* bajo pulsado: el bajo repite su nota al tempo */
    uint8_t bp_div;                      /* 0..3: 1/4 1/8 1/8T 1/16 */
    uint8_t bp_pat;                      /* 0 la nota, 1 octavas, 2 quinta */
    uint8_t bp_len;                      /* 1..9: duración de cada nota, en décimas del paso */
    lfo_t lfo[3];                        /* un LFO por sonido: pad, arpegio, bajo */
    uint8_t cur_bank;                    /* el banco elegido: REC graba en él, PLAY lo toca */
    loop_t bank[NBANK];
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
static const char *const ARP_STATE_NAME[3] = {"NO", "SOLO", "+PAD"};
/* las páginas de los knobs: cada botón recorre las suyas; HOME vuelve a la principal */
enum { PG_MAIN, PG_ARP, PG_DLY, PG_REV, PG_ENV_PAD, PG_ENV_ARP, PG_ENV_BASS, PG_BP, PG_LFO_PAD, PG_LFO_ARP, PG_LFO_BASS, NPG };
static const char *const PAGE_NAME[NPG] = {"", "ARPEGIADOR", "DELAY", "REVERB", "ENV PAD", "ENV ARP", "ENV BAJO",
                                           "BAJO PULSADO", "LFO PAD", "LFO ARP", "LFO BAJO"};
static const char *const ARP_DIV_NAME[3] = {"1/8", "1/8T", "1/16"};
static const char *const BP_DIV_NAME[4] = {"1/4", "1/8", "1/8T", "1/16"};
static const char *const BP_PAT_NAME[3] = {"NOTA", "OCTAVAS", "QUINTA"};
static const char *const LFO_DEST_NAME[5] = {"NO", "FILTRO", "VOLUMEN", "AFINAR", "PANEO"};
static const char *const LFO_PART_NAME[3] = {"PAD", "ARP", "BAJO"};
static const char *lfo_dest_name(int part, int d) { return d == 1 && part != LFO_PAD ? "BRILLO" : LFO_DEST_NAME[d]; }
static const char *const LFO_SHAPE_NAME[4] = {"SENO", "TRIANG", "CUADRA", "AZAR"};
static float lfo_hz(int r) { return 0.1f * fm_exp2f((float)r * (6.6439f / 15.0f)); }   /* 0,1 .. 10 Hz */
static const char *const DLY_DIV_NAME[6] = {"1/16", "1/8", "1/8.", "1/4", "1/4.", "1/2"};
static const float DLY_BEATS[6] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
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
#define NPLK 12
typedef struct { float p1, p2, i1, i2, env; int note, gate, stage; } pad_v;   /* stage: 0 ataque, 1 caída */

/* los tiempos de la envolvente, en segundos (exponenciales: el knob se siente igual en todo su recorrido) */
static float env_att_s(int a) { return 0.002f * fm_exp2f((float)a * (9.9658f / 9.0f)); }   /* 2 ms .. 2 s */
static float env_dec_s(int d) { return 0.05f * fm_exp2f((float)d * (5.9069f / 9.0f)); }    /* 50 ms .. 3 s */
static float env_rel_s(int r) { return 0.02f * fm_exp2f((float)r * (7.6439f / 9.0f)); }    /* 20 ms .. 4 s */
typedef struct {
    float ph, inc, env, amp;
    int32_t left;                        /* arpegio: muestras que le quedan sonando; -1 un rasgueo (se apaga solo) */
    uint8_t note;
    uint8_t stage;                       /* arpegio: 0 ataque, 1 caída */
    uint8_t low;                         /* arpegio del bajo: seno grave, seco, MIDI canal 3 */
} plk_v;
static pad_v pad[NPAD];
static plk_v plk[NPLK];
static float bass_ph, bass_inc, bass_env;
static int bass_gate, bass_stage, bass_note;
static float lp1[2], lp2[2], gain_now;
static uint32_t snd_seen, plq_r, plk_next;
static snd_chord achord;                 /* el acorde según el audio (para el arpegio) */
static int arp_was;                      /* el arpegiador sonaba en el bloque anterior */
/* El reloj maestro: pulsos (negras) y su fracción, contados en el audio a la muestra. El arpegio, el bajo
 * pulsado, el reloj MIDI y las ideas grabadas van todos a esta rejilla, así que siempre caen juntos. */
static uint32_t tp_bint;                 /* pulsos enteros */
static float tp_bfrac;                   /* y la fracción, 0..1 */
static volatile float tp_beats;          /* la posición para el UI: pulsos (vuelve a 0 al reiniciar) */
static volatile uint32_t tp_resets;      /* cuántas veces se reinició (el UI lo ve) */
static volatile uint32_t tp_reset_req;   /* el UI pide reiniciar (PLAY): distinto de tp_reset_done */
static uint32_t tp_reset_done;
static volatile uint8_t tp_locked;       /* una idea suena: un acorde nuevo no reinicia el reloj */
static int tp_gate_was;
static uint32_t clk_last = ~0u, arp_last = ~0u, bp_last = ~0u;   /* el último paso tocado de cada uno */
static uint32_t arp_i, arp_rng = 0x1234567u;
static volatile uint32_t arp_steps;      /* pasos tocados (para las pruebas) */

static float note_inc(int note) { return 440.0f * fm_exp2f((float)(note - 69) / 12.0f) / SR; }

/* el acorde nuevo: las notas que ya suenan siguen sin cortarse; las demás se sueltan o se asignan */
static void take_chord(const snd_chord *c)
{
    int i, j, used[NPAD] = {0};
    int pad_gate = c->gate && set.arp != 1;   /* arpegio solo: el pad calla; arpegio + pad: suena */
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
            pad[best].stage = 0;
            pad[best].i1 = note_inc(c->notes[i]) * 1.0040f;   /* +7 cents */
            pad[best].i2 = note_inc(c->notes[i]) * 0.9960f;   /* -7 cents */
            used[best] = 1;
        }
    {
        int g = c->gate && !set.arp && !set.bp_on;   /* con arpegio o bajo pulsado, sin bajo sostenido */
        if (g && (!bass_gate || c->bass != bass_note)) bass_stage = 0;   /* nota nueva: ataque */
        bass_gate = g;
        bass_note = c->bass;
    }
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

/* ------------------------------------------------------------ efectos --- */
/* Delay estéreo de rebote, sincronizado al tempo (en int16 para que quepa: 0,75 s por canal), y una
 * reverb tipo Freeverb pequeña (4 filtros peine y 2 pasatodo por canal). Los buffers van en el pool. */
#define DLY_MAX 33075                    /* 0,75 s */
static int16_t dly_buf[2][DLY_MAX] PLAT_POOL;
static uint32_t dly_w;
static float dly_cur = 11025.0f;         /* el tiempo actual, en muestras (se desliza hacia el pedido) */
#define NCOMB 4
#define NAP 2
static const uint16_t COMB_LEN[NCOMB] = {1116, 1188, 1277, 1356}, AP_LEN[NAP] = {556, 441};
#define RSPREAD 23
static float comb_buf[2][NCOMB][1356 + RSPREAD] PLAT_POOL;
static float ap_buf[2][NAP][556 + RSPREAD] PLAT_POOL;
static uint16_t comb_i[2][NCOMB], ap_i[2][NAP];
static float comb_lp[2][NCOMB];
static float rv_fb = 0.84f, rv_tone = 0.8f;   /* del bloque: tamaño y tono */

static float dly_target(void)
{
    float d = SR * 60.0f / (float)set.tempo * DLY_BEATS[set.dly_div % 6u];
    return d > (float)(DLY_MAX - 2) ? (float)(DLY_MAX - 2) : d;
}

static inline float dly_read(int ch, float d)
{
    float pos = (float)dly_w - d;
    int32_t i0, i1;
    float fr;
    if (pos < 0.0f) pos += (float)DLY_MAX;
    i0 = (int32_t)pos;
    if (i0 >= DLY_MAX) i0 = DLY_MAX - 1;
    fr = pos - (float)i0;
    i1 = i0 + 1 >= DLY_MAX ? 0 : i0 + 1;
    return ((float)dly_buf[ch][i0] * (1.0f - fr) + (float)dly_buf[ch][i1] * fr) * (1.0f / 32767.0f);
}

static inline int16_t sat16(float v)
{
    v *= 32767.0f;
    return (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
}

static inline float reverb_ch(int ch, float in)
{
    float out = 0.0f;
    int k;
    for (k = 0; k < NCOMB; k++) {
        uint16_t len = (uint16_t)(COMB_LEN[k] + (ch ? RSPREAD : 0));
        float *b = comb_buf[ch][k], y = b[comb_i[ch][k]];
        comb_lp[ch][k] = y * rv_tone + comb_lp[ch][k] * (1.0f - rv_tone);   /* los agudos se apagan antes */
        b[comb_i[ch][k]] = in + comb_lp[ch][k] * rv_fb;
        if (++comb_i[ch][k] >= len) comb_i[ch][k] = 0;
        out += y;
    }
    for (k = 0; k < NAP; k++) {
        uint16_t len = (uint16_t)(AP_LEN[k] + (ch ? RSPREAD : 0));
        float *b = ap_buf[ch][k], bo = b[ap_i[ch][k]];
        b[ap_i[ch][k]] = out + bo * 0.5f;
        out = bo - out;
        if (++ap_i[ch][k] >= len) ap_i[ch][k] = 0;
    }
    return out;
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

static void arp_voice(int note, int low, float step, int len)
{
    plk_v *p = &plk[plk_next++ % NPLK];
    if (p->left > 0) midi_ch(0x80u, p->low ? 3u : 4u, p->note, 0);
    p->ph = 0.0f;
    p->inc = note_inc(note);
    p->amp = low ? 0.34f : 0.26f;
    p->env = 0.0f;
    p->stage = 0;
    p->low = (uint8_t)low;
    p->note = (uint8_t)note;
    p->left = (int32_t)(step * (float)len / 10.0f);
    if (p->left < 1) p->left = 1;
    midi_ch(0x90u, low ? 3u : 4u, (uint32_t)note, 100);
}

static void arp_fire(float step)
{
    int note = arp_note(arp_i++);
    if (note < 0) return;
    arp_voice(note, 0, step, set.arp_len);
    arp_steps++;
}

static void arp_release(int low)        /* las voces del arpegio (low 0) o del bajo pulsado (low 1) */
{
    int j;
    for (j = 0; j < NPLK; j++)
        if (plk[j].left > 0 && plk[j].low == low) plk[j].left = 1;   /* se sueltan en la siguiente muestra (y su note off) */
}
#define arp_release_all() arp_release(0)

/* el bajo pulsado: su paso y su nota (la del bajo; con octavas o quinta, uno sí y otro no) */
static uint32_t bp_i;
static int bp_was;
static float bp_step_len(void)
{
    static const float PER_BEAT[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    return SR * 60.0f / (float)set.tempo / PER_BEAT[set.bp_div % 4u];
}
static void bp_fire(float step)
{
    int note = achord.bass;
    if (bp_i++ & 1u) note += set.bp_pat == 1 ? 12 : set.bp_pat == 2 ? 7 : 0;
    arp_voice(clamp(note, 0, 127), 1, step, set.bp_len);
}

static float e_att, e_dec, e_rel, e_sus;
static float lfo_ph[3], trem_now[3] = {1.0f, 1.0f, 1.0f}, pan_now[3];
static uint32_t lfo_rng[3] = {0x2468ACEu, 0x13579BDu, 0x0F1E2D3u};

/* lo que cada LFO pide este bloque: afinación, volumen, paneo y brillo de su sonido */
typedef struct { float pitch, trem, pan, harm; } lfo_out_t;
static float lfo_step(int k, uint32_t n)       /* avanza el LFO k un bloque; su valor, -1..1 */
{
    const lfo_t *L = &set.lfo[k];
    float ph;
    lfo_ph[k] += lfo_hz(L->rate) * (float)n / SR;
    if (lfo_ph[k] >= 1.0f) { lfo_ph[k] -= 1.0f; lfo_rng[k] = lfo_rng[k] * 1664525u + 1013904223u; }   /* azar: uno por ciclo */
    ph = lfo_ph[k];
    switch (L->shape) {
    case 1: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    case 2: return ph < 0.5f ? 1.0f : -1.0f;
    case 3: return (float)(lfo_rng[k] >> 8 & 0xFFFFu) / 32767.5f - 1.0f;
    default: return fm_sinf(ph * 6.2831853f);
    }
}
static float ae_att, ae_dec, ae_rel, ae_sus;   /* la del arpegio */
static float be_att, be_dec, be_rel, be_sus;   /* la del bajo */

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, seq = snd_seq;
    int j;
    float cut = snd_cut;
    lfo_out_t lo[3];
    {
        int k;
        for (k = 0; k < 3; k++) {
            const lfo_t *L = &set.lfo[k];
            lo[k].pitch = 1.0f; lo[k].trem = 1.0f; lo[k].pan = 0.0f; lo[k].harm = 0.0f;
            if (L->dest && L->depth) {
                float v = lfo_step(k, n), depth = (float)L->depth / 10.0f;
                if (L->dest == 1) {
                    if (k == LFO_PAD) {      /* filtro: hasta dos octavas arriba y abajo */
                        float fc = 300.0f * fm_exp2f((float)set.bright * 0.25f + v * depth * 2.0f);
                        if (fc > 16000.0f) fc = 16000.0f;
                        cut = 1.0f - fm_expf(-6.2831853f * fc / SR);
                    } else lo[k].harm = v * depth;                                     /* brillo: armónicos */
                } else if (L->dest == 2) lo[k].trem = 1.0f - depth * (0.5f - 0.5f * v);   /* volumen */
                else if (L->dest == 3) lo[k].pitch = fm_exp2f(v * depth * 0.5f / 12.0f);  /* hasta medio tono */
                else lo[k].pan = v * depth;                                              /* paneo */
            }
        }
    }
    float pitch_mul = lo[LFO_PAD].pitch;
    float harm_a = 0.35f * (1.0f + lo[LFO_ARP].harm), harm_b = 0.45f * (0.5f + 0.5f * lo[LFO_BASS].harm) * (set.lfo[LFO_BASS].dest == 1);
    float gain = (float)master_q12 / 4096.0f * 7000000.0f;
    float dmix = set.dly_on ? (float)set.dly_mix * 0.07f : 0.0f, dfb = set.dly_on ? (float)set.dly_fb * 0.09f : 0.0f;
    float rmix = set.rev_on ? (float)set.rev_mix * 0.12f : 0.0f, dtarget = dly_target();
    float binc = (float)set.tempo / 60.0f / SR;   /* pulsos por muestra */
    rv_fb = 0.70f + (float)set.rev_size * 0.024f;
    rv_tone = 0.25f + (float)set.rev_tone * 0.08f;
    {   /* la envolvente, una vez por bloque */
        e_att = 1.0f / (env_att_s(set.env_a) * SR);
        e_dec = fm_expf(-1.0f / (env_dec_s(set.env_d) * SR));
        e_rel = fm_expf(-1.0f / (env_rel_s(set.env_r) * SR));
        e_sus = (float)set.env_s / 10.0f;
        ae_att = 1.0f / (env_att_s(set.aenv_a) * SR);
        ae_dec = fm_expf(-1.0f / (env_dec_s(set.aenv_d) * SR));
        ae_rel = fm_expf(-1.0f / (env_rel_s(set.aenv_r) * SR));
        ae_sus = (float)set.aenv_s / 10.0f;
        be_att = 1.0f / (env_att_s(set.benv_a) * SR);
        be_dec = fm_expf(-1.0f / (env_dec_s(set.benv_d) * SR));
        be_rel = fm_expf(-1.0f / (env_rel_s(set.benv_r) * SR));
        be_sus = (float)set.benv_s / 10.0f;
    }
    if (seq != snd_seen) {
        snd_seen = seq;
        take_chord(&snd_buf[seq & 1u]);
    }
    take_plucks();
    {   /* el arpegiador encendido o apagado: el pad entra o sale, el reloj MIDI arranca o para */
        int on = set.arp != 0, bon = set.bp_on != 0;
        int run = on || bon, ran = arp_was != 0 || bp_was;   /* algo va al tempo: reloj MIDI */
        if (set.arp != arp_was || bon != bp_was) {   /* el pad y el bajo entran o salen */
            arp_was = set.arp;
            bp_was = bon;
            take_chord(&achord);
            if (run && !ran) midi_rt(0xFAu);
            else if (!run && ran) midi_rt(0xFCu);
            if (!on) arp_release(0);
            if (!bon) arp_release(1);
            /* el que se enciende espera a la siguiente línea de la rejilla: entra a tiempo con lo que ya suena */
            if (on) arp_last = tp_bint * (uint32_t)(set.arp_div == 0 ? 2 : set.arp_div == 1 ? 3 : 4)
                               + (uint32_t)(tp_bfrac * (float)(set.arp_div == 0 ? 2 : set.arp_div == 1 ? 3 : 4));
            if (bon) bp_last = tp_bint * (uint32_t)(set.bp_div + 1u) + (uint32_t)(tp_bfrac * (float)(set.bp_div + 1u));
        }
        if (!on || !achord.gate) {
            if (achord.gate == 0) arp_release(0);
            arp_i = 0;
        }
        if (!bon || !achord.gate) {
            if (achord.gate == 0) arp_release(1);
            bp_i = 0;
        }
        /* el reloj vuelve a cero: si lo pide PLAY, o con el primer acorde tras el silencio (sin idea sonando);
         * entonces el arpegio y el bajo tocan en el acto */
        if (tp_reset_req != tp_reset_done || (achord.gate && !tp_gate_was && !tp_locked)) {
            tp_reset_done = tp_reset_req;
            tp_bint = 0;
            tp_bfrac = 0.0f;
            clk_last = arp_last = bp_last = ~0u;
            tp_resets++;
        }
        tp_gate_was = achord.gate;
    }
    { uint32_t pkt; int port; while (plat_midi_in(&pkt, &port)) {} }   /* (la entrada MIDI llegará más adelante) */
    for (i = 0; i < n; i++) {
        float l = 0.0f, r = 0.0f, b, s, lowsum = 0.0f;
        {   /* el reloj maestro avanza; cada uno toca al cruzar su línea de la rejilla */
            static const uint8_t ARP_PB[3] = {2, 3, 4}, BP_PB[4] = {1, 2, 3, 4};
            uint32_t idx;
            tp_bfrac += binc;
            if (tp_bfrac >= 1.0f) { tp_bfrac -= 1.0f; tp_bint++; }
            idx = tp_bint * 24u + (uint32_t)(tp_bfrac * 24.0f);
            if (idx != clk_last) { clk_last = idx; if (set.arp || set.bp_on) midi_rt(0xF8u); }
            if (set.arp) {
                uint32_t pb = ARP_PB[set.arp_div % 3u];
                idx = tp_bint * pb + (uint32_t)(tp_bfrac * (float)pb);
                if (idx != arp_last) { arp_last = idx; if (achord.gate) arp_fire(arp_step_len()); }
            }
            if (set.bp_on) {
                uint32_t pb = BP_PB[set.bp_div % 4u];
                idx = tp_bint * pb + (uint32_t)(tp_bfrac * (float)pb);
                if (idx != bp_last) { bp_last = idx; if (achord.gate) bp_fire(bp_step_len()); }
            }
        }
        for (j = 0; j < NPAD; j++) {
            pad_v *v = &pad[j];
            if (v->gate) {
                if (v->stage == 0) { v->env += e_att; if (v->env >= 1.0f) { v->env = 1.0f; v->stage = 1; } }
                else v->env = e_sus + (v->env - e_sus) * e_dec;
            } else if (v->env > 0.0f) { v->env *= e_rel; if (v->env < 0.0001f) v->env = 0.0f; }
            if (v->env == 0.0f) continue;
            v->p1 += v->i1 * pitch_mul; if (v->p1 >= 1.0f) v->p1 -= 1.0f;
            v->p2 += v->i2 * pitch_mul; if (v->p2 >= 1.0f) v->p2 -= 1.0f;
            l += (2.0f * v->p1 - 1.0f) * v->env * 0.11f;
            r += (2.0f * v->p2 - 1.0f) * v->env * 0.11f;
        }
        /* filtro paso bajo de dos polos por canal: el pad, tibio */
        lp1[0] += (l - lp1[0]) * cut; lp2[0] += (lp1[0] - lp2[0]) * cut;
        lp1[1] += (r - lp1[1]) * cut; lp2[1] += (lp1[1] - lp2[1]) * cut;
        {   /* volumen y paneo de cada LFO, sin escalones */
            int k;
            for (k = 0; k < 3; k++) {
                trem_now[k] += (lo[k].trem - trem_now[k]) * 0.002f;
                pan_now[k] += (lo[k].pan - pan_now[k]) * 0.002f;
            }
        }
        l = lp2[0] * trem_now[0] * (pan_now[0] > 0.0f ? 1.0f - pan_now[0] : 1.0f);
        r = lp2[1] * trem_now[0] * (pan_now[0] < 0.0f ? 1.0f + pan_now[0] : 1.0f);
        /* rasgueos: seno con algo de segundo armónico, que se apaga solo */
        for (j = 0; j < NPLK; j++) {
            plk_v *p = &plk[j];
            if (p->env < 0.0005f && p->left <= 0) continue;
            p->ph += p->inc * (p->left < 0 ? 1.0f : p->low ? lo[LFO_BASS].pitch : lo[LFO_ARP].pitch);
            if (p->ph >= 1.0f) p->ph -= 1.0f;
            if (p->left < 0) p->env *= 0.99990f;            /* rasgueo: se apaga solo */
            else if (p->left > 0) {                           /* arpegio: su envolvente mientras dura el paso */
                float at = p->low ? be_att : ae_att, su = p->low ? be_sus : ae_sus, de = p->low ? be_dec : ae_dec;
                if (p->stage == 0) { p->env += at; if (p->env >= 1.0f) { p->env = 1.0f; p->stage = 1; } }
                else p->env = su + (p->env - su) * de;
                if (--p->left == 0) midi_ch(0x80u, p->low ? 3u : 4u, p->note, 0);
            } else p->env *= p->low ? be_rel : ae_rel;        /* y su release */
            if (p->low) {                                     /* bajo arpegiado: seno, sin efectos */
                lowsum += (fm_sinf(p->ph * 6.2831853f) + harm_b * fm_sinf(p->ph * 12.566371f)) * p->env * p->amp;
                continue;
            }
            if (p->left < 0) {                                /* rasgueo: sin LFO */
                s = (fm_sinf(p->ph * 6.2831853f) + 0.35f * fm_sinf(p->ph * 12.566371f)) * p->env * p->amp;
                l += s * 1.1f; r += s * 0.9f;
            } else {                                          /* arpegio: con su LFO */
                s = (fm_sinf(p->ph * 6.2831853f) + harm_a * fm_sinf(p->ph * 12.566371f)) * p->env * p->amp * trem_now[1];
                l += s * 1.1f * (pan_now[1] > 0.0f ? 1.0f - pan_now[1] : 1.0f);
                r += s * 0.9f * (pan_now[1] < 0.0f ? 1.0f + pan_now[1] : 1.0f);
            }
        }
        /* efectos: delay de rebote y reverb, sobre el pad, el rasgueo y el arpegio */
        if (dmix > 0.0f || dfb > 0.0f) {
            float yl, yr;
            dly_cur += (dtarget - dly_cur) * 0.0002f;
            yl = dly_read(0, dly_cur);
            yr = dly_read(1, dly_cur);
            dly_buf[0][dly_w] = sat16((l + r) * 0.5f + yr * dfb);   /* lo nuevo entra por la izquierda */
            dly_buf[1][dly_w] = sat16(yl * dfb);                      /* y rebota a la derecha */
            if (++dly_w >= DLY_MAX) dly_w = 0;
            l += yl * dmix;
            r += yr * dmix;
        }
        if (rmix > 0.0f) {
            float in = (l + r) * 0.03f;
            l += reverb_ch(0, in) * rmix;
            r += reverb_ch(1, in) * rmix;
        }
        lowsum *= trem_now[2];               /* el bajo arpegiado, seco, con su LFO */
        l += lowsum * (pan_now[2] > 0.0f ? 1.0f - pan_now[2] : 1.0f);
        r += lowsum * (pan_now[2] < 0.0f ? 1.0f + pan_now[2] : 1.0f);
        /* bajo: un seno, seco */
        if (bass_gate) {
            if (bass_stage == 0) { bass_env += be_att; if (bass_env >= 1.0f) { bass_env = 1.0f; bass_stage = 1; } }
            else bass_env = be_sus + (bass_env - be_sus) * be_dec;
        } else bass_env *= be_rel;
        if (bass_env > 0.0001f) {
            bass_ph += bass_inc * lo[LFO_BASS].pitch; if (bass_ph >= 1.0f) bass_ph -= 1.0f;
            b = (fm_sinf(bass_ph * 6.2831853f) + harm_b * fm_sinf(bass_ph * 12.566371f)) * bass_env * 0.30f * trem_now[2];
            l += b * (pan_now[2] > 0.0f ? 1.0f - pan_now[2] : 1.0f);
            r += b * (pan_now[2] < 0.0f ? 1.0f + pan_now[2] : 1.0f);
        }
        gain_now += (gain - gain_now) * 0.004f;
        lr[2 * i] = (int32_t)(fm_tanhf(l) * gain_now);
        lr[2 * i + 1] = (int32_t)(fm_tanhf(r) * gain_now);
    }
    tp_beats = (float)tp_bint + tp_bfrac;    /* para el UI: dónde va el reloj */
}

/* (tp_beats se escribe al final de cada bloque: ver app_render_end) */
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
    uint8_t mem_arp, mem_fx, mem_env, mem_lfo;   /* la última página de cada botón: se vuelve a ella */
    uint32_t leds;                       /* los botones iluminados (para las pruebas) */
    uint8_t page;                        /* de quién son K1..K4: 0 acorde, 1 arpegiador, 2 delay, 3 reverb,
                                          * 4..6 envolventes (pad, arpegio, bajo), 7 bajo pulsado */
    uint8_t rec;                         /* 0 no, 1 armado (espera el primer acorde), 2 grabando */
    uint8_t play_bank;                   /* el banco que suena */
    uint8_t next_bank;                   /* el que sonará al acabar el bucle (0xFF: ninguno) */
    uint8_t playing;
    float rec_acc, play_acc;             /* corcheas desde que empezó la grabación / dentro del bucle */
    uint32_t rec_sig, shown_pos;
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
    set.dly_mix = 3;
    set.dly_div = 2;
    set.dly_fb = 4;
    set.rev_mix = 4;
    set.dly_on = 0;                          /* los efectos, apagados de serie */
    set.rev_on = 0;
    set.rev_size = 6;
    set.rev_tone = 7;
    set.env_a = 5;                           /* ~90 ms: lo que sonaba antes */
    set.env_d = 5;
    set.env_s = 10;
    set.env_r = 5;                           /* ~0,4 s */
    set.aenv_a = 0;                          /* el arpegio, como sonaba: golpe y caída larga */
    set.aenv_d = 6;
    set.aenv_s = 0;
    set.aenv_r = 1;
    set.bp_on = 0;
    set.bp_div = 1;
    set.bp_pat = 0;
    set.bp_len = 5;
    {
        int k;
        for (k = 0; k < 3; k++) {            /* los LFO, apagados de serie */
            set.lfo[k].dest = 0;
            set.lfo[k].rate = 6;
            set.lfo[k].depth = 5;
            set.lfo[k].shape = 0;
        }
    }
    set.benv_a = 3;                          /* el bajo, como sonaba: ~20 ms de ataque, ~65 ms de release */
    set.benv_d = 5;
    set.benv_s = 10;
    set.benv_r = 2;
    set.cur_bank = 0;
    {
        int b;
        for (b = 0; b < NBANK; b++) set.bank[b].len = set.bank[b].n = 0;
    }
}

static int lfos_ok(void)
{
    int k;
    for (k = 0; k < 3; k++)
        if (set.lfo[k].dest > 4u || set.lfo[k].rate > 15u || set.lfo[k].depth > 10u || set.lfo[k].shape > 3u) return 0;
    return 1;
}

static int banks_ok(void)
{
    int b;
    for (b = 0; b < NBANK; b++) {
        const loop_t *L = &set.bank[b];
        if (L->len > LOOP_STEPS || L->n > LOOP_MAX || (L->len && !L->n)) return 0;
    }
    return 1;
}

static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.key > 11u || set.mode >= BL_MODE_COUNT || set.octave < -2 || set.octave > 2
        || set.bright > 20u || set.lead > 1u || set.hold > 1u
        || set.arp > 2u || set.arp_mode > 3u || set.arp_div > 2u || set.arp_oct < 1u || set.arp_oct > 3u
        || set.arp_len < 1u || set.arp_len > 9u || set.tempo < 40u || set.tempo > 220u
        || set.dly_mix > 10u || set.dly_div > 5u || set.dly_fb > 9u || set.rev_mix > 10u
        || set.dly_on > 1u || set.rev_on > 1u || set.rev_size > 9u || set.rev_tone > 9u
        || set.env_a > 9u || set.env_d > 9u || set.env_s > 10u || set.env_r > 9u
        || set.aenv_a > 9u || set.aenv_d > 9u || set.aenv_s > 10u || set.aenv_r > 9u || set.bp_on > 1u || set.bp_div > 3u || set.bp_pat > 2u || set.bp_len < 1u || set.bp_len > 9u
        || !lfos_ok()
        || set.benv_a > 9u || set.benv_d > 9u || set.benv_s > 10u || set.benv_r > 9u
        || set.cur_bank >= NBANK || !banks_ok())
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
    st.next_bank = 0xFFu;
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

static void time_text(float sec, char *t)   /* "80ms", "1.2s" */
{
    if (sec < 1.0f) {
        int_text((int32_t)(sec * 1000.0f + 0.5f), t);
        text_cat(t, "ms");
    } else {
        char d[4];
        int32_t ds = (int32_t)(sec * 10.0f + 0.5f);
        int_text(ds / 10, t);
        text_cat(t, ".");
        int_text(ds % 10, d);
        text_cat(t, d);
        text_cat(t, "s");
    }
}

static void hz_text(float hz, char *t)       /* "0.5Hz", "10Hz" */
{
    if (hz < 9.95f) {
        char d[4];
        int32_t dh = (int32_t)(hz * 10.0f + 0.5f);
        int_text(dh / 10, t);
        text_cat(t, ".");
        int_text(dh % 10, d);
        text_cat(t, d);
    } else {
        int_text((int32_t)(hz + 0.5f), t);
    }
    text_cat(t, "Hz");
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
    if (!set.arp && !set.bp_on) midi(0x90u, 3, st.cur.bass, 100);   /* el bajo sostenido, solo si suena */
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

/* ------------------------------------------------------------ páginas --- */
static const char *page_state(int pg)    /* lo que se dice al entrar: si está encendido */
{
    switch (pg) {
    case PG_ARP: return ARP_STATE_NAME[set.arp];
    case PG_BP: return set.bp_on ? "SI" : "NO";
    case PG_DLY: return set.dly_on ? "SI" : "NO";
    case PG_REV: return set.rev_on ? "SI" : "NO";
    case PG_LFO_PAD: case PG_LFO_ARP: case PG_LFO_BASS:
        return lfo_dest_name(pg - PG_LFO_PAD, set.lfo[pg - PG_LFO_PAD].dest);
    default: return "";
    }
}

/* si lo de la página está encendido (las envolventes no se encienden ni se apagan) */
static int page_active(int pg)
{
    switch (pg) {
    case PG_ARP: return set.arp != 0;
    case PG_BP: return set.bp_on != 0;
    case PG_DLY: return set.dly_on != 0;
    case PG_REV: return set.rev_on != 0;
    case PG_LFO_PAD: case PG_LFO_ARP: case PG_LFO_BASS: return set.lfo[pg - PG_LFO_PAD].dest != 0;
    default: return 0;
    }
}

/* un botón con varias páginas: dentro, pasa a la siguiente (y de la última a la primera); desde fuera, lleva a
 * la que tiene algo encendido (la última visitada si hay varias), y si no hay ninguna, a la última visitada */
static void page_go(const uint8_t *pages, int n, uint8_t *mem)
{
    int i, cur = -1, first_on = -1;
    for (i = 0; i < n; i++) {
        if (st.page == pages[i]) cur = i;
        if (first_on < 0 && page_active(pages[i])) first_on = i;
    }
    if (cur >= 0) st.page = pages[(cur + 1) % n];
    else if (*mem && page_active(*mem)) st.page = *mem;
    else if (first_on >= 0) st.page = pages[first_on];
    else st.page = *mem ? *mem : pages[0];
    *mem = st.page;
    app_say(PAGE_NAME[st.page], page_state(st.page));
}

/* ------------------------------------------------------ ideas: REC / PLAY --- */
/* Se graban los cambios de acorde (grado, modificadores, inversión) en corcheas al tempo; el bucle dura
 * compases enteros. Las teclas en directo mandan sobre lo grabado mientras se mantienen. */
static float eighth_ms(void) { return 30000.0f / (float)set.tempo; }
#define REC_LOOP (set.bank[set.cur_bank])
#define PLAY_LOOP (set.bank[st.play_bank])
static void seq_play(int on);

static void seq_tick(void)
{
    /* el tiempo de las ideas sale del reloj del audio (en corcheas), no del de la pantalla */
    static float last_beats;
    static uint32_t last_resets;
    float b = tp_beats, d;
    uint32_t rs = tp_resets;
    d = (rs != last_resets ? b : b - last_beats) * 2.0f;
    if (d < 0.0f) d = 0.0f;
    last_beats = b;
    last_resets = rs;
    tp_locked = (uint8_t)(st.playing || st.rec == 2);   /* sonando o grabando: el reloj no se reinicia solo */
    if (st.rec == 2) st.rec_acc += d;
    if (st.playing && PLAY_LOOP.len) {
        st.play_acc += d;
        while (st.playing && st.play_acc >= (float)PLAY_LOOP.len) {
            st.play_acc -= (float)PLAY_LOOP.len;
            if (st.next_bank < NBANK) {      /* el bucle acabó: entra el banco pedido */
                st.play_bank = st.next_bank;
                st.next_bank = 0xFFu;
                st.redraw = 1;
                if (!PLAY_LOOP.len) { seq_play(0); break; }   /* vacío: silencio */
            }
        }
    }
}

static void seq_current(int *degree, unsigned *mods, int *inv)
{
    const loop_t *L = &PLAY_LOOP;
    int i, k = L->n - 1;
    float ahead = st.play_acc + 45.0f / eighth_ms();   /* 45 ms antes: el acorde ya está cuando llega el paso */
    uint32_t pos;
    if (ahead >= (float)L->len) ahead -= (float)L->len;
    pos = (uint32_t)ahead;
    for (i = 0; i < L->n; i++)
        if (L->ev[i].step <= pos) k = i;
    *degree = L->ev[k].degree;
    *mods = L->ev[k].mods;
    *inv = L->ev[k].inv;
}

static void seq_play(int on)
{
    st.play_bank = set.cur_bank;
    st.next_bank = 0xFFu;
    st.playing = (uint8_t)(on && REC_LOOP.len);
    st.play_acc = 0.0f;
    if (st.playing) tp_reset_req = tp_reset_req + 1u;   /* la idea y el reloj empiezan juntos */
    tp_locked = st.playing;
    if (!st.playing) st.gate_hold_off = 1;   /* al parar, el acorde calla */
    st.redraw = 1;
}

static void seq_finish(void)
{
    int len = ((int)(st.rec_acc + 4.0f) / 8) * 8;   /* al compás más cercano */
    st.rec = 0;
    if (!REC_LOOP.n) { app_say("REC", "NADA"); return; }
    if (len < 8) len = 8;
    while (len <= REC_LOOP.ev[REC_LOOP.n - 1].step) len += 8;
    if (len > LOOP_STEPS) len = LOOP_STEPS;
    REC_LOOP.len = (uint8_t)len;
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
        REC_LOOP.n = 0;
        REC_LOOP.len = 0;
        st.rec_sig = ~0u;
    }
    if (st.rec != 2 || sig == st.rec_sig) return;
    st.rec_sig = sig;
    step = (int)(st.rec_acc + 0.5f);
    if (step >= LOOP_STEPS) { seq_finish(); return; }
    if (REC_LOOP.n && REC_LOOP.ev[REC_LOOP.n - 1].step == step) REC_LOOP.n--;   /* en la misma corchea: el último */
    if (REC_LOOP.n < LOOP_MAX) {
        loop_ev_t *ev = &REC_LOOP.ev[REC_LOOP.n++];
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
            else if (!REC_LOOP.len) app_say("PLAY", "BANCO VACIO");
            else seq_play(!st.playing);
        } else if (k == B_SEQ) {
            char t[16] = "BANCO ";
            char nb[4];
            if (st.rec == 2) continue;       /* grabando: el banco no cambia */
            set.cur_bank = (uint8_t)((set.cur_bank + 1u) % NBANK);
            set_dirty = 1;
            int_text(set.cur_bank + 1, nb);
            text_cat(t, nb);
            if (st.playing) {                /* sonando: entra al acabar el bucle */
                st.next_bank = set.cur_bank == st.play_bank ? 0xFFu : set.cur_bank;
                app_say(t, st.next_bank < NBANK ? "AL ACABAR" : "");
            } else {
                app_say(t, REC_LOOP.len ? "" : "VACIO");
            }
        } else if (k == B_ARP) {             /* arpegiador, bajo pulsado */
            static const uint8_t P[2] = {PG_ARP, PG_BP};
            page_go(P, 2, &st.mem_arp);
        } else if (k == B_FX) {              /* delay, reverb */
            static const uint8_t P[2] = {PG_DLY, PG_REV};
            page_go(P, 2, &st.mem_fx);
        } else if (k == B_ENV) {             /* envolventes: pad, arpegio, bajo */
            static const uint8_t P[3] = {PG_ENV_PAD, PG_ENV_ARP, PG_ENV_BASS};
            page_go(P, 3, &st.mem_env);
        } else if (k == B_LFO) {             /* LFO: pad, arpegio, bajo */
            static const uint8_t P[3] = {PG_LFO_PAD, PG_LFO_ARP, PG_LFO_BASS};
            page_go(P, 3, &st.mem_lfo);
        } else if (k == B_HOME) {
            st.page = PG_MAIN;               /* los knobs vuelven al acorde (lo que suena sigue sonando) */
            app_say("HOME", "");
        } else if (k == B_SAVE) {
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) { set_dirty = 0; app_say("GUARDADO", ""); }
            else app_say("GUARDAR", "FALLO");
        }
    }

    /* potenciómetros: K1..K4 son de la página en la que se está (y en cada una, K1 enciende o apaga) */
    for (k = 0; k < NE; k++) {
        char t[24];
        if ((e = plat_enc((int)k)) == 0) continue;
        if (st.page == PG_ARP && k == EN_ALGO) {   /* ARPEGIADOR: las octavas, en ALGORITHM */
            set.arp_oct = (uint8_t)clamp((int32_t)set.arp_oct + e, 1, 3);
            int_text(set.arp_oct, t);
            set_changed("ARP OCTAVAS", t);
        } else if (st.page == PG_ARP && k >= EN_K1) {   /* ARPEGIADOR */
            if (k == EN_K1) {
                set.arp = (uint8_t)clamp((int32_t)set.arp + e, 0, 2);   /* no, solo, con pad */
                set_changed("ARPEGIO", ARP_STATE_NAME[set.arp]);
            } else if (k == EN_K2) {
                set.arp_mode = (uint8_t)clamp((int32_t)set.arp_mode + e, 0, 3);
                set_changed("ARP MODO", ARP_MODE_NAME[set.arp_mode]);
            } else if (k == EN_K3) {
                set.arp_div = (uint8_t)clamp((int32_t)set.arp_div + e, 0, 2);
                set_changed("ARP PASO", ARP_DIV_NAME[set.arp_div]);
            } else {
                set.arp_len = (uint8_t)clamp((int32_t)set.arp_len + e, 1, 9);
                int_text(set.arp_len * 10, t);
                text_cat(t, "%");
                set_changed("ARP LARGO", t);
            }
        } else if (st.page == PG_DLY && k >= EN_K1) {   /* DELAY */
            if (k == EN_K1) {
                uint8_t on = e > 0;
                if (on && !set.dly_on) { uint32_t z; for (z = 0; z < DLY_MAX; z++) dly_buf[0][z] = dly_buf[1][z] = 0; }
                set.dly_on = on;                     /* sin ecos viejos al volver */
                set_changed("DELAY", on ? "SI" : "NO");
            } else if (k == EN_K2) {
                set.dly_div = (uint8_t)clamp((int32_t)set.dly_div + e, 0, 5);
                set_changed("DELAY TIEMPO", DLY_DIV_NAME[set.dly_div]);
            } else if (k == EN_K3) {
                set.dly_fb = (uint8_t)clamp((int32_t)set.dly_fb + e, 0, 9);
                int_text(set.dly_fb * 10, t); text_cat(t, "%");
                set_changed("REPETICIONES", t);
            } else {
                set.dly_mix = (uint8_t)clamp((int32_t)set.dly_mix + e, 0, 10);
                int_text(set.dly_mix * 10, t); text_cat(t, "%");
                set_changed("DELAY MEZCLA", t);
            }
        } else if (st.page == PG_REV && k >= EN_K1) {   /* REVERB */
            if (k == EN_K1) {
                set.rev_on = e > 0;
                set_changed("REVERB", set.rev_on ? "SI" : "NO");
            } else if (k == EN_K2) {
                set.rev_size = (uint8_t)clamp((int32_t)set.rev_size + e, 0, 9);
                int_text(set.rev_size + 1, t);
                set_changed("REVERB TAMANO", t);
            } else if (k == EN_K3) {
                set.rev_tone = (uint8_t)clamp((int32_t)set.rev_tone + e, 0, 9);
                int_text(set.rev_tone + 1, t);
                set_changed("REVERB TONO", t);
            } else {
                set.rev_mix = (uint8_t)clamp((int32_t)set.rev_mix + e, 0, 10);
                int_text(set.rev_mix * 10, t); text_cat(t, "%");
                set_changed("REVERB MEZCLA", t);
            }
        } else if (st.page >= PG_ENV_PAD && st.page <= PG_ENV_BASS && k >= EN_K1) {   /* ENVOLVENTE: del pad, del arpegio o del bajo */
            int pg = st.page;
            uint8_t *ea = pg == PG_ENV_PAD ? &set.env_a : pg == PG_ENV_ARP ? &set.aenv_a : &set.benv_a;
            uint8_t *ed = pg == PG_ENV_PAD ? &set.env_d : pg == PG_ENV_ARP ? &set.aenv_d : &set.benv_d;
            uint8_t *es = pg == PG_ENV_PAD ? &set.env_s : pg == PG_ENV_ARP ? &set.aenv_s : &set.benv_s;
            uint8_t *er = pg == PG_ENV_PAD ? &set.env_r : pg == PG_ENV_ARP ? &set.aenv_r : &set.benv_r;
            if (k == EN_K1) {
                *ea = (uint8_t)clamp((int32_t)*ea + e, 0, 9);
                time_text(env_att_s(*ea), t);
                set_changed("ATAQUE", t);
            } else if (k == EN_K2) {
                *ed = (uint8_t)clamp((int32_t)*ed + e, 0, 9);
                time_text(env_dec_s(*ed), t);
                set_changed("CAIDA", t);
            } else if (k == EN_K3) {
                *es = (uint8_t)clamp((int32_t)*es + e, 0, 10);
                int_text(*es * 10, t); text_cat(t, "%");
                set_changed("SOSTENIDO", t);
            } else {
                *er = (uint8_t)clamp((int32_t)*er + e, 0, 9);
                time_text(env_rel_s(*er), t);
                set_changed("RELEASE", t);
            }
        } else if (st.page >= PG_LFO_PAD && st.page <= PG_LFO_BASS && k >= EN_K1) {   /* LFO del pad, del arpegio o del bajo */
            int part = st.page - PG_LFO_PAD;
            lfo_t *L = &set.lfo[part];
            if (k == EN_K1) {
                L->dest = (uint8_t)clamp((int32_t)L->dest + e, 0, 4);
                set_changed("LFO DESTINO", lfo_dest_name(part, L->dest));
            } else if (k == EN_K2) {
                L->rate = (uint8_t)clamp((int32_t)L->rate + e, 0, 15);
                hz_text(lfo_hz(L->rate), t);
                set_changed("LFO VELOCIDAD", t);
            } else if (k == EN_K3) {
                L->depth = (uint8_t)clamp((int32_t)L->depth + e, 0, 10);
                int_text(L->depth * 10, t); text_cat(t, "%");
                set_changed("LFO CANTIDAD", t);
            } else {
                L->shape = (uint8_t)clamp((int32_t)L->shape + e, 0, 3);
                set_changed("LFO FORMA", LFO_SHAPE_NAME[L->shape]);
            }
        } else if (st.page == PG_BP && k >= EN_K1) {   /* BAJO PULSADO */
            if (k == EN_K1) {
                set.bp_on = e > 0;
                set_changed("BAJO PULSADO", set.bp_on ? "SI" : "NO");
            } else if (k == EN_K2) {
                set.bp_div = (uint8_t)clamp((int32_t)set.bp_div + e, 0, 3);
                set_changed("BAJO PASO", BP_DIV_NAME[set.bp_div]);
            } else if (k == EN_K3) {
                set.bp_pat = (uint8_t)clamp((int32_t)set.bp_pat + e, 0, 2);
                set_changed("BAJO PATRON", BP_PAT_NAME[set.bp_pat]);
            } else {
                set.bp_len = (uint8_t)clamp((int32_t)set.bp_len + e, 1, 9);
                int_text(set.bp_len * 10, t); text_cat(t, "%");
                set_changed("BAJO LARGO", t);
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
        /* cada botón, encendido mientras algo de su sección suena, o mientras se está en una de sus páginas */
        int arp_on = set.arp || set.bp_on, fx_on = set.dly_on || set.rev_on;
        int lfo_on = set.lfo[0].dest || set.lfo[1].dest || set.lfo[2].dest;
        uint32_t lit = btn | (set_dirty ? 1u << B_SAVE : 0u)
                       | (arp_on || st.page == PG_ARP || st.page == PG_BP ? 1u << B_ARP : 0u)
                       | (fx_on || st.page == PG_DLY || st.page == PG_REV ? 1u << B_FX : 0u)
                       | (st.page >= PG_ENV_PAD && st.page <= PG_ENV_BASS ? 1u << B_ENV : 0u)
                       | (lfo_on || (st.page >= PG_LFO_PAD && st.page <= PG_LFO_BASS) ? 1u << B_LFO : 0u)
                       | (st.page == PG_MAIN ? 1u << B_HOME : 0u);
        st.leds = lit;
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
        {                                    /* los bancos: lleno, vacío; el elegido, enmarcado */
            int b;
            for (b = 0; b < NBANK; b++) {
                int32_t x = 8 + b * 11, y = 46;
                uint16_t fill = set.bank[b].len ? (st.playing && b == st.play_bank ? C_WHITE : C_HI) : C_LINE;
                if (b == set.cur_bank) {
                    uint16_t fr = (st.next_bank == b) ? C_AMB : C_WHITE;
                    cv_rect(x - 2, y - 2, 13, 13, fr);
                    cv_rect(x - 1, y - 1, 11, 11, C_BLACK);
                }
                cv_rect(x, y, 9, 9, fill);
            }
        }
        if (set.arp || set.bp_on) {          /* el arpegiador, el bajo pulsado y el tempo, a la derecha */
            char b[24] = "", n[8];
            if (set.arp) text_cat(b, set.arp == 2 ? "ARP+PAD " : "ARP ");
            if (set.bp_on) text_cat(b, "BAJO ");
            int_text(set.tempo, n);
            text_cat(b, n);
            cv_text(234 - text_w(&FONT_XS, b), 44, &FONT_XS, b, C_HI);
        }
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
                if (st.playing) { text_cat(b, "/"); int_text(PLAY_LOOP.len / 8, n); text_cat(b, n); }
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

    /* abajo, como en el teclado: arriba las teclas negras (modificadores) en sus grupos de 3, 2, 3, 2 y 1;
     * debajo las blancas, los siete grados de la tonalidad */
    cv_begin(240, 96, C_BLACK);
    {
        static const uint8_t GROUP_END[5] = {3, 5, 8, 10, 11};   /* dónde acaba cada grupo */
        int32_t x = 1;
        int g = 0;
        for (i = 0; i < NBK; i++) {
            static const uint8_t W[NBK] = {26, 17, 18, 17, 17, 17, 17, 19, 17, 17, 25};   /* lo que pide cada etiqueta */
            int32_t w = W[i];
            chip(x, 6, w - 1, BK_LABEL[i], (int)(st.keys >> BK[i] & 1u));
            x += w;
            if (i + 1 == GROUP_END[g]) { x += 8; g++; }         /* hueco entre grupos */
        }
    }
    for (i = 0; i < NDEG; i++) {
        bl_chord d;
        bl_chord_build(set.key, set.mode, i, 0, 0, 60, &d);
        chip(i * 34 + 1, 30, 33, d.name, st.gate && st.degree == i);
    }
    cv_rect(0, 64, 240, 1, C_LINE);
    if (st.page >= PG_LFO_PAD && st.page <= PG_LFO_BASS) {   /* los knobs de un LFO */
        static const char *const LP[4] = {"LFO PAD", "K2 VELOC", "K3 CANTID", "K4 FORMA"};
        static const char *const LA[4] = {"LFO ARP", "K2 VELOC", "K3 CANTID", "K4 FORMA"};
        static const char *const LB[4] = {"LFO BAJO", "K2 VELOC", "K3 CANTID", "K4 FORMA"};
        int part = st.page - PG_LFO_PAD;
        const char *const *LBL = part == 0 ? LP : part == 1 ? LA : LB;
        const lfo_t *L = &set.lfo[part];
        char v[4][12];
        v[0][0] = 0; text_cat(v[0], "K1 "); text_cat(v[0], lfo_dest_name(part, L->dest));
        hz_text(lfo_hz(L->rate), v[1]);
        int_text(L->depth * 10, v[2]); text_cat(v[2], "%");
        v[3][0] = 0; text_cat(v[3], LFO_SHAPE_NAME[L->shape]);
        for (i = 0; i < 4; i++) {
            cv_text(4 + i * 60, 68, &FONT_XS, LBL[i], C_AMB);
            cv_text(4 + i * 60, 81, &FONT_XS, v[i], C_WHITE);
        }
    } else if (st.page == PG_BP) {   /* los knobs del bajo pulsado */
        static const char *const LBL[4] = {"BAJO", "K2 PASO", "K3 PATRON", "K4 LARGO"};
        char v[4][12];
        v[0][0] = 0; text_cat(v[0], set.bp_on ? "K1 SI" : "K1 NO");
        v[1][0] = 0; text_cat(v[1], BP_DIV_NAME[set.bp_div]);
        v[2][0] = 0; text_cat(v[2], BP_PAT_NAME[set.bp_pat]);
        int_text(set.bp_len * 10, v[3]); text_cat(v[3], "%");
        for (i = 0; i < 4; i++) {
            cv_text(4 + i * 60, 68, &FONT_XS, LBL[i], C_AMB);
            cv_text(4 + i * 60, 81, &FONT_XS, v[i], C_WHITE);
        }
    } else if (st.page >= PG_ENV_PAD && st.page <= PG_ENV_BASS) {   /* los knobs de una envolvente */
        static const char *const LP[4] = {"PAD ATAQ", "PAD CAIDA", "PAD SOST", "PAD REL"};
        static const char *const LA[4] = {"ARP ATAQ", "ARP CAIDA", "ARP SOST", "ARP REL"};
        static const char *const LB[4] = {"BAJO ATAQ", "BAJO CAIDA", "BAJO SOST", "BAJO REL"};
        const char *const *LBL = st.page == PG_ENV_PAD ? LP : st.page == PG_ENV_ARP ? LA : LB;
        int pg = st.page;
        int ea = pg == PG_ENV_PAD ? set.env_a : pg == PG_ENV_ARP ? set.aenv_a : set.benv_a;
        int ed = pg == PG_ENV_PAD ? set.env_d : pg == PG_ENV_ARP ? set.aenv_d : set.benv_d;
        int es = pg == PG_ENV_PAD ? set.env_s : pg == PG_ENV_ARP ? set.aenv_s : set.benv_s;
        int er = pg == PG_ENV_PAD ? set.env_r : pg == PG_ENV_ARP ? set.aenv_r : set.benv_r;
        char v[4][12];
        time_text(env_att_s(ea), v[0]);
        time_text(env_dec_s(ed), v[1]);
        int_text(es * 10, v[2]); text_cat(v[2], "%");
        time_text(env_rel_s(er), v[3]);
        for (i = 0; i < 4; i++) {
            cv_text(4 + i * 60, 68, &FONT_XS, LBL[i], C_AMB);
            cv_text(4 + i * 60, 81, &FONT_XS, v[i], C_WHITE);
        }
    } else if (st.page == PG_DLY || st.page == PG_REV) {   /* los knobs del delay o de la reverb */
        static const char *const LD[4] = {"DELAY", "K2 TIEMPO", "K3 REPET", "K4 MEZCLA"};
        static const char *const LR[4] = {"REVERB", "K2 TAMANO", "K3 TONO", "K4 MEZCLA"};
        const char *const *LBL = st.page == 2 ? LD : LR;
        char v[4][12];
        if (st.page == 2) {
            v[0][0] = 0; text_cat(v[0], set.dly_on ? "K1 SI" : "K1 NO");
            v[1][0] = 0; text_cat(v[1], DLY_DIV_NAME[set.dly_div]);
            int_text(set.dly_fb * 10, v[2]); text_cat(v[2], "%");
            int_text(set.dly_mix * 10, v[3]); text_cat(v[3], "%");
        } else {
            v[0][0] = 0; text_cat(v[0], set.rev_on ? "K1 SI" : "K1 NO");
            int_text(set.rev_size + 1, v[1]);
            int_text(set.rev_tone + 1, v[2]);
            int_text(set.rev_mix * 10, v[3]); text_cat(v[3], "%");
        }
        for (i = 0; i < 4; i++) {
            cv_text(4 + i * 60, 68, &FONT_XS, LBL[i], C_AMB);
            cv_text(4 + i * 60, 81, &FONT_XS, v[i], C_WHITE);
        }
    } else if (st.page == PG_ARP) {      /* los knobs del arpegiador (y las octavas en ALGORITHM) */
        static const char *const LBL[4] = {"ARPEGIO", "K2 MODO", "K3 PASO", "K4 LARGO"};
        char v[4][12], o[28] = "ALGORITHM: OCTAVAS ", n[4];
        v[0][0] = 0; text_cat(v[0], "K1 "); text_cat(v[0], ARP_STATE_NAME[set.arp]);
        v[1][0] = 0; text_cat(v[1], ARP_MODE_NAME[set.arp_mode]);
        v[2][0] = 0; text_cat(v[2], ARP_DIV_NAME[set.arp_div]);
        int_text(set.arp_len * 10, v[3]); text_cat(v[3], "%");
        int_text(set.arp_oct, n);
        text_cat(o, n);
        cv_text(4, 50, &FONT_XS, o, C_GRAY);
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
    else if (!strcmp(what, "rev")) *got = set.rev_mix;
    else if (!strcmp(what, "dly")) *got = set.dly_mix;
    else if (!strcmp(what, "dly_on")) *got = set.dly_on;
    else if (!strcmp(what, "rev_on")) *got = set.rev_on;
    else if (!strcmp(what, "rev_size")) *got = set.rev_size;
    else if (!strcmp(what, "env_s")) *got = set.env_s;
    else if (!strcmp(what, "bp_on")) *got = set.bp_on;
    else if (!strcmp(what, "bp_pat")) *got = set.bp_pat;
    else if (!strcmp(what, "lfo_dest")) *got = set.lfo[0].dest;
    else if (!strcmp(what, "lfo_arp")) *got = set.lfo[1].dest;
    else if (!strcmp(what, "led_arp")) *got = (int)(st.leds >> B_ARP & 1u);
    else if (!strcmp(what, "led_fx")) *got = (int)(st.leds >> B_FX & 1u);
    else if (!strcmp(what, "led_lfo")) *got = (int)(st.leds >> B_LFO & 1u);
    else if (!strcmp(what, "lfo_bass")) *got = set.lfo[2].dest;
    else if (!strcmp(what, "benv_s")) *got = set.benv_s;
    else if (!strcmp(what, "low_note_is_bass")) {   /* todas las voces de bajo suenan la nota del bajo */
        int j, ok = 1, any = 0;
        for (j = 0; j < NPLK; j++) if (plk[j].low && plk[j].env > 0.01f) { any = 1; ok &= plk[j].note == achord.bass; }
        *got = any && ok;
    }
    else if (!strcmp(what, "low_on")) { int j, on = 0; for (j = 0; j < NPLK; j++) on |= plk[j].low && plk[j].env > 0.01f; *got = on; }
    else if (!strcmp(what, "high_on")) { int j, on = 0; for (j = 0; j < NPLK; j++) on |= !plk[j].low && plk[j].left >= 0 && plk[j].env > 0.01f; *got = on; }
    else if (!strcmp(what, "pad_on")) { int j, on = 0; for (j = 0; j < NPAD; j++) on |= pad[j].env > 0.01f; *got = on; }
    else if (!strcmp(what, "bass_on")) *got = bass_env > 0.01f;
    else if (!strcmp(what, "playing")) *got = st.playing;
    else if (!strcmp(what, "loop_len")) *got = REC_LOOP.len;
    else if (!strcmp(what, "loop_n")) *got = REC_LOOP.n;
    else if (!strcmp(what, "bank")) *got = set.cur_bank;
    else if (!strcmp(what, "play_bank")) *got = st.playing ? st.play_bank : -1;
    else if (!strcmp(what, "arp_mode")) *got = set.arp_mode;
    else if (!strcmp(what, "tempo")) *got = set.tempo;
    else if (!strcmp(what, "arp_moving")) *got = arp_steps >= 4u;   /* al menos cuatro pasos tocados */
    else return 0;
    return 1;
}
#endif
