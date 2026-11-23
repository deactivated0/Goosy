/*
 * Goosy — OpenGL / EGL Hardware-Accelerated Rendering Backend
 * Renders terminal cells, text glyph atlas, procedural box-drawing, and overlays
 * using OpenGL ES 2.0+ via EGL and wayland-egl.
 */

#define _POSIX_C_SOURCE 200809L

#include "goosy_render.h"
#include "goosy_image.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_GL_QUADS 32768
#define GL_ATLAS_SIZE 1024

typedef struct {
    float x, y;
    float u, v;
    float r, g, b, a;
} gl_vertex_t;

typedef struct {
    int32_t  used;
    uint32_t codepoint;
    uint8_t  style;
    float    u0, v0, u1, v1;
    int32_t  bearing_x;
    int32_t  bearing_y;
    int32_t  width;
    int32_t  height;
} gl_atlas_slot_t;

static EGLDisplay            g_egl_dpy  = EGL_NO_DISPLAY;
static EGLContext            g_egl_ctx  = EGL_NO_CONTEXT;
static EGLSurface            g_egl_surf = EGL_NO_SURFACE;
static EGLConfig             g_egl_cfg  = NULL;
static struct wl_egl_window *g_egl_win  = NULL;

static GLuint g_prog   = 0;
static GLuint g_vbo    = 0;
static GLuint g_ebo    = 0;
static GLuint g_atlas  = 0;

static GLint  g_loc_pos   = -1;
static GLint  g_loc_tex   = -1;
static GLint  g_loc_col   = -1;
static GLint  g_loc_vp    = -1;
static GLint  g_loc_atlas = -1;

static gl_vertex_t *g_vertices = NULL;
static int32_t      g_quad_count = 0;

static int32_t g_cur_w     = 800;
static int32_t g_cur_h     = 600;
static int32_t g_cur_scale = 1;

/* Simple glyph atlas packing */
static gl_atlas_slot_t g_atlas_slots[2048];
static int32_t         g_atlas_slot_count = 0;
static int32_t         g_atlas_cur_x = 1;
static int32_t         g_atlas_cur_y = 1;
static int32_t         g_atlas_row_h = 0;

static const char *g_vert_src =
    "attribute vec2 in_pos;\n"
    "attribute vec2 in_tex;\n"
    "attribute vec4 in_col;\n"
    "uniform vec2 u_viewport;\n"
    "varying vec2 v_tex;\n"
    "varying vec4 v_col;\n"
    "void main() {\n"
    "    float x = (in_pos.x / u_viewport.x) * 2.0 - 1.0;\n"
    "    float y = 1.0 - (in_pos.y / u_viewport.y) * 2.0;\n"
    "    gl_Position = vec4(x, y, 0.0, 1.0);\n"
    "    v_tex = in_tex;\n"
    "    v_col = in_col;\n"
    "}\n";

