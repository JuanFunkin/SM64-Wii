/* Backend de video GX (Wii) para el sm64-port.
 * gfx_pc.c entrega vertices ya transformados a espacio "clip" (x,y,z,w).  Aqui los pasamos a GX como
 * (x, y, -w) con una proyeccion perspectiva personalizada (w' = -z) para que GX haga la division por w,
 * la correccion de perspectiva de texturas y el z-buffer.  Las llamadas 2D (w == 1) usan proyeccion ortografica.
 * El combinador de color N64 se traduce a etapas TEV. */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../gfx/gfx_cc.h"
#include "../gfx/gfx_rendering_api.h"
#include "../gfx/gfx_window_manager_api.h"
#include "wii_platform.h"

#define FIFO_SIZE (256 * 1024)
#define MAX_TEXTURES 4096
#define PERSP_NEAR 20.0f
#define PERSP_FAR 30000.0f

uint32_t wii_logical_w = 640, wii_logical_h = 480;

/* ------------------------------------------------------------------ texturas */
struct WiiTex {
    uint8_t *data;       /* RGBA8 de GX (bloques 4x4), alineado a 32 */
    uint16_t w, h;       /* tamano real (multiplo de 4) */
    bool linear;
    uint8_t cms, cmt;
};
static struct WiiTex textures[MAX_TEXTURES];
static uint32_t num_textures = 1;
static uint32_t cur_tex[2];
static int cur_tile;

struct ShaderProgram {
    uint32_t shader_id;
    struct CCFeatures cc;
    uint8_t num_floats;
    uint8_t off_tex, off_fog, off_in[4];
};
static struct ShaderProgram shader_pool[64];
static int shader_pool_size;
static struct ShaderProgram *cur_prg;

static bool depth_test = true, depth_mask = true, zmode_decal = false, use_alpha = false;

static bool z_is_from_0_to_1(void) { return false; }
static void unload_shader(struct ShaderProgram *p) { (void) p; }
static void load_shader(struct ShaderProgram *p) { cur_prg = p; }

static struct ShaderProgram *create_and_load_new_shader(uint32_t id) {
    struct ShaderProgram *p = &shader_pool[shader_pool_size++];
    memset(p, 0, sizeof(*p));
    p->shader_id = id;
    gfx_cc_get_features(id, &p->cc);
    int n = 4;
    if (p->cc.used_textures[0] || p->cc.used_textures[1]) { p->off_tex = n; n += 2; }
    if (p->cc.opt_fog) { p->off_fog = n; n += 4; }
    for (int i = 0; i < p->cc.num_inputs; i++) { p->off_in[i] = n; n += p->cc.opt_alpha ? 4 : 3; }
    p->num_floats = n;
    cur_prg = p;
    return p;
}

static struct ShaderProgram *lookup_shader(uint32_t id) {
    for (int i = 0; i < shader_pool_size; i++)
        if (shader_pool[i].shader_id == id) return &shader_pool[i];
    return NULL;
}

static void shader_get_info(struct ShaderProgram *p, uint8_t *num_inputs, bool used_textures[2]) {
    *num_inputs = p->cc.num_inputs;
    used_textures[0] = p->cc.used_textures[0];
    used_textures[1] = p->cc.used_textures[1];
}

static uint32_t new_texture(void) {
    if (num_textures >= MAX_TEXTURES) return 0;
    return num_textures++;
}

static void select_texture(int tile, uint32_t id) { cur_tile = tile; cur_tex[tile] = id; }

static void upload_texture(const uint8_t *rgba, int w, int h) {
    struct WiiTex *t = &textures[cur_tex[cur_tile]];
    int pw = (w + 3) & ~3, ph = (h + 3) & ~3;
    size_t size = (size_t) pw * ph * 4;
    if (t->data == NULL || (size_t) t->w * t->h * 4 != size) {
        free(t->data);
        t->data = memalign(32, (size + 31) & ~31);
        if (!t->data) return;
    }
    t->w = pw; t->h = ph;
    for (int by = 0; by < ph / 4; by++) {
        for (int bx = 0; bx < pw / 4; bx++) {
            uint8_t *blk = t->data + ((by * (pw / 4)) + bx) * 64;
            for (int y = 0; y < 4; y++) {
                for (int x = 0; x < 4; x++) {
                    int sx = bx * 4 + x, sy = by * 4 + y;
                    if (sx >= w) sx = w - 1;
                    if (sy >= h) sy = h - 1;
                    const uint8_t *s = rgba + ((size_t) sy * w + sx) * 4;
                    int o = (y * 4 + x) * 2;
                    blk[o] = s[3]; blk[o + 1] = s[0];          /* plano A,R */
                    blk[32 + o] = s[1]; blk[32 + o + 1] = s[2]; /* plano G,B */
                }
            }
        }
    }
    DCFlushRange(t->data, size);
    GX_InvalidateTexAll();
}

