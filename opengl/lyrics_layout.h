#ifndef LYRICS_LAYOUT_H
#define LYRICS_LAYOUT_H

#include <stdbool.h>

#define MAX_WRAPPED_LINES 8
#define MAX_LINE_LEN 256

typedef struct {
    char lines[MAX_WRAPPED_LINES][MAX_LINE_LEN];
    int line_count;
    float total_height; /* matches ui.py's _lyric_lines() second return value */
} WrappedText;

typedef float (*MeasureFn)(const void *font, const char *text);

void wrap_lyric_line(const void *font, MeasureFn measure, const char *text,
                      float max_width, float line_height, float wrapped_gap,
                      WrappedText *out);

/* One candidate lyric line in the -2..+2 visible window. */
typedef struct {
    int offset;         /* -2..+2, 0 = currently active line */
    float height;        /* from WrappedText.total_height */
    bool present;         /* false = slot skipped (out of range / empty) */
} VisibleLyricCandidate;

int trim_visible_lyrics(VisibleLyricCandidate *candidates, int count,
                         float verse_gap, float available_height);

/* ---- title marquee ---- */

typedef struct {
    float scroll_x;
    bool active;   /* false if the title fits and needs no scrolling */
} TitleScrollState;

TitleScrollState compute_title_scroll(float title_width, float available_width,
                                       float elapsed_seconds, float scroll_speed,
                                       float pause_seconds);

typedef struct {
    int index;       /* offset from the current line: ..., -1, 0, +1, ... */
    float height;      /* wrapped block height at this mode's font/width */
    bool visible;        /* filled in by layout_scroll_window() */
    float y_offset;       /* filled in: px from the top of the visible block */
} ScrollLine;

int layout_scroll_window(ScrollLine *candidates, int count, float line_gap,
                          float above_budget, float below_budget);

#endif