static const char *g_frag_src =
    "precision mediump float;\n"
    "varying vec2 v_tex;\n"
    "varying vec4 v_col;\n"
    "uniform sampler2D u_atlas;\n"
    "void main() {\n"
    "    if (v_tex.x < 0.0) {\n"
    "        gl_FragColor = v_col;\n"
    "    } else {\n"
    "        float a = texture2D(u_atlas, v_tex).a;\n"
    "        gl_FragColor = vec4(v_col.rgb, v_col.a * a);\n"
    "    }\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    if (!s) return 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "[goosy-gl] Shader compilation error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static bool gl_init(void) {
    struct wl_display *wl_dpy = goosy_wl_get_display();
    struct wl_surface *wl_surf = goosy_wl_get_surface();
    if (!wl_dpy || !wl_surf) return false;

    g_egl_dpy = eglGetDisplay((EGLNativeDisplayType)wl_dpy);
    if (g_egl_dpy == EGL_NO_DISPLAY) return false;

    EGLint major = 0, minor = 0;
    if (!eglInitialize(g_egl_dpy, &major, &minor)) return false;

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLint num_configs = 0;
    if (!eglChooseConfig(g_egl_dpy, attribs, &g_egl_cfg, 1, &num_configs) || num_configs < 1) {
        return false;
    }

    const EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    g_egl_ctx = eglCreateContext(g_egl_dpy, g_egl_cfg, EGL_NO_CONTEXT, ctx_attribs);
    if (g_egl_ctx == EGL_NO_CONTEXT) return false;

    g_cur_w = goosy_wl_width();
    g_cur_h = goosy_wl_height();
    g_cur_scale = goosy_wl_scale();
    if (g_cur_w <= 0) g_cur_w = 800;
    if (g_cur_h <= 0) g_cur_h = 600;
    if (g_cur_scale <= 0) g_cur_scale = 1;

    g_egl_win = wl_egl_window_create(wl_surf, g_cur_w * g_cur_scale, g_cur_h * g_cur_scale);
    if (!g_egl_win) return false;

    g_egl_surf = eglCreateWindowSurface(g_egl_dpy, g_egl_cfg, (EGLNativeWindowType)g_egl_win, NULL);
    if (g_egl_surf == EGL_NO_SURFACE) return false;

    if (!eglMakeCurrent(g_egl_dpy, g_egl_surf, g_egl_surf, g_egl_ctx)) return false;

    /* Build Shader Program */
    GLuint vs = compile_shader(GL_VERTEX_SHADER, g_vert_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, g_frag_src);
    if (!vs || !fs) return false;

    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &linked);
    if (!linked) return false;

    g_loc_pos   = glGetAttribLocation(g_prog, "in_pos");
    g_loc_tex   = glGetAttribLocation(g_prog, "in_tex");
    g_loc_col   = glGetAttribLocation(g_prog, "in_col");
    g_loc_vp    = glGetUniformLocation(g_prog, "u_viewport");
    g_loc_atlas = glGetUniformLocation(g_prog, "u_atlas");

    /* Allocate Dynamic Vertex Buffer */
    g_vertices = (gl_vertex_t *)malloc(sizeof(gl_vertex_t) * MAX_GL_QUADS * 4);
    if (!g_vertices) return false;

    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(gl_vertex_t) * MAX_GL_QUADS * 4, NULL, GL_DYNAMIC_DRAW);

    /* Build Static Index Buffer for Quads */
    GLushort *indices = (GLushort *)malloc(sizeof(GLushort) * MAX_GL_QUADS * 6);
    if (indices) {
        for (int32_t i = 0; i < MAX_GL_QUADS; i++) {
            GLushort base = (GLushort)(i * 4);
            indices[i * 6 + 0] = base + 0;
            indices[i * 6 + 1] = base + 1;
            indices[i * 6 + 2] = base + 2;
            indices[i * 6 + 3] = base + 2;
            indices[i * 6 + 4] = base + 3;
            indices[i * 6 + 5] = base + 0;
        }
        glGenBuffers(1, &g_ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLushort) * MAX_GL_QUADS * 6, indices, GL_STATIC_DRAW);
        free(indices);
    }

    /* Build Glyph Texture Atlas */
    glGenTextures(1, &g_atlas);
    glBindTexture(GL_TEXTURE_2D, g_atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    uint8_t *empty_atlas = (uint8_t *)calloc(1, GL_ATLAS_SIZE * GL_ATLAS_SIZE);
    if (empty_atlas) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, GL_ATLAS_SIZE, GL_ATLAS_SIZE, 0, GL_ALPHA, GL_UNSIGNED_BYTE, empty_atlas);
        free(empty_atlas);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

static void gl_push_quad(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         uint32_t argb) {
    if (g_quad_count >= MAX_GL_QUADS) return;
    float a = ((argb >> 24) & 0xFF) / 255.0f;
    float r = ((argb >> 16) & 0xFF) / 255.0f;
    float g = ((argb >> 8)  & 0xFF) / 255.0f;
    float b = (argb & 0xFF)         / 255.0f;

    int32_t idx = g_quad_count * 4;
    g_vertices[idx + 0] = (gl_vertex_t){ x,     y,     u0, v0, r, g, b, a };
    g_vertices[idx + 1] = (gl_vertex_t){ x + w, y,     u1, v0, r, g, b, a };
    g_vertices[idx + 2] = (gl_vertex_t){ x + w, y + h, u1, v1, r, g, b, a };
    g_vertices[idx + 3] = (gl_vertex_t){ x,     y + h, u0, v1, r, g, b, a };
    g_quad_count++;
}

