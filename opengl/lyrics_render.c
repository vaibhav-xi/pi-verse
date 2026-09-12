#define _DEFAULT_SOURCE
#include "lyrics_render.h"
#include "lyrics_layout.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---- colors, matching ui.py exactly (converted from 0-255 to 0-1) ---- */
#define BG_R (10/255.0f)
#define BG_G (10/255.0f)
#define BG_B (14/255.0f)
#define TEXT_R (240/255.0f)
#define TEXT_G (240/255.0f)
#define TEXT_B (245/255.0f)
#define DIM_R (110/255.0f)
#define DIM_G (110/255.0f)
#define DIM_B (120/255.0f)
#define ACCENT_R (30/255.0f)
#define ACCENT_G (215/255.0f)
#define ACCENT_B (96/255.0f)
#define SEP_R (40/255.0f)
#define SEP_G (40/255.0f)
#define SEP_B (46/255.0f)

#define ART_SIZE 108.0f
#define VERSE_GAP 20.0f
#define WRAPPED_GAP 22.0f
#define TITLE_MAX_CHARS 29
#define TITLE_SCROLL_SPEED 18.0f
#define TITLE_SCROLL_PAUSE 3.0f

static float measure_wrapper(const void *font, const char *text) {
    return font_measure_text((const Font *)font, text);
}

static void draw_text_centered(const Font *font, const char *text, float center_x, float center_y,
                                float r, float g, float b, float a) {
    float width = font_measure_text(font, text);
    float x = center_x - width / 2.0f;
    float baseline_y = center_y + (font->ascent + font->descent) / 2.0f;
    font_draw_text(font, x, baseline_y, text, r, g, b, a);
}

bool lyrics_renderer_init(LyricsRenderer *r, const char *font_path) {
    memset(r, 0, sizeof(*r));
    if (!font_load(font_path, 20.0f, &r->font_title)) return false;
    if (!font_load(font_path, 15.0f, &r->font_artist)) return false;
    if (!font_load(font_path, 22.0f, &r->font_lyric_active)) return false;
    if (!font_load(font_path, 18.0f, &r->font_lyric_dim)) return false;
    return true;
}

bool lyrics_renderer_set_album_art(LyricsRenderer *r, const uint8_t *img_data, size_t img_size) {
    lyrics_renderer_clear_album_art(r);
    if (!image_load_from_memory(img_data, img_size, &r->album_art)) return false;
    r->album_art_loaded = true;
    return true;
}

void lyrics_renderer_clear_album_art(LyricsRenderer *r) {
    if (r->album_art_loaded) image_free(&r->album_art);
    r->album_art_loaded = false;
}

void lyrics_renderer_free(LyricsRenderer *r) {
    font_free(&r->font_title);
    font_free(&r->font_artist);
    font_free(&r->font_lyric_active);
    font_free(&r->font_lyric_dim);
    lyrics_renderer_clear_album_art(r);
}

static void draw_title(LyricsRenderer *r, const TrackSnapshot *snap, float x, float y,
                        int screen_w, int screen_h, float dt_seconds) {
    const char *title = snap->track_name;
    if (!title || !title[0]) return;

    const char *track_id = snap->track_id ? snap->track_id : "";
    if (strncmp(track_id, r->title_track_id, sizeof(r->title_track_id) - 1) != 0) {
        snprintf(r->title_track_id, sizeof(r->title_track_id), "%s", track_id);
        r->title_scroll_elapsed = 0.0f;
    } else {
        r->title_scroll_elapsed += dt_seconds;
    }

    float available_width = (float)screen_w - x - 8.0f;
    float title_width = font_measure_text(&r->font_title, title);

    bool short_by_chars = strlen(title) <= TITLE_MAX_CHARS;
    bool fits_by_width = title_width <= available_width;

    if (short_by_chars || fits_by_width) {
        float baseline_y = y + r->font_title.ascent;
        font_draw_text(&r->font_title, x, baseline_y, title, TEXT_R, TEXT_G, TEXT_B, 1.0f);
        return;
    }

    TitleScrollState st = compute_title_scroll(title_width, available_width,
                                                r->title_scroll_elapsed,
                                                TITLE_SCROLL_SPEED, TITLE_SCROLL_PAUSE);

    float line_height = r->font_title.ascent - r->font_title.descent;

    glEnable(GL_SCISSOR_TEST);
    glScissor((GLint)x, (GLint)((float)screen_h - (y + line_height)),
              (GLsizei)available_width, (GLsizei)line_height);

    float baseline_y = y + r->font_title.ascent;
    font_draw_text(&r->font_title, x - st.scroll_x, baseline_y, title, TEXT_R, TEXT_G, TEXT_B, 1.0f);

    glDisable(GL_SCISSOR_TEST);
}

static void draw_lyric_block(const Font *font, const char *text, float center_y,
                              int screen_w, float r, float g, float b) {
    WrappedText w;
    float line_height = font->ascent - font->descent;
    wrap_lyric_line(font, measure_wrapper, text, (float)screen_w - 2 * 12.0f,
                     line_height, WRAPPED_GAP, &w);
    if (w.line_count == 0) return;

    float line_spacing = (line_height - 3.0f) > WRAPPED_GAP ? (line_height - 3.0f) : WRAPPED_GAP;
    float y = center_y - w.total_height / 2.0f + line_height / 2.0f;

    for (int i = 0; i < w.line_count; i++) {
        draw_text_centered(font, w.lines[i], (float)screen_w / 2.0f, y, r, g, b, 1.0f);
        y += line_spacing;
    }
}

