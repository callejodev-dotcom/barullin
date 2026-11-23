/* Pruebas del motor de acordes en el ordenador: make test */
#include <stdio.h>
#include <string.h>
#include "../app/core/chords.h"

static int fails = 0, checks = 0;

static void expect_name(int key, int mode, int deg, unsigned mods, const char *want)
{
    bl_chord c;
    bl_chord_build(key, mode, deg, mods, 0, 60, &c);
    checks++;
    if (strcmp(c.name, want) != 0) {
        printf("FALLO key=%d mode=%d grado=%d mods=0x%02x: '%s' (esperaba '%s')\n",
               key, mode, deg + 1, mods, c.name, want);
        fails++;
    }
}

static void expect_notes(int key, int mode, int deg, unsigned mods, int inv,
                         int n, const int *want)
{
    bl_chord c;
    bl_chord_build(key, mode, deg, mods, inv, 60, &c);
    checks++;
    int ok = (c.n == n);
    for (int i = 0; ok && i < n; i++) ok = (c.notes[i] == want[i]);
    if (!ok) {
        printf("FALLO notas %s inv=%d:", c.name, inv);
        for (int i = 0; i < c.n; i++) printf(" %d", c.notes[i]);
        printf("\n");
        fails++;
    }
}

int main(void)
{
    /* Diatónicos en Do mayor */
    const char *cmaj[7] = { "C","Dm","Em","F","G","Am","Bdim" };
    for (int d = 0; d < 7; d++) expect_name(0, BL_IONIAN, d, 0, cmaj[d]);

    /* Con séptima */
    const char *cmaj7[7] = { "Cmaj7","Dm7","Em7","Fmaj7","G7","Am7","Bm7b5" };
    for (int d = 0; d < 7; d++) expect_name(0, BL_IONIAN, d, BL_MOD_7, cmaj7[d]);

    /* La menor natural, y bemoles en Fa mayor */
    const char *amin[7] = { "Am","Bdim","C","Dm","Em","F","G" };
    for (int d = 0; d < 7; d++) expect_name(9, BL_AEOLIAN, d, 0, amin[d]);
    const char *fmaj[7] = { "F","Gm","Am","Bb","C","Dm","Edim" };
    for (int d = 0; d < 7; d++) expect_name(5, BL_IONIAN, d, 0, fmaj[d]);

    /* Modificadores */
    expect_name(0, BL_IONIAN, 0, BL_MOD_FLIP, "Cm");
    expect_name(0, BL_IONIAN, 1, BL_MOD_FLIP, "D");
    expect_name(0, BL_IONIAN, 4, BL_MOD_SUS4, "Gsus4");
    expect_name(0, BL_IONIAN, 1, BL_MOD_SUS2, "Dsus2");
    expect_name(0, BL_IONIAN, 4, BL_MOD_SUS4 | BL_MOD_7, "G7sus4");
    expect_name(0, BL_IONIAN, 0, BL_MOD_9, "Cadd9");
    expect_name(0, BL_IONIAN, 0, BL_MOD_7 | BL_MOD_9, "Cmaj9");
    expect_name(0, BL_IONIAN, 1, BL_MOD_7 | BL_MOD_9, "Dm9");
    expect_name(0, BL_IONIAN, 4, BL_MOD_7 | BL_MOD_9, "G9");
    expect_name(0, BL_IONIAN, 0, BL_MOD_6, "C6");
    expect_name(0, BL_IONIAN, 0, BL_MOD_6 | BL_MOD_9, "C6/9");
    expect_name(0, BL_IONIAN, 5, BL_MOD_MAJ7, "Am(maj7)");
    expect_name(0, BL_IONIAN, 0, BL_MOD_DIMAUG, "Caug");
    expect_name(0, BL_IONIAN, 5, BL_MOD_DIMAUG, "Adim");
    expect_name(0, BL_IONIAN, 6, BL_MOD_DIMAUG, "Bm");
    expect_name(0, BL_IONIAN, 4, BL_MOD_FLIP | BL_MOD_7, "Gm7");

    /* Notas e inversiones: C = 60 64 67 */
    int c0[] = { 60, 64, 67 }, c1[] = { 64, 67, 72 }, c2[] = { 67, 72, 76 };
    expect_notes(0, BL_IONIAN, 0, 0, 0, 3, c0);
    expect_notes(0, BL_IONIAN, 0, 0, 1, 3, c1);
    expect_notes(0, BL_IONIAN, 0, 0, 2, 3, c2);
    int g7[] = { 55, 59, 62, 65 };   /* G más cercano a 60 es 55 */
    expect_notes(0, BL_IONIAN, 4, BL_MOD_7, 0, 4, g7);

    /* Voice leading: C -> F -> G -> C se mueve poco */
    bl_chord prev, c;
    bl_chord_build(0, BL_IONIAN, 0, 0, 0, 60, &prev);
    int prog[] = { 3, 4, 0 };
    for (int i = 0; i < 3; i++) {
        bl_chord_build(0, BL_IONIAN, prog[i], 0, 0, 60, &c);
        bl_chord_lead(&prev, &c, 52, 79);
        int move = 0;
        for (int j = 0; j < 3; j++) {
            int d = c.notes[j] - prev.notes[j];
            move += d < 0 ? -d : d;
        }
        checks++;
        /* F -> G no comparte notas: 6 semitonos es el mínimo posible */
        if (move > 6) { printf("FALLO voice leading hacia %s: mueve %d\n", c.name, move); fails++; }
        prev = c;
    }

    /* Grados en romanos */
    const char *rom[7] = { "I","ii","iii","IV","V","vi","viio" };
    for (int d = 0; d < 7; d++) {
        char r[8];
        bl_chord_build(0, BL_IONIAN, d, 0, 0, 60, &c);
        bl_degree_name(&c, d, r, sizeof r);
        checks++;
        if (strcmp(r, rom[d])) { printf("FALLO romano %d: %s\n", d, r); fails++; }
    }

    printf("%d/%d pruebas bien\n", checks - fails, checks);
    return fails ? 1 : 0;
}
