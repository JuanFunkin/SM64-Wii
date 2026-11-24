/* Punto de entrada Wii: video, SD/USB, lanzador (busca la ROM, extrae assets con barra de progreso)
 * y luego arranca el juego (main_func de pc_main.c).
 *
 * Carpetas junto a boot.dol:  sm64/ (la ROM, cualquier nombre/formato), extracted/ (assets), saves/ (partidas). */
#include <gccore.h>
#include <fat.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ogc/conf.h>
#include <wiiuse/wpad.h>
#include "rom_loader.h"
#include "wii_platform.h"

extern void main_func(void);

GXRModeObj *wii_rmode;
void *wii_xfb[2];
bool wii_widescreen;

#define ROM_SIZE_US 0x800000
#define TBL_ENTRY 28

/* Idioma: ingles por defecto; espanol si la Wii esta en espanol, o si lang.txt dice "es" / "en". */
static int lang_es = -1;
static bool is_es(void) {
    if (lang_es < 0) {
        lang_es = CONF_GetLanguage() == CONF_LANG_SPANISH;
        FILE *f = fopen("lang.txt", "rb");
        if (f) {
            char b[4] = {0};
            fread(b, 1, 2, f);
            fclose(f);
            if (b[0] == 'e' && b[1] == 's') lang_es = 1;
            if (b[0] == 'e' && b[1] == 'n') lang_es = 0;
        }
    }
    return lang_es;
}
#define T(en, es) (is_es() ? (es) : (en))

void wii_video_init(void) {
    VIDEO_Init();
    CONF_Init();
    wii_input_init();
    wii_rmode = VIDEO_GetPreferredMode(NULL);
    wii_widescreen = CONF_GetAspectRatio() == CONF_ASPECT_16_9;
    if (wii_widescreen) {                       /* ocupa toda la pantalla 16:9 */
        wii_rmode->viWidth = 678;
        wii_rmode->viXOrigin = (VI_MAX_WIDTH_NTSC - 678) / 2;
    }
    VIDEO_Configure(wii_rmode);
    wii_xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(wii_rmode));
    wii_xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(wii_rmode));
    console_init(wii_xfb[0], 20, 20, wii_rmode->fbWidth, wii_rmode->xfbHeight, wii_rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_ClearFrameBuffer(wii_rmode, wii_xfb[0], COLOR_BLACK);
    VIDEO_ClearFrameBuffer(wii_rmode, wii_xfb[1], COLOR_BLACK);
    VIDEO_SetNextFramebuffer(wii_xfb[0]);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (wii_rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
}

static void wait_button(void) {
    for (;;) {
        WPAD_ScanPads();
        PAD_ScanPads();
        if (WPAD_ButtonsDown(0) || PAD_ButtonsDown(0)) return;
        VIDEO_WaitVSync();
    }
}

void wii_fatal(const char *fmt, ...) {
    va_list ap;
    printf("%s", T("\n\n  ERROR: ", "\n\n  ERROR: "));
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("%s", T("\n\n  Press any button to exit.\n", "\n\n  Pulsa cualquier boton para salir.\n"));
    wait_button();
    exit(1);
}

static void mkpath(const char *file) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", file);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0777); *p = '/'; }
    }
}

static void bar(const char *label, int cur, int total) {
    static int last = -1;
    int pct = total ? (int) ((long long) cur * 100 / total) : 100;
    if (pct == last && cur != total) return;
    last = pct;
    char b[41];
    int fill = pct * 40 / 100;
    for (int i = 0; i < 40; i++) b[i] = i < fill ? '#' : '.';
    b[40] = 0;
    printf("\r  %s [%s] %3d%%", label, b, pct);
    fflush(stdout);
    if (cur == total) { printf("\n"); last = -1; }
}

static void missing_table(void);

