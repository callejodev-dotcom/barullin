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
#ifdef PLAT_HOST
#include <string.h>
#else
void *memcpy(void *d, const void *s, unsigned n);   /* (felucca-bsp chip/libc.c: sin cabeceras de la libc) */
void *memset(void *d, int c, unsigned n);
#endif
#include "fastmath.h"
#include "core/chords.h"

#define SR 44100.0f

/* ------------------------------------------------------------- ajustes --- */
#define SET_MAGIC 0x42524C50u            /* "BRLP" */
#define LOOP_MAX 64                      /* cambios de acorde en una idea */
#define LOOP_STEPS 128                   /* 16 compases de corcheas */
typedef struct { uint8_t step; int8_t degree; uint8_t mods; uint8_t inv; } loop_ev_t;
typedef struct {
    uint8_t on;                          /* encendido (SEL) */
    uint8_t dest;                        /* a qué afecta: enum lfo_dest */
    uint8_t rate;                        /* 0..15: 0,1 .. 10 Hz */
    uint8_t depth;                       /* 0..10 */
    uint8_t shape;                       /* 0 seno, 1 triángulo, 2 cuadrada, 3 azar */
    uint8_t sync;                        /* 0 libre (VELOCIDAD); si no, un ciclo cada LFO_SYNC_BEATS, con el reloj */
} lfo_t;
/* Los tres LFO son generales: cada uno mueve lo que se le diga */
enum lfo_dest {
    D_PAD_FILT, D_PAD_VOL, D_PAD_PITCH, D_PAD_PAN, D_PAD_DET,
    D_ARP_VOL, D_ARP_PITCH, D_ARP_PAN, D_ARP_BRIGHT, D_ARP_LEN,
    D_BASS_VOL, D_BASS_PITCH, D_BASS_BRIGHT, D_BASS_LEN,
    D_DLY_MIX, D_DLY_TIME, D_DL2_TIME, D_REV_MIX,
    D_DRUM_VOL, D_HAT_VOL, D_MASTER_VOL, D_MASTER_FILT, NDEST
};
enum { FX_DLY, FX_DLY2, FX_REV };       /* los efectos, para sus envíos (set.fx_send) */
static const char *const LAYER_NAME[4] = {"PAD", "BAJO", "ARP", "RASGUEO"};
#define VOL_MAX 20                       /* pasos de volumen: una vuelta de knob (20 clics) va de 0 a 100 % */
#define VOL_UNITY 16                     /* el nivel de antes (80 %) */
#define ARP_ON (set.vol_arp != 0)        /* el arpegio suena en cuanto tiene volumen */
#define DRUM_ON (!set.drm_mute && (set.drm_kick || set.drm_snare || set.drm_hat))
#define NBANK 8                          /* bancos de ideas (SEQ pasa de uno a otro) */
typedef struct {
    uint8_t len;                         /* corcheas (0: vacío), compases enteros */
    uint8_t n;                           /* sus cambios de acorde */
    loop_ev_t ev[LOOP_MAX];
} loop_t;
typedef struct {
    uint32_t magic;
    /* lo global (no cambia con el banco) */
    uint8_t lead;                        /* voice leading */
    uint8_t quant;                       /* cuadrar los cambios de acorde: 0 al momento, 1 corchea, 2 negra, 3 compás */
    uint8_t hold;                        /* el acorde sigue al soltar (por defecto, sí) */
    uint8_t cur_bank;                    /* el banco elegido: REC graba en él, PLAY lo toca */
    /* --- el sonido de la idea: cada banco tiene el suyo, de key a kit_end (ver kit_store / kit_load) --- */
    uint8_t key;                         /* 0 = C .. 11 = B */
    uint8_t mode;                        /* enum bl_mode */
    int8_t octave;                       /* -2 .. +2 */
    uint8_t bright;                      /* 0..20: el corte del filtro del pad */
    uint8_t sound;                       /* el sonido del pad (GLO, K3): enum snd */
    uint8_t arp_free;                    /* ciclo del arpegio: 0 en el compás (grupos de 4), 1 libre */
    uint8_t meter;                       /* tipo de compás (PRESET): 4/4, 3/4, 6/8, 5/4, 7/8 */
    uint8_t arp_mute_vol;                /* el volumen que tenía el arpegio antes de silenciarlo con SEL */
    uint8_t arp_mode;                    /* 0 sube, 1 baja, 2 sube-baja, 3 azar */
    uint8_t arp_div;                     /* 0 corcheas, 1 tresillos, 2 semicorcheas */
    uint8_t arp_oct;                     /* 1..3 octavas */
    uint8_t arp_len;                     /* 1..9: duración de cada nota, en décimas del paso */
    uint8_t tempo;                       /* 40..220 BPM */
    uint8_t fx_send[3][4];               /* 0..10: cuánto manda cada capa (pad, bajo, arpegio, rasgueo) a cada
                                          * efecto (delay, delay libre, reverb) */
    uint8_t dly_div;                     /* 0..5: 1/16 1/8 1/8. 1/4 1/4. 1/2 */
    uint8_t dly_fb;                      /* 0..9: repeticiones */
    uint8_t dly_on, rev_on;              /* 0: silenciado con SEL (suena si alguna capa le manda algo: FX_ON) */
    uint8_t rev_size;                    /* 0..9: tamaño de la sala */
    uint8_t rev_tone;                    /* 0..9: oscura .. brillante */
    uint8_t env_a, env_d, env_s, env_r;  /* la envolvente del pad: ataque, caída (0..9), sostenido (0..10), release (0..9) */
    uint8_t aenv_a, aenv_d, aenv_s, aenv_r;   /* la envolvente del arpegio, igual */
    uint8_t benv_a, benv_d, benv_s, benv_r;   /* la del bajo (el de siempre y el arpegiado) */
    uint8_t bp_on;                       /* bajo pulsado: el bajo repite su nota al tempo */
    uint8_t bp_div;                      /* 0..3: 1/4 1/8 1/8T 1/16 */
    uint8_t bp_pat;                      /* 0 nota, 1 quinta, 2 octava, 3 acorde (las notas del que suena) */
    uint8_t bp_len;                      /* 1..9: duración de cada nota, en décimas del paso */
    int8_t bp_oct;                       /* -1..+1: la octava del bajo pulsado */
    uint8_t vol_pad, vol_bass, vol_arp, vol_strum;   /* 0..VOL_MAX: el volumen de cada capa (HOME) */
    uint8_t dly_tone;                    /* 0..9: las repeticiones, de oscuras a brillantes */
    uint8_t d2_on;                       /* el delay libre (FX, segunda página) */
    uint16_t d2_ms;                      /* 10..750 ms */
    uint8_t d2_fb, d2_tone;
    uint8_t drm_kick, drm_snare, drm_hat;   /* caja de ritmos (EDIT): el volumen de cada parte, 0 callada */
    uint8_t drm_hopen;                   /* la apertura del charles: 0 cerrado .. 10 abierto */
    uint8_t drm_mute;                    /* SEL en su página: silencio (los volúmenes se guardan) */
    uint8_t rev_width;                   /* 0..10: ancho estéreo de la reverb */
    lfo_t lfo[3];                        /* los tres LFO */
    uint8_t kit_end;                     /* (la marca: aquí acaba el sonido de la idea) */
    loop_t bank[NBANK];
} settings_t;
/* Lo que se guarda: los ajustes (con el sonido del banco que suena) y el sonido de cada banco. Un banco que
 * nunca se tocó (kit_used 0) empieza con todo de serie. */
#define KIT_OFS __builtin_offsetof(settings_t, key)
#define KIT_SIZE (__builtin_offsetof(settings_t, kit_end) - __builtin_offsetof(settings_t, key))
typedef struct {
    settings_t s;
    uint8_t kit_used[NBANK];
    uint8_t kits[NBANK][KIT_SIZE];
} saved_t;
static saved_t sv;
#define set (sv.s)
typedef char saved_fits[sizeof(saved_t) <= PLAT_STORE_MAX ? 1 : -1];   /* cabe en un objeto del almacén */
static uint8_t set_dirty;

static const char *const MODE_NAME[BL_MODE_COUNT] = {"MAYOR", "DORICO", "FRIGIO", "LIDIO",
                                                     "MIXOLIDIO", "MENOR", "LOCRIO"};
/* las teclas negras, de izquierda a derecha */
enum { BK_FLIP, BK_7, BK_MAJ7, BK_SUS2, BK_SUS4, BK_9, BK_6, BK_DIMAUG, BK_INV1, BK_INV2, BK_HOLD, NBK };
static const char *const BK_LABEL[NBK] = {"m/M", "7", "M7", "s2", "s4", "9", "6", "o/+", "I1", "I2", "STOP"};
static const unsigned BK_MOD[8] = {BL_MOD_FLIP, BL_MOD_7, BL_MOD_MAJ7, BL_MOD_SUS2,
                                   BL_MOD_SUS4, BL_MOD_9, BL_MOD_6, BL_MOD_DIMAUG};
static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
/* los compases: corcheas por compás y por pulso, y cómo se agrupa el arpegio en corcheas y en semicorcheas */
typedef struct {
    const char *name;
    uint8_t bar8, beat8;
    uint8_t ng8, g8[3];
    uint8_t ng16, g16[5];
    uint16_t beats;                      /* la batería: las corcheas del compás donde empieza un pulso (bit s) */
} meter_t;
#define NMETER 5
static const meter_t METER[NMETER] = {
    {"4/4", 8, 2, 2, {4, 4}, 4, {4, 4, 4, 4}, 0x55},           /* pulsos en 0 2 4 6 */
    {"3/4", 6, 2, 2, {3, 3}, 3, {4, 4, 4}, 0x15},              /* 0 2 4 */
    {"6/8", 6, 3, 2, {3, 3}, 4, {3, 3, 3, 3}, 0x09},           /* 0 3 */
    {"5/4", 10, 2, 3, {4, 3, 3}, 5, {4, 4, 4, 4, 4}, 0x155},   /* 0 2 4 6 8 */
    {"7/8", 7, 1, 2, {4, 3}, 4, {4, 4, 3, 3}, 0x15},           /* 0 2 4 (2+2+3) */
};
#define MT (&METER[set.meter % NMETER])
static const char *const ARP_MODE_NAME[4] = {"SUBE", "BAJA", "S-B", "AZAR"};

/* las páginas de los knobs: cada botón recorre las suyas; HOME vuelve a la principal */
enum { PG_MAIN, PG_ARP, PG_DLY, PG_REV, PG_ENV_PAD, PG_ENV_ARP, PG_ENV_BASS, PG_BP, PG_LFO_PAD, PG_LFO_ARP, PG_LFO_BASS, PG_GLO, PG_DRUM, PG_DLY2, PG_BT, NPG };
static const char *const PAGE_NAME[NPG] = {"", "ARPEGIADOR", "DELAY", "REVERB", "ENVOLVENTE", "ENVOLVENTE", "ENVOLVENTE",
                                           "BAJO PULSADO", "LFO 1", "LFO 2", "LFO 3", "GLOBAL", "RITMO", "DELAY LIBRE", "BLUETOOTH"};
static const char *const ARP_DIV_NAME[3] = {"1/8", "1/8T", "1/16"};
static const char *const BP_DIV_NAME[4] = {"1/4", "1/8", "1/8T", "1/16"};
static const char *const BP_PAT_NAME[4] = {"NOTA", "QUINTA", "OCTAVA", "ACORDE"};
static const char *const LFO_DEST_NAME[NDEST] = {
    "PAD FILTRO", "PAD VOL", "PAD AFIN", "PAD PANEO", "PAD CHORUS",
    "ARP VOL", "ARP AFIN", "ARP PANEO", "ARP BRILLO", "ARP LARGO",
    "BAJO VOL", "BAJO AFIN", "BAJO BRILL", "BAJO LARGO",
    "DLY MEZCLA", "DLY TIEMPO", "DL2 TIEMPO", "REV MEZCLA",
    "RITMO VOL", "CHARLES", "TOTAL VOL", "TOTAL AGUDOS"};
static float arp_len_mul = 1.0f, bp_len_mul = 1.0f;   /* LARGO: la duración de las notas, movida por un LFO */
static const char *const LFO_SHAPE_NAME[4] = {"SENO", "TRIANG", "CUADRA", "AZAR"};
#define NSYNC 8
static const char *const LFO_SYNC_NAME[NSYNC] = {"LIBRE", "1/16", "1/8", "1/4", "1/2", "1 COMPAS", "2 COMPAS", "4 COMPAS"};
static const float LFO_SYNC_BEATS[5] = {0.0f, 0.25f, 0.5f, 1.0f, 2.0f};   /* (de 5 en adelante, compases) */
static float lfo_hz(int r) { return 0.1f * fm_exp2f((float)r * (6.6439f / 15.0f)); }   /* 0,1 .. 10 Hz */
static const char *const DLY_DIV_NAME[6] = {"1/16", "1/8", "1/8.", "1/4", "1/4.", "1/2"};
static const float DLY_BEATS[6] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
#define NDEG 7
#define NSTRUM 9

/* ---------------------------------------------- del UI al audio --- */
/* El UI publica el acorde en uno de dos buffers y luego sube seq: el audio nunca lee uno a medio escribir. */
typedef struct {
    uint8_t n, gate, bass;
    uint8_t trig;                        /* sube con cada pulsación: el pad vuelve a atacar (aunque sea el mismo acorde) */
    uint8_t notes[BL_MAX_NOTES];
} snd_chord;
static snd_chord snd_buf[2];
static volatile uint32_t snd_seq;
static volatile float snd_cut = 0.2f;    /* coeficiente del filtro (lo calcula el UI) */

#define NPLQ 16u                         /* cola de rasgueos */
static volatile uint8_t plq_note[NPLQ], plq_vel[NPLQ];
static volatile uint32_t plq_w;

