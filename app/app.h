/* Barullín: la mitad del contrato con la plataforma (fm1-base: device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "BARULLIN"
#define APP_SPLASH_RGB RGB(255, 184, 70)
#define APP_BLOCK 256u                   /* frames por bloque de audio: 5,8 ms a 44,1 kHz */
#define APP_TOUCH                        /* velocidad de las teclas (lib/touch.h) */

/* la memoria: los ajustes, la calibración del panel (de la plataforma) y el Bluetooth */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

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