static uint32_t rd32(const uint8_t *p) { return ((uint32_t) p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

struct Ent { uint32_t kind, addr, size, addr2, src, pos, name_off; };

static uint8_t *tbl; static uint32_t tbl_count; static const char *tbl_names;

static void load_table(void) {
    FILE *f = fopen("assets.tbl", "rb");
    if (!f) missing_table();
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    tbl = malloc(n);
    if (!tbl || fread(tbl, 1, n, f) != (size_t) n) wii_fatal(T("Could not read assets.tbl", "No se pudo leer assets.tbl"));
    fclose(f);
    if (memcmp(tbl, "SMA2", 4) != 0) wii_fatal(T("Invalid assets.tbl", "assets.tbl invalido"));
    tbl_count = rd32(tbl + 4);
    tbl_names = (const char *) tbl + rd32(tbl + 8);
}

static struct Ent get_ent(uint32_t i) {
    const uint8_t *p = tbl + 12 + i * TBL_ENTRY;
    struct Ent e = { rd32(p), rd32(p + 4), rd32(p + 8), rd32(p + 12), rd32(p + 16), rd32(p + 20), rd32(p + 24) };
    return e;
}

static void ent_path(const struct Ent *e, char *out, size_t n) {
    snprintf(out, n, "extracted/%s.bin", tbl_names + e->name_off);
}

static bool assets_ready(void) {
    FILE *f = fopen("extracted/.ok", "rb");
    char buf[32] = {0};
    if (!f) return false;
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    char want[32];
    snprintf(want, sizeof(want), "SMA2 %u", (unsigned) tbl_count);
    return strcmp(buf, want) == 0;
}

static uint8_t *load_rom(size_t *len) {
    char path[300];
    if (rom_find_in_dir("sm64", path, sizeof(path)) != 0)
        wii_fatal(T("ROM not found.\n  Copy your Super Mario 64 (US) ROM into the sm64/ folder\n  next to boot.dol (.z64, .v64 or .n64, any file name).", "No encuentro la ROM.\n  Copia tu ROM de Super Mario 64 (US) en la carpeta\n  sm64/ junto a boot.dol (.z64, .v64 o .n64, cualquier nombre)."));
    printf("  ROM: %s\n", path);
    FILE *f = fopen(path, "rb");
    if (!f) wii_fatal(T("Could not open the ROM.", "No se pudo abrir la ROM."));
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n != ROM_SIZE_US) wii_fatal(T("The ROM must be 8 MB (US). Size: %ld bytes.", "La ROM debe medir 8 MB (US). Tamano: %ld bytes."), n);
    uint8_t *rom = memalign(32, n);
    if (!rom) wii_fatal(T("Out of memory for the ROM.", "Sin memoria para la ROM."));
    size_t got = 0;
    while (got < (size_t) n) {                       /* lectura por trozos para mostrar progreso */
        size_t c = fread(rom + got, 1, 256 * 1024, f);
        if (c == 0) break;
        got += c;
        bar(T("Reading ROM    ", "Leyendo ROM    "), (int) got, (int) n);
    }
    fclose(f);
    if (got != (size_t) n) wii_fatal(T("Error reading the ROM.", "Error leyendo la ROM."));
    RomFormat fmt = rom_detect_format(rom);
    if (fmt == ROM_FMT_UNKNOWN) wii_fatal(T("Unrecognized ROM format.", "Formato de ROM no reconocido."));
    if (rom_to_z64(rom, n, fmt) != 0) wii_fatal(T("Could not convert the ROM.", "No se pudo convertir la ROM."));
    RomRegion r = rom_detect_region(rom, n);
    if (r != ROM_REGION_US) wii_fatal(T("Only the Super Mario 64 (US / NTSC-U) ROM is supported.", "Esta version solo admite la ROM Super Mario 64 (US / NTSC-U)."));
    *len = n;
    return rom;
}

/* ---- Extraccion: ROM -> extracted/ -------------------------------------------------- */
static void extract_all(void) {
    size_t rlen;
    uint8_t *rom = load_rom(&rlen);
    uint8_t *blk = NULL; size_t blk_cap = 0, blk_len = 0; uint32_t blk_src = 0xFFFFFFFF;
    printf("%s", T("\n  Preparing assets (first run only)...\n", "\n  Preparando assets (esto solo ocurre la primera vez)...\n"));
    for (uint32_t i = 0; i < tbl_count; i++) {
        struct Ent e = get_ent(i);
        const uint8_t *data;
        if (e.src == 0) {
            if ((size_t) e.pos + e.size > rlen) wii_fatal(T("Entry %u is outside the ROM.", "Entrada %u fuera de la ROM."), i);
            data = rom + e.pos;
        } else {
            if (blk_src != e.src) {
                uint32_t out = rd32(rom + e.src + 4);
                if (out > blk_cap) { free(blk); blk = malloc(out); blk_cap = out; }
                if (!blk) wii_fatal(T("Out of memory while decompressing.", "Sin memoria para descomprimir."));
                if (mio0_decode(rom + e.src, rlen - e.src, blk, blk_cap) < 0) wii_fatal(T("Invalid MIO0 block (%u).", "Bloque MIO0 invalido (%u)."), i);
                blk_src = e.src; blk_len = out;
            }
            if ((size_t) e.pos + e.size > blk_len) wii_fatal(T("Entry %u is outside its block.", "Entrada %u fuera del bloque."), i);
            data = blk + e.pos;
        }
        char path[300];
        ent_path(&e, path, sizeof(path));
        mkpath(path);
        FILE *f = fopen(path, "wb");
        if (!f || fwrite(data, 1, e.size, f) != e.size) wii_fatal(T("Could not write %s", "No se pudo escribir %s"), path);
        fclose(f);
        bar(T("Building        ", "Compilando     "), (int) i + 1, (int) tbl_count);
    }
    free(blk); free(rom);
    FILE *ok = fopen("extracted/.ok", "wb");
    if (ok) { fprintf(ok, "SMA2 %u", (unsigned) tbl_count); fclose(ok); }
}

/* ---- Carga: extracted/ -> RAM del juego ---------------------------------------------- */
static void load_all(void) {
    printf("%s", T("\n  Loading game...\n", "\n  Cargando juego...\n"));
    for (uint32_t i = 0; i < tbl_count; i++) {
        struct Ent e = get_ent(i);
        char path[300];
        ent_path(&e, path, sizeof(path));
        FILE *f = fopen(path, "rb");
        if (!f) wii_fatal(T("Missing %s (delete the extracted folder to rebuild it).", "Falta %s (borra la carpeta extracted para regenerarla)."), path);
        if (e.kind == 0) {
            if (fread((void *) e.addr, 1, e.size, f) != e.size) wii_fatal(T("Corrupt file: %s", "Archivo corrupto: %s"), path);
        } else {                                   /* skybox: mosaicos + tabla de 80 punteros */
            uint32_t tiles = e.size - 80 * 4;
            uint8_t tab[80 * 4];
            if (fread((void *) e.addr, 1, tiles, f) != tiles || fread(tab, 1, sizeof(tab), f) != sizeof(tab))
                wii_fatal(T("Corrupt file: %s", "Archivo corrupto: %s"), path);
            uint32_t base = rd32(tab);
            const uint8_t **list = (const uint8_t **) e.addr2;
            for (int k = 0; k < 80; k++) list[k] = (const uint8_t *) e.addr + (rd32(tab + k * 4) - base);
        }
        fclose(f);
        bar(T("Loading         ", "Cargando       "), (int) i + 1, (int) tbl_count);
    }
}

static char tried[512];

static bool has_table(const char *dir) {
    char p[300];
    snprintf(p, sizeof(p), "%s/assets.tbl", dir);
    FILE *f = fopen(p, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

static bool try_dir(const char *dir) {
    if (strlen(tried) + strlen(dir) + 4 < sizeof(tried)) { strcat(tried, "\n    "); strcat(tried, dir); }
    if (has_table(dir) && chdir(dir) == 0) return true;
    return false;
}

/* Cada raiz (sd:, usb:, fat:) se recorre: raiz, apps/<cualquiera>/ */
static bool scan_root(const char *root) {
    char d[300];
    snprintf(d, sizeof(d), "%s/apps", root);
    DIR *dp = opendir(d);
    if (dp) {
        struct dirent *e;
        while ((e = readdir(dp))) {
            if (e->d_name[0] == '.') continue;
            snprintf(d, sizeof(d), "%s/apps/%s", root, e->d_name);
            if (try_dir(d)) { closedir(dp); return true; }
        }
        closedir(dp);
    }
    return try_dir(root);
}

static char argv0_copy[256];

static bool goto_app_dir(const char *argv0) {
    char dir[256] = "";
    tried[0] = 0;
    if (argv0) snprintf(argv0_copy, sizeof(argv0_copy), "%s", argv0);
    if (argv0 && strchr(argv0, '/')) {                 /* carpeta del boot.dol */
        snprintf(dir, sizeof(dir), "%s", argv0);
        char *s = strrchr(dir, '/');
        if (s) *s = 0;
        if (try_dir(dir)) return true;
    }
    if (try_dir(".")) return true;                      /* carpeta de trabajo actual */
    static const char *roots[] = { "sd:", "usb:", "fat:", "fat3:", "fat4:", "sd2:", "usb2:" };
    for (unsigned i = 0; i < sizeof(roots) / sizeof(roots[0]); i++)
        if (scan_root(roots[i])) return true;
    return false;
}

static void missing_table(void) {
    char cwd[256] = "?";
    getcwd(cwd, sizeof(cwd));
    wii_fatal(T("assets.tbl not found next to boot.dol.\n  argv[0]: %s\n  current dir: %s\n  looked in:%s",
                "No encuentro assets.tbl junto a boot.dol.\n  argv[0]: %s\n  carpeta actual: %s\n  busque en:%s"),
              argv0_copy[0] ? argv0_copy : "(none)", cwd, tried);
}

int main(int argc, char **argv) {
    (void) argc;
    wii_video_init();
    printf("\x1b[2;0H");
    printf("  Super Mario 64 - Wii (homebrew)\n  -------------------------------\n\n");
    if (!fatInitDefault()) wii_fatal(T("Could not mount the SD/USB.", "No se pudo montar la SD/USB."));
    if (!goto_app_dir(argc > 0 ? argv[0] : NULL)) missing_table();
    lang_es = -1;   /* relee lang.txt ahora que estamos en la carpeta de la app */
    mkdir("sm64", 0777); mkdir("extracted", 0777); mkdir("saves", 0777);
    load_table();
    if (!assets_ready()) extract_all();
    load_all();
    printf("%s", T("\n  Starting...\n", "\n  Iniciando...\n"));
    main_func();
    return 0;
}
