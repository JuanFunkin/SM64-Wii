#ifndef WII_PLATFORM_H
#define WII_PLATFORM_H
/* Declaraciones compartidas de la capa Wii. Este header solo usa libogc / stdint
 * (nunca los tipos de SM64), porque libogc y SM64 definen s32/u32 de forma distinta. */
#include <gccore.h>
#include <stdbool.h>
#include <stdint.h>

extern GXRModeObj *wii_rmode;
extern void *wii_xfb[2];
extern bool wii_widescreen;

void wii_video_init(void);
void wii_input_init(void);
void wii_input_poll(void);
/* Rellena un OSContPad (mismo layout: u16 button; s8 stick_x; s8 stick_y; u8 errnum) del jugador idx (0 o 1). */
void wii_read_pad(int idx, void *pad);
void wii_fatal(const char *fmt, ...);
bool wii_home_pressed(void);
int wii_audio_fill(void);   /* muestras de audio en cola (0 si no hay audio) */

/* Definidos en wii_gfx.c / wii_audio.c; los usa pc_main.c */
struct GfxWindowManagerAPI; struct GfxRenderingAPI; struct AudioAPI;
extern struct GfxWindowManagerAPI gfx_wii_wm;
extern struct GfxRenderingAPI gfx_wii_rapi;
extern struct AudioAPI audio_wii;

/* Resolucion "logica" que ve el juego: siempre 480 de alto; 640 (4:3) o 853 (16:9) de ancho. */
extern uint32_t wii_logical_w, wii_logical_h;
#endif
