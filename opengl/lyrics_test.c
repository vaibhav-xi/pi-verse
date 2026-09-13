/*
 * lyrics_test.c - STAGE 4 DIAGNOSTIC.
 *
 * The full "now playing" UI: real album art (JPEG decode), a long title
 * that should scroll, and multi-line synced lyrics with word-wrap and
 * the active line highlighted - simulating a song actually playing by
 * advancing progress_ms over time and stepping through lyric lines.
 *
 * Build:  make
 * Run:    sudo ./lyrics_test [--gpu ...] [--panel ...] [--font ./NotoSans.ttf] [--art ./test_album_art.jpg] [--mode classic|queue]
 */
#define _DEFAULT_SOURCE
#include "gpu_panel.h"
#include "lyrics_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TARGET_FPS 15

static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)size);
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_size = (size_t)size;
    return buf;
}

int main(int argc, char **argv) {
    const char *gpu_path = NULL;
    const char *panel_path = NULL;
    const char *font_path = "./NotoSans.ttf";
    const char *art_path = "./test_album_art.jpg";
    bool queue_mode = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--gpu") && i + 1 < argc) gpu_path = argv[++i];
        else if (!strcmp(argv[i], "--panel") && i + 1 < argc) panel_path = argv[++i];
        else if (!strcmp(argv[i], "--font") && i + 1 < argc) font_path = argv[++i];
        else if (!strcmp(argv[i], "--art") && i + 1 < argc) art_path = argv[++i];
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
            const char *mode = argv[++i];
            if (!strcmp(mode, "queue")) queue_mode = true;
            else if (strcmp(mode, "classic") != 0) DIE("--mode must be 'classic' or 'queue'");
        }
    }

    char *auto_gpu = NULL, *auto_panel = NULL;
    if (!gpu_path) { auto_gpu = find_gpu_render_node(); if (!auto_gpu) DIE("No GPU render node found."); gpu_path = auto_gpu; }
    if (!panel_path) { auto_panel = find_connected_card(); if (!auto_panel) DIE("No panel found."); panel_path = auto_panel; }
    LOG("GPU: %s  Panel: %s", gpu_path, panel_path);

    PanelCtx panel = {0};
    panel_init(&panel, panel_path);

    GpuCtx gpu = {0};
    gpu_init(&gpu, gpu_path, panel.width, panel.height);

    if (!gfx_init_shader(panel.width, panel.height)) DIE("gfx_init_shader failed.");

    LyricsRenderer renderer;
    if (!lyrics_renderer_init(&renderer, font_path, panel.width, panel.height))
        DIE("Could not load fonts from %s", font_path);

    size_t art_size;
    uint8_t *art_data = read_file(art_path, &art_size);
    if (art_data) {
        if (lyrics_renderer_set_album_art(&renderer, art_data, art_size))
            LOG("Album art loaded from %s", art_path);
        else
            LOG("WARNING: album art decode failed, continuing without it");
        free(art_data);
    } else {
        LOG("WARNING: could not read %s, continuing without album art", art_path);
    }

    if (queue_mode) {
        const char *queue_art_paths[2] = { "./test_album_art.jpg", "./test_album_art_2.jpg" };
        for (int i = 0; i < 2; i++) {
            size_t qsize;
            uint8_t *qdata = read_file(queue_art_paths[i], &qsize);
            if (qdata) {
                if (lyrics_renderer_set_queue_art(&renderer, i, qdata, qsize))
                    LOG("Queue thumbnail %d loaded from %s", i, queue_art_paths[i]);
                else
                    LOG("WARNING: queue thumbnail %d decode failed", i);
                free(qdata);
            }
        }
    }

    /* Mock "now playing" data: a long title (forces marquee) and several
     * synced lines (forces wrap on some, cycling as if the song plays).
     * More lines than the classic-mode test needs, since queue mode's
     * continuous scroll wants enough lines to actually demonstrate. */
    static const SyncedLine LYRICS[] = {
        { 0,     "This is the first line of a test song" },
        { 3000,  "Rendered entirely by hand-written OpenGL ES code" },
        { 6000,  "With word-wrap, a scrolling title (feat. a very long name), and real album art" },
        { 9000,  "Running on a Raspberry Pi's actual GPU" },
        { 12000, "No pygame, no SDL, no Python in this part at all" },
        { 15000, "Just EGL, GLES2, and a font atlas we baked ourselves" },
        { 18000, "This line exists just to test the continuous left-aligned scroll" },
        { 21000, "Watch how the current line stays higher in the frame" },
        { 24000, "While upcoming lines wait below it" },
        { 27000, "And already-sung lines scroll up and fade above" },
        { 30000, "Almost done with this loop now" },
        { 33000, "Back to the top in a few seconds..." },
    };
    const int LYRICS_COUNT = sizeof(LYRICS) / sizeof(LYRICS[0]);
    const long SONG_DURATION_MS = 36000;

    static const QueueItem QUEUE[] = {
        { "A Pretty Long Upcoming Song Title That Needs Truncating", "Some Artist" },
        { "Second Track In The Queue", "Another Artist" },
        { "Third One Coming Up", "Yet Another Artist" },
        { "Fourth And Last Shown", "Final Artist" },
        { "Fifth Track (never shown - past QUEUE_MAX_ITEMS)", "Overflow Artist" },
    };
    const int QUEUE_COUNT = sizeof(QUEUE) / sizeof(QUEUE[0]);

    TrackSnapshot snap = {
        .has_track = true,
        .track_name = "This Is An Intentionally Very Long Song Title To Force The Marquee To Scroll (Extended Version)",
        .artist_name = "Test Artist, Featuring Someone Else",
        .duration_ms = SONG_DURATION_MS,
        .progress_ms = 0,
        .is_playing = true,
        .track_id = "test_track_1",
    };
    LyricsState lyrics_state = {
        .has_data = true,
        .synced = LYRICS,
        .synced_count = LYRICS_COUNT,
        .plain = NULL,
        .instrumental = false,
        .found = true,
    };

    size_t pixel_count = (size_t)panel.width * panel.height;
    uint8_t *rgba_buf = malloc(pixel_count * 4);
    uint8_t *packed_buf = malloc(pixel_count * 4);
    if (!rgba_buf || !packed_buf) DIE("out of memory");

    LOG("Starting lyrics UI test in %s mode - simulating a %ldms song on loop, ~45s total...",
        queue_mode ? "QUEUE" : "CLASSIC", SONG_DURATION_MS);

    struct timespec last_time;
    clock_gettime(CLOCK_MONOTONIC, &last_time);
    long sim_time_ms = 0;
    int total_frames = TARGET_FPS * 45;

    for (int f = 0; f < total_frames; f++) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        float dt = (float)(now.tv_sec - last_time.tv_sec) + (float)(now.tv_nsec - last_time.tv_nsec) / 1e9f;
        last_time = now;
        if (f == 0) dt = 0.0f; /* first frame: no meaningful delta yet */

        sim_time_ms += (long)(1000.0f / TARGET_FPS);
        snap.progress_ms = sim_time_ms % SONG_DURATION_MS;

        gpu_begin_frame(&gpu);
        if (queue_mode)
            lyrics_render_frame_queue_mode(&renderer, panel.width, panel.height, dt, &snap, &lyrics_state,
                                            QUEUE, QUEUE_COUNT);
        else
            lyrics_render_frame(&renderer, panel.width, panel.height, dt, &snap, &lyrics_state);
        gpu_end_frame(&gpu, rgba_buf);

        convert_rgba_for_panel(rgba_buf, panel.width, panel.height, panel.drm_format, packed_buf);
        panel_present(&panel, packed_buf);

        if (f % TARGET_FPS == 0) LOG("frame %d/%d, progress=%ldms", f, total_frames, snap.progress_ms);
    }

    LOG("Done. Check: title scrolled smoothly, lyric lines wrapped and changed over "
        "time with the active line in green, album art showed the test pattern.");

    lyrics_renderer_free(&renderer);
    free(rgba_buf);
    free(packed_buf);
    free(auto_gpu);
    free(auto_panel);
    return 0;
}
