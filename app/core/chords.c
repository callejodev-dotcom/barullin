/* Barullín: motor de acordes. Ver chords.h. */
#include "chords.h"

/* Escala mayor; los modos son rotaciones de ella. */
static const int8_t MAJOR[7] = { 0, 2, 4, 5, 7, 9, 11 };

/* Semitonos del grado d (puede pasar de 6: se sube de octava) en el modo dado. */
static int scale_step(int mode, int d)
{
    int oct = d / 7, i = d % 7;
    int s = MAJOR[(i + mode) % 7] - MAJOR[mode];
    if (s < 0) s += 12;
    return s + 12 * oct;
}

/* ---------- nombres ---------- */

static const char *const SHARPS[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
static const char *const FLATS[12]  = { "C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B" };

const char *bl_note_name(int pc, int key)
{
    /* Tonalidades que se escriben con bemoles: F Bb Eb Ab Db Gb. */
    static const uint8_t flat_key[12] = { 0,1,0,1,0,1,1,0,1,0,1,0 };
    pc = ((pc % 12) + 12) % 12;
    key = ((key % 12) + 12) % 12;
    return flat_key[key] ? FLATS[pc] : SHARPS[pc];
}

/* Concatenación sin stdio (el firmware no necesita printf). */
static int put(char *dst, int pos, int len, const char *s)
{
    while (*s && pos < len - 1) dst[pos++] = *s++;
    dst[pos] = 0;
    return pos;
}

/*
 * Nombre a partir de los intervalos:
 *   third: 2 sus2, 3 menor, 4 mayor, 5 sus4
 *   fifth: 6, 7, 8
 *   seventh: 0 (no hay), 10, 11
 *   ninth: 0, 13, 14
 *   sixth: 0, 8, 9
 */
static void chord_name(char *out, const char *root, int third, int fifth,
                       int seventh, int ninth, int sixth)
{
    int p = put(out, 0, BL_NAME_LEN, root);
    int sus = (third == 2 || third == 5);
    const char *q = "";            /* calidad de la tríada */
    const char *alt = "";          /* quinta alterada que no cabe en la calidad */

    if (sus) {
        if (fifth == 6) alt = "b5"; else if (fifth == 8) alt = "#5";
    } else if (third == 3) {
        if (fifth == 6 && seventh != 10) q = "dim";
        else if (fifth == 8) { q = "m"; alt = "#5"; }
        else q = "m";
    } else {
        if (fifth == 8) q = "aug";
        else if (fifth == 6) alt = "b5";
    }

    /* m7b5 (semidisminuido) */
    if (third == 3 && fifth == 6 && seventh == 10) {
        p = put(out, p, BL_NAME_LEN, ninth == 14 ? "m9b5" : "m7b5");
        if (ninth == 13) p = put(out, p, BL_NAME_LEN, "(b9)");
        return;
    }

    p = put(out, p, BL_NAME_LEN, q);

    if (seventh) {
        int nine = (ninth == 14);
        if (seventh == 11) {
            if (third == 3) p = put(out, p, BL_NAME_LEN, nine ? "(maj9)" : "(maj7)");
            else            p = put(out, p, BL_NAME_LEN, nine ? "maj9" : "maj7");
        } else {
            p = put(out, p, BL_NAME_LEN, nine ? "9" : "7");
        }
        if (ninth == 13) p = put(out, p, BL_NAME_LEN, "(b9)");
        if (sixth) p = put(out, p, BL_NAME_LEN, sixth == 9 ? "(13)" : "(b13)");
    } else {
        if (sixth == 9) {
            p = put(out, p, BL_NAME_LEN, ninth == 14 ? "6/9" : "6");
            if (ninth == 13) p = put(out, p, BL_NAME_LEN, "(b9)");
        } else {
            if (sixth == 8) p = put(out, p, BL_NAME_LEN, "(b6)");
            if (ninth == 14) p = put(out, p, BL_NAME_LEN, "add9");
            else if (ninth == 13) p = put(out, p, BL_NAME_LEN, "(b9)");
        }
    }
    if (sus) p = put(out, p, BL_NAME_LEN, third == 2 ? "sus2" : "sus4");
    put(out, p, BL_NAME_LEN, alt);
}

/* ---------- construcción ---------- */

static void sort_notes(uint8_t *a, int n)
{
    for (int i = 1; i < n; i++) {
        uint8_t v = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
        a[j + 1] = v;
    }
}

static int clamp_midi(int v) { return v < 0 ? 0 : v > 127 ? 127 : v; }

void bl_chord_build(int key, int mode, int degree, unsigned mods,
                    int inversion, int base, bl_chord *out)
{
    key = ((key % 12) + 12) % 12;
    mode = ((mode % BL_MODE_COUNT) + BL_MODE_COUNT) % BL_MODE_COUNT;
    degree = ((degree % 7) + 7) % 7;

    int r = scale_step(mode, degree);
    int third   = scale_step(mode, degree + 2) - r;
    int fifth   = scale_step(mode, degree + 4) - r;
    int seventh = 0, ninth = 0, sixth = 0;

    if (mods & BL_MOD_FLIP) third = (third == 3) ? 4 : 3;
    if (mods & BL_MOD_SUS4) third = 5;
    else if (mods & BL_MOD_SUS2) third = 2;
    if (mods & BL_MOD_DIMAUG) {
        if (fifth == 7) fifth = (third == 3) ? 6 : 8;
        else fifth = 7;
    }
    if (mods & BL_MOD_MAJ7) seventh = 11;
    else if (mods & BL_MOD_7) seventh = scale_step(mode, degree + 6) - r;
    if (mods & BL_MOD_9) ninth = scale_step(mode, degree + 8) - r;
    if (mods & BL_MOD_6) sixth = scale_step(mode, degree + 5) - r;

    int root_pc = (key + r) % 12;
    out->root = (uint8_t)root_pc;
    chord_name(out->name, bl_note_name(root_pc, key), third, fifth, seventh, ninth, sixth);

    /* Intervalos en orden de altura. La novena se pliega a la octava (2ª añadida)
       para que el acorde quede compacto; el nombre sigue diciendo 9. */
    int iv[BL_MAX_NOTES], n = 0;
    iv[n++] = 0;
    iv[n++] = third;
    iv[n++] = fifth;
    if (sixth)   iv[n++] = sixth;
    if (seventh) iv[n++] = seventh;
    if (ninth)   iv[n++] = ninth;

    /* Fundamental: la octava de root_pc más cercana a base. */
    int root_note = base - ((base - root_pc) % 12 + 12) % 12;
    if (base - root_note > 6) root_note += 12;

    uint8_t tmp[BL_MAX_NOTES];
    for (int i = 0; i < n; i++) tmp[i] = (uint8_t)clamp_midi(root_note + iv[i]);
    sort_notes(tmp, n);

    /* Inversión: sube las k notas más graves una octava. */
    int k = ((inversion % n) + n) % n;
    for (int i = 0; i < k; i++) tmp[i] = (uint8_t)clamp_midi(tmp[i] + 12);
    sort_notes(tmp, n);

    out->n = (uint8_t)n;
    for (int i = 0; i < n; i++) out->notes[i] = tmp[i];
    out->bass = (uint8_t)clamp_midi(root_note - 24);
}

/* ---------- voice leading ---------- */

static int iabs(int v) { return v < 0 ? -v : v; }

/* Coste: cada nota nueva va a la nota anterior más cercana, y al revés
   (así funciona aunque cambie el número de notas). */
static int lead_cost(const uint8_t *a, int na, const uint8_t *b, int nb)
{
    int cost = 0;
    for (int i = 0; i < na; i++) {
        int best = 1000;
        for (int j = 0; j < nb; j++) { int d = iabs(a[i] - b[j]); if (d < best) best = d; }
        cost += best;
    }
    for (int j = 0; j < nb; j++) {
        int best = 1000;
        for (int i = 0; i < na; i++) { int d = iabs(a[i] - b[j]); if (d < best) best = d; }
        cost += best;
    }
    return cost;
}

void bl_chord_lead(const bl_chord *prev, bl_chord *c, int lo, int hi)
{
    if (!prev || prev->n == 0 || c->n == 0) return;
    int n = c->n;

    /* Las clases de altura del acorde, en el orden de su estado fundamental. */
    uint8_t base[BL_MAX_NOTES];
    for (int i = 0; i < n; i++) base[i] = c->notes[i];

    uint8_t best[BL_MAX_NOTES];
    int best_cost = -1;

    /* Candidatos: cada inversión, desplazada varias octavas. */
    for (int inv = 0; inv < n; inv++) {
        for (int shift = -24; shift <= 24; shift += 12) {
            uint8_t cand[BL_MAX_NOTES];
            int ok = 1;
            for (int i = 0; i < n; i++) {
                int v = base[i] + shift + (i < inv ? 12 : 0);
                if (v < lo || v > hi) { ok = 0; break; }
                cand[i] = (uint8_t)v;
            }
            if (!ok) continue;
            sort_notes(cand, n);
            int cost = lead_cost(cand, n, prev->notes, prev->n);
            /* A igualdad de coste, el más cercano al centro del rango. */
            if (best_cost < 0 || cost < best_cost ||
                (cost == best_cost &&
                 iabs(cand[0] + cand[n - 1] - lo - hi) < iabs(best[0] + best[n - 1] - lo - hi))) {
                best_cost = cost;
                for (int i = 0; i < n; i++) best[i] = cand[i];
            }
        }
    }
    if (best_cost >= 0)
        for (int i = 0; i < n; i++) c->notes[i] = best[i];
}

/* ---------- grado en romanos ---------- */

void bl_degree_name(const bl_chord *c, int degree, char *out, int len)
{
    static const char *const UP[7] = { "I","II","III","IV","V","VI","VII" };
    static const char *const LO[7] = { "i","ii","iii","iv","v","vi","vii" };
    degree = ((degree % 7) + 7) % 7;
    const char *nm = c->name;
    /* Salta la fundamental (letra y alteración) para leer la calidad. */
    int i = 1;
    if (nm[i] == '#' || nm[i] == 'b') i++;
    int minor = (nm[i] == 'm' && nm[i + 1] != 'a');      /* "m", no "maj" */
    int dim = (nm[i] == 'd') || (minor && nm[i + 1] == '7' && nm[i + 2] == 'b');
    int aug = (nm[i] == 'a' && nm[i + 1] == 'u');
    int p = put(out, 0, len, (minor || dim) ? LO[degree] : UP[degree]);
    if (dim) p = put(out, p, len, "o");
    if (aug) put(out, p, len, "+");
}
