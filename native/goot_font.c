/*
 * Goot — FreeType2 + Fontconfig + HarfBuzz Font & Glyph Cache Bridge
 * Uses a bounded open-addressed glyph cache to keep RAM tiny and avoid
 * per-frame rasterization or heap churn.
 */

#define _POSIX_C_SOURCE 200809L

#include "goot_native.h"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

#include <stdlib.h>
#include <string.h>

#define GLYPH_CACHE_CAP 4096 /* Power of 2, bounded RAM */

typedef struct {
    uint64_t     key; /* (codepoint << 8) | style_flags */
    bool         used;
    goot_glyph_t glyph;
} glyph_entry_t;

static FT_Library    g_ft         = NULL;
static FT_Face       g_face_reg   = NULL;
static FT_Face       g_face_bold  = NULL;
static FT_Face       g_face_ital  = NULL;
static FT_Face       g_face_emoji = NULL;
static hb_font_t    *g_hb_reg     = NULL;

static char          g_family[128] = "monospace";
static int32_t       g_size_pt     = 11;
static int32_t       g_dpi         = 96;
static int32_t       g_line_space  = 0;
static int32_t       g_cell_w      = 8;
static int32_t       g_cell_h      = 16;
static int32_t       g_baseline    = 12;

#define MAX_FALLBACK_FACES 16
static FT_Face g_fallback_faces[MAX_FALLBACK_FACES];
static int32_t g_fallback_count = 0;

static FT_Face find_fallback_face(uint32_t codepoint) {
    for (int32_t i = 0; i < g_fallback_count; i++) {
        if (g_fallback_faces[i] && FT_Get_Char_Index(g_fallback_faces[i], codepoint) != 0) {
            return g_fallback_faces[i];
        }
    }
    if (g_fallback_count >= MAX_FALLBACK_FACES) {
        return NULL;
    }

    FcPattern *pat = FcPatternCreate();
    if (!pat) return NULL;
    FcCharSet *cs = FcCharSetCreate();
    if (!cs) { FcPatternDestroy(pat); return NULL; }
    FcCharSetAddChar(cs, codepoint);
    FcPatternAddCharSet(pat, FC_CHARSET, cs);
    FcCharSetDestroy(cs);
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult res;
    FcPattern *match = FcFontMatch(NULL, pat, &res);
    FcPatternDestroy(pat);
    if (!match) return NULL;

    FcChar8 *file = NULL;
    FT_Face face = NULL;
    if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
        if (FT_New_Face(g_ft, (const char *)file, 0, &face) == 0) {
            if (FT_HAS_FIXED_SIZES(face) && face->num_fixed_sizes > 0) {
                FT_Select_Size(face, 0);
            } else {
                FT_Set_Char_Size(face, 0, (FT_F26Dot6)g_size_pt * 64, (FT_UInt)g_dpi, (FT_UInt)g_dpi);
            }
            g_fallback_faces[g_fallback_count++] = face;
        }
    }
    FcPatternDestroy(match);
    return face;
}

static glyph_entry_t g_cache[GLYPH_CACHE_CAP];
static const goot_glyph_t *g_ascii_cache[128][4];

static void clear_glyph_cache(void) {
    memset((void *)g_ascii_cache, 0, sizeof(g_ascii_cache));
    for (int i = 0; i < GLYPH_CACHE_CAP; i++) {
        if (g_cache[i].used && g_cache[i].glyph.bitmap) {
            free(g_cache[i].glyph.bitmap);
        }
        memset(&g_cache[i], 0, sizeof(g_cache[i]));
    }
}

static FT_Face load_fc_face(const char *family, int weight, int slant, int size_pt, int dpi) {
    FcPattern *pat = FcNameParse((const FcChar8 *)family);
    if (!pat) return NULL;
    FcPatternAddInteger(pat, FC_WEIGHT, weight);
    FcPatternAddInteger(pat, FC_SLANT, slant);
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult res;
    FcPattern *match = FcFontMatch(NULL, pat, &res);
    FcPatternDestroy(pat);
    if (!match) return NULL;

    FcChar8 *file = NULL;
    FT_Face face = NULL;
    if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
        if (FT_New_Face(g_ft, (const char *)file, 0, &face) == 0) {
            if (FT_HAS_FIXED_SIZES(face) && face->num_fixed_sizes > 0) {
                FT_Select_Size(face, 0);
            } else {
                FT_Set_Char_Size(face, 0, (FT_F26Dot6)size_pt * 64, (FT_UInt)dpi, (FT_UInt)dpi);
            }
        }
    }
    FcPatternDestroy(match);
    return face;
}

