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

typedef struct {
    Font font_title, font_artist, font_lyric_active, font_lyric_dim;

    Image album_art;
    bool album_art_loaded;

    char title_track_id[256];
    float title_scroll_elapsed; /* seconds since title_track_id last changed */

    float scale; /* see ui_scale.h - 1.0 at the reference 480x320, bigger on larger displays */
} LyricsRenderer;

bool lyrics_renderer_init(LyricsRenderer *r, const char *font_path, int screen_w, int screen_h);

/* Decodes and uploads new album art; replaces any previous art. */
bool lyrics_renderer_set_album_art(LyricsRenderer *r, const uint8_t *img_data, size_t img_size);
void lyrics_renderer_clear_album_art(LyricsRenderer *r);

void lyrics_render_frame(LyricsRenderer *r, int screen_w, int screen_h, float dt_seconds,
                          const TrackSnapshot *snap, const LyricsState *lyrics);

void lyrics_renderer_free(LyricsRenderer *r);

#endif
