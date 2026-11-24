#ifndef WII_PLATFORM_GAME_H
#define WII_PLATFORM_GAME_H
/* Funciones de la capa Wii visibles desde el codigo del juego (sin libogc). */
void wii_input_init(void);
void wii_input_poll(void);
void wii_read_pad(int idx, void *pad);
void wii_video_init(void);
extern struct GfxWindowManagerAPI gfx_wii_wm;
extern struct GfxRenderingAPI gfx_wii_rapi;
extern struct AudioAPI audio_wii;
#endif