static int find_current_lyric_index(const SyncedLine *synced, int count, long progress_ms) {
    int idx = -1;
    for (int i = 0; i < count; i++) {
        if (synced[i].timestamp_ms <= progress_ms) idx = i;
        else break;
    }
    return idx;
}

void lyrics_render_frame(LyricsRenderer *r, int screen_w, int screen_h, float dt_seconds,
                          const TrackSnapshot *snap, const LyricsState *lyrics) {
    glClearColor(BG_R, BG_G, BG_B, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!snap || !snap->has_track) {
        draw_text_centered(&r->font_title, "Nothing playing", (float)screen_w / 2.0f,
                            (float)screen_h / 2.0f, DIM_R, DIM_G, DIM_B, 1.0f);
        return;
    }

    /* ---- header: album art, title, artist ---- */
    if (r->album_art_loaded)
        gfx_draw_textured_rect(r->album_art.texture, 12, 12, ART_SIZE, ART_SIZE, 1.0f);

    float text_x = r->album_art_loaded ? (12.0f + ART_SIZE + 14.0f) : 16.0f;
    draw_title(r, snap, text_x, 20.0f, screen_w, screen_h, dt_seconds);

    if (snap->artist_name && snap->artist_name[0]) {
        float baseline_y = 48.0f + r->font_artist.ascent;
        font_draw_text(&r->font_artist, text_x, baseline_y, snap->artist_name, DIM_R, DIM_G, DIM_B, 1.0f);
    }

    float header_bottom = 12.0f + ART_SIZE + 10.0f;
    gfx_fill_rect(0, header_bottom, (float)screen_w, 1, SEP_R, SEP_G, SEP_B, 1.0f);

    /* ---- lyrics ---- */
    float lyrics_top = header_bottom;
    float center_y = lyrics_top + ((float)screen_h - lyrics_top) / 2.0f;

    if (lyrics && lyrics->instrumental) {
        draw_text_centered(&r->font_lyric_dim, "(Instrumental)", (float)screen_w / 2.0f, center_y,
                            DIM_R, DIM_G, DIM_B, 1.0f);
    } else if (lyrics && lyrics->synced && lyrics->synced_count > 0) {
        int idx = find_current_lyric_index(lyrics->synced, lyrics->synced_count, snap->progress_ms);

        if (idx >= 0) {
            VisibleLyricCandidate cands[5];
            float heights[5];
            const char *texts[5];
            const Font *fonts[5];
            int cand_count = 0;

            for (int offset = -2; offset <= 2; offset++) {
                int i = idx + offset;
                if (i < 0 || i >= lyrics->synced_count) continue;

                const Font *font = (offset == 0) ? &r->font_lyric_active : &r->font_lyric_dim;
                WrappedText w;
                float line_height = font->ascent - font->descent;
                wrap_lyric_line(font, measure_wrapper, lyrics->synced[i].text,
                                 (float)screen_w - 2 * 12.0f, line_height, WRAPPED_GAP, &w);
                if (w.line_count == 0) continue;

                cands[cand_count].offset = offset;
                cands[cand_count].height = w.total_height;
                cands[cand_count].present = true;
                heights[cand_count] = w.total_height;
                texts[cand_count] = lyrics->synced[i].text;
                fonts[cand_count] = font;
                cand_count++;
            }

            float available_height = (float)screen_h - lyrics_top - 8.0f;
            int remaining = trim_visible_lyrics(cands, cand_count, VERSE_GAP, available_height);
            (void)remaining;

            float total_height = 0;
            int visible_count = 0;
            for (int i = 0; i < cand_count; i++) {
                if (!cands[i].present) continue;
                total_height += heights[i];
                visible_count++;
            }
            if (visible_count > 1) total_height += (visible_count - 1) * VERSE_GAP;

            float group_center = lyrics_top + ((float)screen_h - lyrics_top) / 2.0f;
            float top = group_center - total_height / 2.0f;
            float current_y = top;

            for (int i = 0; i < cand_count; i++) {
                if (!cands[i].present) continue;
                float block_center = current_y + heights[i] / 2.0f;
                float rr = (cands[i].offset == 0) ? ACCENT_R : DIM_R;
                float gg = (cands[i].offset == 0) ? ACCENT_G : DIM_G;
                float bb = (cands[i].offset == 0) ? ACCENT_B : DIM_B;
                draw_lyric_block(fonts[i], texts[i], block_center, screen_w, rr, gg, bb);
                current_y += heights[i] + VERSE_GAP;
            }
        }
    } else if (lyrics && lyrics->plain) {
        draw_text_centered(&r->font_lyric_dim, "Lyrics found (not time-synced)", (float)screen_w / 2.0f,
                            center_y, DIM_R, DIM_G, DIM_B, 1.0f);
    } else if (lyrics && lyrics->has_data && !lyrics->found) {
        draw_text_centered(&r->font_lyric_dim, "No lyrics found", (float)screen_w / 2.0f, center_y,
                            DIM_R, DIM_G, DIM_B, 1.0f);
    } else {
        draw_text_centered(&r->font_lyric_dim, "Loading lyrics...", (float)screen_w / 2.0f, center_y,
                            DIM_R, DIM_G, DIM_B, 1.0f);
    }
}