static void gl_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb) {
    if (w <= 0 || h <= 0) return;
    uint32_t argb = 0xFF000000U | (rgb & 0x00FFFFFFU);
    gl_push_quad((float)x, (float)y, (float)w, (float)h, -1.0f, -1.0f, -1.0f, -1.0f, argb);
}

/* Cache glyph bitmap in OpenGL Texture Atlas */
static gl_atlas_slot_t *gl_get_or_upload_glyph(uint32_t cp, uint8_t style) {
    for (int32_t i = 0; i < g_atlas_slot_count; i++) {
        if (g_atlas_slots[i].used && g_atlas_slots[i].codepoint == cp && g_atlas_slots[i].style == style) {
            return &g_atlas_slots[i];
        }
    }

    const goosy_glyph_t *g = goosy_font_get_glyph(cp, style);
    if (!g || !g->bitmap || g->width <= 0 || g->height <= 0) return NULL;

    if (g_atlas_cur_x + g->width + 1 >= GL_ATLAS_SIZE) {
        g_atlas_cur_x = 1;
        g_atlas_cur_y += g_atlas_row_h + 1;
        g_atlas_row_h = 0;
    }
    if (g_atlas_cur_y + g->height + 1 >= GL_ATLAS_SIZE) {
        /* Atlas full: reset packaging */
        g_atlas_slot_count = 0;
        g_atlas_cur_x = 1;
        g_atlas_cur_y = 1;
        g_atlas_row_h = 0;
    }
    if (g_atlas_slot_count >= 2048) {
        g_atlas_slot_count = 0;
    }

    int32_t gx = g_atlas_cur_x;
    int32_t gy = g_atlas_cur_y;
    glBindTexture(GL_TEXTURE_2D, g_atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, gx, gy, g->width, g->height, GL_ALPHA, GL_UNSIGNED_BYTE, g->bitmap);

    gl_atlas_slot_t *slot = &g_atlas_slots[g_atlas_slot_count++];
    slot->used = 1;
    slot->codepoint = cp;
    slot->style = style;
    slot->u0 = (float)gx / (float)GL_ATLAS_SIZE;
    slot->v0 = (float)gy / (float)GL_ATLAS_SIZE;
    slot->u1 = (float)(gx + g->width)  / (float)GL_ATLAS_SIZE;
    slot->v1 = (float)(gy + g->height) / (float)GL_ATLAS_SIZE;
    slot->bearing_x = g->bearing_x;
    slot->bearing_y = g->bearing_y;
    slot->width = g->width;
    slot->height = g->height;

    g_atlas_cur_x += g->width + 1;
    if (g->height > g_atlas_row_h) g_atlas_row_h = g->height;
    return slot;
}

