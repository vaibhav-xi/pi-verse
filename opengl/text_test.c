

#define _DEFAULT_SOURCE
#include "gpu_panel.h"
#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define RUN_SECONDS 20.0
#define TARGET_FPS  15

int main(int argc, char **argv) {
    const char *gpu_path = NULL;
    const char *panel_path = NULL;
    const char *font_path = "./NotoSans.ttf";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--gpu") && i + 1 < argc) gpu_path = argv[++i];
        else if (!strcmp(argv[i], "--panel") && i + 1 < argc) panel_path = argv[++i];
        else if (!strcmp(argv[i], "--font") && i + 1 < argc) font_path = argv[++i];
    }

    char *auto_gpu = NULL, *auto_panel = NULL;
    if (!gpu_path) {
        auto_gpu = find_gpu_render_node();
        if (!auto_gpu) DIE("No /dev/dri/renderD1xx found. Pass --gpu explicitly.");
        gpu_path = auto_gpu;
        LOG("Auto-detected GPU render node: %s", gpu_path);
    }
    if (!panel_path) {
        auto_panel = find_connected_card();
        if (!auto_panel) DIE("No /dev/dri/cardN with a connected connector found. Pass --panel explicitly.");
        panel_path = auto_panel;
        LOG("Auto-detected panel device: %s", panel_path);
    }

    PanelCtx panel = {0};
    panel_init(&panel, panel_path);

    GpuCtx gpu = {0};
    gpu_init(&gpu, gpu_path, panel.width, panel.height);

    if (!gfx_init_shader(panel.width, panel.height))
        DIE("gfx_init_shader failed - see shader compile/link errors above.");

    Font font_title, font_small, font_lyric;
    if (!font_load(font_path, 20.0f, &font_title)) DIE("Could not load font (title size) from %s", font_path);
    if (!font_load(font_path, 15.0f, &font_small)) DIE("Could not load font (small size) from %s", font_path);
    if (!font_load(font_path, 22.0f, &font_lyric)) DIE("Could not load font (lyric size) from %s", font_path);
    LOG("Font atlases baked from %s", font_path);

    size_t pixel_count = (size_t)panel.width * panel.height;
    uint8_t *rgba_buf = malloc(pixel_count * 4);
    uint8_t *packed_buf = malloc(pixel_count * 4);
    if (!rgba_buf || !packed_buf) DIE("out of memory");

    const char *title = "Star Song (feat. Lil Durk)";
    const char *artist = "Sally Sossa, Lil Durk";
    const char *lyric = "This is a test lyric line, rendered live on the GPU";

    LOG("Starting %g second text render test at %d fps...", RUN_SECONDS, TARGET_FPS);
    int total_frames = (int)(RUN_SECONDS * TARGET_FPS);

    for (int f = 0; f < total_frames; f++) {
        double t = (double)f / total_frames;
        double hue = fmod(t * 2.0, 1.0) * 360.0;
        double c = 1.0, x = 1.0 - fabs(fmod(hue / 60.0, 2.0) - 1.0);
        double r1, g1, b1;
        if      (hue < 60)  { r1 = c; g1 = x; b1 = 0; }
        else if (hue < 120) { r1 = x; g1 = c; b1 = 0; }
        else if (hue < 180) { r1 = 0; g1 = c; b1 = x; }
        else if (hue < 240) { r1 = 0; g1 = x; b1 = c; }
        else if (hue < 300) { r1 = x; g1 = 0; b1 = c; }
        else                { r1 = c; g1 = 0; b1 = x; }

        gpu_begin_frame(&gpu);
        glClearColor(0.04f, 0.04f, 0.055f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        /* album-art placeholder square, top-left */
        gfx_fill_rect(12, 12, 60, 60, 0.25f, 0.25f, 0.30f, 1.0f);

        /* title + artist, to the right of it */
        font_draw_text(&font_title, 84, 12 + font_title.ascent, title, 0.94f, 0.94f, 0.96f, 1.0f);
        font_draw_text(&font_small, 84, 12 + 30 + font_small.ascent, artist, 0.55f, 0.55f, 0.58f, 1.0f);

        /* separator line */
        gfx_fill_rect(0, 88, (float)panel.width, 1, 0.16f, 0.16f, 0.18f, 1.0f);

        /* animated sample lyric line, centered-ish */
        float lyric_width = font_measure_text(&font_lyric, lyric);
        float lyric_x = ((float)panel.width - lyric_width) / 2.0f;
        if (lyric_x < 4) lyric_x = 4; /* clamp if it would overflow - real wrapping comes in Stage 2b */
        font_draw_text(&font_lyric, lyric_x, 88 + (panel.height - 88) / 2.0f, lyric,
                        (float)r1, (float)g1, (float)b1, 1.0f);

        gpu_end_frame(&gpu, rgba_buf);
        convert_rgba_for_panel(rgba_buf, panel.width, panel.height, panel.drm_format, packed_buf);
        panel_present(&panel, packed_buf);

        if (f % TARGET_FPS == 0) LOG("frame %d/%d", f, total_frames);
    }

    LOG("Done. If the panel showed the mock title/artist/lyric layout with "
        "readable text, GPU text rendering works end to end.");

    font_free(&font_title);
    font_free(&font_small);
    font_free(&font_lyric);
    free(rgba_buf);
    free(packed_buf);
    free(auto_gpu);
    free(auto_panel);
    return 0;
}
