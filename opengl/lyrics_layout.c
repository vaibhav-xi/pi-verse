#define _DEFAULT_SOURCE
#include "lyrics_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define MAX_PIECES 64

typedef struct { char text[MAX_LINE_LEN]; bool is_group; } Segment;

static int extract_segments(const char *text, Segment *segments, int max_segments) {
    int n = 0;
    int len = (int)strlen(text);
    int last = 0;
    int i = 0;

    while (i < len && n < max_segments) {
        char c = text[i];
        char close = 0;
        if (c == '(') close = ')';
        else if (c == '[') close = ']';
        else if (c == '{') close = '}';

        if (close) {
            int close_idx = -1;
            int j = i + 1;
            while (j < len) {
                if (text[j] == close) { close_idx = j; break; }
                if (text[j] == c) break; /* same-type open first: not a valid group here */
                j++;
            }
            if (close_idx >= 0) {
                if (i > last) {
                    int seg_len = i - last;
                    if (seg_len >= MAX_LINE_LEN) seg_len = MAX_LINE_LEN - 1;
                    memcpy(segments[n].text, text + last, (size_t)seg_len);
                    segments[n].text[seg_len] = 0;
                    segments[n].is_group = false;
                    n++;
                }
                if (n < max_segments) {
                    int glen = close_idx - i + 1;
                    if (glen >= MAX_LINE_LEN) glen = MAX_LINE_LEN - 1;
                    memcpy(segments[n].text, text + i, (size_t)glen);
                    segments[n].text[glen] = 0;
                    segments[n].is_group = true;
                    n++;
                }
                last = close_idx + 1;
                i = close_idx + 1;
                continue;
            }
        }
        i++;
    }
    if (last < len && n < max_segments) {
        int seg_len = len - last;
        if (seg_len >= MAX_LINE_LEN) seg_len = MAX_LINE_LEN - 1;
        memcpy(segments[n].text, text + last, (size_t)seg_len);
        segments[n].text[seg_len] = 0;
        segments[n].is_group = false;
        n++;
    }
    return n;
}

static void split_on_punct_whitespace(const char *seg, char pieces[][MAX_LINE_LEN],
                                       int *piece_count, int max_pieces) {
    int len = (int)strlen(seg);
    int i = 0;
    while (i < len && isspace((unsigned char)seg[i])) i++; /* leading strip, matches part.strip() */
    int piece_start = i;

    while (i < len) {
        if (isspace((unsigned char)seg[i]) && i > 0 && strchr(",/)]}", seg[i - 1]) && piece_start < i) {
            int plen = i - piece_start;
            if (plen > 0 && *piece_count < max_pieces) {
                if (plen >= MAX_LINE_LEN) plen = MAX_LINE_LEN - 1;
                memcpy(pieces[*piece_count], seg + piece_start, (size_t)plen);
                pieces[*piece_count][plen] = 0;
                (*piece_count)++;
            }
            while (i < len && isspace((unsigned char)seg[i])) i++;
            piece_start = i;
            continue;
        }
        i++;
    }

    int plen = len - piece_start;
    while (plen > 0 && isspace((unsigned char)seg[piece_start + plen - 1])) plen--;
    if (plen > 0 && *piece_count < max_pieces) {
        if (plen >= MAX_LINE_LEN) plen = MAX_LINE_LEN - 1;
        memcpy(pieces[*piece_count], seg + piece_start, (size_t)plen);
        pieces[*piece_count][plen] = 0;
        (*piece_count)++;
    }
}

static int word_wrap_impl(const void *font, MeasureFn measure, const char *text, float max_width,
                           char out_lines[][MAX_LINE_LEN], int max_lines) {
    char words[32][MAX_LINE_LEN];
    int word_count = 0;
    {
        const char *p = text;
        while (*p && word_count < 32) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) break;
            const char *start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            int wlen = (int)(p - start);
            if (wlen >= MAX_LINE_LEN) wlen = MAX_LINE_LEN - 1;
            memcpy(words[word_count], start, (size_t)wlen);
            words[word_count][wlen] = 0;
            word_count++;
        }
    }

    int line_count = 0;
    char current[MAX_LINE_LEN] = "";

    for (int w = 0; w < word_count && line_count < max_lines; w++) {
        char candidate[MAX_LINE_LEN];
        if (current[0] == '\0') snprintf(candidate, sizeof(candidate), "%s", words[w]);
        else snprintf(candidate, sizeof(candidate), "%s %s", current, words[w]);

        if (measure(font, candidate) <= max_width) {
            snprintf(current, sizeof(current), "%s", candidate);
            continue;
        }

        if (current[0] != '\0' && line_count < max_lines) {
            snprintf(out_lines[line_count], MAX_LINE_LEN, "%s", current);
            line_count++;
        }

        if (measure(font, words[w]) > max_width) {
            /* Extremely long single word: break it character by character. */
            char chunk[MAX_LINE_LEN] = "";
            const char *ch = words[w];
            while (*ch && line_count < max_lines) {
                char chunk_candidate[MAX_LINE_LEN];
                size_t clen = strlen(chunk);
                if (clen + 1 < sizeof(chunk_candidate)) {
                    memcpy(chunk_candidate, chunk, clen);
                    chunk_candidate[clen] = *ch;
                    chunk_candidate[clen + 1] = 0;
                } else {
                    chunk_candidate[0] = 0;
                }

                if (measure(font, chunk_candidate) <= max_width || chunk[0] == '\0') {
                    snprintf(chunk, sizeof(chunk), "%s", chunk_candidate);
                    ch++;
                } else {
                    if (line_count < max_lines) {
                        snprintf(out_lines[line_count], MAX_LINE_LEN, "%s", chunk);
                        line_count++;
                    }
                    chunk[0] = 0;
                }
            }
            snprintf(current, sizeof(current), "%s", chunk);
        } else {
            snprintf(current, sizeof(current), "%s", words[w]);
        }
    }

    if (current[0] != '\0' && line_count < max_lines) {
        snprintf(out_lines[line_count], MAX_LINE_LEN, "%s", current);
        line_count++;
    }
    return line_count;
}