/* Procedural Box-Drawing & Block Elements (Identical geometry to Pixman for 100% parity) */
static bool gl_render_procedural_glyph(int32_t px, int32_t py, int32_t w, int32_t h,
                                       uint32_t cp, uint32_t fg_rgb, uint32_t bg_rgb) {
    (void)bg_rgb;
    if (cp < 0x2500 || cp > 0x259F) return false;

    int32_t t = h >= 28 ? 2 : 1;
    int32_t th = t * 2;
    int32_t mx = px + w / 2;
    int32_t my = py + h / 2;

    if (cp >= 0x2580 && cp <= 0x259F) {
        switch (cp) {
            case 0x2580: gl_fill_rect(px, py, w, h / 2, fg_rgb); return true;
            case 0x2581: gl_fill_rect(px, py + h * 7 / 8, w, h - h * 7 / 8, fg_rgb); return true;
            case 0x2582: gl_fill_rect(px, py + h * 3 / 4, w, h - h * 3 / 4, fg_rgb); return true;
            case 0x2583: gl_fill_rect(px, py + h * 5 / 8, w, h - h * 5 / 8, fg_rgb); return true;
            case 0x2584: gl_fill_rect(px, py + h / 2, w, h - h / 2, fg_rgb); return true;
            case 0x2585: gl_fill_rect(px, py + h * 3 / 8, w, h - h * 3 / 8, fg_rgb); return true;
            case 0x2586: gl_fill_rect(px, py + h / 4, w, h - h / 4, fg_rgb); return true;
            case 0x2587: gl_fill_rect(px, py + h / 8, w, h - h / 8, fg_rgb); return true;
            case 0x2588: gl_fill_rect(px, py, w, h, fg_rgb); return true;
            case 0x2589: gl_fill_rect(px, py, w * 7 / 8, h, fg_rgb); return true;
            case 0x258A: gl_fill_rect(px, py, w * 3 / 4, h, fg_rgb); return true;
            case 0x258B: gl_fill_rect(px, py, w * 5 / 8, h, fg_rgb); return true;
            case 0x258C: gl_fill_rect(px, py, w / 2, h, fg_rgb); return true;
            case 0x258D: gl_fill_rect(px, py, w * 3 / 8, h, fg_rgb); return true;
            case 0x258E: gl_fill_rect(px, py, w / 4, h, fg_rgb); return true;
            case 0x258F: gl_fill_rect(px, py, w / 8, h, fg_rgb); return true;
            case 0x2590: gl_fill_rect(px + w / 2, py, w - w / 2, h, fg_rgb); return true;
            case 0x2594: gl_fill_rect(px, py, w, h / 8, fg_rgb); return true;
            case 0x2595: gl_fill_rect(px + w * 7 / 8, py, w - w * 7 / 8, h, fg_rgb); return true;
            case 0x2591:
            case 0x2592:
            case 0x2593: {
                uint32_t step = (cp == 0x2591) ? 4 : ((cp == 0x2592) ? 2 : 1);
                for (int32_t dy = 0; dy < h; dy++) {
                    for (int32_t dx = 0; dx < w; dx++) {
                        if (((dx + dy) % step) == 0) {
                            gl_fill_rect(px + dx, py + dy, 1, 1, fg_rgb);
                        }
                    }
                }
                return true;
            }
            default: return false;
        }
    }

    int32_t l = 0, r = 0, u = 0, d = 0;
    switch (cp) {
        case 0x2500: l = 1; r = 1; break;
        case 0x2501: l = 2; r = 2; break;
        case 0x2502: u = 1; d = 1; break;
        case 0x2503: u = 2; d = 2; break;
        case 0x250C: r = 1; d = 1; break;
        case 0x250D: r = 2; d = 1; break;
        case 0x250E: r = 1; d = 2; break;
        case 0x250F: r = 2; d = 2; break;
        case 0x2510: l = 1; d = 1; break;
        case 0x2511: l = 2; d = 1; break;
        case 0x2512: l = 1; d = 2; break;
        case 0x2513: l = 2; d = 2; break;
        case 0x2514: u = 1; r = 1; break;
        case 0x2515: u = 1; r = 2; break;
        case 0x2516: u = 2; r = 1; break;
        case 0x2517: u = 2; r = 2; break;
        case 0x2518: u = 1; l = 1; break;
        case 0x2519: u = 1; l = 2; break;
        case 0x251A: u = 2; l = 1; break;
        case 0x251B: u = 2; l = 2; break;
        case 0x251C: u = 1; d = 1; r = 1; break;
        case 0x251D: u = 1; d = 1; r = 2; break;
        case 0x2520: u = 2; d = 2; r = 1; break;
        case 0x2523: u = 2; d = 2; r = 2; break;
        case 0x2524: u = 1; d = 1; l = 1; break;
        case 0x2525: u = 1; d = 1; l = 2; break;
        case 0x2528: u = 2; d = 2; l = 1; break;
        case 0x252B: u = 2; d = 2; l = 2; break;
        case 0x252C: l = 1; r = 1; d = 1; break;
        case 0x252F: l = 2; r = 2; d = 1; break;
        case 0x2530: l = 1; r = 1; d = 2; break;
        case 0x2533: l = 2; r = 2; d = 2; break;
        case 0x2534: l = 1; r = 1; u = 1; break;
        case 0x2537: l = 2; r = 2; u = 1; break;
        case 0x2538: l = 1; r = 1; u = 2; break;
        case 0x253B: l = 2; r = 2; u = 2; break;
        case 0x253C: l = 1; r = 1; u = 1; d = 1; break;
        case 0x253F: l = 2; r = 2; u = 1; d = 1; break;
        case 0x2542: l = 1; r = 1; u = 2; d = 2; break;
        case 0x254B: l = 2; r = 2; u = 2; d = 2; break;
        case 0x2550: l = 3; r = 3; break;
        case 0x2551: u = 3; d = 3; break;
        case 0x2554: r = 3; d = 3; break;
        case 0x2557: l = 3; d = 3; break;
        case 0x255A: r = 3; u = 3; break;
        case 0x255D: l = 3; u = 3; break;
        case 0x2560: u = 3; d = 3; r = 3; break;
        case 0x2563: u = 3; d = 3; l = 3; break;
        case 0x2566: l = 3; r = 3; d = 3; break;
        case 0x2569: l = 3; r = 3; u = 3; break;
        case 0x256C: l = 3; r = 3; u = 3; d = 3; break;
        case 0x2574: l = 1; break;
        case 0x2575: u = 1; break;
        case 0x2576: r = 1; break;
        case 0x2577: d = 1; break;
        case 0x2578: l = 2; break;
        case 0x2579: u = 2; break;
        case 0x257A: r = 2; break;
        case 0x257B: d = 2; break;
        default: return false;
    }

    if (l) {
        int32_t lw = l == 3 ? 1 : (l == 2 ? th : t);
        if (l == 3) {
            gl_fill_rect(px, my - t - 1, mx - px + 1, 1, fg_rgb);
            gl_fill_rect(px, my + t, mx - px + 1, 1, fg_rgb);
        } else {
            gl_fill_rect(px, my - lw / 2, mx - px + (r ? 1 : 0), lw, fg_rgb);
        }
    }
    if (r) {
        int32_t rw = r == 3 ? 1 : (r == 2 ? th : t);
        if (r == 3) {
            gl_fill_rect(mx, my - t - 1, px + w - mx, 1, fg_rgb);
            gl_fill_rect(mx, my + t, px + w - mx, 1, fg_rgb);
        } else {
            gl_fill_rect(mx - (l ? 0 : rw / 2), my - rw / 2, px + w - mx + (l ? 0 : rw / 2), rw, fg_rgb);
        }
    }
    if (u) {
        int32_t uw = u == 3 ? 1 : (u == 2 ? th : t);
        if (u == 3) {
            gl_fill_rect(mx - t - 1, py, 1, my - py + 1, fg_rgb);
            gl_fill_rect(mx + t, py, 1, my - py + 1, fg_rgb);
        } else {
            gl_fill_rect(mx - uw / 2, py, uw, my - py + (d ? 1 : 0), fg_rgb);
        }
    }
    if (d) {
        int32_t dw = d == 3 ? 1 : (d == 2 ? th : t);
        if (d == 3) {
            gl_fill_rect(mx - t - 1, my, 1, py + h - my, fg_rgb);
            gl_fill_rect(mx + t, my, 1, py + h - my, fg_rgb);
        } else {
            gl_fill_rect(mx - dw / 2, my - (u ? 0 : dw / 2), dw, py + h - my + (u ? 0 : dw / 2), fg_rgb);
        }
    }
    return true;
}