static void set_sampler_parameters(int sampler, bool linear, uint32_t cms, uint32_t cmt) {
    struct WiiTex *t = &textures[cur_tex[sampler]];
    t->linear = linear; t->cms = cms; t->cmt = cmt;
}

static void set_depth_test(bool v) { depth_test = v; }
static void set_depth_mask(bool v) { depth_mask = v; }
static void set_zmode_decal(bool v) { zmode_decal = v; }
static void set_use_alpha(bool v) { use_alpha = v; }

static void set_viewport(int x, int y, int w, int h) {
    float sx = 640.0f / (float) wii_logical_w;
    float top = (float) wii_logical_h - (float) (y + h);
    GX_SetViewport(x * sx, top, w * sx, h, 0.0f, 1.0f);
}

static void set_scissor(int x, int y, int w, int h) {
    float sx = 640.0f / (float) wii_logical_w;
    int top = (int) wii_logical_h - (y + h);
    int x0 = (int) (x * sx), x1 = (int) ((x + w) * sx);
    int y0 = top, y1 = top + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 640) x1 = 640;
    if (y1 > 480) y1 = 480;
    if (x1 <= x0 || y1 <= y0) { x0 = y0 = 0; x1 = y1 = 1; }
    GX_SetScissor(x0, y0, x1 - x0, y1 - y0);
}

/* ------------------------------------------------------------------ TEV */
struct StageIn { uint8_t a, b, c, d; };

/* Origen de cada "item" del combinador: color y alfa */
struct Srcs {
    uint8_t in_c[4], in_a[4];     /* SHADER_INPUT_1..4 -> GX_CC_* / GX_CA_* */
    uint8_t t0_c, t0_a, t1_c, t1_a;
};

static uint8_t map_color(const struct Srcs *s, int item) {
    switch (item) {
        case SHADER_0: return GX_CC_ZERO;
        case SHADER_INPUT_1: return s->in_c[0];
        case SHADER_INPUT_2: return s->in_c[1];
        case SHADER_INPUT_3: return s->in_c[2];
        case SHADER_INPUT_4: return s->in_c[3];
        case SHADER_TEXEL0: return s->t0_c;
        case SHADER_TEXEL0A: return s->t0_a;   /* alfa usado como color (difusion) */
        case SHADER_TEXEL1: return s->t1_c;
    }
    return GX_CC_ZERO;
}

static uint8_t map_alpha(const struct Srcs *s, int item) {
    switch (item) {
        case SHADER_0: return GX_CA_ZERO;
        case SHADER_INPUT_1: return s->in_a[0];
        case SHADER_INPUT_2: return s->in_a[1];
        case SHADER_INPUT_3: return s->in_a[2];
        case SHADER_INPUT_4: return s->in_a[3];
        case SHADER_TEXEL0:
        case SHADER_TEXEL0A: return s->t0_a == GX_CC_A1 ? GX_CA_A1 : GX_CA_TEXA;
        case SHADER_TEXEL1: return GX_CA_TEXA;
    }
    return GX_CA_ZERO;
}

/* Genera hasta 2 etapas (A,B,C,D) para (A-B)*C+D segun el modo.  Devuelve cuantas etapas. */
static int build_formula(const uint8_t c[4], bool single, bool mul, bool mix, bool alpha, const struct Srcs *s,
                         struct StageIn st[2], uint8_t sub[2]) {
    uint8_t Z = alpha ? GX_CA_ZERO : GX_CC_ZERO;
    uint8_t PREV = alpha ? GX_CA_APREV : GX_CC_CPREV;
    uint8_t A = alpha ? map_alpha(s, c[0]) : map_color(s, c[0]);
    uint8_t B = alpha ? map_alpha(s, c[1]) : map_color(s, c[1]);
    uint8_t C = alpha ? map_alpha(s, c[2]) : map_color(s, c[2]);
    uint8_t D = alpha ? map_alpha(s, c[3]) : map_color(s, c[3]);
    sub[0] = sub[1] = 0;
    if (single) { st[0] = (struct StageIn){Z, Z, Z, D}; return 1; }
    if (mul)    { st[0] = (struct StageIn){Z, A, C, Z}; return 1; }     /* d + (1-c)*0 + c*A */
    if (mix)    { st[0] = (struct StageIn){B, A, C, Z}; return 1; }     /* B + C*(A-B) */
    st[0] = (struct StageIn){B, Z, Z, A}; sub[0] = 1;                    /* A - B */
    st[1] = (struct StageIn){Z, PREV, C, D};                             /* D + C*(A-B) */
    return 2;
}

