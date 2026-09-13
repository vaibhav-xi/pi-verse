#ifndef LYRICS_RENDER_H
#define LYRICS_RENDER_H

#include "font.h"
#include "image.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool has_track;       /* false = draw "Nothing playing" */
    const char *track_name;
    const char *artist_name;
    long duration_ms;
    long progress_ms;      /* caller (Python side, eventually) interpolates this each frame */
    bool is_playing;
    const char *track_id;   /* used only to detect track changes, for the title-scroll reset */
} TrackSnapshot;

typedef struct {
    long timestamp_ms;
    const char *text;
} SyncedLine;

typedef struct {
    bool has_data;             /* false = still loading, show "Loading lyrics..." */
    const SyncedLine *synced;  /* NULL if none */
    int synced_count;
    const char *plain;          /* non-NULL = unsynced lyrics available */
    bool instrumental;
    bool found;                  /* only meaningful when synced/plain are both empty */
} LyricsState;

#define LYRICS_QUEUE_ART_SLOTS 4 

typedef struct {
    Font font_title, font_artist, font_lyric_active, font_lyric_dim;

    Image album_art;
    bool album_art_loaded;

    Image queue_art[LYRICS_QUEUE_ART_SLOTS];
    bool queue_art_loaded[LYRICS_QUEUE_ART_SLOTS];

    char title_track_id[256];
    float title_scroll_elapsed; /* seconds since title_track_id last changed */

    float scale;
} LyricsRenderer;

bool lyrics_renderer_init(LyricsRenderer *r, const char *font_path, int screen_w, int screen_h);

bool lyrics_renderer_set_album_art(LyricsRenderer *r, const uint8_t *img_data, size_t img_size);
void lyrics_renderer_clear_album_art(LyricsRenderer *r);

bool lyrics_renderer_set_queue_art(LyricsRenderer *r, int slot, const uint8_t *img_data, size_t img_size);
void lyrics_renderer_clear_queue_art(LyricsRenderer *r, int slot);

typedef struct {
    const char *track_name;
    const char *artist_name;
} QueueItem;

void lyrics_render_frame(LyricsRenderer *r, int screen_w, int screen_h, float dt_seconds,
                          const TrackSnapshot *snap, const LyricsState *lyrics);

void lyrics_render_frame_queue_mode(LyricsRenderer *r, int screen_w, int screen_h, float dt_seconds,
                                     const TrackSnapshot *snap, const LyricsState *lyrics,
                                     const QueueItem *queue_items, int queue_count);

void lyrics_renderer_free(LyricsRenderer *r);

#endif