static void gl_cell_ext(int32_t px, int32_t py, int32_t cell_w, int32_t cell_h,
                        uint32_t codepoint, uint32_t combining, uint32_t fg_rgb, uint32_t bg_rgb,
                        uint32_t attr_flags, int32_t span_cols,
                        uint8_t ul_style, uint32_t ul_color) {
    (void)combining;
    if (span_cols < 1) span_cols = 1;
    int32_t total_w = cell_w * span_cols;

    /* 1. Cell background */
    gl_fill_rect(px, py, total_w, cell_h, bg_rgb);

    /* 2. Procedural Box-Drawing & Block Elements */
    if (gl_render_procedural_glyph(px, py, total_w, cell_h, codepoint, fg_rgb, bg_rgb)) {
        /* rendered procedurally */
    } else if (codepoint > 0x20U) {
        uint8_t style = (uint8_t)(attr_flags & 0x03U);
        gl_atlas_slot_t *slot = gl_get_or_upload_glyph(codepoint, style);
        if (slot) {
            float gx = (float)(px + slot->bearing_x);
            float gy = (float)(py + goosy_font_baseline() - slot->bearing_y);
            uint32_t argb = 0xFF000000U | (fg_rgb & 0x00FFFFFFU);
            gl_push_quad(gx, gy, (float)slot->width, (float)slot->height,
                         slot->u0, slot->v0, slot->u1, slot->v1, argb);
        }
    }

    /* 3. Underline / Strike */
    uint32_t line_color = (ul_color != 0) ? ul_color : fg_rgb;
    int32_t base_y = py + goosy_font_baseline();
    int32_t line_th = cell_h >= 28 ? 2 : 1;

    if (ul_style == 1 || (attr_flags & 0x04U)) {
        int32_t uy = base_y + 2;
        if (uy + line_th > py + cell_h) uy = py + cell_h - line_th;
        gl_fill_rect(px, uy, total_w, line_th, line_color);
    } else if (ul_style == 2) {
        int32_t uy1 = base_y + 1;
        int32_t uy2 = base_y + 3;
        if (uy2 + line_th > py + cell_h) { uy2 = py + cell_h - line_th; uy1 = uy2 - 2; }
        gl_fill_rect(px, uy1, total_w, line_th, line_color);
        gl_fill_rect(px, uy2, total_w, line_th, line_color);
    } else if (ul_style == 3) {
        int32_t uy = base_y + 2;
        if (uy + 3 > py + cell_h) uy = py + cell_h - 3;
        for (int32_t cx = 0; cx < total_w; cx++) {
            int32_t wave = (cx % 4 < 2) ? 0 : 2;
            gl_fill_rect(px + cx, uy + wave, 1, 1, line_color);
        }
    } else if (ul_style == 4) {
        int32_t uy = base_y + 2;
        if (uy + line_th > py + cell_h) uy = py + cell_h - line_th;
        for (int32_t cx = 0; cx < total_w; cx += 3) {
            gl_fill_rect(px + cx, uy, 1, line_th, line_color);
        }
    } else if (ul_style == 5) {
        int32_t uy = base_y + 2;
        if (uy + line_th > py + cell_h) uy = py + cell_h - line_th;
        for (int32_t cx = 0; cx < total_w; cx += 6) {
            int32_t dw = (cx + 3 <= total_w) ? 3 : (total_w - cx);
            gl_fill_rect(px + cx, uy, dw, line_th, line_color);
        }
    }

    if (attr_flags & 0x20U) {
        int32_t sy = py + cell_h / 2;
        gl_fill_rect(px, sy, total_w, line_th, fg_rgb);
    }
}

