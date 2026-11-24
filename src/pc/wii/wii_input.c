/* Controles Wii -> pad N64.  P1/P2 = canal 0/1 de Wiimote (+Nunchuk / Classic) o mando de GameCube. */
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <math.h>
#include <stdint.h>
#include "wii_platform.h"

#define N64_A      0x8000
#define N64_B      0x4000
#define N64_Z      0x2000
#define N64_START  0x1000
#define N64_L      0x0020
#define N64_R      0x0010
#define N64_CUP    0x0008
#define N64_CDOWN  0x0004
#define N64_CLEFT  0x0002
#define N64_CRIGHT 0x0001

struct WiiPad { uint16_t button; int8_t stick_x; int8_t stick_y; uint8_t errnum; };

static bool home;

void wii_input_init(void) {
    WPAD_Init();
    PAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
}

void wii_input_poll(void) {
    WPAD_ScanPads();
    PAD_ScanPads();
    home = (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME) || (WPAD_ButtonsDown(0) & WPAD_CLASSIC_BUTTON_HOME)
           || (PAD_ButtonsHeld(0) & (PAD_BUTTON_START | PAD_TRIGGER_L | PAD_TRIGGER_R)) == (PAD_BUTTON_START | PAD_TRIGGER_L | PAD_TRIGGER_R);
}

bool wii_home_pressed(void) { return home; }

static int8_t clamp80(float v) { return v > 80 ? 80 : v < -80 ? -80 : (int8_t) v; }

static void stick_from_js(const struct joystick_t *js, float *x, float *y) {
    float rad = js->ang * (float) M_PI / 180.0f;   /* 0 grados = arriba */
    *x = sinf(rad) * js->mag * 80.0f;
    *y = cosf(rad) * js->mag * 80.0f;
}

void wii_read_pad(int idx, void *out) {
    struct WiiPad *pad = out;
    uint16_t b = 0;
    float sx = 0, sy = 0;
    pad->button = 0; pad->stick_x = 0; pad->stick_y = 0; pad->errnum = 0;

    u32 type;
    if (WPAD_Probe(idx, &type) == WPAD_ERR_NONE) {
        u32 h = WPAD_ButtonsHeld(idx);
        if (type == WPAD_EXP_NUNCHUK || (WPAD_Data(idx)->exp.type == WPAD_EXP_NUNCHUK)) {
            const WPADData *d = WPAD_Data(idx);
            stick_from_js(&d->exp.nunchuk.js, &sx, &sy);
            if (h & WPAD_BUTTON_A) b |= N64_A;
            if (h & WPAD_BUTTON_B) b |= N64_B;
            if (h & WPAD_NUNCHUK_BUTTON_Z) b |= N64_Z;
            if (h & WPAD_NUNCHUK_BUTTON_C) b |= N64_R;
            if (h & WPAD_BUTTON_PLUS) b |= N64_START;
            if (h & WPAD_BUTTON_MINUS) b |= N64_L;
            if (h & WPAD_BUTTON_UP) b |= N64_CUP;
            if (h & WPAD_BUTTON_DOWN) b |= N64_CDOWN;
            if (h & WPAD_BUTTON_LEFT) b |= N64_CLEFT;
            if (h & WPAD_BUTTON_RIGHT) b |= N64_CRIGHT;
        } else if (WPAD_Data(idx)->exp.type == WPAD_EXP_CLASSIC) {
            const WPADData *d = WPAD_Data(idx);
            stick_from_js(&d->exp.classic.ljs, &sx, &sy);
            float cx, cy;
            stick_from_js(&d->exp.classic.rjs, &cx, &cy);
            if (cy > 40) b |= N64_CUP;
            if (cy < -40) b |= N64_CDOWN;
            if (cx < -40) b |= N64_CLEFT;
            if (cx > 40) b |= N64_CRIGHT;
            if (h & WPAD_CLASSIC_BUTTON_A) b |= N64_A;
            if (h & WPAD_CLASSIC_BUTTON_B) b |= N64_B;
            if (h & (WPAD_CLASSIC_BUTTON_ZL | WPAD_CLASSIC_BUTTON_ZR)) b |= N64_Z;
            if (h & WPAD_CLASSIC_BUTTON_FULL_R) b |= N64_R;
            if (h & WPAD_CLASSIC_BUTTON_FULL_L) b |= N64_L;
            if (h & WPAD_CLASSIC_BUTTON_PLUS) b |= N64_START;
            if (h & WPAD_CLASSIC_BUTTON_UP) sy = 80;
            if (h & WPAD_CLASSIC_BUTTON_DOWN) sy = -80;
            if (h & WPAD_CLASSIC_BUTTON_LEFT) sx = -80;
            if (h & WPAD_CLASSIC_BUTTON_RIGHT) sx = 80;
        } else {
            /* Wiimote solo, de lado (1 y 2 a la derecha): cruz = palanca, 2 = A, 1 = B */
            if (h & WPAD_BUTTON_UP) sx = -80;
            if (h & WPAD_BUTTON_DOWN) sx = 80;
            if (h & WPAD_BUTTON_LEFT) sy = -80;
            if (h & WPAD_BUTTON_RIGHT) sy = 80;
            if (h & WPAD_BUTTON_2) b |= N64_A;
            if (h & WPAD_BUTTON_1) b |= N64_B;
            if (h & WPAD_BUTTON_A) b |= N64_Z;
            if (h & WPAD_BUTTON_B) b |= N64_R;
            if (h & WPAD_BUTTON_PLUS) b |= N64_START;
            if (h & WPAD_BUTTON_MINUS) b |= N64_CUP;
        }
    }

    u16 g = PAD_ButtonsHeld(idx);        /* mando de GameCube (se suma al Wiimote) */
    if (PAD_ButtonsHeld(idx) || PAD_StickX(idx) || PAD_StickY(idx)) {
        if (g & PAD_BUTTON_A) b |= N64_A;
        if (g & PAD_BUTTON_B) b |= N64_B;
        if (g & PAD_TRIGGER_Z) b |= N64_R;
        if (g & PAD_TRIGGER_R) b |= N64_Z;
        if (g & PAD_TRIGGER_L) b |= N64_Z;
        if (g & PAD_BUTTON_START) b |= N64_START;
        if (g & PAD_BUTTON_X) b |= N64_B;
        int cx = PAD_SubStickX(idx), cy = PAD_SubStickY(idx);
        if (cy > 40) b |= N64_CUP;
        if (cy < -40) b |= N64_CDOWN;
        if (cx < -40) b |= N64_CLEFT;
        if (cx > 40) b |= N64_CRIGHT;
        if (PAD_StickX(idx) || PAD_StickY(idx)) { sx = PAD_StickX(idx) * 80.0f / 72.0f; sy = PAD_StickY(idx) * 80.0f / 72.0f; }
    }
    pad->button = b;
    pad->stick_x = clamp80(sx);
    pad->stick_y = clamp80(sy);
}
