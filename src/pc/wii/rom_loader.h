#ifndef WII_ROM_LOADER_H
#define WII_ROM_LOADER_H
#include <stdint.h>
#include <stddef.h>

typedef enum { ROM_FMT_UNKNOWN = 0, ROM_FMT_Z64, ROM_FMT_V64, ROM_FMT_N64 } RomFormat;
typedef enum { ROM_REGION_UNKNOWN = 0, ROM_REGION_JP, ROM_REGION_US, ROM_REGION_EU } RomRegion;

/* Detecta el formato por los 4 primeros bytes (80 37 12 40 = z64, 37 80 40 12 = v64, 40 12 37 80 = n64). */
RomFormat rom_detect_format(const uint8_t *head4);
/* Convierte el buffer a big-endian (z64) en el lugar. Devuelve 0 si ok. */
int rom_to_z64(uint8_t *buf, size_t len, RomFormat fmt);
/* Lee el codigo de region del header (byte 0x3E) y valida el titulo. */
RomRegion rom_detect_region(const uint8_t *z64, size_t len);
/* Busca una ROM dentro de dir (cualquier nombre/extension, ~8MB). Escribe la ruta en out. 0 = encontrada. */
int rom_find_in_dir(const char *dir, char *out, size_t outlen);

/* MIO0: devuelve bytes escritos en dst o -1 si error. */
int mio0_decode(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap);
#endif