static void gl_cursor(int32_t cx_px, int32_t cy_px, int32_t cell_w, int32_t cell_h,
                      uint32_t color_rgb, int32_t style) {
    int32_t line_th = (cell_h >= 28) ? 2 : 1;
    switch (style) {
        case 0:
            gl_fill_rect(cx_px, cy_px, cell_w, cell_h, color_rgb);
            break;
        case 1: {
            int32_t uy = cy_px + cell_h - line_th * 2;
            gl_fill_rect(cx_px, uy, cell_w, line_th * 2, color_rgb);
            break;
        }
        case 2:
            gl_fill_rect(cx_px, cy_px, line_th * 2, cell_h, color_rgb);
            break;
        case 3:
            gl_fill_rect(cx_px, cy_px, cell_w, line_th, color_rgb);
            gl_fill_rect(cx_px, cy_px + cell_h - line_th, cell_w, line_th, color_rgb);
            gl_fill_rect(cx_px, cy_px, line_th, cell_h, color_rgb);
            gl_fill_rect(cx_px + cell_w - line_th, cy_px, line_th, cell_h, color_rgb);
            break;
        default:
            gl_fill_rect(cx_px, cy_px, cell_w, cell_h, color_rgb);
            break;
    }
}

static int32_t gl_title_icon(int32_t x, int32_t y, int32_t size_px) {
    (void)x; (void)y; (void)size_px;
    return 0; /* Title bar draws vector fallback prompt >_ */
}

static void gl_images(int32_t pad_px, int32_t title_h, int32_t view_top_abs_row,
                      int32_t rows, int32_t cols, int32_t cell_w, int32_t cell_h) {
    (void)pad_px; (void)title_h; (void)view_top_abs_row;
    (void)rows; (void)cols; (void)cell_w; (void)cell_h;
}

static void gl_damage(int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)x; (void)y; (void)w; (void)h;
}

static int32_t gl_begin(uint32_t bg_rgb) {
    if (!g_egl_dpy || !g_egl_surf || !g_egl_ctx) return -1;
    eglMakeCurrent(g_egl_dpy, g_egl_surf, g_egl_surf, g_egl_ctx);

    int32_t vp_w = g_cur_w * g_cur_scale;
    int32_t vp_h = g_cur_h * g_cur_scale;
    glViewport(0, 0, vp_w, vp_h);

    float r = ((bg_rgb >> 16) & 0xFF) / 255.0f;
    float g = ((bg_rgb >> 8)  & 0xFF) / 255.0f;
    float b = (bg_rgb & 0xFF)         / 255.0f;
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    g_quad_count = 0;
    return 0;
}