static uint8_t st_trig;                  /* cuántas pulsaciones (el UI); ver snd_chord.trig */
static void publish(const bl_chord *c, int gate)
{
    snd_chord *b = &snd_buf[(snd_seq + 1u) & 1u];
    int i;
    b->n = c->n;
    b->gate = (uint8_t)gate;
    b->bass = c->bass;
    b->trig = st_trig;
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
/* Los sonidos del pad, a la manera de Barullo Xuno (y de 7FM para los de FM). El pad es parafónico, como
 * un Juno con un solo filtro: cada voz es un DCO (sierra y pulso sin aliasing, PWM y sub una octava abajo),
 * se suman, pasan por un filtro resonante con su envolvente (se abre con cada acorde) y por el chorus de
 * dos BBD del Juno-106 (I: 0,51 Hz ±2,1 ms; II: 0,86 Hz ±1,7 ms, alrededor de 3,3 ms), que lo abre en estéreo.
 * Los de FM (piano eléctrico, campana) y el cristal usan el mismo filtro y chorus. */
enum { K_DCO, K_EP, K_BELL, K_GLASS };
typedef struct {
    const char *name;
    uint8_t kind;
    float saw, pulse, sub;               /* mezcla del DCO */
    float pw, pwm;                       /* ancho del pulso (0,5 cuadrada) y cuánto lo mueve su LFO */
    int8_t bri;                          /* el corte, en 1/4 de octava sobre BRILLO */
    float res;                           /* resonancia 0..0,9 */
    float fenv, fdec;                    /* la envolvente del filtro: octavas que abre y su caída (s) */
    uint8_t chorus;                      /* 0 sin, 1 chorus I, 2 chorus II */
    float gain;
    uint8_t env[4];                      /* la envolvente que carga: ataque, caída, sostenido, release */
} sound_t;
#define NSND 8
static const sound_t SOUND[NSND] = {
    {"CUERDAS",  K_DCO,  1.00f, 0.00f, 0.00f, 0.50f, 0.00f,   3, 0.10f, 0.6f, 1.2f, 1, 1.00f, {6, 5, 10, 6}},
    {"NEBOA",    K_DCO,  0.55f, 0.60f, 0.35f, 0.50f, 0.35f,  -1, 0.30f, 1.6f, 2.5f, 2, 0.65f, {6, 6, 9, 7}},
    {"METAIS",   K_DCO,  1.00f, 0.00f, 0.25f, 0.50f, 0.00f,  -4, 0.20f, 2.8f, 0.4f, 1, 1.10f, {2, 5, 9, 3}},
    {"PIANO EL", K_EP,   0.00f, 0.00f, 0.00f, 0.50f, 0.00f,  14, 0.00f, 0.0f, 1.0f, 1, 1.50f, {0, 7, 0, 4}},
    {"ORGANO",   K_DCO,  0.00f, 0.80f, 0.55f, 0.50f, 0.00f,   4, 0.00f, 0.0f, 1.0f, 2, 0.58f, {0, 5, 10, 1}},
    {"SINTE 80", K_DCO,  0.80f, 0.50f, 0.00f, 0.25f, 0.10f,  -2, 0.45f, 3.0f, 0.3f, 2, 0.95f, {0, 5, 4, 3}},
    {"CAMPANA",  K_BELL, 0.00f, 0.00f, 0.00f, 0.50f, 0.00f,  14, 0.00f, 0.0f, 1.0f, 1, 0.95f, {0, 9, 0, 6}},
    {"CRISTAL",  K_GLASS,0.00f, 0.00f, 0.00f, 0.50f, 0.00f,  10, 0.00f, 0.0f, 1.0f, 2, 0.85f, {3, 6, 7, 6}},
};
#define NPLK 12
typedef struct { float p1, p2, i1, i2, env, tr, sub; int note, gate, stage; } pad_v;   /* stage: 0 ataque, 1 caída;
                                                                                      * tr: el golpe (FM), sub: su onda */

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
static uint32_t plk_on, pad_on;          /* qué voces suenan (bit a bit): el render solo pasa por esas */
static float bass_ph, bass_inc, bass_env;
static int bass_gate, bass_stage, bass_note;
static float gain_now, mf_l1, mf_l2, mf_r1, mf_r2;   /* el filtro de agudos de todo (LFO: TOTAL AGUDOS) */
/* el filtro del pad (SVF, un polo más detrás), su envolvente, el PWM, el chorus; el filtro del bajo */
static float pf_s1, pf_s2, pf_env, pf_g = 0.1f, pf_k = 1.4f, pf_h = 1.0f;
static int pf_stage, pf_trig;
static float pwm_ph, ch_ph, ch_buf[256], ch_w0, ch_w1;
static uint32_t ch_w;
static float bf_s1, bf_s2, bf_env, bf_g = 0.05f;
static int bf_trig;
static inline float blep(float t, float dt)  /* polyBLEP: quita el aliasing del salto de la sierra y del pulso */
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
static void pad_strike(pad_v *v) { pad_on |= 1u << (v - pad); v->stage = 0; v->tr = 1.0f; if (v->sub == 0.0f) v->sub = 1.0f; pf_trig = 1; }
static uint32_t snd_seen, plq_r, plk_next;
static snd_chord achord;                 /* el acorde según el audio (para el arpegio) */
static int arp_was;                      /* el arpegiador sonaba en el bloque anterior */
/* El reloj maestro: pulsos (negras) y su fracción, contados en el audio a la muestra. El arpegio, el bajo
 * pulsado, el reloj MIDI y las ideas grabadas van todos a esta rejilla, así que siempre caen juntos. */
static uint32_t tp_bint;                 /* pulsos enteros */
static float tp_bfrac;                   /* y la fracción, 0..1 */
/* La rejilla del arpegio y del bajo pulsado: la misma velocidad que el reloj maestro, pero tocando en directo
 * vuelve a cero con cada acorde nuevo, así que entran en el acto (y siguen juntos). Con una idea sonando o
 * grabando no se toca: va con el reloj maestro y con el compás. */
static uint32_t pf_bint;
static float pf_bfrac;
static int chord_new;                    /* take_chord: llegó un acorde distinto */
static uint32_t arp_age, bp_age;         /* muestras desde el último paso del arpegio / del bajo */
static volatile float tp_beats;          /* la posición para el UI: pulsos (vuelve a 0 al reiniciar) */
static volatile uint32_t tp_resets;      /* cuántas veces se reinició (el UI lo ve) */
static volatile uint32_t tp_reset_req;   /* el UI pide reiniciar (PLAY): distinto de tp_reset_done */
static uint32_t tp_reset_done;
static volatile uint8_t tp_locked;       /* una idea suena: un acorde nuevo no reinicia el reloj */
static int tp_gate_was;
static uint32_t clk_last = ~0u, arp_last = ~0u, bp_last = ~0u, bar_last = ~0u;   /* el último paso tocado de cada uno */
static uint32_t arp_i, arp_rng = 0x1234567u;
static volatile uint32_t arp_steps;      /* pasos tocados (para las pruebas) */

/* Seno por tabla (512 puntos e interpolación lineal): mucho más barato que calcularlo en cada muestra, y
 * de sobra para un seno que suena. ph en 0..1; tsin2 da el segundo armónico. */
#define SINT_N 512
static float sint[SINT_N + 1];
static void sint_init(void)
{
    int i;
    for (i = 0; i <= SINT_N; i++) sint[i] = fm_sinf((float)i * (6.2831853f / (float)SINT_N));
}
static inline float tsin(float ph)
{
    float x = ph * (float)SINT_N;
    int32_t i = (int32_t)x;
    float f = x - (float)i;
    i &= SINT_N - 1;
    return sint[i] + (sint[i + 1] - sint[i]) * f;
}
static inline float psin2(float ph)        /* seno aproximado (parábolas) del doble de ph: sin tabla, para moduladores */
{
    float x = ph + ph;
    if (x >= 1.0f) x -= 1.0f;
    return x < 0.5f ? 16.0f * x * (0.5f - x) : 16.0f * (x - 0.5f) * (x - 1.0f);
}
static inline float tsin2(float ph) { float p2 = ph + ph; return tsin(p2 >= 1.0f ? p2 - 1.0f : p2); }

/* limitador suave (aproximación racional de tanh, sin exponenciales) */
static inline float soft_clip(float x)      /* cúbico, sin divisiones: lineal abajo, redondo hasta ±1 */
{
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - 0.148148f * x * x * x;
}

static float note_inc(int note) { return 440.0f * fm_exp2f((float)(note - 69) / 12.0f) / SR; }

/* el acorde nuevo: las notas que ya suenan siguen sin cortarse; las demás se sueltan o se asignan */
static int arp_restart;
static void take_chord(const snd_chord *c)
{
    int i, j, used[NPAD] = {0};
    int pad_gate = c->gate;               /* el pad, su propia capa: su volumen en HOME */
    static uint8_t trig_seen;
    int retrig = c->trig != trig_seen;    /* tecla pulsada otra vez: todo vuelve a atacar (el piano, la campana...) */
    trig_seen = c->trig;
    if (c != &achord) {
        int k, same = c->n == achord.n && c->bass == achord.bass;
        for (k = 0; same && k < c->n; k++) same = c->notes[k] == achord.notes[k];
        if (!same) { arp_restart = 1; chord_new = 1; }   /* acorde nuevo: el arpegio empieza su patrón desde el principio */
        achord = *c;
    }
    for (j = 0; j < NPAD; j++) {
        int keep = 0;
        if (pad_gate && pad[j].gate)
            for (i = 0; i < c->n; i++)
                if (pad[j].note == c->notes[i]) keep = 1;
        if (!keep) pad[j].gate = 0;
        else { used[j] = 1; if (retrig) pad_strike(&pad[j]); }
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
            pad_strike(&pad[best]);
            pad[best].i1 = note_inc(c->notes[i]);   /* (las dos sierras se desafinan en el render: det) */
            pad[best].i2 = pad[best].i1;
            used[best] = 1;
        }
    {
        int g = c->gate && !set.bp_on;        /* el bajo pulsado sustituye al sostenido */
        if (g && (!bass_gate || c->bass != bass_note || retrig)) { bass_stage = 0; bf_trig = 1; }   /* nota nueva: ataque */
        bass_gate = g;
        bass_note = c->bass;
    }
    if (c->gate) bass_inc = note_inc(c->bass);
}

static void take_plucks(void)
{
    uint32_t w = plq_w;
    while (plq_r != w) {
        plk_v *p = &plk[plk_next % NPLK];
        plk_on |= 1u << (plk_next++ % NPLK);
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
static float dly_lp[2];
/* el delay libre: mono (0,75 s), su propio tiempo en ms, repeticiones, tono y mezcla */
static int16_t dl2_buf[DLY_MAX] PLAT_POOL;
static uint32_t dl2_w;
static float dl2_cur = 13230.0f, dl2_lp;
static float dly_cur = 11025.0f;         /* el tiempo actual, en muestras (se desliza hacia el pedido) */
/* cuatro peines en mono (la cola) y dos allpass por lado (el estéreo): la mitad de cuentas que antes */
#define NCOMB 4
#define NAP 2
static const uint16_t COMB_LEN[NCOMB] = {1116, 1188, 1277, 1356}, AP_LEN[NAP] = {556, 441};
#define RSPREAD 23
static float comb_buf[NCOMB][1356] PLAT_POOL;
static float ap_buf[2][NAP][556 + RSPREAD] PLAT_POOL;
static uint16_t comb_i[NCOMB], ap_i[2][NAP];
static float comb_lp[NCOMB];
static int fx_any(int f)                 /* alguna capa le manda algo al efecto f */
{
    return (set.fx_send[f][0] | set.fx_send[f][1] | set.fx_send[f][2] | set.fx_send[f][3]) != 0;
}
static int fx_unmuted(int f) { return f == FX_DLY ? set.dly_on : f == FX_DLY2 ? set.d2_on : set.rev_on; }
#define FX_ON(f) (fx_unmuted(f) && fx_any(f))   /* un efecto suena en cuanto alguna capa le manda algo */
static void fx_clear(int f)              /* sin ecos ni cola viejos al volver a sonar */
{
    uint32_t z;
    if (f == FX_DLY) for (z = 0; z < DLY_MAX; z++) dly_buf[0][z] = dly_buf[1][z] = 0;
    else if (f == FX_DLY2) for (z = 0; z < DLY_MAX; z++) dl2_buf[z] = 0;
    else { memset(comb_buf, 0, sizeof comb_buf); memset(ap_buf, 0, sizeof ap_buf); }
}
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

static inline void reverb_st(float in, float *ol, float *or_)
{
    float sum = 0.0f;
    int k, ch;
    for (k = 0; k < NCOMB; k++) {
        float *b = comb_buf[k], y = b[comb_i[k]];
        comb_lp[k] += (y - comb_lp[k]) * rv_tone;   /* los agudos se apagan antes */
        b[comb_i[k]] = in + comb_lp[k] * rv_fb;
        if (++comb_i[k] >= COMB_LEN[k]) comb_i[k] = 0;
        sum += y;
    }
    for (ch = 0; ch < 2; ch++) {
        float out = sum;
        for (k = 0; k < NAP; k++) {
            uint16_t len = (uint16_t)(AP_LEN[k] + (ch ? RSPREAD : 0));
            float *b = ap_buf[ch][k], bo = b[ap_i[ch][k]];
            b[ap_i[ch][k]] = out + bo * 0.5f;
            out = bo - out;
            if (++ap_i[ch][k] >= len) ap_i[ch][k] = 0;
        }
        if (ch) *or_ = out; else *ol = out;
    }
}

/* -------------------------------------------------------- caja de ritmos --- */
/* Bombo: seno que cae de tono; caja: ruido y un tono; charles: ruido brillante y corto. Seca, al centro. */
static float dk_env, dk_pitch, dk_ph, ds_env, ds_tone, ds_ph, dh_env, dh_prev;
static float ds_ph2, ds_bend, ds_click, sn_hp, sn_x1, sn_lp, sn_lp2;   /* la caja: segundo tono, caída de afinación, golpe, filtros */
static float dk_amp, ds_amp, dh_amp, dh_dec = 0.9965f;
static uint32_t d_rng = 0x51ED27u, drum_last = ~0u;
static volatile uint32_t drum_hits;      /* golpes tocados (para las pruebas) */

static void midi_ch(uint32_t st_, uint32_t ch, uint32_t d1, uint32_t d2);
static void drum_hit(int which, float amp)
{
    static const uint8_t NOTE[3] = {36, 38, 42};
    if (amp <= 0.0f) return;
    if (which == 0) { dk_env = 1.0f; dk_pitch = 1.0f; dk_ph = 0.0f; dk_amp = amp; }
    else if (which == 1) { ds_env = 1.0f; ds_tone = 1.0f; ds_bend = 1.0f; ds_click = 1.0f; ds_amp = amp; }
    else {                                   /* charles: su caída según la apertura, de ~6 ms a ~400 ms */
        dh_env = 1.0f;
        dh_amp = amp;
        dh_dec = fm_expf(-1.0f / (0.006f * fm_exp2f((float)set.drm_hopen * 0.6f) * SR));
    }
    midi_ch(0x90u, 10, NOTE[which], (uint32_t)clamp((int32_t)(amp * 127.0f), 1, 127));
    midi_ch(0x80u, 10, NOTE[which], 0);
    drum_hits++;
}

/* la corchea s del compás: qué suena */
static void drum_step(uint32_t s)
{
    const meter_t *m = MT;
    int beat = (int)(m->beats >> s & 1u), nbeat = 0;
    uint32_t k;
    float hat = 0.0f;
    for (k = 0; k < s; k++) nbeat += (int)(m->beats >> k & 1u);   /* qué pulso es (0, 1, 2...) */
    if (beat) drum_hit(0, (float)set.drm_kick / VOL_UNITY);
    if (beat && (nbeat & 1)) drum_hit(1, (float)set.drm_snare / VOL_UNITY);   /* en el 2 y el 4 */
    hat = (float)set.drm_hat / VOL_UNITY * (beat ? 1.0f : 0.8f);   /* charles a corcheas, el contratiempo algo más suave */
    drum_hit(2, hat);
}

static inline float drum_sample(float g_hat)
{
    float out = 0.0f, n;
    d_rng = d_rng * 1664525u + 1013904223u;
    n = (float)(int32_t)d_rng * (1.0f / 2147483648.0f);
    if (dk_env > 0.0005f) {
        dk_ph += (45.0f + 110.0f * dk_pitch) / SR;
        if (dk_ph >= 1.0f) dk_ph -= 1.0f;
        out += tsin(dk_ph) * dk_env * dk_amp * 0.55f;
        dk_env *= 0.99984f;                  /* ~140 ms */
        dk_pitch *= 0.9988f;                 /* el golpe: el tono cae rápido */
    }
    if (ds_env > 0.003f) {                   /* caja tipo 909, grave: parche (dos tonos que caen un poco) + bordonera */
        float bend = 1.0f + 0.12f * ds_bend, body, wires;
        ds_ph += 145.0f * bend / SR;
        if (ds_ph >= 1.0f) ds_ph -= 1.0f;
        ds_ph2 += 207.0f * bend / SR;
        if (ds_ph2 >= 1.0f) ds_ph2 -= 1.0f;
        body = (tsin(ds_ph) * 0.70f + tsin(ds_ph2) * 0.30f) * ds_tone;
        /* la bordonera: ruido grave y gordo (paso alto ~330 Hz, dos pasos bajos ~2,7 kHz) */
        sn_hp = 0.955f * (sn_hp + n - sn_x1);
        sn_x1 = n;
        sn_lp += (sn_hp - sn_lp) * 0.32f;
        sn_lp2 += (sn_lp - sn_lp2) * 0.32f;
        wires = sn_lp2 * ds_env * ds_env;    /* al cuadrado: pega fuerte y se corta seca, sin cola */
        out += (body * 1.05f + wires * 0.95f + sn_lp * ds_click * 0.25f) * ds_amp * 0.95f;
        ds_env *= 0.99960f;                  /* la bordonera: se apaga del todo en ~250 ms */
        ds_tone *= 0.99930f;                 /* el parche: ~30 ms, un golpe sordo, sin que cante */
        ds_bend *= 0.9980f;                  /* la afinación cae en ~10 ms */
        ds_click *= 0.990f;                  /* el golpe: ~2 ms */
    }
    if (dh_env > 0.0005f) {
        float hp = n - dh_prev;              /* ruido brillante */
        dh_prev = n;
        out += hp * dh_env * dh_amp * 0.16f * g_hat;
        dh_env *= dh_dec;                    /* cerrado o abierto (EDIT, K4) */
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
    plk_v *p = &plk[plk_next % NPLK];
    plk_on |= 1u << (plk_next++ % NPLK);
    if (p->left > 0) midi_ch(0x80u, p->low ? 3u : 4u, p->note, 0);
    p->ph = 0.0f;
    p->inc = note_inc(note);
    p->amp = low ? 0.34f : 0.26f;
    p->env = 0.0f;
    p->stage = 0;
    if (low) bf_trig = 1;
    p->low = (uint8_t)low;
    p->note = (uint8_t)note;
    p->left = (int32_t)(step * (float)len / 10.0f);
    if (p->left < 1) p->left = 1;
    midi_ch(0x90u, low ? 3u : 4u, (uint32_t)note, 100);
}

/* En el compás: grupos de 4 pasos (3 en tresillos) contados desde el reloj, así que siempre caen en su sitio.
 * Las notas del acorde suben (con 3 notas, la cuarta es la fundamental arriba); con varias octavas, cada
 * grupo sube una. */
static int arp_note_bar(uint32_t idx)
{
    static const uint8_t SB4[4] = {0, 1, 2, 1}, SB3[3] = {0, 2, 1};
    int n = achord.n, G, pos, k, oct;
    if (n == 0) return -1;
    if (set.arp_div == 1) {                  /* tresillos: de 3 en 3 */
        G = 3;
        pos = (int)(idx % 3u);
        oct = (int)((idx / 3u) % set.arp_oct);
    } else {                                 /* corcheas o semicorcheas: los grupos del compás */
        const meter_t *m = MT;
        int sixteenths = set.arp_div == 2, ng = sixteenths ? m->ng16 : m->ng8, g = 0;
        const uint8_t *gs = sixteenths ? m->g16 : m->g8;
        uint32_t bar = (uint32_t)(sixteenths ? 2 * m->bar8 : m->bar8);
        int s_in = (int)(idx % bar);
        while (g < ng - 1 && s_in >= gs[g]) { s_in -= gs[g]; g++; }
        G = gs[g];
        pos = s_in;
        oct = (int)(((idx / bar) * (uint32_t)ng + (uint32_t)g) % set.arp_oct);
    }
    switch (set.arp_mode) {
    case 1: k = G - 1 - pos; break;                               /* baja */
    case 2: k = G == 4 ? SB4[pos] : SB3[pos]; break;              /* sube y baja */
    case 3:                                                       /* azar */
        arp_rng = arp_rng * 1664525u + 1013904223u;
        k = (int)((arp_rng >> 16) % (uint32_t)n);
        break;
    default: k = pos; break;                                      /* sube */
    }
    return clamp(achord.notes[k % n] + 12 * (k / n) + 12 * oct, 0, 127);
}

static void arp_fire(float step, uint32_t idx)
{
    int note;
    if (arp_restart) { arp_restart = 0; arp_i = 0; }
    note = set.arp_free ? arp_note(arp_i) : arp_note_bar(idx);
    arp_i++;
    if (note < 0) return;
    arp_voice(note, 0, step * arp_len_mul, set.arp_len);
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
static void bp_fire(float step, uint32_t idx)
{
    int note = achord.bass;
    if (set.bp_pat == 3) {                   /* acorde: sus notas en el grave, en grupos de 4 (3 en tresillos) */
        int iv[BL_MAX_NOTES], n = 0, k, j, G = set.bp_div == 2 ? 3 : 4, pos;
        for (k = 0; k < achord.n; k++) {     /* intervalos únicos sobre la fundamental, ordenados */
            int d = ((achord.notes[k] - achord.bass) % 12 + 12) % 12, dup = 0;
            for (j = 0; j < n; j++) if (iv[j] == d) dup = 1;
            if (dup) continue;
            for (j = n; j > 0 && iv[j - 1] > d; j--) iv[j] = iv[j - 1];
            iv[j] = d;
            n++;
        }
        pos = (int)(idx % (uint32_t)G);
        if (n) note += iv[pos % n] + 12 * (pos / n);   /* con 3 notas: fundamental, tercera, quinta, octava */
    } else if (idx & 1u) {                   /* quinta u octava: la fundamental, siempre en el pulso */
        note += set.bp_pat == 1 ? 7 : set.bp_pat == 2 ? 12 : 0;
    }
    bp_i++;
    note += 12 * set.bp_oct;
    arp_voice(clamp(note, 0, 127), 1, step * bp_len_mul, set.bp_len);
}

static float e_att, e_dec, e_rel, e_sus;
static float lfo_ph[3];
/* volúmenes y paneos que mueven los LFO, suavizados por muestra: pad, arpegio, bajo, batería, charles, total */
enum { SM_PAD, SM_ARP, SM_BASS, SM_DRUM, SM_HAT, SM_MASTER, NSM };
static float trem_now[NSM] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, pan_now[3];
static uint32_t lfo_rng[3] = {0x2468ACEu, 0x13579BDu, 0x0F1E2D3u};

/* lo que cada LFO pide este bloque: afinación, volumen, paneo y brillo de su sonido */

static float lfo_step(int k, uint32_t n)       /* avanza el LFO k un bloque; su valor, -1..1 */
{
    const lfo_t *L = &set.lfo[k];
    float ph;
    if (L->sync) {                           /* a tempo: la fase sale del reloj maestro (cae con el compás) */
        float per = L->sync >= 5 ? (float)MT->bar8 * 0.5f * (float)(1 << (L->sync - 5)) : LFO_SYNC_BEATS[L->sync];
        float b = (float)tp_bint + tp_bfrac, c = b / per;
        float np = c - fm_floorf(c);
        if (np < lfo_ph[k]) lfo_rng[k] = lfo_rng[k] * 1664525u + 1013904223u;
        lfo_ph[k] = np;
    } else {
        lfo_ph[k] += lfo_hz(L->rate) * (float)n / SR;
        if (lfo_ph[k] >= 1.0f) { lfo_ph[k] -= 1.0f; lfo_rng[k] = lfo_rng[k] * 1664525u + 1013904223u; }   /* azar: uno por ciclo */
    }
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
    float M[NDEST], D[NDEST];              /* lo que pide cada destino este bloque: modulación y cuánta como mucho */
    float trem_t[NSM], pan_t[3], pitch_pad, pitch_arp, pitch_bass;
    {
        int k;
        for (k = 0; k < NDEST; k++) M[k] = D[k] = 0.0f;
        for (k = 0; k < 3; k++) {
            const lfo_t *L = &set.lfo[k];
            if (L->on && L->depth && L->dest < NDEST) {
                float depth = (float)L->depth / 10.0f;
                M[L->dest] += lfo_step(k, n) * depth;
                D[L->dest] += depth;
            }
        }
        /* volumen: baja desde arriba (1 - cantidad .. 1) */
        trem_t[SM_PAD] = 1.0f + 0.5f * M[D_PAD_VOL] - 0.5f * D[D_PAD_VOL];
        trem_t[SM_ARP] = 1.0f + 0.5f * M[D_ARP_VOL] - 0.5f * D[D_ARP_VOL];
        trem_t[SM_BASS] = 1.0f + 0.5f * M[D_BASS_VOL] - 0.5f * D[D_BASS_VOL];
        trem_t[SM_DRUM] = 1.0f + 0.5f * M[D_DRUM_VOL] - 0.5f * D[D_DRUM_VOL];
        trem_t[SM_HAT] = 1.0f + 0.5f * M[D_HAT_VOL] - 0.5f * D[D_HAT_VOL];
        trem_t[SM_MASTER] = 1.0f + 0.5f * M[D_MASTER_VOL] - 0.5f * D[D_MASTER_VOL];
        for (k = 0; k < NSM; k++) if (trem_t[k] < 0.0f) trem_t[k] = 0.0f;
        pan_t[0] = fm_clampf(M[D_PAD_PAN], -1.0f, 1.0f);
        pan_t[1] = fm_clampf(M[D_ARP_PAN], -1.0f, 1.0f);
        pan_t[2] = 0.0f;
        pitch_pad = D[D_PAD_PITCH] > 0.0f ? fm_exp2f(M[D_PAD_PITCH] * 0.5f / 12.0f) : 1.0f;   /* hasta medio tono */
        pitch_arp = D[D_ARP_PITCH] > 0.0f ? fm_exp2f(M[D_ARP_PITCH] * 0.5f / 12.0f) : 1.0f;
        pitch_bass = D[D_BASS_PITCH] > 0.0f ? fm_exp2f(M[D_BASS_PITCH] * 0.5f / 12.0f) : 1.0f;
        arp_len_mul = fm_clampf(1.0f + 0.8f * M[D_ARP_LEN], 0.15f, 1.9f);
        bp_len_mul = fm_clampf(1.0f + 0.8f * M[D_BASS_LEN], 0.15f, 1.9f);
    }
    const sound_t *SD = &SOUND[set.sound % NSND];
    /* el filtro del pad: BRILLO + el del sonido + el LFO; la envolvente lo abre desde ahí */
    float pf_base = (float)(set.bright + SD->bri) * 0.25f + M[D_PAD_FILT] * 2.0f;
    float pw;                              /* el ancho del pulso, con su LFO lento (0,6 Hz): una vez por bloque */
    {
        float tri;
        pwm_ph += 0.6f * (float)n / SR; if (pwm_ph >= 1.0f) pwm_ph -= 1.0f;
        tri = 1.0f - 4.0f * fm_fabsf(pwm_ph - 0.5f);
        pw = fm_clampf(SD->pw + SD->pwm * 0.45f * tri, 0.05f, 0.95f);
    }
    float pf_dec16 = fm_expf(-16.0f / (SD->fdec * SR)), pf_r = 2.0f - 2.0f * SD->res;   /* k = 1/Q */
    float ch_rate = SD->chorus == 2 ? 0.863f / SR : 0.514f / SR;
    float ch_dep = (SD->chorus == 2 ? 1.71e-3f : 2.13e-3f) * SR * fm_clampf(1.0f + M[D_PAD_DET], 0.0f, 2.0f);
    float ch_dry = SD->chorus ? 0.5753f : 1.0f, ch_wet = SD->chorus ? 0.838f : 0.0f;
    float bf_base = (float)set.bright * 0.10f + M[D_BASS_BRIGHT] * 1.5f;   /* el bajo: su filtro */
    float bf_dec16 = fm_expf(-16.0f / (0.18f * SR));
    float arp_ix = fm_clampf(0.22f * (1.0f + M[D_ARP_BRIGHT]), 0.0f, 0.6f);   /* el arpegio: FM, brillo al golpe */
    float padL = 1.0f, padR = 1.0f, arpL = 0.0f, arpR = 0.0f, bassG = 0.0f, drumG = 1.0f, hatG = 1.0f, outG = 0.0f;
    float gain = (float)master_q12 / 4096.0f * 7000000.0f;
    float mf_g = 0.0f;                     /* TOTAL AGUDOS: el LFO cierra los agudos de todo, desde abierto */
    if (D[D_MASTER_FILT] > 0.0f) {
        float fc = 16000.0f * fm_exp2f(2.5f * (M[D_MASTER_FILT] - D[D_MASTER_FILT]));   /* hasta 5 octavas abajo */
        mf_g = 1.0f - fm_expf(-6.2831853f * fc / SR);
    }
    /* lo que manda cada capa a cada efecto (FX: ALGORITHM elige la capa, K4 cuánto) */
    float dsend[4], d2send[4], rsend[4];
    {
        int k;
        float dm = FX_ON(FX_DLY) ? fm_maxf(0.0f, 0.07f * (1.0f + M[D_DLY_MIX])) : 0.0f;
        float rm = FX_ON(FX_REV) ? fm_maxf(0.0f, 0.12f * 0.03f * (1.0f + M[D_REV_MIX])) : 0.0f;
        float d2m = FX_ON(FX_DLY2) ? 0.07f : 0.0f;
        for (k = 0; k < 4; k++) {
            dsend[k] = (float)set.fx_send[FX_DLY][k] * dm;
            d2send[k] = (float)set.fx_send[FX_DLY2][k] * d2m;
            rsend[k] = (float)set.fx_send[FX_REV][k] * rm;
        }
    }
    int dly_run = FX_ON(FX_DLY), d2_run = FX_ON(FX_DLY2), rev_run = FX_ON(FX_REV);
    float dfb = dly_run ? (float)set.dly_fb * 0.09f : 0.0f;
    float dtarget = dly_target() * (1.0f + 0.04f * M[D_DLY_TIME]);   /* el tiempo, movido: efecto de cinta */
    float d2fb = d2_run ? (float)set.d2_fb * 0.09f : 0.0f;
    float d2target = fm_minf(SR * (float)set.d2_ms / 1000.0f * (1.0f + 0.04f * M[D_DL2_TIME]), (float)(DLY_MAX - 2));
    float d2tone = 0.15f + (float)set.d2_tone * 0.094f;
    int dstill = D[D_DLY_TIME] <= 0.0f && fm_fabsf(dtarget - dly_cur) < 0.5f;
    int32_t dint = (int32_t)(dly_cur + 0.5f);
    int d2still = D[D_DL2_TIME] <= 0.0f && fm_fabsf(d2target - dl2_cur) < 0.5f;
    int32_t d2int = (int32_t)(dl2_cur + 0.5f);
    float dglide = (D[D_DLY_TIME] > 0.0f || D[D_DL2_TIME] > 0.0f) ? 0.002f : 0.0002f;
    float binc = (float)set.tempo / 60.0f / SR;   /* pulsos por muestra */
    float dtone = 0.15f + (float)set.dly_tone * 0.094f, rwid = (float)set.rev_width / 10.0f;
    /* los volúmenes de HOME: 8 es el nivel de antes (el bajo, de serie en 6: tapaba al pad) */
    float g_pad = 0.11f * (float)set.vol_pad / VOL_UNITY, g_bass = (float)set.vol_bass / VOL_UNITY;
    float g_arp = (float)set.vol_arp / VOL_UNITY, g_strum = (float)set.vol_strum / VOL_UNITY;
    float gpad = g_pad * SD->gain;
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
        int on = ARP_ON, bon = set.bp_on != 0;
        int run = on || bon, ran = arp_was != 0 || bp_was;   /* algo va al tempo: reloj MIDI */
        if (on != arp_was || bon != bp_was) {  /* el arpegio o el bajo pulsado entran o salen */
            arp_was = on;
            bp_was = bon;
            take_chord(&achord);
            if (run && !ran) midi_rt(0xFAu);
            else if (!run && ran) midi_rt(0xFCu);
            if (!on) arp_release(0);
            if (!bon) arp_release(1);
            /* el que se enciende espera a la siguiente línea de la rejilla: entra a tiempo con lo que ya suena */
            if (on) arp_last = pf_bint * (uint32_t)(set.arp_div == 0 ? 2 : set.arp_div == 1 ? 3 : 4)
                               + (uint32_t)(pf_bfrac * (float)(set.arp_div == 0 ? 2 : set.arp_div == 1 ? 3 : 4));
            if (bon) bp_last = pf_bint * (uint32_t)(set.bp_div + 1u) + (uint32_t)(pf_bfrac * (float)(set.bp_div + 1u));
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
            pf_bint = 0;
            pf_bfrac = 0.0f;
            clk_last = arp_last = bp_last = drum_last = bar_last = ~0u;
            tp_resets++;
        } else if (chord_new && achord.gate && tp_gate_was && !tp_locked && !set.quant) {
            pf_bint = 0;                     /* en directo, acorde nuevo: el arpegio y el bajo, en el acto */
            pf_bfrac = 0.0f;
            arp_last = bp_last = drum_last = ~0u;
        } else if (chord_new && achord.gate && tp_gate_was) {
            /* con la rejilla del compás: si el acorde llega un pelín tarde (hasta 60 ms tras un paso), ese paso
             * se repite con el acorde nuevo */
            if (ARP_ON && arp_age < (uint32_t)(0.06f * SR)) { arp_release(0); arp_fire(arp_step_len(), arp_last); arp_age = 0; }
            if (set.bp_on && bp_age < (uint32_t)(0.06f * SR)) { arp_release(1); bp_fire(bp_step_len(), bp_last); bp_age = 0; }
        }
        chord_new = 0;
        if (tp_locked || set.quant) { pf_bint = tp_bint; pf_bfrac = tp_bfrac; }   /* con una idea, o cuadrando: la rejilla del compás */
        tp_gate_was = achord.gate;
    }
    { uint32_t pkt; int port; while (plat_midi_in(&pkt, &port)) {} }   /* (la entrada MIDI llegará más adelante) */
    for (i = 0; i < n; i++) {
        uint32_t vm;
        float l = 0.0f, r = 0.0f, s, lowsum = 0.0f, al = 0.0f, ar = 0.0f, sl = 0.0f, sr = 0.0f, bm, din, d2in, rin;
        {   /* el reloj maestro avanza; cada uno toca al cruzar su línea de la rejilla */
            static const uint8_t ARP_PB[3] = {2, 3, 4}, BP_PB[4] = {1, 2, 3, 4};
            uint32_t idx;
            tp_bfrac += binc;
            if (tp_bfrac >= 1.0f) { tp_bfrac -= 1.0f; tp_bint++; }
            pf_bfrac += binc;
            if (pf_bfrac >= 1.0f) { pf_bfrac -= 1.0f; pf_bint++; }
            arp_age++;
            bp_age++;
            idx = tp_bint * 2u + (tp_bfrac >= 0.5f ? 1u : 0u);   /* corcheas: al empezar cada compás, un pad sin
                                                                   * sostenido (piano, campana) vuelve a tocar */
            if (idx != bar_last) {
                bar_last = idx;
                if (idx % MT->bar8 == 0u && set.env_s == 0u && achord.gate) {
                    for (j = 0; j < NPAD; j++) if (pad[j].gate) pad_strike(&pad[j]);
                }
            }
            idx = tp_bint * 24u + (uint32_t)(tp_bfrac * 24.0f);
            if (idx != clk_last) { clk_last = idx; if (ARP_ON || set.bp_on || DRUM_ON) midi_rt(0xF8u); }
            if (ARP_ON) {
                uint32_t pb = ARP_PB[set.arp_div % 3u];
                idx = pf_bint * pb + (uint32_t)(pf_bfrac * (float)pb);
                if (idx != arp_last) { arp_last = idx; arp_age = 0; if (achord.gate) arp_fire(arp_step_len(), idx); }
            }
            if (set.bp_on) {
                uint32_t pb = BP_PB[set.bp_div % 4u];
                idx = pf_bint * pb + (uint32_t)(pf_bfrac * (float)pb);
                if (idx != bp_last) { bp_last = idx; bp_age = 0; if (achord.gate) bp_fire(bp_step_len(), idx); }
            }
            if (DRUM_ON) {                   /* la batería, en corcheas, con la misma rejilla */
                idx = pf_bint * 2u + (uint32_t)(pf_bfrac * 2.0f);
                if (idx != drum_last) { drum_last = idx; if (achord.gate) drum_step(idx % MT->bar8); }
            }
        }
        /* el pad: un bucle por tipo de sonido (sin decidirlo en cada voz y cada muestra), en mono */
#define PAD_ENV(v) do { \
            if ((v)->gate) { \
                if ((v)->stage == 0) { (v)->env += e_att; if ((v)->env >= 1.0f) { (v)->env = 1.0f; (v)->stage = 1; } } \
                else (v)->env = e_sus + ((v)->env - e_sus) * e_dec; \
            } else if ((v)->env > 0.0f) { (v)->env *= e_rel; if ((v)->env < 0.0003f) (v)->env = 0.0f; } \
        } while (0)
        if ((i & 15u) == 0) {                /* el filtro del pad y el del bajo: su corte y su envolvente, cada 16 */
            float fc, w;
            if (pf_trig) { pf_trig = 0; pf_stage = 0; }   /* la del pad: sube con el ataque, cae sola */
            if (pf_stage == 0) { pf_env += e_att * 16.0f; if (pf_env >= 1.0f) { pf_env = 1.0f; pf_stage = 1; } }
            else pf_env *= pf_dec16;
            if (bf_trig) { bf_trig = 0; bf_env = 1.0f; }  /* la del bajo: se abre con cada nota */
            else bf_env *= bf_dec16;
            fc = 300.0f * fm_exp2f(pf_base + SD->fenv * pf_env);
            if (fc > 12000.0f) fc = 12000.0f;
            w = 3.14159265f * fc / SR;
            pf_g = w + w * w * w * (1.0f / 3.0f) + w * w * w * w * w * (2.0f / 15.0f);   /* tan */
            pf_k = pf_r;
            pf_h = 1.0f / (1.0f + pf_g * (pf_g + pf_k));
            fc = 110.0f * fm_exp2f(bf_base + 3.2f * bf_env);
            if (fc > 6000.0f) fc = 6000.0f;
            bf_g = 6.2831853f * fc / SR;     /* un polo: 1 - e^-w, de sobra por debajo de 6 kHz */
            bf_g = bf_g * (1.0f - 0.5f * bf_g);
        }
        switch (SD->kind) {
        case K_EP:                           /* piano eléctrico (7FM): cuerpo 1:1 que ladra al golpe y el diente */
            for (vm = pad_on; vm; vm &= vm - 1u) {
                pad_v *v = &pad[j = __builtin_ctz(vm)];
                float a, ix;
                PAD_ENV(v);
                if (v->env == 0.0f) { if (!v->gate) pad_on &= ~(1u << j); continue; }
                v->p1 += v->i1 * pitch_pad * 0.5f; if (v->p1 >= 1.0f) v->p1 -= 1.0f;   /* una octava abajo */
                v->p2 += v->i1 * pitch_pad * 7.0f; if (v->p2 >= 1.0f) v->p2 -= 1.0f;   /* el diente: 14:1 */
                ix = 0.16f + 0.45f * v->tr;
                a = tsin(v->p1 + 1.0f + ix * tsin(v->p1)) + 0.22f * v->tr * tsin(v->p2);
                v->tr *= 0.99930f;           /* ~30 ms */
                l += a * v->env;
            }
            break;
        case K_BELL:                         /* campana: 3,5:1, inarmónica */
            for (vm = pad_on; vm; vm &= vm - 1u) {
                pad_v *v = &pad[j = __builtin_ctz(vm)];
                PAD_ENV(v);
                if (v->env == 0.0f) { if (!v->gate) pad_on &= ~(1u << j); continue; }
                v->p1 += v->i1 * pitch_pad; if (v->p1 >= 1.0f) v->p1 -= 1.0f;
                v->p2 += v->i1 * pitch_pad * 3.5f; if (v->p2 >= 1.0f) v->p2 -= 1.0f;
                l += tsin(v->p1 + 1.0f + (0.25f + 0.40f * v->tr) * v->env * tsin(v->p2)) * v->env;
                v->tr *= 0.99985f;
            }
            break;
        case K_GLASS:                        /* cristal: seno y su octava */
            for (vm = pad_on; vm; vm &= vm - 1u) {
                pad_v *v = &pad[j = __builtin_ctz(vm)];
                PAD_ENV(v);
                if (v->env == 0.0f) { if (!v->gate) pad_on &= ~(1u << j); continue; }
                v->p1 += v->i1 * pitch_pad; if (v->p1 >= 1.0f) v->p1 -= 1.0f;
                l += (tsin(v->p1) + 0.25f * tsin2(v->p1)) * v->env;
            }
            break;
        default: {                           /* DCO: sierra + pulso (PWM) + sub, sin aliasing */
            float kS = SD->saw, kP = SD->pulse, kU = SD->sub;
            for (vm = pad_on; vm; vm &= vm - 1u) {
                pad_v *v = &pad[j = __builtin_ctz(vm)];
                float t, dt, a;
                PAD_ENV(v);
                if (v->env == 0.0f) { if (!v->gate) pad_on &= ~(1u << j); continue; }
                dt = v->i1 * pitch_pad;
                t = v->p1 + dt;
                if (t >= 1.0f) { t -= 1.0f; v->sub = -v->sub; }   /* el sub cambia cada vuelta: una octava abajo */
                v->p1 = t;
                {
                    float b1 = blep(t, dt), t2 = t + pw;
                    if (t2 >= 1.0f) t2 -= 1.0f;
                    a = kS * (2.0f * t - 1.0f - b1) + kU * v->sub;
                    if (kP > 0.0f) a += kP * ((2.0f * t - b1) - (2.0f * t2 - blep(t2, dt)));
                }
                l += a * v->env;
            }
            break;
        }
        }
        {   /* el filtro (SVF paso bajo, resonante) y el chorus del Juno */
            float x = l * gpad, hp, bp, lp, d0, d1, tri, rp, fr, w0, w1;
            int32_t i0;
            hp = (x - (pf_g + pf_k) * pf_s1 - pf_s2) * pf_h;
            bp = pf_g * hp + pf_s1; pf_s1 = pf_g * hp + bp;
            lp = pf_g * bp + pf_s2; pf_s2 = pf_g * bp + lp;
            x = lp;
            if (ch_wet > 0.0f) {
                ch_buf[ch_w] = x;
                if (i & 1u) { w0 = ch_w0; w1 = ch_w1; goto chorus_out; }   /* el BBD, a media velocidad: de sobra */
                ch_ph += ch_rate * 2.0f; if (ch_ph >= 1.0f) ch_ph -= 1.0f;
                tri = 1.0f - 4.0f * fm_fabsf(ch_ph - 0.5f);
                d0 = 3.3e-3f * SR + ch_dep * tri;
                d1 = 3.3e-3f * SR - ch_dep * tri;
                rp = (float)ch_w - d0 + 256.0f; i0 = (int32_t)rp; fr = rp - (float)i0;
                w0 = ch_buf[i0 & 255] + (ch_buf[(i0 + 1) & 255] - ch_buf[i0 & 255]) * fr;
                rp = (float)ch_w - d1 + 256.0f; i0 = (int32_t)rp; fr = rp - (float)i0;
                w1 = ch_buf[i0 & 255] + (ch_buf[(i0 + 1) & 255] - ch_buf[i0 & 255]) * fr;
                ch_w0 = w0; ch_w1 = w1;
            chorus_out:
                ch_w = (ch_w + 1u) & 255u;
                l = ch_dry * x + ch_wet * w0;
                r = ch_dry * x + ch_wet * w1;
            } else {
                l = r = x;
            }
        }
        if ((i & 7u) == 0) {   /* volumen y paneo de cada LFO, sin escalones (cada 8 muestras basta) */
            int k;
            for (k = 0; k < NSM; k++) trem_now[k] += (trem_t[k] - trem_now[k]) * 0.016f;
            for (k = 0; k < 2; k++) pan_now[k] += (pan_t[k] - pan_now[k]) * 0.016f;
            padL = trem_now[SM_PAD] * (pan_now[0] > 0.0f ? 1.0f - pan_now[0] : 1.0f);
            padR = trem_now[SM_PAD] * (pan_now[0] < 0.0f ? 1.0f + pan_now[0] : 1.0f);
            arpL = trem_now[SM_ARP] * g_arp * 1.1f * (pan_now[1] > 0.0f ? 1.0f - pan_now[1] : 1.0f);
            arpR = trem_now[SM_ARP] * g_arp * 0.9f * (pan_now[1] < 0.0f ? 1.0f + pan_now[1] : 1.0f);
            bassG = trem_now[SM_BASS] * g_bass;
            drumG = trem_now[SM_DRUM];
            hatG = trem_now[SM_HAT];
            outG = trem_now[SM_MASTER];
        }
        /* cada capa por su lado (cada una manda lo suyo a los efectos): pad, arpegio, rasgueo, bajo */
        l *= padL;
        r *= padR;
        /* rasgueos: seno con algo de segundo armónico, que se apaga solo */
        for (vm = plk_on; vm; vm &= vm - 1u) {   /* solo las voces que suenan */
            plk_v *p;
            j = __builtin_ctz(vm);
            p = &plk[j];
            if (p->left <= 0 && p->env < 0.002f) { plk_on &= ~(1u << j); continue; }   /* (bajo -54 dB ya no se oye) */
            p->ph += p->inc * (p->left < 0 ? 1.0f : p->low ? pitch_bass : pitch_arp);
            if (p->ph >= 1.0f) p->ph -= 1.0f;
            if (p->left < 0) p->env *= 0.99990f;            /* rasgueo: se apaga solo */
            else if (p->left > 0) {                           /* arpegio: su envolvente mientras dura el paso */
                float at = p->low ? be_att : ae_att, su = p->low ? be_sus : ae_sus, de = p->low ? be_dec : ae_dec;
                if (p->stage == 0) { p->env += at; if (p->env >= 1.0f) { p->env = 1.0f; p->stage = 1; } }
                else p->env = su + (p->env - su) * de;
                if (--p->left == 0) midi_ch(0x80u, p->low ? 3u : 4u, p->note, 0);
            } else p->env *= p->low ? be_rel : ae_rel;        /* y su release */
            if (p->low) {                                     /* bajo arpegiado: sierra y seno, a su filtro */
                float dt = p->inc;
                lowsum += (0.55f * (2.0f * p->ph - 1.0f - blep(p->ph, dt)) + 0.65f * tsin(p->ph)) * p->env * p->amp;
                continue;
            }
            if (p->left < 0) {                                /* rasgueo: FM 2:1, brilla al golpe y se apaga */
                s = tsin(p->ph + 1.0f + 0.30f * p->env * psin2(p->ph)) * p->env * p->amp * g_strum;
                sl += s * 1.1f; sr += s * 0.9f;
            } else {                                          /* arpegio: FM 2:1, con su LFO */
                s = tsin(p->ph + 1.0f + arp_ix * (0.4f + p->env) * psin2(p->ph)) * p->env * p->amp;
                al += s * arpL;
                ar += s * arpR;
            }
        }
        /* bajo: el pulsado y el sostenido (un seno), mono */
        bm = lowsum;
        if (bass_gate) {
            if (bass_stage == 0) { bass_env += be_att; if (bass_env >= 1.0f) { bass_env = 1.0f; bass_stage = 1; } }
            else bass_env = be_sus + (bass_env - be_sus) * be_dec;
        } else bass_env *= be_rel;
        if (bass_env > 0.0001f) {
            bass_ph += bass_inc * pitch_bass; if (bass_ph >= 1.0f) bass_ph -= 1.0f;
            bm += (0.55f * (2.0f * bass_ph - 1.0f - blep(bass_ph, bass_inc)) + 0.65f * tsin(bass_ph)) * bass_env * 0.30f;
        }
        /* el filtro del bajo (dos polos): se abre con cada nota y se cierra en ~0,2 s, como un Juno */
        bf_s1 += (bm - bf_s1) * bf_g;
        bf_s2 += (bf_s1 - bf_s2) * bf_g;
        bm = bf_s2 * bassG;
        {   /* lo que entra a cada efecto, en mono */
            float pm = (l + r) * 0.5f, am = (al + ar) * 0.5f, sm = (sl + sr) * 0.5f;
            din = dly_run ? dsend[0] * pm + dsend[1] * bm + dsend[2] * am + dsend[3] * sm : 0.0f;
            d2in = d2_run ? d2send[0] * pm + d2send[1] * bm + d2send[2] * am + d2send[3] * sm : 0.0f;
            rin = rev_run ? rsend[0] * pm + rsend[1] * bm + rsend[2] * am + rsend[3] * sm : 0.0f;
        }
        l += al + sl + bm;
        r += ar + sr + bm;
        /* efectos: delay de rebote y reverb, sobre el pad, el rasgueo y el arpegio */
        if (dly_run) {
            float yl, yr;
            if (dstill) {                    /* tiempo quieto: lectura directa */
                int32_t i0 = (int32_t)dly_w - dint;
                if (i0 < 0) i0 += DLY_MAX;
                yl = (float)dly_buf[0][i0] * (1.0f / 32767.0f);
                yr = (float)dly_buf[1][i0] * (1.0f / 32767.0f);
            } else {
                dly_cur += (dtarget - dly_cur) * dglide;
                yl = dly_read(0, dly_cur);
                yr = dly_read(1, dly_cur);
            }
            dly_lp[0] += (yr - dly_lp[0]) * dtone;   /* las repeticiones se oscurecen según el tono */
            dly_lp[1] += (yl - dly_lp[1]) * dtone;
            dly_buf[0][dly_w] = sat16(din + dly_lp[0] * dfb);   /* lo nuevo entra por la izquierda */
            dly_buf[1][dly_w] = sat16(dly_lp[1] * dfb);                      /* y rebota a la derecha */
            if (++dly_w >= DLY_MAX) dly_w = 0;
            l += yl;
            r += yr;
        }
        if (d2_run) {                        /* el delay libre */
            float y;
            if (d2still) {                   /* tiempo quieto: lectura directa */
                int32_t i0 = (int32_t)dl2_w - d2int;
                if (i0 < 0) i0 += DLY_MAX;
                y = (float)dl2_buf[i0] * (1.0f / 32767.0f);
            } else {                         /* tiempo moviéndose: interpola */
                float pos = (float)dl2_w - dl2_cur, fr;
                int32_t i0, i1;
                dl2_cur += (d2target - dl2_cur) * dglide;
                if (pos < 0.0f) pos += (float)DLY_MAX;
                i0 = (int32_t)pos;
                if (i0 >= DLY_MAX) i0 = DLY_MAX - 1;
                fr = pos - (float)i0;
                i1 = i0 + 1 >= DLY_MAX ? 0 : i0 + 1;
                y = ((float)dl2_buf[i0] * (1.0f - fr) + (float)dl2_buf[i1] * fr) * (1.0f / 32767.0f);
            }
            dl2_lp += (y - dl2_lp) * d2tone;
            dl2_buf[dl2_w] = sat16(d2in + dl2_lp * d2fb);
            if (++dl2_w >= DLY_MAX) dl2_w = 0;
            l += y;
            r += y;
        }
        if (rev_run) {
            float rl, rr;
            reverb_st(rin, &rl, &rr);
            l += (rl * (1.0f + rwid) + rr * (1.0f - rwid)) * 0.5f;   /* ancho: de mono a todo estéreo */
            r += (rr * (1.0f + rwid) + rl * (1.0f - rwid)) * 0.5f;
        }
        gain_now += (gain - gain_now) * 0.004f;
        {
            float g = gain_now * outG;
        {   /* la batería, seca y al centro */
            float d = drum_sample(hatG) * drumG;
            l += d;
            r += d;
        }
        if (mf_g > 0.0f) {                   /* dos polos por lado */
            mf_l1 += (l - mf_l1) * mf_g; mf_l2 += (mf_l1 - mf_l2) * mf_g; l = mf_l2;
            mf_r1 += (r - mf_r1) * mf_g; mf_r2 += (mf_r1 - mf_r2) * mf_g; r = mf_r2;
        }
        lr[2 * i] = (int32_t)(soft_clip(l) * g);
        lr[2 * i + 1] = (int32_t)(soft_clip(r) * g);
        }
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
    uint8_t q_pend;                      /* un acorde nuevo espera a su línea de la rejilla (CUADRAR) */
    uint8_t repress;
    uint8_t fx_layer;
    uint8_t hinted;
    uint8_t kit_bank;
    int8_t bt_pick;                      /* Bluetooth: lo elegido con ALGORITHM (0 BUSCAR) */
    uint8_t bt_tried, bt_last;           /* la reconexión de al arrancar, ya intentada; el último estado dicho */                    /* el banco cuyo sonido suena ahora (el que se edita) */                      /* el aviso de "sube el volumen", ya dado */                    /* FX: la capa cuyo envío lleva K4 (ALGORITHM la elige) */                     /* se pulsó una tecla de acorde: vuelve a sonar aunque sea el mismo */
    uint32_t q_sig, q_resets;
    int8_t q_deg;                        /* el acorde que espera (sigue esperando aunque se suelte la tecla) */
    uint8_t q_mods, q_inv;
    float q_due;                         /* en pulsos del reloj maestro */
    uint8_t mem_arp, mem_fx, mem_env, mem_lfo, mem_glo, mem_drum;   /* la última página de cada botón: se vuelve a ella */
    uint32_t leds;                       /* los botones iluminados (para las pruebas) */
    uint8_t page;                        /* de quién son K1..K4: 0 acorde, 1 arpegiador, 2 delay, 3 reverb,
                                          * 4..6 envolventes (pad, arpegio, bajo), 7 bajo pulsado */
    uint8_t rec;                         /* 0 no, 1 armado (espera el primer acorde), 2 grabando */
    uint8_t play_bank;                   /* el banco que suena */
    uint8_t next_bank;                   /* el que sonará al acabar el bucle (0xFF: ninguno) */
    uint8_t playing;
    float rec_acc, play_acc;             /* corcheas desde que empezó la grabación / dentro del bucle */
    uint32_t rec_sig, shown_pos;
    uint32_t rec_resets;                 /* tp_resets al empezar a grabar */
    uint32_t stop_t;                     /* la tecla STOP bajó (0: no está pulsada, o ya actuó)
                                          * corta: para el acorde; un segundo: HOLD sí / no */
} st;

static int fx_send_ok(void)
{
    int a, b;
    for (a = 0; a < 3; a++) for (b = 0; b < 4; b++) if (set.fx_send[a][b] > 10u) return 0;
    return 1;
}
static int page_fx(int pg) { return pg == PG_DLY ? FX_DLY : pg == PG_DLY2 ? FX_DLY2 : pg == PG_REV ? FX_REV : -1; }

static void cut_update(void)
{
    float fc = 300.0f * fm_exp2f((float)set.bright * 0.25f);   /* (ya no lo usa el audio: el filtro va en el render) */
    if (fc > 16000.0f) fc = 16000.0f;
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
    set.sound = 0;
    set.quant = 2;
    set.hold = 1;
    set.arp_mute_vol = VOL_UNITY;
    set.arp_free = 0;
    set.meter = 0;
    set.arp_mode = 0;
    set.arp_div = 0;
    set.arp_oct = 1;
    set.arp_len = 5;
    set.tempo = 100;
    memset(set.fx_send, 0, sizeof set.fx_send);   /* los efectos: nada les manda nada; suenan al subir una mezcla */
    set.dly_div = 2;
    set.dly_fb = 4;
    set.dly_on = 1;                          /* (no silenciados: suenan en cuanto se les manda algo) */
    set.rev_on = 1;
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
    set.bp_oct = 0;
    set.vol_pad = 0;                         /* pad, bajo y arpegio, callados de serie: se suben en HOME */
    set.vol_bass = 0;
    set.vol_arp = 0;                         /* el arpegio, callado hasta que se le sube */
    set.vol_strum = VOL_UNITY;
    set.dly_tone = 7;
    set.d2_on = 1;
    set.d2_ms = 300;
    set.d2_fb = 3;
    set.d2_tone = 6;
    set.drm_kick = set.drm_snare = set.drm_hat = 0;
    set.drm_hopen = 0;
    set.drm_mute = 0;
    set.rev_width = 10;
    {
        int k;
        for (k = 0; k < 3; k++) {            /* los LFO, apagados de serie */
            set.lfo[k].on = 0;
            set.lfo[k].dest = k == 0 ? D_PAD_FILT : k == 1 ? D_ARP_VOL : D_BASS_VOL;
            set.lfo[k].rate = 6;
            set.lfo[k].depth = 5;
            set.lfo[k].shape = 0;
            set.lfo[k].sync = 0;
        }
    }
    set.benv_a = 1;                          /* el bajo: ~4 ms de ataque (con 20 ms llegaba tarde al arpegio), ~65 ms de release */
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
        if (set.lfo[k].on > 1u || set.lfo[k].dest >= NDEST || set.lfo[k].rate > 15u || set.lfo[k].depth > 10u || set.lfo[k].shape > 3u || set.lfo[k].sync >= NSYNC) return 0;
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
    if (plat_store_load(OBJ_SETTINGS, &sv, sizeof sv) != (int)sizeof sv || set.magic != SET_MAGIC
        || set.key > 11u || set.mode >= BL_MODE_COUNT || set.octave < -2 || set.octave > 2
        || set.bright > 20u || set.lead > 1u || set.sound >= NSND || !fx_send_ok() || set.quant > 3u || set.hold > 1u
        || set.arp_mute_vol > VOL_MAX || set.arp_free > 1u || set.meter >= NMETER || set.arp_mode > 3u || set.arp_div > 2u || set.arp_oct < 1u || set.arp_oct > 3u
        || set.arp_len < 1u || set.arp_len > 9u || set.tempo < 40u || set.tempo > 220u
        || set.dly_div > 5u || set.dly_fb > 9u 
        || set.dly_on > 1u || set.rev_on > 1u || set.rev_size > 9u || set.rev_tone > 9u
        || set.env_a > 9u || set.env_d > 9u || set.env_s > 10u || set.env_r > 9u
        || set.aenv_a > 9u || set.aenv_d > 9u || set.aenv_s > 10u || set.aenv_r > 9u || set.bp_on > 1u || set.bp_div > 3u || set.bp_pat > 3u || set.bp_len < 1u || set.bp_len > 9u || set.bp_oct < -1 || set.bp_oct > 1
        || set.vol_pad > VOL_MAX || set.vol_bass > VOL_MAX || set.vol_arp > VOL_MAX || set.vol_strum > VOL_MAX || set.dly_tone > 9u || set.d2_on > 1u || set.d2_ms < 10u || set.d2_ms > 750u
        || set.d2_fb > 9u || set.d2_tone > 9u || set.drm_kick > VOL_MAX || set.drm_snare > VOL_MAX
        || set.drm_hat > VOL_MAX || set.drm_hopen > 10u || set.drm_mute > 1u || set.rev_width > 10u
        || !lfos_ok()
        || set.benv_a > 9u || set.benv_d > 9u || set.benv_s > 10u || set.benv_r > 9u
        || set.cur_bank >= NBANK || !banks_ok()) {
        settings_defaults();
        memset(sv.kit_used, 0, sizeof sv.kit_used);
    }
}
static int settings_valid(void)          /* el sonido que acaba de entrar, ¿tiene sentido? (los de los bancos) */
{
    return !(set.key > 11u || set.mode >= BL_MODE_COUNT || set.octave < -2 || set.octave > 2
        || set.bright > 20u || set.sound >= NSND || !fx_send_ok()
        || set.arp_mute_vol > VOL_MAX || set.arp_free > 1u || set.meter >= NMETER || set.arp_mode > 3u || set.arp_div > 2u || set.arp_oct < 1u || set.arp_oct > 3u
        || set.arp_len < 1u || set.arp_len > 9u || set.tempo < 40u || set.tempo > 220u
        || set.dly_div > 5u || set.dly_fb > 9u
        || set.dly_on > 1u || set.rev_on > 1u || set.rev_size > 9u || set.rev_tone > 9u
        || set.env_a > 9u || set.env_d > 9u || set.env_s > 10u || set.env_r > 9u
        || set.aenv_a > 9u || set.aenv_d > 9u || set.aenv_s > 10u || set.aenv_r > 9u || set.bp_on > 1u || set.bp_div > 3u || set.bp_pat > 3u || set.bp_len < 1u || set.bp_len > 9u || set.bp_oct < -1 || set.bp_oct > 1
        || set.vol_pad > VOL_MAX || set.vol_bass > VOL_MAX || set.vol_arp > VOL_MAX || set.vol_strum > VOL_MAX || set.dly_tone > 9u || set.d2_on > 1u || set.d2_ms < 10u || set.d2_ms > 750u
        || set.d2_fb > 9u || set.d2_tone > 9u || set.drm_kick > VOL_MAX || set.drm_snare > VOL_MAX
        || set.drm_hat > VOL_MAX || set.drm_hopen > 10u || set.drm_mute > 1u || set.rev_width > 10u
        || !lfos_ok()
        || set.benv_a > 9u || set.benv_d > 9u || set.benv_s > 10u || set.benv_r > 9u);
}

/* el sonido de la idea: guardarlo en su banco, o traer el de otro (o el de serie, si ese nunca se tocó) */
static void kit_store(int b)
{
    memcpy(sv.kits[b], (uint8_t *)&set + KIT_OFS, KIT_SIZE);
    sv.kit_used[b] = 1;
}
static void kit_defaults(void)
{
    static settings_t keep;              /* lo que no es del sonido (lo global y las ideas) se queda */
    keep = set;
    settings_defaults();
    memcpy((uint8_t *)&keep + KIT_OFS, (uint8_t *)&set + KIT_OFS, KIT_SIZE);
    set = keep;
}
static void kit_load(int b)
{
    if (sv.kit_used[b]) {
        memcpy((uint8_t *)&set + KIT_OFS, sv.kits[b], KIT_SIZE);
        if (!settings_valid()) kit_defaults();
    } else {
        kit_defaults();
    }
}

void app_init(void)
{
    sint_init();
    settings_load();
    cut_update();
    st.degree = -1;
    st.gate = 0;
    st.sig = 0;
    st.rec = 0;
    st.playing = 0;
    st.next_bank = 0xFFu;
    st.kit_bank = set.cur_bank;          /* lo que suena al arrancar es el sonido del banco elegido */
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
    if (!set.bp_on) midi(0x90u, 3, st.cur.bass, 100);   /* el bajo sostenido, solo si suena */
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
    if (gate && !st.hinted && !set.vol_pad && !set.vol_bass && !set.vol_arp) {   /* suena nada: dónde se sube */
        st.hinted = 1;
        app_say("SIN VOLUMEN", "HOME: K1 K2 K3");
    }
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

/* K4 en las páginas de efectos: lo que manda al efecto la capa elegida con ALGORITHM */
static void fx_send_knob(int32_t e, char *t)
{
    int f = page_fx(st.page), was = FX_ON(f);
    uint8_t *v = &set.fx_send[f][st.fx_layer & 3u];
    char h[20] = "MEZCLA ";
    *v = (uint8_t)clamp((int32_t)*v + e, 0, 10);
    if (!was && FX_ON(f)) fx_clear(f);       /* vuelve a sonar: sin ecos viejos */
    text_cat(h, LAYER_NAME[st.fx_layer & 3u]);
    int_text(*v * 10, t);
    text_cat(t, "%");
    set_changed(h, t);
}

static const char *bt_word(int s);      /* (Bluetooth: más abajo) */
static int bt_streaming(void);
static void bt_sel(void);
/* ------------------------------------------------------------ páginas --- */
static const char *page_state(int pg)    /* lo que se dice al entrar: si está encendido */
{
    switch (pg) {
    case PG_ARP: return ARP_ON ? "SI" : "NO";
    case PG_BP: return set.bp_on ? "SI" : "NO";
    case PG_BT: return bt_word(plat_bta_state());
    case PG_DLY: case PG_REV: case PG_DLY2: {
        int f = page_fx(pg);
        return !fx_unmuted(f) ? "SILENCIO" : fx_any(f) ? "SI" : "NO  SUBE UNA MEZCLA";
    }
    case PG_DRUM: return set.drm_mute ? "SILENCIO" : DRUM_ON ? "SI" : "NO  SEL: BASICO";
    case PG_LFO_PAD: case PG_LFO_ARP: case PG_LFO_BASS:
        return set.lfo[pg - PG_LFO_PAD].on ? LFO_DEST_NAME[set.lfo[pg - PG_LFO_PAD].dest % NDEST] : "NO";
    default: return "";
    }
}

/* si lo de la página está encendido (las envolventes no se encienden ni se apagan) */
static int page_active(int pg)
{
    switch (pg) {
    case PG_ARP: return ARP_ON;
    case PG_BP: return set.bp_on != 0;
    case PG_DLY: case PG_REV: case PG_DLY2: return FX_ON(page_fx(pg));
    case PG_BT: return bt_streaming();
    case PG_LFO_PAD: case PG_LFO_ARP: case PG_LFO_BASS: return set.lfo[pg - PG_LFO_PAD].on != 0;
    case PG_DRUM: return DRUM_ON;
    default: return 0;
    }
}

/* SEL: enciende o apaga lo de la página en la que se está */
static void sel_toggle(void)
{
    int pg = st.page;
    switch (pg) {
    case PG_ARP:                             /* silencio rápido: a 0, y vuelta al volumen que tenía */
        if (ARP_ON) { set.arp_mute_vol = set.vol_arp; set.vol_arp = 0; }
        else set.vol_arp = set.arp_mute_vol ? set.arp_mute_vol : VOL_UNITY;
        break;
    case PG_BP:
        set.bp_on = !set.bp_on;
        if (set.bp_on && !set.vol_bass) { set.vol_bass = 12; app_say("BAJO PULSADO", "SI  (BAJO AL 60%)"); return; }
        break;
    case PG_BT: bt_sel(); return;
    case PG_DLY: case PG_REV: case PG_DLY2: {   /* silencio rápido; si nadie le manda nada, la capa elegida al 30 % */
        int f = page_fx(pg);
        uint8_t *on = f == FX_DLY ? &set.dly_on : f == FX_DLY2 ? &set.d2_on : &set.rev_on;
        if (!fx_any(f)) {
            fx_clear(f);
            set.fx_send[f][st.fx_layer & 3u] = 3;
            *on = 1;
            set_dirty = 1;
            {
                char h[24] = "SI  (";
                text_cat(h, LAYER_NAME[st.fx_layer & 3u]);
                text_cat(h, " 30%)");
                app_say(PAGE_NAME[pg], h);
            }
            return;
        }
        if (!*on) fx_clear(f);
        *on = (uint8_t)!*on;
        break;
    }
    case PG_LFO_PAD: case PG_LFO_ARP: case PG_LFO_BASS: set.lfo[pg - PG_LFO_PAD].on = !set.lfo[pg - PG_LFO_PAD].on; break;
    case PG_DRUM:
        if (!DRUM_ON && !set.drm_mute) {     /* todo a 0: SEL pone un ritmo básico */
            set.drm_kick = 16; set.drm_snare = 14; set.drm_hat = 10;
            set_dirty = 1;
            app_say("RITMO", "SI  (BASICO)");
            return;
        }
        set.drm_mute = !set.drm_mute;
        break;
    case PG_ENV_PAD: case PG_ENV_ARP: case PG_ENV_BASS: app_say("ENVOLVENTE", "SIEMPRE ACTIVA"); return;
    default: app_say("SEL", "EN ARP, FX O LFO"); return;   /* (HOME y GLO: nada que encender) */
    }
    set_changed(PAGE_NAME[pg], page_state(pg));
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
static void cut_update(void);

/* ------------------------------------------------- Bluetooth: cascos y altavoces --- */
/* GLO, segunda página. ALGORITHM elige: BUSCAR, o un aparato (los encontrados, o si no los recordados); SEL hace lo
 * elegido (buscar, conectar) o cuelga si ya suena; K4, la salida (el aparato o el FM-1). Llega 150-300 ms tarde:
 * para escuchar lo hecho, no para tocar a tiempo. Al arrancar se conecta solo al último. */
static const char *bt_word(int s)
{
    switch (s) {
    case PLAT_BTA_SCANNING: return "BUSCANDO";
    case PLAT_BTA_FOUND: return "ELIGE UNO";
    case PLAT_BTA_NONE: return "NO HAY NADA";
    case PLAT_BTA_PAGING: return "LLAMANDO, ESPERA";
    case PLAT_BTA_LINKED: case PLAT_BTA_SETUP: return "CONECTANDO";
    case PLAT_BTA_STREAMING: return "SONANDO";
    case PLAT_BTA_REFUSED: return "NO QUISO";
    case PLAT_BTA_NO_LAST: return "SIN ANTERIOR";
    case PLAT_BTA_FAILED: return "NO CONTESTA";
    case PLAT_BTA_READY: return "LISTO";
    default: return "APAGADO";
    }
}
static int bt_streaming(void) { return FM1B_BT_AUDIO && plat_bta_state() == PLAT_BTA_STREAMING; }
static int bt_found_n(void) { char nm[17]; int n = 0; while (n < 16 && plat_bta_found(n, nm)) n++; return n; }
static int bt_known_n(void) { char nm[17]; int n = 0; while (n < 4 && plat_bta_known(n, nm)) n++; return n; }
/* el elemento i de la lista de ALGORITHM: 0 BUSCAR, luego los encontrados (o, si no hay, los recordados) */
static int bt_items(void) { int f = bt_found_n(); return 1 + (f ? f : bt_known_n()); }
static void bt_item_name(int i, char *nm)
{
    nm[0] = 0;
    if (i <= 0) { text_cat(nm, "BUSCAR"); return; }
    if (bt_found_n()) { if (!plat_bta_found(i - 1, nm)) nm[0] = 0; }
    else if (!plat_bta_known(i - 1, nm)) nm[0] = 0;
    if (!nm[0]) text_cat(nm, "?");
}
static void bt_sel(void)
{
    int s = plat_bta_state(), i = st.bt_pick;
    char nm[17];
    if (s == PLAT_BTA_STREAMING || s == PLAT_BTA_LINKED || s == PLAT_BTA_SETUP || s == PLAT_BTA_PAGING) {
        plat_bta_hangup();
        app_say("BLUETOOTH", "COLGADO");
        return;
    }
    if (i <= 0 || i >= bt_items()) {
        st.bt_pick = 0;
        plat_bta_scan(1);
        app_say("BLUETOOTH", "BUSCANDO");
        return;
    }
    bt_item_name(i, nm);
    if (bt_found_n()) plat_bta_pick(i - 1);
    else (void)plat_bta_reconnect(i - 1);
    app_say("CONECTANDO", nm);
}

/* el sonido pasa al del banco b (el que suena se guarda en el suyo) */
static void kit_switch(int b)
{
    if (b == st.kit_bank) return;
    kit_store(st.kit_bank);
    kit_load(b);
    st.kit_bank = (uint8_t)b;
    cut_update();
    st.redraw = 1;
}

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
    /* sonando o grabando: el reloj no se reinicia solo (grabando, en cuanto el audio lo puso a cero con el
     * primer acorde) */
    tp_locked = (uint8_t)(st.playing || (st.rec == 2 && tp_resets != st.rec_resets));
    if (st.rec == 2) st.rec_acc += d;
    if (st.playing && PLAY_LOOP.len) {
        st.play_acc += d;
        while (st.playing && st.play_acc >= (float)PLAY_LOOP.len) {
            st.play_acc -= (float)PLAY_LOOP.len;
            if (st.next_bank < NBANK) {      /* el bucle acabó: entra el banco pedido */
                st.play_bank = st.next_bank;
                st.next_bank = 0xFFu;
                kit_switch(st.play_bank);    /* y su sonido */
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
    kit_switch(set.cur_bank);            /* suena (o se para en) el banco elegido, con su sonido */
    st.playing = (uint8_t)(on && REC_LOOP.len);
    st.play_acc = 0.0f;
    if (st.playing) tp_reset_req = tp_reset_req + 1u;   /* la idea y el reloj empiezan juntos */
    tp_locked = st.playing;
    if (!st.playing) st.gate_hold_off = 1;   /* al parar, el acorde calla */
    st.redraw = 1;
}

static void seq_finish(void)
{
    int bar = MT->bar8, len = ((int)(st.rec_acc + (float)bar / 2.0f) / bar) * bar;   /* al compás más cercano */
    st.rec = 0;
    if (!REC_LOOP.n) { app_say("REC", "NADA"); return; }
    if (len < bar) len = bar;
    while (len <= REC_LOOP.ev[REC_LOOP.n - 1].step) len += bar;
    if (len > LOOP_STEPS) len = LOOP_STEPS / bar * bar;
    REC_LOOP.len = (uint8_t)len;
    set_dirty = 1;
    {
        char t[12];
        int_text(len / bar, t);
        text_cat(t, len == bar ? " COMPAS" : " COMPASES");
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
        st.rec_resets = tp_resets;           /* el reloj se pondrá a cero con este acorde (el audio lo hace) */
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

/* CUADRAR: con algo sonando, un acorde nuevo espera a la siguiente corchea, negra o compás del reloj, y
 * entra un pelín antes (20 ms) para que el paso del arpegio ya lo coja. Si se pulsa justo después de la
 * línea (hasta 60 ms), entra en el acto: ese paso se repite con él. Devuelve 1 mientras lo retiene. */
static const char *const QUANT_NAME[4] = {"AL MOMENTO", "CORCHEA", "NEGRA", "COMPAS"};
static int quant_hold(int *degree, unsigned *mods, int *inv)
{
    float q, b = tp_beats, bps = (float)set.tempo / 60.0f;
    uint32_t sig;
    if (!set.quant || !st.gate || *degree < 0
        || (*degree == st.degree && *mods == st.mods && *inv == st.inv && !st.repress)) {
        st.q_pend = 0;
        return 0;
    }
    q = set.quant == 1 ? 0.5f : set.quant == 2 ? 1.0f : (float)MT->bar8 * 0.5f;
    sig = (uint32_t)(*degree + 1) | *mods << 4 | (uint32_t)*inv << 12;
    if (!st.q_pend || sig != st.q_sig || tp_resets != st.q_resets) {
        float last = fm_floorf(b / q) * q;
        st.q_pend = 1;
        st.q_sig = sig;
        st.q_resets = tp_resets;
        st.q_due = b - last < 0.060f * bps ? b : last + q;
    }
    st.q_deg = (int8_t)*degree;
    st.q_mods = (uint8_t)*mods;
    st.q_inv = (uint8_t)*inv;
    if (b + 0.020f * bps >= st.q_due) { st.q_pend = 0; return 0; }
    *degree = st.degree;
    *mods = st.mods;
    *inv = st.inv;
    return 1;
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k;
    unsigned mods = 0;
    int inv = 0, degree = -1, live_degree, i, from_seq = 0, held = 0;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    seq_tick();

    /* grados: el último pulsado manda */
    for (i = 0; i < NDEG; i++) {
        uint32_t bit = 1u << WK[i];
        if ((keys & bit) && !(st.keys & bit)) { st.deg_order[i] = ++st.deg_stamp; st.repress = 1; }
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
    for (i = 0; i < 10; i++) if ((keys >> BK[i] & 1u) && !(st.keys >> BK[i] & 1u)) st.repress = 1;   /* modificadores e inversiones */
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
        from_seq = 1;
    } else if (st.gate_hold_off) {                /* parado: hasta el siguiente grado */
        if (degree >= 0) st.gate_hold_off = 0;
    } else if (degree < 0 && set.hold && st.gate) {   /* HOLD: el acorde sigue; los modificadores aún lo cambian */
        degree = st.q_pend ? st.q_deg : st.degree;   /* (o el que espera a su línea, aunque ya se soltara) */
        if (!mods) mods = st.q_pend ? st.q_mods : st.mods;
        if (st.q_pend && !inv) inv = st.q_inv;
    }
    if (!from_seq) held = quant_hold(&degree, &mods, &inv);
    else st.q_pend = 0;
    if (!held && st.repress) {               /* la pulsación entra: el pad vuelve a atacar, y el MIDI también */
        st.repress = 0;
        if (degree >= 0 && st.gate) { st_trig++; st.sig = ~0u; }
    }
    seq_record(degree, mods, inv, live_degree >= 0 && !held);
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
                kit_switch(set.cur_bank);    /* parado: su sonido, ya */
                app_say(t, REC_LOOP.len ? "" : sv.kit_used[set.cur_bank] ? "VACIO" : "VACIO  TODO DE SERIE");
            }
        } else if (k == B_SEL) {             /* enciende o apaga lo de esta página */
            sel_toggle();
        } else if (k == B_ARP) {             /* arpegiador, bajo pulsado */
            static const uint8_t P[2] = {PG_ARP, PG_BP};
            page_go(P, 2, &st.mem_arp);
        } else if (k == B_FX) {              /* delay, reverb */
            static const uint8_t P[3] = {PG_DLY, PG_DLY2, PG_REV};
            page_go(P, 3, &st.mem_fx);
        } else if (k == B_ENV) {             /* envolventes: pad, arpegio, bajo */
            static const uint8_t P[3] = {PG_ENV_PAD, PG_ENV_BASS, PG_ENV_ARP};
            page_go(P, 3, &st.mem_env);
        } else if (k == B_LFO) {             /* LFO: pad, arpegio, bajo */
            static const uint8_t P[3] = {PG_LFO_PAD, PG_LFO_ARP, PG_LFO_BASS};
            page_go(P, 3, &st.mem_lfo);
        } else if (k == B_EDIT) {            /* la caja de ritmos */
            static const uint8_t P[1] = {PG_DRUM};
            page_go(P, 1, &st.mem_drum);
        } else if (k == B_GLO) {             /* tonalidad, modo, octava, brillo */
            static const uint8_t P[2] = {PG_GLO, PG_BT};   /* global, y el sonido por Bluetooth */
            page_go(P, 2, &st.mem_glo);
        } else if (k == B_HOME) {
            st.page = PG_MAIN;               /* los knobs vuelven al acorde (lo que suena sigue sonando) */
            app_say("HOME", "");
        } else if (k == B_SAVE) {
            kit_store(set.cur_bank);         /* el sonido que suena, en su banco */
            if (plat_store_save(OBJ_SETTINGS, &sv, sizeof sv) == 0) { set_dirty = 0; app_say("GUARDADO", ""); }
            else app_say("GUARDAR", "FALLO");
        }
    }

    /* potenciómetros: K1..K4 son de la página en la que se está (y en cada una, K1 enciende o apaga) */
    for (k = 0; k < NE; k++) {
        char t[24];
        if ((e = plat_enc((int)k)) == 0) continue;
        if (st.page == PG_ARP && k == EN_ALGO) {   /* ARPEGIADOR: el ciclo, en ALGORITHM */
            set.arp_free = e > 0;
            set_changed("ARP CICLO", set.arp_free ? "LIBRE" : "COMPAS");
        } else if (st.page == PG_ARP && k >= EN_K1) {   /* ARPEGIADOR (SEL lo enciende) */
            if (k == EN_K1) {
                set.arp_oct = (uint8_t)clamp((int32_t)set.arp_oct + e, 1, 3);
                int_text(set.arp_oct, t);
                set_changed("ARP OCTAVAS", t);
            } else if (k == EN_K2) {
                set.arp_mode = (uint8_t)clamp((int32_t)set.arp_mode + e, 0, 3);
                set_changed("ARP MODO", ARP_MODE_NAME[set.arp_mode]);
            } else if (k == EN_K3) {
                set.arp_div = (uint8_t)clamp((int32_t)set.arp_div + e, 0, 2);
                set_changed("ARP PASO", ARP_DIV_NAME[set.arp_div]);
            } else {
                set.arp_len = (uint8_t)clamp((int32_t)set.arp_len + e, 1, 9);
                int_text(set.arp_len * 10, t); text_cat(t, "%");
                set_changed("ARP LARGO", t);
            }
        } else if (st.page == PG_DLY2 && k >= EN_K1) {   /* DELAY LIBRE (SEL lo enciende) */
            if (k == EN_K1) {                /* el tiempo en ms: pasos finos abajo y más gordos arriba */
                int32_t ms = set.d2_ms, d;
                for (d = 0; d < (e < 0 ? -e : e); d++) {
                    int32_t st_ = e > 0 ? (ms < 100 ? 5 : ms < 300 ? 10 : 25) : (ms <= 100 ? 5 : ms <= 300 ? 10 : 25);
                    ms = clamp(ms + (e > 0 ? st_ : -st_), 10, 750);
                }
                set.d2_ms = (uint16_t)ms;
                int_text(ms, t); text_cat(t, " MS");
                set_changed("DELAY LIBRE", t);
            } else if (k == EN_K2) {
                set.d2_fb = (uint8_t)clamp((int32_t)set.d2_fb + e, 0, 9);
                int_text(set.d2_fb * 10, t); text_cat(t, "%");
                set_changed("REPETICIONES", t);
            } else if (k == EN_K3) {
                set.d2_tone = (uint8_t)clamp((int32_t)set.d2_tone + e, 0, 9);
                int_text(set.d2_tone + 1, t);
                set_changed("DELAY LIBRE TONO", t);
            } else {
                fx_send_knob(e, t);
            }
        } else if (st.page == PG_DLY && k >= EN_K1) {   /* DELAY (SEL lo enciende) */
            if (k == EN_K1) {
                set.dly_div = (uint8_t)clamp((int32_t)set.dly_div + e, 0, 5);
                set_changed("DELAY TIEMPO", DLY_DIV_NAME[set.dly_div]);
            } else if (k == EN_K2) {
                set.dly_fb = (uint8_t)clamp((int32_t)set.dly_fb + e, 0, 9);
                int_text(set.dly_fb * 10, t); text_cat(t, "%");
                set_changed("REPETICIONES", t);
            } else if (k == EN_K3) {
                set.dly_tone = (uint8_t)clamp((int32_t)set.dly_tone + e, 0, 9);
                int_text(set.dly_tone + 1, t);
                set_changed("DELAY TONO", t);
            } else {                         /* la mezcla, siempre en K4 */
                fx_send_knob(e, t);
            }
        } else if (st.page == PG_REV && k >= EN_K1) {   /* REVERB (SEL la enciende) */
            if (k == EN_K1) {
                set.rev_size = (uint8_t)clamp((int32_t)set.rev_size + e, 0, 9);
                int_text(set.rev_size + 1, t);
                set_changed("REVERB TAMANO", t);
            } else if (k == EN_K2) {
                set.rev_tone = (uint8_t)clamp((int32_t)set.rev_tone + e, 0, 9);
                int_text(set.rev_tone + 1, t);
                set_changed("REVERB TONO", t);
            } else if (k == EN_K3) {
                set.rev_width = (uint8_t)clamp((int32_t)set.rev_width + e, 0, 10);
                int_text(set.rev_width * 10, t); text_cat(t, "%");
                set_changed("REVERB ANCHO", t);
            } else {                         /* la mezcla, siempre en K4 */
                fx_send_knob(e, t);
            }
        } else if (st.page >= PG_ENV_PAD && st.page <= PG_ENV_BASS && k == EN_ALGO) {   /* ENV: de quién (pad, arpegio, bajo) */
            static const uint8_t ORD[3] = {PG_ENV_PAD, PG_ENV_BASS, PG_ENV_ARP};   /* pad, bajo, arpegio: como en HOME */
            int q = st.page == PG_ENV_PAD ? 0 : st.page == PG_ENV_BASS ? 1 : 2;
            st.page = ORD[clamp(q + e, 0, 2)];
            st.mem_env = st.page;
            app_say("ENVOLVENTE", st.page == PG_ENV_PAD ? "PAD" : st.page == PG_ENV_ARP ? "ARPEGIO" : "BAJO");
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
        } else if (st.page >= PG_LFO_PAD && st.page <= PG_LFO_BASS && (k >= EN_K1 || k == EN_ALGO)) {
            /* LFO (SEL lo enciende): ALGORITHM, a dónde va; K1 velocidad, K2 cantidad, K3 forma, K4 a tempo */
            lfo_t *L = &set.lfo[st.page - PG_LFO_PAD];
            if (k == EN_ALGO) {
                L->dest = (uint8_t)clamp((int32_t)L->dest + e, 0, NDEST - 1);
                set_changed("LFO DESTINO", LFO_DEST_NAME[L->dest]);
            } else if (k == EN_K1) {
                L->rate = (uint8_t)clamp((int32_t)L->rate + e, 0, 15);
                if (L->sync) { L->sync = 0; }   /* tocar la velocidad lo pasa a libre */
                hz_text(lfo_hz(L->rate), t);
                set_changed("LFO VELOCIDAD", t);
            } else if (k == EN_K2) {
                L->depth = (uint8_t)clamp((int32_t)L->depth + e, 0, 10);
                int_text(L->depth * 10, t); text_cat(t, "%");
                set_changed("LFO CANTIDAD", t);
            } else if (k == EN_K3) {
                L->shape = (uint8_t)clamp((int32_t)L->shape + e, 0, 3);
                set_changed("LFO FORMA", LFO_SHAPE_NAME[L->shape]);
            } else {
                L->sync = (uint8_t)clamp((int32_t)L->sync + e, 0, NSYNC - 1);
                set_changed("LFO A TEMPO", LFO_SYNC_NAME[L->sync]);
            }
        } else if (st.page == PG_BP && k >= EN_K1) {   /* BAJO PULSADO (SEL lo enciende) */
            if (k == EN_K1) {
                set.bp_div = (uint8_t)clamp((int32_t)set.bp_div + e, 0, 3);
                set_changed("BAJO PASO", BP_DIV_NAME[set.bp_div]);
            } else if (k == EN_K2) {
                set.bp_pat = (uint8_t)clamp((int32_t)set.bp_pat + e, 0, 3);
                set_changed("BAJO PATRON", BP_PAT_NAME[set.bp_pat]);
            } else if (k == EN_K3) {
                set.bp_len = (uint8_t)clamp((int32_t)set.bp_len + e, 1, 9);
                int_text(set.bp_len * 10, t); text_cat(t, "%");
                set_changed("BAJO LARGO", t);
            } else {
                set.bp_oct = (int8_t)clamp(set.bp_oct + e, -1, 1);
                int_text(set.bp_oct, t);
                set_changed("BAJO OCTAVA", t);
            }
        } else if (k == EN_SELECT) {
            set.tempo = (uint8_t)clamp((int32_t)set.tempo + e, 40, 220);
            int_text(set.tempo, t);
            set_changed("TEMPO", t);
        } else if (st.page == PG_DRUM && k == EN_K4) {   /* RITMO: la apertura del charles */
            set.drm_hopen = (uint8_t)clamp((int32_t)set.drm_hopen + e, 0, 10);
            int_text(set.drm_hopen * 10, t); text_cat(t, "%");
            set_changed("APERTURA", t);
        } else if (st.page == PG_DRUM && k >= EN_K1) {   /* RITMO: el volumen de bombo, caja y charles */
            static const char *const NAME[3] = {"BOMBO", "CAJA", "CHARLES"};
            uint8_t *vol = k == EN_K1 ? &set.drm_kick : k == EN_K2 ? &set.drm_snare : &set.drm_hat;
            *vol = (uint8_t)clamp((int32_t)*vol + e, 0, VOL_MAX);
            int_text(*vol * 100 / VOL_MAX, t); text_cat(t, "%");
            set_changed(NAME[k - EN_K1], t);
        } else if (st.page == PG_BT && k == EN_ALGO) {   /* BLUETOOTH: qué (BUSCAR o un aparato) */
            char nm[17];
            st.bt_pick = (int8_t)clamp(st.bt_pick + e, 0, bt_items() - 1);
            bt_item_name(st.bt_pick, nm);
            app_say("BLUETOOTH", nm);
        } else if (st.page == PG_BT && k == EN_K4) {   /* la salida: el aparato o el FM-1 */
            if (bt_streaming()) {
                plat_bta_output(e > 0 ? 0 : 1);
                app_say("SALIDA", plat_bta_out_device() ? "APARATO" : "FM-1");
            } else app_say("SALIDA", "NADA CONECTADO");
        } else if (st.page == PG_GLO && k == EN_ALGO) {   /* GLOBAL: encadenar acordes, en ALGORITHM */
            set.lead = e > 0;
            set_changed("ENCADENAR", set.lead ? "SI" : "NO");
        } else if (st.page == PG_GLO && k >= EN_K1) {   /* GLOBAL: tonalidad, modo, octava, brillo */
            if (k == EN_K1) {
                set.key = (uint8_t)(((int)set.key + e % 12 + 12) % 12);
                key_text(t);
                set_changed("TONO", t);
            } else if (k == EN_K2) {
                set.mode = (uint8_t)clamp((int32_t)set.mode + e, 0, BL_MODE_COUNT - 1);
                key_text(t);
                set_changed("MODO", t);
            } else if (k == EN_K3) {         /* el sonido del pad: carga su envolvente */
                set.sound = (uint8_t)clamp((int32_t)set.sound + e, 0, NSND - 1);
                set.env_a = SOUND[set.sound].env[0];
                set.env_d = SOUND[set.sound].env[1];
                set.env_s = SOUND[set.sound].env[2];
                set.env_r = SOUND[set.sound].env[3];
                set_changed("SONIDO", SOUND[set.sound].name);
            } else {
                set.bright = (uint8_t)clamp((int32_t)set.bright + e, 0, 20);
                cut_update();
                int_text(set.bright * 5, t); text_cat(t, "%");
                set_changed("BRILLO", t);
            }
        } else if (page_fx(st.page) >= 0 && k == EN_ALGO) {   /* FX: qué capa lleva K4 (pad, bajo, arpegio, rasgueo) */
            st.fx_layer = (uint8_t)clamp((int32_t)st.fx_layer + e, 0, 3);
            int_text(set.fx_send[page_fx(st.page)][st.fx_layer] * 10, t);
            text_cat(t, "%");
            {
                char h[20] = "MEZCLA ";
                text_cat(h, LAYER_NAME[st.fx_layer]);
                app_say(h, t);
            }
        } else if (st.page == PG_MAIN && k == EN_ALGO) {   /* HOME: cuadrar los cambios de acorde */
            set.quant = (uint8_t)clamp((int32_t)set.quant + e, 0, 3);
            set_changed("CUADRAR", QUANT_NAME[set.quant]);
        } else if (k >= EN_K1) {             /* HOME: el volumen de cada capa */
            static const char *const NAME[4] = {"VOLUMEN PAD", "VOLUMEN BAJO", "VOLUMEN ARP", "VOLUMEN RASGUEO"};
            uint8_t *vol = k == EN_K1 ? &set.vol_pad : k == EN_K2 ? &set.vol_bass : k == EN_K3 ? &set.vol_arp : &set.vol_strum;
            *vol = (uint8_t)clamp((int32_t)*vol + e, 0, VOL_MAX);
            int_text(*vol * 100 / VOL_MAX, t); text_cat(t, "%");
            set_changed(NAME[k - EN_K1], t);
        } else if (k == EN_PRESET) {         /* PRESET: el tipo de compás */
            set.meter = (uint8_t)clamp((int32_t)set.meter + e, 0, NMETER - 1);
            set_changed("COMPAS", MT->name);
        }
    }

    if (keys != st.keys || btn != st.btn) st.redraw = 1;
    st.keys = keys;
    st.btn = btn;
    plat_glow(1);
    {
        /* cada botón, encendido mientras algo de su sección suena, o mientras se está en una de sus páginas */
        int arp_on = ARP_ON || set.bp_on, fx_on = FX_ON(FX_DLY) || FX_ON(FX_REV) || FX_ON(FX_DLY2);
        int lfo_on = set.lfo[0].on || set.lfo[1].on || set.lfo[2].on;
        uint32_t lit = btn | (set_dirty ? 1u << B_SAVE : 0u)
                       | (arp_on || st.page == PG_ARP || st.page == PG_BP ? 1u << B_ARP : 0u)
                       | (fx_on || st.page == PG_DLY || st.page == PG_DLY2 || st.page == PG_REV ? 1u << B_FX : 0u)
                       | (st.page >= PG_ENV_PAD && st.page <= PG_ENV_BASS ? 1u << B_ENV : 0u)
                       | (lfo_on || (st.page >= PG_LFO_PAD && st.page <= PG_LFO_BASS) ? 1u << B_LFO : 0u)
                       | (st.page == PG_MAIN ? 1u << B_HOME : 0u) | (st.page == PG_GLO || st.page == PG_BT || bt_streaming() ? 1u << B_GLO : 0u)
                       | (DRUM_ON || st.page == PG_DRUM ? 1u << B_EDIT : 0u)
                       | (page_active(st.page) ? 1u << B_SEL : 0u);   /* SEL: lo de esta página suena */
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

/* Cada botón, su color: se sabe en qué página se está de un vistazo (la etiqueta con el nombre, la raya,
 * los nombres de los knobs y lo resaltado). HOME, el ámbar de siempre. */
#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
static void section_color(int pg, uint16_t *hi, uint16_t *lo)
{
    switch (pg) {
    case PG_ARP: case PG_BP:                                   /* ARP: azul */
        *hi = RGB565(110, 200, 255); *lo = RGB565(50, 140, 210); break;
    case PG_DLY: case PG_DLY2: case PG_REV:                    /* FX: violeta */
        *hi = RGB565(200, 150, 255); *lo = RGB565(150, 100, 220); break;
    case PG_ENV_PAD: case PG_ENV_ARP: case PG_ENV_BASS:        /* ENV: verde */
        *hi = RGB565(120, 235, 140); *lo = RGB565(60, 180, 95); break;
    case PG_LFO_PAD: case PG_LFO_ARP: case PG_LFO_BASS:        /* LFO: rosa */
        *hi = RGB565(255, 130, 200); *lo = RGB565(220, 80, 160); break;
    case PG_DRUM:                                              /* EDIT (ritmo): rojo */
        *hi = RGB565(255, 120, 100); *lo = RGB565(220, 70, 55); break;
    case PG_GLO: case PG_BT:                                   /* GLO: blanco */
        *hi = RGB565(235, 235, 240); *lo = RGB565(170, 170, 178); break;
    default:                                                   /* HOME: ámbar */
        *hi = C_HI; *lo = C_AMB; break;
    }
}
static int text_len(const char *t) { int n = 0; while (t[n]) n++; return n; }
/* la etiqueta de la página: su nombre en negro sobre su color; devuelve dónde acaba */
static int32_t page_tag(const char *name, uint16_t col)
{
    int32_t w = text_w(&FONT_XS, name);
    cv_rect(0, 46, w + 10, 17, col);
    cv_text(5, 50, &FONT_XS, name, C_BLACK);
    return w + 10;
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
        uint16_t sc, sd;
        section_color(st.page, &sc, &sd);
        cv_text(x + 8, 7, &FONT_B, st.msg[1], sc);
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
        {                                    /* el arpegiador, el bajo pulsado, el tempo y el compás, a la derecha */
            char b[28] = "", n[8];
            if (ARP_ON) text_cat(b, "ARP ");
            if (set.bp_on) text_cat(b, "BAJO ");
            int_text(set.tempo, n);
            text_cat(b, n);
            text_cat(b, " BPM  ");
            text_cat(b, MT->name);
            cv_text(234 - text_w(&FONT_XS, b), 44, &FONT_XS, b, C_HI);
        }
        if (st.rec || st.playing) {          /* REC / PLAY y compás.pulso */
            char b[16] = "";
            uint32_t p = (uint32_t)(st.playing ? st.play_acc : st.rec_acc);
            if (st.rec == 1) text_cat(b, "REC ...");
            else {
                char n[8];
                text_cat(b, st.rec == 2 ? "REC " : "PLAY ");
                int_text((int32_t)(p / MT->bar8 + 1u), n); text_cat(b, n);
                text_cat(b, ".");
                int_text((int32_t)(p % MT->bar8 / MT->beat8 + 1u), n); text_cat(b, n);
                if (st.playing) { text_cat(b, "/"); int_text(PLAY_LOOP.len / MT->bar8, n); text_cat(b, n); }
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
    {
    uint16_t SC, SD;
    section_color(st.page, &SC, &SD);
    cv_rect(0, 64, 240, 1, SC);              /* la raya, del color de la sección */
    if (st.page != PG_MAIN) {            /* una página: su nombre, si suena (SEL) y sus cuatro knobs */
        const char *lbl[4] = {"", "", "", ""};
        char v[4][12], head[40] = "";
        int pg = st.page;
        for (i = 0; i < 4; i++) v[i][0] = 0;
        text_cat(head, PAGE_NAME[pg]);
        if (pg == PG_ARP || pg == PG_BP || pg == PG_DLY || pg == PG_REV || (pg >= PG_LFO_PAD && pg <= PG_LFO_BASS)
            || pg == PG_DRUM || pg == PG_DLY2) {
            text_cat(head, page_active(pg) ? "  SI" : "  NO");
        }
        if (pg == PG_ARP) {
            text_cat(head, set.arp_free ? "  CICLO LIBRE" : "  CICLO COMPAS");
            lbl[0] = "OCTAVAS"; lbl[1] = "MODO"; lbl[2] = "PASO"; lbl[3] = "LARGO";
            int_text(set.arp_oct, v[0]);
            text_cat(v[1], ARP_MODE_NAME[set.arp_mode]);
            text_cat(v[2], ARP_DIV_NAME[set.arp_div]);
            int_text(set.arp_len * 10, v[3]); text_cat(v[3], "%");
        } else if (pg == PG_DRUM) {
            lbl[0] = "BOMBO"; lbl[1] = "CAJA"; lbl[2] = "CHARLES"; lbl[3] = "APERTURA";
            int_text(set.drm_kick * 100 / VOL_MAX, v[0]); text_cat(v[0], "%");
            int_text(set.drm_snare * 100 / VOL_MAX, v[1]); text_cat(v[1], "%");
            int_text(set.drm_hat * 100 / VOL_MAX, v[2]); text_cat(v[2], "%");
            int_text(set.drm_hopen * 10, v[3]); text_cat(v[3], "%");
        } else if (pg == PG_BT) {
            int32_t lat = plat_bta_latency_us();
            char nm[17];
            text_cat(head, "  ");
            text_cat(head, bt_word(plat_bta_state()));
            lbl[0] = "ELEGIDO"; lbl[1] = ""; lbl[2] = "RETRASO"; lbl[3] = "SALIDA";
            bt_item_name(st.bt_pick, nm);
            text_cat(v[0], nm);
            if (lat >= 0) { int_text(lat / 1000 + 150, v[2]); text_cat(v[2], "+ms"); } else text_cat(v[2], "-");
            text_cat(v[3], bt_streaming() ? (plat_bta_out_device() ? "APARATO" : "FM-1") : "FM-1");
        } else if (pg == PG_GLO) {
            text_cat(head, set.lead ? "  ENCADENAR SI" : "  ENCADENAR NO");
            lbl[0] = "TONO"; lbl[1] = "MODO"; lbl[2] = "SONIDO"; lbl[3] = "BRILLO";
            text_cat(v[0], bl_note_name(set.key, set.key));
            text_cat(v[1], MODE_NAME[set.mode]);
            text_cat(v[2], SOUND[set.sound % NSND].name);
            int_text(set.bright * 5, v[3]); text_cat(v[3], "%");
        } else if (pg == PG_BP) {
            lbl[0] = "PASO"; lbl[1] = "PATRON"; lbl[2] = "LARGO"; lbl[3] = "OCTAVA";
            text_cat(v[0], BP_DIV_NAME[set.bp_div]);
            text_cat(v[1], BP_PAT_NAME[set.bp_pat]);
            int_text(set.bp_len * 10, v[2]); text_cat(v[2], "%");
            int_text(set.bp_oct, v[3]);
        } else if (pg == PG_DLY) {
            lbl[0] = "TIEMPO"; lbl[1] = "REPETIR"; lbl[2] = "TONO"; lbl[3] = "MEZCLA";
            text_cat(v[0], DLY_DIV_NAME[set.dly_div]);
            int_text(set.dly_fb * 10, v[1]); text_cat(v[1], "%");
            int_text(set.dly_tone + 1, v[2]);
            int_text(set.fx_send[page_fx(pg)][st.fx_layer & 3u] * 10, v[3]); text_cat(v[3], "%");
        } else if (pg == PG_DLY2) {
            lbl[0] = "TIEMPO"; lbl[1] = "REPETIR"; lbl[2] = "TONO"; lbl[3] = "MEZCLA";
            int_text(set.d2_ms, v[0]); text_cat(v[0], "ms");
            int_text(set.d2_fb * 10, v[1]); text_cat(v[1], "%");
            int_text(set.d2_tone + 1, v[2]);
            int_text(set.fx_send[page_fx(pg)][st.fx_layer & 3u] * 10, v[3]); text_cat(v[3], "%");
        } else if (pg == PG_REV) {
            lbl[0] = "SALA"; lbl[1] = "TONO"; lbl[2] = "ANCHO"; lbl[3] = "MEZCLA";
            int_text(set.rev_size + 1, v[0]);
            int_text(set.rev_tone + 1, v[1]);
            int_text(set.rev_width * 10, v[2]); text_cat(v[2], "%");
            int_text(set.fx_send[page_fx(pg)][st.fx_layer & 3u] * 10, v[3]); text_cat(v[3], "%");
        } else if (pg >= PG_ENV_PAD && pg <= PG_ENV_BASS) {
            int ea = pg == PG_ENV_PAD ? set.env_a : pg == PG_ENV_ARP ? set.aenv_a : set.benv_a;
            int ed = pg == PG_ENV_PAD ? set.env_d : pg == PG_ENV_ARP ? set.aenv_d : set.benv_d;
            int es = pg == PG_ENV_PAD ? set.env_s : pg == PG_ENV_ARP ? set.aenv_s : set.benv_s;
            int er = pg == PG_ENV_PAD ? set.env_r : pg == PG_ENV_ARP ? set.aenv_r : set.benv_r;
            lbl[0] = "ATAQUE"; lbl[1] = "CAIDA"; lbl[2] = "SOSTENIDO"; lbl[3] = "RELEASE";
            time_text(env_att_s(ea), v[0]);
            time_text(env_dec_s(ed), v[1]);
            int_text(es * 10, v[2]); text_cat(v[2], "%");
            time_text(env_rel_s(er), v[3]);
        } else {                             /* LFO */
            int part = pg - PG_LFO_PAD;
            const lfo_t *L = &set.lfo[part];
            lbl[0] = "VELOCIDAD"; lbl[1] = "CANTIDAD"; lbl[2] = "FORMA"; lbl[3] = "A TEMPO";
            (void)part;
            text_cat(head, "  > ");          /* a dónde va (ALGORITHM) */
            text_cat(head, LFO_DEST_NAME[L->dest % NDEST]);
            if (L->sync) text_cat(v[0], "-");
            else hz_text(lfo_hz(L->rate), v[0]);
            int_text(L->depth * 10, v[1]); text_cat(v[1], "%");
            text_cat(v[2], LFO_SHAPE_NAME[L->shape]);
            text_cat(v[3], LFO_SYNC_NAME[L->sync % NSYNC]);
        }
        {
            int32_t x = page_tag(PAGE_NAME[pg], SC);   /* el nombre, en su etiqueta de color */
            x = cv_text(x + 6, 50, &FONT_XS, head + text_len(PAGE_NAME[pg]), page_active(pg) ? SC : C_GRAY);
            if (pg >= PG_ENV_PAD && pg <= PG_ENV_BASS) {   /* de quién es (ALGORITHM), resaltado */
                static const char *const WHO[3] = {"PAD", "BAJO", "ARP"};
                static const uint8_t WP[3] = {PG_ENV_PAD, PG_ENV_BASS, PG_ENV_ARP};
                int q;
                x += 8;
                for (q = 0; q < 3; q++) x = cv_text(x, 50, &FONT_XS, WHO[q], WP[q] == pg ? SC : C_GRAY) + 6;
            }
            if (page_fx(pg) >= 0) {          /* los envíos de las cuatro capas; la de K4, resaltada */
                static const char *const AB[4] = {"PAD ", "BAJO ", "ARP ", "RAS "};
                int f = page_fx(pg), q;
                lbl[3] = st.fx_layer == 0 ? "MEZ PAD" : st.fx_layer == 1 ? "MEZ BAJO" : st.fx_layer == 2 ? "MEZ ARP" : "MEZ RASG";
                x += 8;
                for (q = 0; q < 4; q++) {
                    char tt[12] = "", nn[4];
                    text_cat(tt, AB[q]);
                    int_text(set.fx_send[f][q], nn);
                    text_cat(tt, nn);
                    x = cv_text(x, 50, &FONT_XS, tt, q == st.fx_layer ? SC : C_GRAY) + 5;
                }
            }
        }
        for (i = 0; i < 4; i++) {
            cv_text(4 + i * 60, 68, &FONT_XS, lbl[i], SD);
            cv_text(4 + i * 60, 81, &FONT_XS, v[i], C_WHITE);
        }
    } else {
        static const char *const LBL[4] = {"PAD", "BAJO", "ARPEGIO", "RASGUEO"};   /* los volúmenes */
        char v[4][12];
        int_text(set.vol_pad * 100 / VOL_MAX, v[0]); text_cat(v[0], "%");
        int_text(set.vol_bass * 100 / VOL_MAX, v[1]); text_cat(v[1], "%");
        int_text(set.vol_arp * 100 / VOL_MAX, v[2]); text_cat(v[2], "%");
        int_text(set.vol_strum * 100 / VOL_MAX, v[3]); text_cat(v[3], "%");
        {
            char q[24] = "CUADRAR ";
            int32_t x = page_tag("VOLUMEN", SC);
            text_cat(q, QUANT_NAME[set.quant]);
            cv_text(x + 10, 50, &FONT_XS, q, set.quant ? SC : C_GRAY);
        }
        for (i = 0; i < 4; i++) {
            cv_text(4 + i * 60, 68, &FONT_XS, LBL[i], SD);
            cv_text(4 + i * 60, 81, &FONT_XS, v[i], C_WHITE);
        }
    }
    }
    cv_blit(0, 144);
}

void app_ui_init(void)
{
    plat_bta_on(1);                      /* Bluetooth: listo para buscar y conectar cascos o altavoces */
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    st.redraw = 1;
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    {   /* Bluetooth: al arrancar, el último aparato; y se dice cuando cambia algo */
        int s = plat_bta_state();
        char nm[17];
        if (!st.bt_tried && now > 3000u && s != PLAT_BTA_OFF) {
            st.bt_tried = 1;
            if (plat_bta_known(0, nm)) (void)plat_bta_reconnect(0);
        }
        if (s != st.bt_last) {
            if (s == PLAT_BTA_STREAMING || s == PLAT_BTA_REFUSED || s == PLAT_BTA_FAILED
                || (s == PLAT_BTA_FOUND && st.page == PG_BT) || (s == PLAT_BTA_NONE && st.page == PG_BT))
                app_say("BLUETOOTH", bt_word(s));
            if (s == PLAT_BTA_FOUND && st.page == PG_BT) st.bt_pick = 1;   /* el primero encontrado, listo para SEL */
            st.bt_last = (uint8_t)s;
            st.redraw = 1;
        }
        if (plat_bta_lost() != PLAT_BTA_LOST_NONE) app_say("BLUETOOTH", "SE CORTO");
    }
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
    else if (!strcmp(what, "arp")) *got = ARP_ON;
    else if (!strcmp(what, "vol_arp")) *got = set.vol_arp;
    else if (!strcmp(what, "arp_free")) *got = set.arp_free;
    else if (!strcmp(what, "meter")) *got = set.meter;
    else if (!strcmp(what, "drm_kick")) *got = set.drm_kick;
    else if (!strcmp(what, "drum_hits")) *got = drum_hits >= 4u;
    else if (!strcmp(what, "d2_ms")) *got = set.d2_ms;
    else if (!strcmp(what, "quant")) *got = set.quant;
    else if (!strcmp(what, "bta")) *got = plat_bta_state();
    else if (!strcmp(what, "bt_pick")) *got = st.bt_pick;
    else if (!strcmp(what, "sound")) *got = set.sound;
    else if (!strcmp(what, "q_pend")) *got = st.q_pend;
    else if (!strcmp(what, "d2_on")) *got = FX_ON(FX_DLY2);
    else if (!strcmp(what, "page")) *got = st.page;
    else if (!strcmp(what, "rec")) *got = st.rec;
    else if (!strcmp(what, "rev")) *got = set.fx_send[FX_REV][0];
    else if (!strcmp(what, "dly")) *got = set.fx_send[FX_DLY][0];
    else if (!strcmp(what, "dly_bass")) *got = set.fx_send[FX_DLY][1];
    else if (!strcmp(what, "fx_layer")) *got = st.fx_layer;
    else if (!strcmp(what, "fx_bass")) *got = page_fx(st.page) >= 0 ? set.fx_send[page_fx(st.page)][1] : -1;
    else if (!strcmp(what, "dly_on")) *got = FX_ON(FX_DLY);
    else if (!strcmp(what, "rev_on")) *got = FX_ON(FX_REV);
    else if (!strcmp(what, "rev_size")) *got = set.rev_size;
    else if (!strcmp(what, "env_s")) *got = set.env_s;
    else if (!strcmp(what, "bp_on")) *got = set.bp_on;
    else if (!strcmp(what, "bp_pat")) *got = set.bp_pat;
    else if (!strcmp(what, "lfo_dest")) *got = set.lfo[0].on ? set.lfo[0].dest : -1;
    else if (!strcmp(what, "lfo_arp")) *got = set.lfo[1].on ? set.lfo[1].dest : -1;
    else if (!strcmp(what, "led_arp")) *got = (int)(st.leds >> B_ARP & 1u);
    else if (!strcmp(what, "led_fx")) *got = (int)(st.leds >> B_FX & 1u);
    else if (!strcmp(what, "led_glo")) *got = (int)(st.leds >> B_GLO & 1u);
    else if (!strcmp(what, "bt_out")) *got = plat_bta_out_device();
    else if (!strcmp(what, "led_lfo")) *got = (int)(st.leds >> B_LFO & 1u);
    else if (!strcmp(what, "lfo_bass")) *got = set.lfo[2].on ? set.lfo[2].dest : -1;
    else if (!strcmp(what, "vol_pad")) *got = set.vol_pad;
    else if (!strcmp(what, "vol_bass")) *got = set.vol_bass;
    else if (!strcmp(what, "led_sel")) *got = (int)(st.leds >> B_SEL & 1u);
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