static int split_lyric(const void *font, MeasureFn measure, const char *text, float max_width,
                        char out_lines[][MAX_LINE_LEN], int max_lines) {
    if (measure(font, text) <= max_width) {
        snprintf(out_lines[0], MAX_LINE_LEN, "%s", text);
        return 1;
    }

    Segment segments[MAX_PIECES];
    int seg_count = extract_segments(text, segments, MAX_PIECES);

    char pieces[MAX_PIECES][MAX_LINE_LEN];
    int piece_count = 0;
    for (int s = 0; s < seg_count && piece_count < MAX_PIECES; s++) {
        if (segments[s].is_group) {
            snprintf(pieces[piece_count], MAX_LINE_LEN, "%s", segments[s].text);
            piece_count++;
        } else {
            split_on_punct_whitespace(segments[s].text, pieces, &piece_count, MAX_PIECES);
        }
    }

    int line_count = 0;
    char current[MAX_LINE_LEN] = "";

    for (int p = 0; p < piece_count && line_count < max_lines; p++) {
        char candidate[MAX_LINE_LEN];
        if (current[0] == '\0') snprintf(candidate, sizeof(candidate), "%s", pieces[p]);
        else snprintf(candidate, sizeof(candidate), "%s %s", current, pieces[p]);

        if (measure(font, candidate) <= max_width) {
            snprintf(current, sizeof(current), "%s", candidate);
            continue;
        }

        if (current[0] != '\0') {
            snprintf(out_lines[line_count], MAX_LINE_LEN, "%s", current);
            line_count++;
            if (line_count >= max_lines) break;
        }

        if (measure(font, pieces[p]) <= max_width) {
            snprintf(current, sizeof(current), "%s", pieces[p]);
        } else {
            char wrapped[MAX_WRAPPED_LINES][MAX_LINE_LEN];
            int wrapped_count = word_wrap_impl(font, measure, pieces[p], max_width,
                                                wrapped, max_lines - line_count);
            if (wrapped_count > 0) {
                for (int i = 0; i < wrapped_count - 1 && line_count < max_lines; i++) {
                    snprintf(out_lines[line_count], MAX_LINE_LEN, "%s", wrapped[i]);
                    line_count++;
                }
                snprintf(current, sizeof(current), "%s", wrapped[wrapped_count - 1]);
            } else {
                current[0] = 0;
            }
        }
    }

    if (current[0] != '\0' && line_count < max_lines) {
        snprintf(out_lines[line_count], MAX_LINE_LEN, "%s", current);
        line_count++;
    }
    return line_count;
}

void wrap_lyric_line(const void *font, MeasureFn measure, const char *text,
                      float max_width, float line_height, float wrapped_gap,
                      WrappedText *out) {
    memset(out, 0, sizeof(*out));

    char trimmed[MAX_LINE_LEN];
    snprintf(trimmed, sizeof(trimmed), "%s", text);
    /* trim leading/trailing whitespace, matching text.strip() */
    char *start = trimmed;
    while (*start && isspace((unsigned char)*start)) start++;
    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    *end = 0;

    if (start[0] == '\0') return;

    out->line_count = split_lyric(font, measure, start, max_width, out->lines, MAX_WRAPPED_LINES);
    if (out->line_count == 0) return;

    float spacing = wrapped_gap > (line_height - 3) ? wrapped_gap : (line_height - 3);
    out->total_height = line_height + (out->line_count - 1) * spacing;
}

int trim_visible_lyrics(VisibleLyricCandidate *candidates, int count,
                         float verse_gap, float available_height) {
    int present_count = 0;
    for (int i = 0; i < count; i++) if (candidates[i].present) present_count++;

    while (present_count > 3) {
        float total = 0;
        for (int i = 0; i < count; i++) if (candidates[i].present) total += candidates[i].height;
        if (present_count > 1) total += (present_count - 1) * verse_gap;

        if (total <= available_height) break;

        int furthest_idx = -1, furthest_dist = -1;
        for (int i = 0; i < count; i++) {
            if (!candidates[i].present) continue;
            int dist = abs(candidates[i].offset);
            if (dist > furthest_dist) { furthest_dist = dist; furthest_idx = i; }
        }
        if (furthest_idx < 0) break;
        candidates[furthest_idx].present = false;
        present_count--;
    }
    return present_count;
}

TitleScrollState compute_title_scroll(float title_width, float available_width,
                                       float elapsed_seconds, float scroll_speed,
                                       float pause_seconds) {
    TitleScrollState st = {0.0f, false};
    if (title_width <= available_width) return st;

    st.active = true;
    float max_scroll = title_width - available_width;
    float scroll_duration = max_scroll / scroll_speed;
    float cycle_duration = pause_seconds + scroll_duration + pause_seconds;
    float cycle_position = fmodf(elapsed_seconds, cycle_duration + pause_seconds);

    if (cycle_position < pause_seconds) {
        st.scroll_x = 0.0f;
    } else if (cycle_position < pause_seconds + scroll_duration) {
        st.scroll_x = (cycle_position - pause_seconds) * scroll_speed;
    } else if (cycle_position < pause_seconds + scroll_duration + pause_seconds) {
        st.scroll_x = max_scroll;
    } else {
        st.scroll_x = 0.0f;
    }
    return st;
}
