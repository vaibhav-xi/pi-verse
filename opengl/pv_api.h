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

int pv_set_album_art(const uint8_t *img_data, size_t img_size);
void pv_clear_album_art(void);

int pv_set_queue_art(int slot, const uint8_t *img_data, size_t img_size);
void pv_clear_queue_art(int slot);

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
    bool found,                     /* only meaningful when synced/plain are both absent */
    const QueueItem *queue_items,   /* NULL ok */
    int queue_count
);

void pv_shutdown(void);

typedef enum {
    PV_MODE_CLASSIC = 0, /* centered lyric block, matches the original pygame layout */
    PV_MODE_QUEUE = 1,    /* left-aligned scroll + album art/title/queue sidebar */
} PvDisplayMode;

void pv_set_display_mode(PvDisplayMode mode);

#ifdef __cplusplus
}
#endif
#endif