static void gl_end(bool request_frame_cb) {
    (void)request_frame_cb;
    if (g_quad_count > 0 && g_vertices) {
        glUseProgram(g_prog);
        glUniform2f(g_loc_vp, (float)(g_cur_w * g_cur_scale), (float)(g_cur_h * g_cur_scale));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_atlas);
        glUniform1i(g_loc_atlas, 0);

        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(gl_vertex_t) * g_quad_count * 4, g_vertices);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ebo);

        glEnableVertexAttribArray((GLuint)g_loc_pos);
        glEnableVertexAttribArray((GLuint)g_loc_tex);
        glEnableVertexAttribArray((GLuint)g_loc_col);

        glVertexAttribPointer((GLuint)g_loc_pos, 2, GL_FLOAT, GL_FALSE, sizeof(gl_vertex_t), (const void *)offsetof(gl_vertex_t, x));
        glVertexAttribPointer((GLuint)g_loc_tex, 2, GL_FLOAT, GL_FALSE, sizeof(gl_vertex_t), (const void *)offsetof(gl_vertex_t, u));
        glVertexAttribPointer((GLuint)g_loc_col, 4, GL_FLOAT, GL_FALSE, sizeof(gl_vertex_t), (const void *)offsetof(gl_vertex_t, r));

        glDrawElements(GL_TRIANGLES, g_quad_count * 6, GL_UNSIGNED_SHORT, 0);

        glDisableVertexAttribArray((GLuint)g_loc_pos);
        glDisableVertexAttribArray((GLuint)g_loc_tex);
        glDisableVertexAttribArray((GLuint)g_loc_col);
    }

    eglSwapBuffers(g_egl_dpy, g_egl_surf);
    if (request_frame_cb) {
        goosy_wl_request_frame_callback();
    }
    g_quad_count = 0;
}

static void gl_resize(int32_t width_px, int32_t height_px, int32_t scale) {
    g_cur_w = width_px;
    g_cur_h = height_px;
    g_cur_scale = scale > 0 ? scale : 1;
    if (g_egl_win) {
        wl_egl_window_resize(g_egl_win, g_cur_w * g_cur_scale, g_cur_h * g_cur_scale, 0, 0);
    }
}

static void gl_cleanup(void) {
    if (g_prog)   { glDeleteProgram(g_prog); g_prog = 0; }
    if (g_vbo)    { glDeleteBuffers(1, &g_vbo); g_vbo = 0; }
    if (g_ebo)    { glDeleteBuffers(1, &g_ebo); g_ebo = 0; }
    if (g_atlas)  { glDeleteTextures(1, &g_atlas); g_atlas = 0; }
    if (g_vertices) { free(g_vertices); g_vertices = NULL; }
    if (g_egl_surf != EGL_NO_SURFACE) { eglDestroySurface(g_egl_dpy, g_egl_surf); g_egl_surf = EGL_NO_SURFACE; }
    if (g_egl_ctx != EGL_NO_CONTEXT)  { eglDestroyContext(g_egl_dpy, g_egl_ctx);  g_egl_ctx = EGL_NO_CONTEXT; }
    if (g_egl_win)  { wl_egl_window_destroy(g_egl_win); g_egl_win = NULL; }
    if (g_egl_dpy != EGL_NO_DISPLAY)  { eglTerminate(g_egl_dpy); g_egl_dpy = EGL_NO_DISPLAY; }
}

goosy_renderer_backend_t g_backend_gl = {
    .name       = "OpenGL / EGL (Hardware)",
    .type       = GOOSY_RENDERER_OPENGL,
    .init       = gl_init,
    .begin      = gl_begin,
    .fill_rect  = gl_fill_rect,
    .cell_ext   = gl_cell_ext,
    .cursor     = gl_cursor,
    .title_icon = gl_title_icon,
    .images     = gl_images,
    .damage     = gl_damage,
    .end        = gl_end,
    .cleanup    = gl_cleanup,
    .resize     = gl_resize
};
