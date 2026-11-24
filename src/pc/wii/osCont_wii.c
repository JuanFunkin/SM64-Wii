/* Sustituye a controller_entry_point.c: implementa osCont* de libultra para la Wii (2 jugadores). */
#include <ultra64.h>
#include "macros.h"
#include "wii_platform_game.h"

s32 osContInit(UNUSED OSMesgQueue *mq, u8 *controllerBits, UNUSED OSContStatus *status) {
    wii_input_init();
    *controllerBits = 3;           /* puertos 1 y 2 conectados */
    return 0;
}

s32 osContStartReadData(UNUSED OSMesgQueue *mesg) { return 0; }

void osContGetReadData(OSContPad *pad) { wii_read_pad(0, pad); }