static void update_metrics(void) {
    if (!g_face_reg) return;
    FT_Load_Char(g_face_reg, 'M', FT_LOAD_DEFAULT);
    int32_t adv = (int32_t)(g_face_reg->glyph->advance.x >> 6);
    int32_t asc = (int32_t)(g_face_reg->size->metrics.ascender >> 6);
    int32_t desc = (int32_t)(-g_face_reg->size->metrics.descender >> 6);
    int32_t h = (int32_t)(g_face_reg->size->metrics.height >> 6);
    if (h < asc + desc) h = asc + desc;
    g_cell_w   = adv > 0 ? adv : 8;
    g_cell_h   = (h > 0 ? h : 16) + g_line_space;
    g_baseline = asc > 0 ? asc : (g_cell_h - 3);
}

int32_t goot_font_init(goot_const_bytes_t family, int32_t size_pt, int32_t dpi, int32_t line_spacing_px) {
    if (!FcInit()) return -1;
    if (FT_Init_FreeType(&g_ft) != 0) return -1;

    if (family.data && family.len > 0 && family.len < (int64_t)sizeof(g_family)) {
        memcpy(g_family, family.data, (size_t)family.len);
        g_family[family.len] = '\0';
    }
    g_size_pt    = (size_pt >= 6 && size_pt <= 72) ? size_pt : 11;
    g_dpi        = (dpi >= 72 && dpi <= 384) ? dpi : 96;
    g_line_space = (line_spacing_px >= 0 && line_spacing_px <= 32) ? line_spacing_px : 0;

    g_face_reg   = load_fc_face(g_family, FC_WEIGHT_REGULAR, FC_SLANT_ROMAN, g_size_pt, g_dpi);
    if (!g_face_reg) {
        g_face_reg = load_fc_face("monospace", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN, g_size_pt, g_dpi);
    }
    if (!g_face_reg) return -1;

    g_face_bold  = load_fc_face(g_family, FC_WEIGHT_BOLD, FC_SLANT_ROMAN, g_size_pt, g_dpi);
    g_face_ital  = load_fc_face(g_family, FC_WEIGHT_REGULAR, FC_SLANT_ITALIC, g_size_pt, g_dpi);
    g_face_emoji = load_fc_face("emoji", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN, g_size_pt, g_dpi);
    g_hb_reg     = hb_ft_font_create_referenced(g_face_reg);

    update_metrics();
    return 0;
}

int32_t goot_font_resize(int32_t size_pt, int32_t dpi) {
    if (!g_face_reg) return -1;
    if (size_pt < 6 || size_pt > 72 || dpi < 72 || dpi > 384) return -1;
    g_size_pt = size_pt;
    g_dpi     = dpi;
    FT_Set_Char_Size(g_face_reg, 0, (FT_F26Dot6)g_size_pt * 64, (FT_UInt)g_dpi, (FT_UInt)g_dpi);
    if (g_face_bold) FT_Set_Char_Size(g_face_bold, 0, (FT_F26Dot6)g_size_pt * 64, (FT_UInt)g_dpi, (FT_UInt)g_dpi);
    if (g_face_ital) FT_Set_Char_Size(g_face_ital, 0, (FT_F26Dot6)g_size_pt * 64, (FT_UInt)g_dpi, (FT_UInt)g_dpi);
    for (int32_t i = 0; i < g_fallback_count; i++) {
        if (g_fallback_faces[i] && !(FT_HAS_FIXED_SIZES(g_fallback_faces[i]) && g_fallback_faces[i]->num_fixed_sizes > 0)) {
            FT_Set_Char_Size(g_fallback_faces[i], 0, (FT_F26Dot6)g_size_pt * 64, (FT_UInt)g_dpi, (FT_UInt)g_dpi);
        }
    }
    if (g_hb_reg) hb_ft_font_changed(g_hb_reg);
    clear_glyph_cache();
    update_metrics();
    return 0;
}

int32_t goot_font_cell_width(void)  { return g_cell_w; }
int32_t goot_font_cell_height(void) { return g_cell_h; }
int32_t goot_font_baseline(void)    { return g_baseline; }