static void set_stage(int stage, const struct StageIn *cs, bool csub, uint8_t cout,
                      const struct StageIn *as, bool asub, uint8_t aout) {
    GX_SetTevColorIn(stage, cs->a, cs->b, cs->c, cs->d);
    GX_SetTevColorOp(stage, csub ? GX_TEV_SUB : GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, cout);
    GX_SetTevAlphaIn(stage, as->a, as->b, as->c, as->d);
    GX_SetTevAlphaOp(stage, asub ? GX_TEV_SUB : GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, aout);
}

static GXColor color_from(const float *v, bool alpha) {
    GXColor c;
    float r = v[0], g = v[1], b = v[2], a = alpha ? v[3] : 1.0f;
    c.r = r < 0 ? 0 : r > 1 ? 255 : (uint8_t) (r * 255.0f + 0.5f);
    c.g = g < 0 ? 0 : g > 1 ? 255 : (uint8_t) (g * 255.0f + 0.5f);
    c.b = b < 0 ? 0 : b > 1 ? 255 : (uint8_t) (b * 255.0f + 0.5f);
    c.a = a < 0 ? 0 : a > 1 ? 255 : (uint8_t) (a * 255.0f + 0.5f);
    return c;
}

struct DrawCfg {
    int chan0_in, chan1_in;   /* indice de input con color por vertice, o -1 */
    bool fog_on;
    bool textured;
};

