/* Audio Wii (AESND).  El juego produce PCM estereo s16 a 32 kHz; lo guardamos en un anillo y AESND lo consume. */
#include <gccore.h>
#include <aesndlib.h>
#include <malloc.h>
#include <string.h>
#include "../audio/audio_api.h"

#define RING_SAMPLES 16384            /* muestras estereo (4 bytes c/u) */
#define DMA_SAMPLES 1024              /* 4096 bytes, multiplo de 32 */

static int16_t ring[RING_SAMPLES * 2];
static volatile uint32_t rd, wr;      /* en muestras, crecen sin parar */
static AESNDPB *voice;
static uint8_t *dma[2];
static int cur;

static uint32_t fill(void) { return wr - rd; }

static void voice_cb(AESNDPB *pb, uint32_t state) {
    if (state != VOICE_STATE_STREAM) return;
    int16_t *out = (int16_t *) dma[cur];
    uint32_t avail = fill();
    uint32_t n = avail < DMA_SAMPLES ? avail : DMA_SAMPLES;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t p = (rd + i) % RING_SAMPLES;
        out[i * 2] = ring[p * 2];
        out[i * 2 + 1] = ring[p * 2 + 1];
    }
    if (n < DMA_SAMPLES) memset(out + n * 2, 0, (DMA_SAMPLES - n) * 4);
    rd += n;
    DCFlushRange(out, DMA_SAMPLES * 4);
    AESND_SetVoiceBuffer(pb, out, DMA_SAMPLES * 4);
    cur ^= 1;
}

static bool wii_audio_init(void) {
    AESND_Init();
    dma[0] = memalign(32, DMA_SAMPLES * 4);
    dma[1] = memalign(32, DMA_SAMPLES * 4);
    if (!dma[0] || !dma[1]) return false;
    memset(dma[0], 0, DMA_SAMPLES * 4);
    memset(dma[1], 0, DMA_SAMPLES * 4);
    voice = AESND_AllocateVoice(voice_cb);
    if (!voice) return false;
    AESND_SetVoiceFormat(voice, VOICE_STEREO16);
    AESND_SetVoiceFrequency(voice, 32000);
    AESND_SetVoiceVolume(voice, 255, 255);
    AESND_SetVoiceStream(voice, true);
    DCFlushRange(dma[0], DMA_SAMPLES * 4);
    AESND_SetVoiceBuffer(voice, dma[0], DMA_SAMPLES * 4);
    AESND_SetVoiceStop(voice, false);
    AESND_Pause(false);
    cur = 1;
    return true;
}

static int wii_audio_buffered(void) { return (int) fill(); }
static int wii_audio_desired(void) { return 1100; }

static void wii_audio_play(const uint8_t *buf, size_t len) {
    const int16_t *s = (const int16_t *) buf;
    size_t n = len / 4;
    if (fill() + n > RING_SAMPLES) return;          /* sin espacio: descarta */
    for (size_t i = 0; i < n; i++) {
        uint32_t p = (wr + i) % RING_SAMPLES;
        ring[p * 2] = s[i * 2];
        ring[p * 2 + 1] = s[i * 2 + 1];
    }
    wr += n;
}

struct AudioAPI audio_wii = { wii_audio_init, wii_audio_buffered, wii_audio_desired, wii_audio_play };