const goot_glyph_t *goot_font_get_glyph(uint32_t codepoint, uint8_t style_flags) {
    uint8_t style = style_flags & 0x03U;
    if (codepoint < 128 && g_ascii_cache[codepoint][style]) {
        return g_ascii_cache[codepoint][style];
    }

    uint64_t key = ((uint64_t)codepoint << 8) | (uint64_t)style;
    uint32_t idx = (uint32_t)((key * 2654435761ULL) & (GLYPH_CACHE_CAP - 1));

    for (uint32_t probe = 0; probe < 16; probe++) {
        uint32_t slot = (idx + probe) & (GLYPH_CACHE_CAP - 1);
        if (g_cache[slot].used && g_cache[slot].key == key) {
            if (codepoint < 128) {
                g_ascii_cache[codepoint][style] = &g_cache[slot].glyph;
            }
            return &g_cache[slot].glyph;
        }
        if (!g_cache[slot].used) {
            idx = slot;
            break;
        }
    }

    /* Evict target slot if occupied */
    if (g_cache[idx].used) {
        uint32_t old_cp = (uint32_t)(g_cache[idx].key >> 8);
        uint8_t old_st = (uint8_t)(g_cache[idx].key & 0x03);
        if (old_cp < 128) {
            g_ascii_cache[old_cp][old_st] = NULL;
        }
        if (g_cache[idx].glyph.bitmap) {
            free(g_cache[idx].glyph.bitmap);
            g_cache[idx].glyph.bitmap = NULL;
        }
    }

    FT_Face face = g_face_reg;
    if ((style_flags & 1) && g_face_bold)      face = g_face_bold;
    else if ((style_flags & 2) && g_face_ital) face = g_face_ital;

    FT_UInt gid = 0;
    FT_Int32 load_flags = FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL;

    bool is_emoji_range = (codepoint >= 0x1F000 && codepoint <= 0x1FAFF) ||
                          (codepoint >= 0x2600  && codepoint <= 0x27BF)  ||
                          (codepoint >= 0xFE00  && codepoint <= 0xFE0F);
    if (is_emoji_range && g_face_emoji) {
        FT_UInt egid = FT_Get_Char_Index(g_face_emoji, codepoint);
        if (egid != 0) {
            face = g_face_emoji;
            gid  = egid;
            load_flags = FT_LOAD_RENDER | FT_LOAD_COLOR;
        }
    }

    if (gid == 0) {
        gid = FT_Get_Char_Index(face, codepoint);
    }
    if (gid == 0 && g_face_emoji) {
        FT_UInt egid = FT_Get_Char_Index(g_face_emoji, codepoint);
        if (egid != 0) {
            face = g_face_emoji;
            gid  = egid;
            load_flags = FT_LOAD_RENDER | FT_LOAD_COLOR;
        }
    }
    if (gid == 0) {
        FT_Face fb = find_fallback_face(codepoint);
        if (fb) {
            FT_UInt fbgid = FT_Get_Char_Index(fb, codepoint);
            if (fbgid != 0) {
                face = fb;
                gid  = fbgid;
                load_flags = FT_LOAD_RENDER | (FT_HAS_COLOR(fb) ? FT_LOAD_COLOR : FT_LOAD_TARGET_NORMAL);
            }
        }
    }

    if (FT_HAS_COLOR(face)) {
        load_flags = FT_LOAD_RENDER | FT_LOAD_COLOR;
    }

    goot_glyph_t *g = &g_cache[idx].glyph;
    memset(g, 0, sizeof(*g));
    g_cache[idx].key  = key;
    g_cache[idx].used = true;

    if (FT_Load_Glyph(face, gid, load_flags) != 0) {
        return g;
    }

    FT_GlyphSlot slot = face->glyph;
    g->width     = (int32_t)slot->bitmap.width;
    g->height    = (int32_t)slot->bitmap.rows;
    g->bearing_x = (int32_t)slot->bitmap_left;
    g->bearing_y = (int32_t)slot->bitmap_top;
    g->advance   = (int32_t)(slot->advance.x >> 6);
    g->is_color  = (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) ? 1 : 0;

    if (g->width > 0 && g->height > 0 && slot->bitmap.buffer) {
        size_t bpp = g->is_color ? 4U : 1U;
        size_t rowstride = ((size_t)g->width * bpp + 3U) & ~3U;
        g->stride = (int32_t)rowstride;
        size_t bytes = rowstride * (size_t)g->height;
        g->bitmap = (uint8_t *)calloc(1, bytes);
        if (g->bitmap) {
            int32_t pitch = slot->bitmap.pitch;
            for (int32_t row = 0; row < g->height; row++) {
                const uint8_t *src = slot->bitmap.buffer + (size_t)row * (size_t)(pitch >= 0 ? pitch : -pitch);
                memcpy(g->bitmap + (size_t)row * rowstride, src, (size_t)g->width * bpp);
            }
        }
    }
    if (codepoint < 128) {
        g_ascii_cache[codepoint][style] = g;
    }
    return g;
}

