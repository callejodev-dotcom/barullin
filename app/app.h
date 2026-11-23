/* Barullín: la mitad del contrato con la plataforma (fm1-base: device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "BARULLIN"
#define APP_SPLASH_RGB RGB(255, 184, 70)
#define APP_BLOCK 256u                   /* frames por bloque de audio: 5,8 ms a 44,1 kHz */
#define APP_TOUCH                        /* velocidad de las teclas (lib/touch.h) */

/* la memoria: los ajustes (y el sonido de cada banco), la calibración del panel (de la plataforma), el Bluetooth
 * y las ideas (aparte: sobreviven a las actualizaciones) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_IDEAS, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u, 0xDA000u}

/* copia de seguridad por USB (base/tools/fm1_backup.py; tools/barullin_export.py las saca a MIDI y a Ableton) */
#define APP_BACKUP_OBJS {OBJ_SETTINGS, OBJ_IDEAS}
#define APP_BACKUP_MAGIC 0x4E4C5242u     /* "BRLN" */
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "SUSTITUYE TODAS LAS IDEAS"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* comprobaciones del simulador (expect ...) */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