static void bind_texture(int tile, int map) {
    struct WiiTex *t = &textures[cur_tex[tile]];
    static GXTexObj obj[2];
    if (!t->data) return;
    uint8_t wrap_s = t->cms & 2 ? GX_CLAMP : (t->cms & 1 ? GX_MIRROR : GX_REPEAT);
    uint8_t wrap_t = t->cmt & 2 ? GX_CLAMP : (t->cmt & 1 ? GX_MIRROR : GX_REPEAT);
    bool pow2 = !(t->w & (t->w - 1)) && !(t->h & (t->h - 1));
    if (!pow2) { wrap_s = GX_CLAMP; wrap_t = GX_CLAMP; }
    GX_InitTexObj(&obj[tile], t->data, t->w, t->h, GX_TF_RGBA8, wrap_s, wrap_t, GX_FALSE);
    GX_InitTexObjLOD(&obj[tile], t->linear ? GX_LINEAR : GX_NEAR, t->linear ? GX_LINEAR : GX_NEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GX_LoadTexObj(&obj[tile], map);
}

static void setup_draw(struct ShaderProgram *p, const float *v0, bool varying[4], struct DrawCfg *cfg, bool edge) {
    const struct CCFeatures *cc = &p->cc;
    int nin = cc->num_inputs;
    cfg->chan0_in = cfg->chan1_in = -1;
    cfg->fog_on = cc->opt_fog;
    cfg->textured = cc->used_textures[0] || cc->used_textures[1];

    for (int i = 0; i < nin; i++) {
        if (!varying[i]) continue;
        if (cfg->chan0_in < 0) cfg->chan0_in = i;
        else if (cfg->chan1_in < 0 && !cfg->fog_on) cfg->chan1_in = i;
    }

    /* registros TEV libres para constantes: REG0, REG2, REG1 (REG1 se reserva si hay 2 texturas) */
    bool two_tex = cc->used_textures[0] && cc->used_textures[1];
    uint8_t regs[3] = {GX_TEVREG0, GX_TEVREG2, GX_TEVREG1};
    int nregs = two_tex ? 2 : 3, ri = 0;
    struct Srcs s;
    memset(&s, 0, sizeof(s));
    int stage = 0;
    int pre_copy_chan1 = -1; uint8_t pre_reg = 0;

    for (int i = 0; i < nin; i++) {
        s.in_c[i] = GX_CC_ZERO; s.in_a[i] = GX_CA_ZERO;
        if (i == cfg->chan0_in) { s.in_c[i] = GX_CC_RASC; s.in_a[i] = GX_CA_RASA; continue; }
        if (ri >= nregs) continue;
        uint8_t reg = regs[ri++];
        uint8_t k = reg == GX_TEVREG0 ? 0 : reg == GX_TEVREG1 ? 1 : 2;
        s.in_c[i] = GX_CC_C0 + 2 * k; s.in_a[i] = GX_CA_A0 + k;
        if (i == cfg->chan1_in) { pre_copy_chan1 = i; pre_reg = reg; }
        else GX_SetTevColor(reg, color_from(v0 + p->off_in[i], cc->opt_alpha));
    }

    /* textura(s) */
    int map0 = cc->used_textures[0] ? GX_TEXMAP0 : GX_TEXMAP1;
    if (cc->used_textures[0]) bind_texture(0, GX_TEXMAP0);
    if (cc->used_textures[1]) bind_texture(1, GX_TEXMAP1);
    s.t0_c = GX_CC_TEXC; s.t0_a = GX_CC_TEXA; s.t1_c = GX_CC_TEXC; s.t1_a = GX_CC_TEXA;
    GX_SetNumTexGens(cfg->textured ? 1 : 0);
    if (cfg->textured) GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

    uint8_t chan0 = cfg->chan0_in >= 0 ? GX_COLOR0A0 : GX_COLORNULL;
    GX_SetNumChans(cfg->chan1_in >= 0 || cfg->fog_on ? 2 : 1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetChanCtrl(GX_COLOR1A1, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);

    struct StageIn pass = {GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO};
    struct StageIn apass = {GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO};

    if (two_tex) {                      /* etapa previa: textura 0 -> REG1 */
        struct StageIn c = {GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC};
        struct StageIn a = {GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA};
        GX_SetTevOrder(stage, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
        set_stage(stage, &c, false, GX_TEVREG1, &a, false, GX_TEVREG1);
        s.t0_c = GX_CC_C1; s.t0_a = GX_CC_A1;
        stage++;
    }
    if (pre_copy_chan1 >= 0) {          /* etapa previa: color por vertice 2 -> registro */
        struct StageIn c = {GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC};
        struct StageIn a = {GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA};
        GX_SetTevOrder(stage, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR1A1);
        set_stage(stage, &c, false, pre_reg, &a, false, pre_reg);
        stage++;
    }

    struct StageIn cs[2], as[2];
    uint8_t csub[2], asub[2];
    int nc = build_formula(cc->c[0], cc->do_single[0], cc->do_multiply[0], cc->do_mix[0], false, &s, cs, csub);
    int na = cc->opt_alpha ? build_formula(cc->c[1], cc->do_single[1], cc->do_multiply[1], cc->do_mix[1], true, &s, as, asub) : 1;
    if (!cc->opt_alpha) { as[0] = (struct StageIn){GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO}; asub[0] = 0; }
    if (cc->opt_alpha && cc->color_alpha_same) { /* misma formula: reusa */ }
    int n = nc > na ? nc : na;
    for (int k = 0; k < n; k++) {
        int ci = k - (n - nc), ai = k - (n - na);       /* alinea al final */
        const struct StageIn *cp = ci >= 0 ? &cs[ci] : &pass;
        const struct StageIn *ap = ai >= 0 ? &as[ai] : &apass;
        bool csb = ci >= 0 ? csub[ci] : false, asb = ai >= 0 ? asub[ai] : false;
        GX_SetTevOrder(stage, cfg->textured ? GX_TEXCOORD0 : GX_TEXCOORDNULL,
                       cfg->textured ? map0 : GX_TEXMAP_NULL, chan0);
        if (two_tex) GX_SetTevOrder(stage, GX_TEXCOORD0, GX_TEXMAP1, chan0);
        set_stage(stage, cp, csb, GX_TEVPREV, ap, asb, GX_TEVPREV);
        stage++;
    }
    if (cfg->fog_on) {                  /* mezcla de niebla: lerp(prev, fog.rgb, fog.a) */
        struct StageIn c = {GX_CC_CPREV, GX_CC_RASC, GX_CC_RASA, GX_CC_ZERO};
        struct StageIn a = {GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV};
        GX_SetTevOrder(stage, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR1A1);
        set_stage(stage, &c, false, GX_TEVPREV, &a, false, GX_TEVPREV);
        stage++;
    }
    GX_SetNumTevStages(stage);

    /* alfa: borde de textura = prueba alfa; si no, mezcla */
    if (edge && cc->opt_alpha) {
        GX_SetAlphaCompare(GX_GREATER, 77, GX_AOP_AND, GX_ALWAYS, 0);
        GX_SetZCompLoc(GX_FALSE);
        GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    } else {
        GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
        GX_SetZCompLoc(GX_TRUE);
        if (use_alpha && cc->opt_alpha)
            GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        else
            GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    }

    /* descriptor de vertices */
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    if (cfg->chan0_in >= 0) GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    if (cfg->chan1_in >= 0 || cfg->fog_on) GX_SetVtxDesc(GX_VA_CLR1, GX_DIRECT);
    if (cfg->textured) GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR1, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
}

static void load_projection(bool ortho) {
    Mtx44 m;
    memset(m, 0, sizeof(m));
    if (ortho) {
        guOrtho(m, 1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 1.0f);   /* z en [0,1] -> Z = -z */
        GX_LoadProjectionMtx(m, GX_ORTHOGRAPHIC);
    } else {
        /* x' = x, y' = y, z' = e*Z + f, w' = -Z   (Z = -w) */
        float n = PERSP_NEAR, f = PERSP_FAR;
        m[0][0] = 1.0f; m[1][1] = 1.0f;
        m[2][2] = -n / (f - n);          /* GX recorta z' en [-w', 0]: cerca = -1, lejos = 0 */
        m[2][3] = -(f * n) / (f - n);
        m[3][2] = -1.0f;
        GX_LoadProjectionMtx(m, GX_PERSPECTIVE);
    }
}

static inline uint8_t f2b(float v) { return v <= 0 ? 0 : v >= 1 ? 255 : (uint8_t) (v * 255.0f + 0.5f); }

static void draw_triangles(float buf[], size_t len, size_t num_tris) {
    (void) len;
    struct ShaderProgram *p = cur_prg;
    if (!p || num_tris == 0) return;
    size_t nf = p->num_floats, nv = num_tris * 3;
    const struct CCFeatures *cc = &p->cc;

    /* 2D (w == 1 en todos) o 3D */
    bool ortho = true;
    for (size_t i = 0; i < nv; i++) if (buf[i * nf + 3] != 1.0f) { ortho = false; break; }

    /* inputs que varian entre vertices */
    bool varying[4] = {false, false, false, false};
    int comps = cc->opt_alpha ? 4 : 3;
    for (int k = 0; k < cc->num_inputs; k++)
        for (size_t i = 1; i < nv && !varying[k]; i++)
            for (int c = 0; c < comps; c++)
                if (buf[i * nf + p->off_in[k] + c] != buf[p->off_in[k] + c]) { varying[k] = true; break; }

    struct DrawCfg cfg;
    setup_draw(p, buf, varying, &cfg, cc->opt_texture_edge);

    GX_SetZMode(depth_test ? GX_TRUE : GX_FALSE, GX_LEQUAL, depth_mask ? GX_TRUE : GX_FALSE);
    GX_SetCoPlanar(zmode_decal ? GX_ENABLE : GX_DISABLE);
    load_projection(ortho);

    GX_Begin(GX_TRIANGLES, GX_VTXFMT0, (u16) nv);
    for (size_t i = 0; i < nv; i++) {
        const float *v = buf + i * nf;
        if (ortho) GX_Position3f32(v[0], v[1], -(0.001f + 0.998f * (v[2] * 0.5f + 0.5f)));
        else       GX_Position3f32(v[0], v[1], -v[3]);
        if (cfg.chan0_in >= 0) {
            const float *c = v + p->off_in[cfg.chan0_in];
            GX_Color4u8(f2b(c[0]), f2b(c[1]), f2b(c[2]), cc->opt_alpha ? f2b(c[3]) : 255);
        }
        if (cfg.chan1_in >= 0) {
            const float *c = v + p->off_in[cfg.chan1_in];
            GX_Color4u8(f2b(c[0]), f2b(c[1]), f2b(c[2]), cc->opt_alpha ? f2b(c[3]) : 255);
        } else if (cfg.fog_on) {
            const float *f = v + p->off_fog;
            GX_Color4u8(f2b(f[0]), f2b(f[1]), f2b(f[2]), f2b(f[3]));
        }
        if (cfg.textured) GX_TexCoord2f32(v[p->off_tex], v[p->off_tex + 1]);
    }
    GX_End();
}

/* ------------------------------------------------------------------ init / frame */
static void *gx_fifo;

static void rapi_init(void) {
    gx_fifo = memalign(32, FIFO_SIZE);
    memset(gx_fifo, 0, FIFO_SIZE);
    GX_Init(gx_fifo, FIFO_SIZE);
    GXColor bg = {0, 0, 0, 255};
    GX_SetCopyClear(bg, GX_MAX_Z24);
    GX_SetViewport(0, 0, wii_rmode->fbWidth, wii_rmode->efbHeight, 0, 1);
    GX_SetDispCopyYScale((f32) wii_rmode->xfbHeight / (f32) wii_rmode->efbHeight);
    GX_SetScissor(0, 0, wii_rmode->fbWidth, wii_rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, wii_rmode->fbWidth, wii_rmode->efbHeight);
    GX_SetDispCopyDst(wii_rmode->fbWidth, wii_rmode->xfbHeight);
    GX_SetCopyFilter(wii_rmode->aa, wii_rmode->sample_pattern, GX_TRUE, wii_rmode->vfilter);
    GX_SetFieldMode(wii_rmode->field_rendering, wii_rmode->viHeight == 2 * wii_rmode->xfbHeight ? GX_ENABLE : GX_DISABLE);
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_FALSE);
    Mtx id;
    guMtxIdentity(id);
    GX_LoadPosMtxImm(id, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);
    GX_SetNumTexGens(0);
    GX_CopyDisp(wii_xfb[0], GX_TRUE);   /* primer borrado */
    GX_Flush();
}

static void rapi_on_resize(void) {}
static void rapi_start_frame(void) {
    GX_SetViewport(0, 0, 640, 480, 0, 1);
    GX_SetScissor(0, 0, 640, 480);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_InvalidateTexAll();
}
static void rapi_end_frame(void) {}
static void rapi_finish_render(void) {}

struct GfxRenderingAPI gfx_wii_rapi = {
    z_is_from_0_to_1, unload_shader, load_shader, create_and_load_new_shader, lookup_shader, shader_get_info,
    new_texture, select_texture, upload_texture, set_sampler_parameters, set_depth_test, set_depth_mask,
    set_zmode_decal, set_viewport, set_scissor, set_use_alpha, draw_triangles, rapi_init, rapi_on_resize,
    rapi_start_frame, rapi_end_frame, rapi_finish_render,
};

/* ------------------------------------------------------------------ gestor de ventana */
static int fb_index;

static void wm_init(const char *name, bool fs) {
    (void) name; (void) fs;
    wii_logical_h = 480;
    wii_logical_w = wii_widescreen ? 853 : 640;   /* 16:9 sin estirar: el juego ensancha el campo de vision */
}
static void wm_set_keyboard_callbacks(bool (*d)(int), bool (*u)(int), void (*a)(void)) { (void) d; (void) u; (void) a; }
static void wm_set_fullscreen_changed_callback(void (*cb)(bool)) { (void) cb; }
static void wm_set_fullscreen(bool e) { (void) e; }
static void wm_main_loop(void (*run_one_game_iter)(void)) {
    run_one_game_iter();
    VIDEO_WaitVSync();   /* 60 Hz / 2 = 30 fps como en N64 */
}
static void wm_get_dimensions(uint32_t *w, uint32_t *h) { *w = wii_logical_w; *h = wii_logical_h; }
static void wm_handle_events(void) { wii_input_poll(); if (wii_home_pressed()) exit(0); }
static bool wm_start_frame(void) { return true; }
static void wm_swap_begin(void) {
    GX_DrawDone();
    fb_index ^= 1;
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(wii_xfb[fb_index], GX_TRUE);
}
static void wm_swap_end(void) {
    GX_Flush();
    VIDEO_SetNextFramebuffer(wii_xfb[fb_index]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    /* el audio marca el ritmo: si hay demasiado sonido en cola, espera */
    for (int i = 0; i < 6 && wii_audio_fill() > 4400; i++) VIDEO_WaitVSync();
}
static double wm_get_time(void) { return (double) ticks_to_millisecs(gettime()) / 1000.0; }

struct GfxWindowManagerAPI gfx_wii_wm = {
    wm_init, wm_set_keyboard_callbacks, wm_set_fullscreen_changed_callback, wm_set_fullscreen, wm_main_loop,
    wm_get_dimensions, wm_handle_events, wm_start_frame, wm_swap_begin, wm_swap_end, wm_get_time,
};
