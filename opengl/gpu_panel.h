#ifndef GPU_PANEL_H
#define GPU_PANEL_H

#include <stdint.h>
#include <stdbool.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <xf86drmMode.h>

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

extern bool g_pv_use_longjmp;
extern jmp_buf g_pv_error_jmp;

#define LOG(...)  do { fprintf(stderr, "[piverse-gl] " __VA_ARGS__); fprintf(stderr, "\n"); } while (0)
#define DIE(...)  do { \
    LOG("FATAL: " __VA_ARGS__); \
    if (g_pv_use_longjmp) longjmp(g_pv_error_jmp, 1); \
    exit(1); \
} while (0)

/* ---- device auto-detection ---- */

/* First /dev/dri/renderD1xx found (malloc'd path), or NULL if none exists. */
char *find_gpu_render_node(void);

/* First /dev/dri/cardN with a connected connector (malloc'd path), or NULL. */
char *find_connected_card(void);

/* ---- GPU: EGL + GLES2 off-screen rendering ---- */

typedef struct {
    int gpu_fd;
    struct gbm_device *gbm;
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface; /* always EGL_NO_SURFACE - we render fully surfaceless */
    GLuint fbo;
    GLuint color_tex;
    int width, height;
} GpuCtx;

void gpu_init(GpuCtx *ctx, const char *render_node, int width, int height);

void gpu_begin_frame(GpuCtx *ctx);

void gpu_end_frame(GpuCtx *ctx, uint8_t *out_rgba);

void gpu_render_solid_frame(GpuCtx *ctx, float r, float g, float b, uint8_t *out_rgba);

void convert_rgba_for_panel(const uint8_t *rgba, int width, int height,
                             uint32_t drm_format, uint8_t *out);

/* ---- panel: DRM dumb-buffer double buffering + page flip ---- */

typedef struct {
    int fd;
    uint32_t connector_id;
    uint32_t crtc_id;
    drmModeModeInfo mode;
    uint32_t fb_id[2];
    uint32_t handle[2];
    uint8_t *map[2];
    uint32_t pitch[2];
    int width, height;
    int front;            /* index of the buffer currently on-screen */
    uint32_t drm_format;   /* DRM_FORMAT_RGB565 or DRM_FORMAT_XRGB8888 */
    int bpp;                /* 16 or 32, matches drm_format */
} PanelCtx;

void panel_init(PanelCtx *p, const char *card_path);

void panel_present(PanelCtx *p, const uint8_t *packed_pixels);

#endif
