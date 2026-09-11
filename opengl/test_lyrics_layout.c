#include "lyrics_layout.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>

static float mock_measure(const void *font, const char *text) {
    (void)font;
    return (float)strlen(text) * 6.0f;
}

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else printf("  ok:   %s\n", msg); \
} while (0)

static void print_wrapped(const WrappedText *w) {
    for (int i = 0; i < w->line_count; i++) printf("    line %d: \"%s\"\n", i, w->lines[i]);
}

int main(void) {
    printf("=== short text, fits on one line ===\n");
    {
        WrappedText w;
        wrap_lyric_line(NULL, mock_measure, "hello world", 200, 20, 22, &w);
        print_wrapped(&w);
        CHECK(w.line_count == 1, "single line");
        CHECK(strcmp(w.lines[0], "hello world") == 0, "text unchanged");
    }

    printf("=== long text wraps across multiple lines ===\n");
    {
        /* max_width=120 -> 20 chars/line at 6px/char */
        WrappedText w;
        wrap_lyric_line(NULL, mock_measure,
                         "this line is definitely too long to fit on one row",
                         120, 20, 22, &w);
        print_wrapped(&w);
        CHECK(w.line_count > 1, "wrapped into multiple lines");
        for (int i = 0; i < w.line_count; i++)
            CHECK(mock_measure(NULL, w.lines[i]) <= 120 + 0.01f, "each line fits max_width");
    }

    printf("=== parenthetical group stays intact when it fits on one line ===\n");
    {
        WrappedText w;
        wrap_lyric_line(NULL, mock_measure,
                         "Star Song (feat. Someone Else) more text after",
                         150, 20, 22, &w);
        print_wrapped(&w);
        bool group_intact = false;
        for (int i = 0; i < w.line_count; i++)
            if (strstr(w.lines[i], "(feat. Someone Else)")) group_intact = true;
        CHECK(group_intact, "group appears whole on one line when it fits");
        for (int i = 0; i < w.line_count; i++) {
            CHECK(w.lines[i][0] != ' ', "no line starts with a stray leading space");
        }
    }

    printf("=== oversized group (too wide even alone) falls back to word-wrap, not comma-split ===\n");
    {
        WrappedText w;
        wrap_lyric_line(NULL, mock_measure,
                         "Star Song (feat. Someone Else) another bit of text here",
                         100, 20, 22, &w);
        print_wrapped(&w);
        char rebuilt[512] = "";
        for (int i = 0; i < w.line_count; i++) {
            if (i > 0) strcat(rebuilt, " ");
            strcat(rebuilt, w.lines[i]);
        }
        CHECK(strstr(rebuilt, "(feat. Someone Else)") != NULL,
              "group's words survive intact when reassembled, just wrapped across lines");
    }

    printf("=== comma-separated text splits at commas before mid-word ===\n");
    {
        WrappedText w;
        wrap_lyric_line(NULL, mock_measure,
                         "Sally Sossa, Lil Durk, Some Other Artist Name",
                         90, 20, 22, &w);
        print_wrapped(&w);
        CHECK(w.line_count > 1, "wrapped");
        /* first line should end right after a comma-separated chunk, not mid-word */
        size_t l0 = strlen(w.lines[0]);
        CHECK(l0 > 0 && w.lines[0][l0 - 1] != ' ', "no trailing space on wrapped line");
    }

    printf("=== single word longer than max_width breaks char by char ===\n");
    {
        WrappedText w;
        wrap_lyric_line(NULL, mock_measure,
                         "Supercalifragilisticexpialidocious",
                         60, 20, 22, &w); /* 60px = 10 chars/line */
        print_wrapped(&w);
        CHECK(w.line_count > 1, "broken into multiple chunks");
        for (int i = 0; i < w.line_count; i++)
            CHECK(mock_measure(NULL, w.lines[i]) <= 60 + 0.01f, "each chunk fits max_width");
        /* reassembling should give back the original word */
        char rebuilt[256] = "";
        for (int i = 0; i < w.line_count; i++) strcat(rebuilt, w.lines[i]);
        CHECK(strcmp(rebuilt, "Supercalifragilisticexpialidocious") == 0, "chunks reassemble to original word");
    }

    printf("=== empty/whitespace-only text yields no lines ===\n");
    {
        WrappedText w;
        wrap_lyric_line(NULL, mock_measure, "   ", 200, 20, 22, &w);
        CHECK(w.line_count == 0, "no lines for blank text");
    }

    printf("=== trim_visible_lyrics keeps center, drops furthest first ===\n");
    {
        VisibleLyricCandidate cands[5] = {
            { -2, 30, true }, { -1, 30, true }, { 0, 40, true }, { 1, 30, true }, { 2, 30, true }
        };
        /* total = 30+30+40+30+30 + 4*10(verse_gap) = 160+40=200, available=120 -> must trim */
        int remaining = trim_visible_lyrics(cands, 5, 10, 120);
        printf("  remaining=%d\n", remaining);
        for (int i = 0; i < 5; i++) printf("    offset %d present=%d\n", cands[i].offset, cands[i].present);
        CHECK(remaining == 3, "trimmed down to exactly 3 (the floor)");
        CHECK(cands[2].present, "center (offset 0) survives");
        CHECK(!cands[0].present && !cands[4].present, "furthest (+-2) removed first");
    }

    printf("=== trim_visible_lyrics leaves things alone if they already fit ===\n");
    {
        VisibleLyricCandidate cands[3] = { { -1, 20, true }, { 0, 20, true }, { 1, 20, true } };
        int remaining = trim_visible_lyrics(cands, 3, 10, 1000);
        CHECK(remaining == 3, "nothing trimmed when it already fits");
    }

    printf("=== title scroll: short title never scrolls ===\n");
    {
        TitleScrollState st = compute_title_scroll(100, 200, 5.0f, 18.0f, 3.0f);
        CHECK(!st.active, "inactive when title fits");
        CHECK(st.scroll_x == 0.0f, "scroll_x is 0");
    }

    printf("=== title scroll: long title cycles pause -> scroll -> pause -> reset ===\n");
    {
        /* title_width=300, available=100 -> max_scroll=200, speed=20 -> scroll_duration=10s
         * cycle: [0,3)=pause@0, [3,13)=scrolling 0..200, [13,16)=pause@200, [16,19)=snapped to 0 (extra pause gap) */
        float title_width = 300, avail = 100, speed = 20, pause = 3;

        TitleScrollState s0 = compute_title_scroll(title_width, avail, 1.0f, speed, pause);
        CHECK(s0.active && s0.scroll_x == 0.0f, "t=1s: still in opening pause, scroll_x=0");

        TitleScrollState s1 = compute_title_scroll(title_width, avail, 8.0f, speed, pause);
        /* elapsed in scroll phase = 8-3=5s * 20px/s = 100px */
        CHECK(fabsf(s1.scroll_x - 100.0f) < 0.01f, "t=8s: mid-scroll at expected position (100px)");

        TitleScrollState s2 = compute_title_scroll(title_width, avail, 14.0f, speed, pause);
        CHECK(fabsf(s2.scroll_x - 200.0f) < 0.01f, "t=14s: end-of-scroll pause, scroll_x=max_scroll(200px)");

        TitleScrollState s3 = compute_title_scroll(title_width, avail, 18.0f, speed, pause);
        CHECK(s3.scroll_x == 0.0f, "t=18s: past full cycle (19s), snapped back to 0");

        /* cycle_duration+pause = 3+10+3+3 = 19s, so t=19 should equal t=0 */
        TitleScrollState s_wrap_a = compute_title_scroll(title_width, avail, 19.0f, speed, pause);
        TitleScrollState s_wrap_b = compute_title_scroll(title_width, avail, 0.0f, speed, pause);
        CHECK(fabsf(s_wrap_a.scroll_x - s_wrap_b.scroll_x) < 0.01f, "cycle wraps correctly at period boundary");
    }

    printf("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
