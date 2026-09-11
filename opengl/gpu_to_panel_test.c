
#define _DEFAULT_SOURCE
#include "gpu_panel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define RUN_SECONDS 15.0
#define TARGET_FPS  15

int main(int argc, char **argv) {
    const char *gpu_path = NULL;
    const char *panel_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--gpu") && i + 1 < argc) gpu_path = argv[++i];
        else if (!strcmp(argv[i], "--panel") && i + 1 < argc) panel_path = argv[++i];
    }

    char *auto_gpu = NULL, *auto_panel = NULL;
    if (!gpu_path) {
        auto_gpu = find_gpu_render_node();
        if (!auto_gpu) DIE("No /dev/dri/renderD1xx found. Pass --gpu explicitly if you know the path.");
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

    size_t pixel_count = (size_t)panel.width * panel.height;
    uint8_t *rgba_buf = malloc(pixel_count * 4);
    uint8_t *packed_buf = malloc(pixel_count * 4);
    if (!rgba_buf || !packed_buf) DIE("out of memory");

    LOG("Starting %g second red -> green -> blue -> red cycle at %d fps...", RUN_SECONDS, TARGET_FPS);

    int total_frames = (int)(RUN_SECONDS * TARGET_FPS);
    for (int f = 0; f < total_frames; f++) {
        double t = (double)f / total_frames;
        double hue = fmod(t * 3.0, 1.0) * 360.0;

        double c = 1.0, x = 1.0 - fabs(fmod(hue / 60.0, 2.0) - 1.0);
        double r1, g1, b1;
        if      (hue < 60)  { r1 = c; g1 = x; b1 = 0; }
        else if (hue < 120) { r1 = x; g1 = c; b1 = 0; }
        else if (hue < 180) { r1 = 0; g1 = c; b1 = x; }
        else if (hue < 240) { r1 = 0; g1 = x; b1 = c; }
        else if (hue < 300) { r1 = x; g1 = 0; b1 = c; }
        else                { r1 = c; g1 = 0; b1 = x; }

        gpu_render_solid_frame(&gpu, (float)r1, (float)g1, (float)b1, rgba_buf);
        convert_rgba_for_panel(rgba_buf, panel.width, panel.height, panel.drm_format, packed_buf);
        panel_present(&panel, packed_buf);

        if (f % TARGET_FPS == 0) LOG("frame %d/%d, hue=%.0f", f, total_frames, hue);
    }

    LOG("Done.");
    free(rgba_buf);
    free(packed_buf);
    free(auto_gpu);
    free(auto_panel);
    return 0;
}
