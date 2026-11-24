#include "rom_loader.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

RomFormat rom_detect_format(const uint8_t *h) {
    if (h[0] == 0x80 && h[1] == 0x37 && h[2] == 0x12 && h[3] == 0x40) return ROM_FMT_Z64;
    if (h[0] == 0x37 && h[1] == 0x80 && h[2] == 0x40 && h[3] == 0x12) return ROM_FMT_V64;
    if (h[0] == 0x40 && h[1] == 0x12 && h[2] == 0x37 && h[3] == 0x80) return ROM_FMT_N64;
    return ROM_FMT_UNKNOWN;
}

int rom_to_z64(uint8_t *b, size_t len, RomFormat fmt) {
    size_t i;
    if (fmt == ROM_FMT_Z64) return 0;
    if (len & 3) return -1;
    if (fmt == ROM_FMT_V64) {          /* swap de bytes por pares */
        for (i = 0; i < len; i += 2) { uint8_t t = b[i]; b[i] = b[i+1]; b[i+1] = t; }
        return 0;
    }
    if (fmt == ROM_FMT_N64) {          /* little-endian de 32 bits */
        for (i = 0; i < len; i += 4) {
            uint8_t t0 = b[i], t1 = b[i+1];
            b[i] = b[i+3]; b[i+1] = b[i+2]; b[i+2] = t1; b[i+3] = t0;
        }
        return 0;
    }
    return -1;
}

RomRegion rom_detect_region(const uint8_t *z, size_t len) {
    if (len < 0x40 || memcmp(z + 0x20, "SUPER MARIO 64", 14) != 0) return ROM_REGION_UNKNOWN;
    switch (z[0x3E]) {
        case 'J': return ROM_REGION_JP;
        case 'E': return ROM_REGION_US;
        case 'P': return ROM_REGION_EU;
    }
    return ROM_REGION_UNKNOWN;
}

int rom_find_in_dir(const char *dir, char *out, size_t outlen) {
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return -1;
    while ((e = readdir(d))) {
        char path[512]; struct stat st; FILE *f; uint8_t h[4];
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) continue;
        if (st.st_size < 0x400000 || st.st_size > 0x2000000) continue; /* 4-32 MB */
        f = fopen(path, "rb");
        if (!f) continue;
        if (fread(h, 1, 4, f) == 4 && rom_detect_format(h) != ROM_FMT_UNKNOWN) {
            fclose(f); closedir(d);
            snprintf(out, outlen, "%s", path);
            return 0;
        }
        fclose(f);
    }
    closedir(d);
    return -1;
}

/* Formato MIO0: "MIO0", u32 len, u32 off_comp, u32 off_raw, luego bits / pares comprimidos / bytes crudos. */
static uint32_t be32(const uint8_t *p) { return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]; }

int mio0_decode(const uint8_t *s, size_t sl, uint8_t *dst, size_t cap) {
    uint32_t outlen, comp_off, raw_off, bit_i = 0, comp_i, raw_i, out = 0;
    const uint8_t *bits;
    if (sl < 16 || memcmp(s, "MIO0", 4) != 0) return -1;
    outlen = be32(s + 4); comp_off = be32(s + 8); raw_off = be32(s + 12);
    if (outlen > cap || comp_off >= sl || raw_off >= sl) return -1;
    bits = s + 16; comp_i = comp_off; raw_i = raw_off;
    while (out < outlen) {
        if (bits[bit_i >> 3] & (0x80 >> (bit_i & 7))) {
            if (raw_i >= sl) return -1;
            dst[out++] = s[raw_i++];
        } else {
            uint16_t pair; int len, dist, k;
            if (comp_i + 2 > sl) return -1;
            pair = (s[comp_i] << 8) | s[comp_i + 1]; comp_i += 2;
            len = (pair >> 12) + 3; dist = (pair & 0xFFF) + 1;
            if ((uint32_t)dist > out) return -1;
            for (k = 0; k < len && out < outlen; k++, out++) dst[out] = dst[out - dist];
        }
        bit_i++;
    }
    return (int)out;
}
