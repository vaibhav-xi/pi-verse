
#ifndef PV_API_H
#define PV_API_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "lyrics_render.h" /* for SyncedLine */

#ifdef __cplusplus
extern "C" {
#endif

int pv_init(const char *gpu_render_node, const char *panel_device, const char *font_path);

/* Decodes image bytes (JPEG/PNG - whatever Spotify serves) */
int pv_set_album_art(const uint8_t *img_data, size_t img_size);
void pv_clear_album_art(void);

/* Renders and presents exactly one frame. */
int pv_render_frame(
    bool has_track,
    const char *track_id,        /* used only to detect track changes (resets title scroll) */
    const char *track_name,
    const char *artist_name,
    long duration_ms,
    long progress_ms,             /* Python side should already be interpolating this, as spotify_client.py does */
    bool is_playing,
    const SyncedLine *synced_lines, /* NULL if none */
    int synced_count,
    const char *plain_lyrics,     /* NULL if none */
    bool instrumental,
    bool has_lyrics_data,          /* false = still loading (lyrics_state == {} in Python) */
    bool found                      /* only meaningful when synced/plain are both absent */
);

void pv_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
