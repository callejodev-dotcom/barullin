/*
 * Barullín: motor de acordes.
 *
 * C99 puro, sin float, sin malloc y sin depender del hardware: el mismo
 * código corre en el FM-1, en el simulador y en las pruebas del ordenador.
 *
 * Idea: eliges tonalidad y modo; cada grado (I..vii) da su acorde diatónico;
 * los modificadores lo transforman (menor/mayor, 7, maj7, sus, 9, 6, dim/aum);
 * la inversión y el voice leading deciden qué notas concretas suenan.
 */
#ifndef BL_CHORDS_H
#define BL_CHORDS_H

#include <stdint.h>

#define BL_MAX_NOTES 6
#define BL_NAME_LEN  16

/* Modos (rotaciones de la escala mayor). */
enum bl_mode {
    BL_IONIAN = 0,   /* mayor */
    BL_DORIAN,
    BL_PHRYGIAN,
    BL_LYDIAN,
    BL_MIXOLYDIAN,
    BL_AEOLIAN,      /* menor natural */
    BL_LOCRIAN,
    BL_MODE_COUNT
};

/* Modificadores: se combinan con OR mientras mantienes las teclas negras. */
enum bl_mod {
    BL_MOD_FLIP   = 1u << 0,  /* cambia la tercera: mayor <-> menor */
    BL_MOD_7      = 1u << 1,  /* séptima diatónica */
    BL_MOD_MAJ7   = 1u << 2,  /* séptima mayor (manda sobre MOD_7) */
    BL_MOD_SUS2   = 1u << 3,
    BL_MOD_SUS4   = 1u << 4,  /* manda sobre SUS2 */
    BL_MOD_9      = 1u << 5,  /* novena diatónica */
    BL_MOD_6      = 1u << 6,  /* sexta diatónica */
    BL_MOD_DIMAUG = 1u << 7   /* quinta justa -> dim (si menor) / aum (si mayor) */
};

typedef struct {
    uint8_t n;                    /* cuántas notas */
    uint8_t notes[BL_MAX_NOTES];  /* MIDI, de grave a agudo */
    uint8_t bass;                 /* nota de bajo (fundamental, una octava abajo) */
    uint8_t root;                 /* clase de altura de la fundamental, 0..11 */
    char    name[BL_NAME_LEN];    /* "Am7", "Gsus4", "Cadd9"... */
} bl_chord;

/*
 * Construye el acorde.
 *   key       tonalidad, 0 = Do .. 11 = Si
 *   mode      enum bl_mode
 *   degree    0..6 (I..vii)
 *   mods      combinación de enum bl_mod
 *   inversion 0 = estado fundamental, 1 = primera, ... (se reduce módulo n)
 *   base      nota MIDI alrededor de la que se coloca la fundamental (p. ej. 60)
 */
void bl_chord_build(int key, int mode, int degree, unsigned mods,
                    int inversion, int base, bl_chord *out);

/*
 * Voice leading: recoloca las notas de c (misma clase de alturas, otra
 * inversión u octava) para que se muevan lo menos posible desde prev,
 * sin salirse de [lo, hi]. El nombre y el bajo no cambian.
 */
void bl_chord_lead(const bl_chord *prev, bl_chord *c, int lo, int hi);

/* Nombre de una clase de altura según la tonalidad (bemoles en F, Bb, Eb...). */
const char *bl_note_name(int pc, int key);

/* Grado en números romanos según la calidad del acorde: "I", "ii", "vii°"... */
void bl_degree_name(const bl_chord *c, int degree, char *out, int len);

#endif