const goot_glyph_t *goot_font_shape_ligature(const uint32_t *cps, int32_t len, uint8_t style_flags) {
    if (!g_hb_reg || !cps || len < 2 || len > 4) return NULL;
    FT_Face face = g_face_reg;
    if ((style_flags & 1) && g_face_bold) face = g_face_bold;
    else if ((style_flags & 2) && g_face_ital) face = g_face_ital;

    hb_buffer_t *buf = hb_buffer_create();
    if (!buf) return NULL;
    hb_buffer_add_codepoints(buf, cps, len, 0, len);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(g_hb_reg, buf, NULL, 0);

    unsigned int count = 0;
    hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buf, &count);
    if (count < (unsigned int)len && count > 0 && infos) {
        uint32_t gid = infos[0].codepoint;
        hb_buffer_destroy(buf);
        if (FT_Load_Glyph(face, gid, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) == 0) {
            static goot_glyph_t s_lig;
            static uint8_t *s_lig_bmp = NULL;
            if (s_lig_bmp) { free(s_lig_bmp); s_lig_bmp = NULL; }
            FT_GlyphSlot slot = face->glyph;
            s_lig.width     = (int32_t)slot->bitmap.width;
            s_lig.height    = (int32_t)slot->bitmap.rows;
            s_lig.bearing_x = (int32_t)slot->bitmap_left;
            s_lig.bearing_y = (int32_t)slot->bitmap_top;
            s_lig.advance   = (int32_t)(slot->advance.x >> 6);
            s_lig.is_color  = 0;
            if (s_lig.width > 0 && s_lig.height > 0 && slot->bitmap.buffer) {
                size_t rowstride = ((size_t)s_lig.width + 3U) & ~3U;
                s_lig.stride = (int32_t)rowstride;
                s_lig_bmp = (uint8_t *)calloc(1, rowstride * (size_t)s_lig.height);
                s_lig.bitmap = s_lig_bmp;
                if (s_lig_bmp) {
                    int32_t pitch = slot->bitmap.pitch;
                    for (int32_t r = 0; r < s_lig.height; r++) {
                        const uint8_t *src = slot->bitmap.buffer + (size_t)r * (size_t)(pitch >= 0 ? pitch : -pitch);
                        memcpy(s_lig_bmp + (size_t)r * rowstride, src, (size_t)s_lig.width);
                    }
                }
            }
            return &s_lig;
        }
        return NULL;
    }
    hb_buffer_destroy(buf);
    return NULL;
}

int32_t goot_font_is_rtl(uint32_t cp) {
    return ((cp >= 0x0590 && cp <= 0x08FF) ||
            (cp >= 0xFB1D && cp <= 0xFDFF) ||
            (cp >= 0xFE70 && cp <= 0xFEFF)) ? 1 : 0;
}

void goot_font_close(void) {
    clear_glyph_cache();
    for (int32_t i = 0; i < g_fallback_count; i++) {
        if (g_fallback_faces[i]) {
            FT_Done_Face(g_fallback_faces[i]);
            g_fallback_faces[i] = NULL;
        }
    }
    g_fallback_count = 0;
    if (g_hb_reg)     { hb_font_destroy(g_hb_reg); g_hb_reg = NULL; }
    if (g_face_emoji) { FT_Done_Face(g_face_emoji); g_face_emoji = NULL; }
    if (g_face_ital)  { FT_Done_Face(g_face_ital); g_face_ital = NULL; }
    if (g_face_bold)  { FT_Done_Face(g_face_bold); g_face_bold = NULL; }
    if (g_face_reg)   { FT_Done_Face(g_face_reg); g_face_reg = NULL; }
    if (g_ft)         { FT_Done_FreeType(g_ft); g_ft = NULL; }
    FcFini();
}
