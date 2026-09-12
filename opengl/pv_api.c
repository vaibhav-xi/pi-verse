#define _DEFAULT_SOURCE
#include "pv_api.h"
#include "gpu_panel.h"
#include "lyrics_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static GpuCtx g_gpu;
static PanelCtx g_panel;
static LyricsRenderer g_renderer;
static uint8_t *g_rgba_buf = NULL;
static uint8_t *g_packed_buf = NULL;
static struct timespec g_last_frame_time;
static bool g_initialized = false;

int pv_init(const char *gpu_render_node, const char *panel_device, const char *font_path) {
    if (g_initialized) {
        fprintf(stderr, "[piverse-gl] pv_init() called twice - call pv_shutdown() first\n");
        return -1;
    }

    char * volatile auto_gpu = NULL;
    char * volatile auto_panel = NULL;
    const char * volatile resolved_gpu = gpu_render_node;
    const char * volatile resolved_panel = panel_device;

    g_pv_use_longjmp = true;
    if (setjmp(g_pv_error_jmp) != 0) {
        free(auto_gpu);
        free(auto_panel);
        g_pv_use_longjmp = false;
        return -1;
    }

    if (!resolved_gpu) {
        auto_gpu = find_gpu_render_node();
        if (!auto_gpu) DIE("No /dev/dri/renderD1xx found.");
        resolved_gpu = auto_gpu;
    }
    if (!resolved_panel) {
        auto_panel = find_connected_card();
        if (!auto_panel) DIE("No /dev/dri/cardN with a connected connector found.");
        resolved_panel = auto_panel;
    }

    panel_init(&g_panel, resolved_panel);
    gpu_init(&g_gpu, resolved_gpu, g_panel.width, g_panel.height);

    if (!gfx_init_shader(g_panel.width, g_panel.height)) DIE("gfx_init_shader failed");
    if (!lyrics_renderer_init(&g_renderer, font_path)) DIE("Could not load fonts from %s", font_path);

    size_t pixel_count = (size_t)g_panel.width * g_panel.height;
    g_rgba_buf = malloc(pixel_count * 4);
    g_packed_buf = malloc(pixel_count * 4);
    if (!g_rgba_buf || !g_packed_buf) DIE("out of memory allocating frame buffers");

    free(auto_gpu);
    free(auto_panel);

    clock_gettime(CLOCK_MONOTONIC, &g_last_frame_time);
    g_initialized = true;
    LOG("pv_init OK (%dx%d)", g_panel.width, g_panel.height);
    return 0;
}

int pv_set_album_art(const uint8_t *img_data, size_t img_size) {
    if (!g_initialized) { fprintf(stderr, "[piverse-gl] pv_set_album_art() called before pv_init()\n"); return -1; }

    if (setjmp(g_pv_error_jmp) != 0) return -1;

    if (!img_data || img_size == 0) {
        lyrics_renderer_clear_album_art(&g_renderer);
        return 0;
    }
    if (!lyrics_renderer_set_album_art(&g_renderer, img_data, img_size)) {
        LOG("album art decode failed");
        return -1;
    }
    return 0;
}

void pv_clear_album_art(void) {
    if (!g_initialized) return;
    lyrics_renderer_clear_album_art(&g_renderer);
}

int pv_render_frame(
    bool has_track,
    const char *track_id,
    const char *track_name,
    const char *artist_name,
    long duration_ms,
    long progress_ms,
    bool is_playing,
    const SyncedLine *synced_lines,
    int synced_count,
    const char *plain_lyrics,
    bool instrumental,
    bool has_lyrics_data,
    bool found
) {
    if (!g_initialized) { fprintf(stderr, "[piverse-gl] pv_render_frame() called before pv_init()\n"); return -1; }

    if (setjmp(g_pv_error_jmp) != 0) return -1;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    float dt = (float)(now.tv_sec - g_last_frame_time.tv_sec) +
               (float)(now.tv_nsec - g_last_frame_time.tv_nsec) / 1e9f;
    g_last_frame_time = now;
    if (dt < 0.0f || dt > 1.0f) dt = 0.0f; /* clock jump or first call: don't jerk the marquee */

    TrackSnapshot snap = {
        .has_track = has_track,
        .track_name = track_name,
        .artist_name = artist_name,
        .duration_ms = duration_ms,
        .progress_ms = progress_ms,
        .is_playing = is_playing,
        .track_id = track_id,
    };

    LyricsState lyrics = {
        .has_data = has_lyrics_data,
        .synced = synced_lines,
        .synced_count = synced_count,
        .plain = plain_lyrics,
        .instrumental = instrumental,
        .found = found,
    };

    gpu_begin_frame(&g_gpu);
    lyrics_render_frame(&g_renderer, g_panel.width, g_panel.height, dt, &snap, &lyrics);
    gpu_end_frame(&g_gpu, g_rgba_buf);

    convert_rgba_for_panel(g_rgba_buf, g_panel.width, g_panel.height, g_panel.drm_format, g_packed_buf);
    panel_present(&g_panel, g_packed_buf);

    return 0;
}

void pv_shutdown(void) {
    if (!g_initialized) return;
    lyrics_renderer_free(&g_renderer);
    free(g_rgba_buf);
    free(g_packed_buf);
    g_rgba_buf = NULL;
    g_packed_buf = NULL;
    g_initialized = false;
    g_pv_use_longjmp = false;
    LOG("pv_shutdown done");
}
