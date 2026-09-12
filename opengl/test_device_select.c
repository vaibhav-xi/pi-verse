#include "device_select.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else printf("  ok:   %s\n", msg); \
} while (0)

int main(void) {
    printf("=== SPI-only setup: one connected candidate, picked ===\n");
    {
        DisplayCandidate cands[] = {
            { "/dev/dri/card0", true, 480, 320, "ili9486drmfb" },
        };
        int idx = select_display_candidate(cands, 1, NULL);
        CHECK(idx == 0, "SPI panel selected (only candidate)");
    }

    printf("=== HDMI-only setup: vc4 connected, no SPI overlay present ===\n");
    {
        DisplayCandidate cands[] = {
            { "/dev/dri/card1", true, 800, 480, "vc4" },
        };
        int idx = select_display_candidate(cands, 1, NULL);
        CHECK(idx == 0, "HDMI display selected");
    }

    printf("=== both SPI overlay loaded and vc4 present, only SPI actually connected ===\n");
    {
        /* vc4's HDMI connector correctly reports disconnected (no cable) */
        DisplayCandidate cands[] = {
            { "/dev/dri/card0", true,  480, 320, "ili9486drmfb" },
            { "/dev/dri/card1", false, 0,   0,   "vc4" },
        };
        int idx = select_display_candidate(cands, 2, NULL);
        CHECK(idx == 0, "correctly skips the disconnected vc4/HDMI output, picks the SPI panel");
    }

    printf("=== ambiguous: both SPI panel and HDMI actually connected simultaneously ===\n");
    {
        DisplayCandidate cands[] = {
            { "/dev/dri/card0", true, 480, 320, "ili9486drmfb" },
            { "/dev/dri/card1", true, 800, 480, "vc4" },
        };
        int idx = select_display_candidate(cands, 2, NULL);
        CHECK(idx == 0, "ambiguous case picks the first candidate deterministically (caller should log + suggest --panel)");
    }

    printf("=== explicit override wins even over a different connected candidate ===\n");
    {
        DisplayCandidate cands[] = {
            { "/dev/dri/card0", true, 480, 320, "ili9486drmfb" },
            { "/dev/dri/card1", true, 800, 480, "vc4" },
        };
        int idx = select_display_candidate(cands, 2, "/dev/dri/card1");
        CHECK(idx == 1, "forced_path selects card1 even though card0 would win by default priority");
    }

    printf("=== forced path that matches nothing falls back to auto-detect ===\n");
    {
        DisplayCandidate cands[] = {
            { "/dev/dri/card0", true, 480, 320, "ili9486drmfb" },
        };
        int idx = select_display_candidate(cands, 1, "/dev/dri/card99");
        CHECK(idx == 0, "unmatched forced_path doesn't return -1, falls through to normal selection");
    }

    printf("=== nothing connected at all ===\n");
    {
        DisplayCandidate cands[] = {
            { "/dev/dri/card1", false, 0, 0, "vc4" },
        };
        int idx = select_display_candidate(cands, 1, NULL);
        CHECK(idx == -1, "returns -1 when no candidate is connected");
    }

    printf("=== empty candidate list ===\n");
    {
        int idx = select_display_candidate(NULL, 0, NULL);
        CHECK(idx == -1, "returns -1 for an empty list");
    }

    printf("=== render node: single candidate picked ===\n");
    {
        RenderNodeCandidate cands[] = { { "/dev/dri/renderD128" } };
        int idx = select_render_node_candidate(cands, 1, NULL);
        CHECK(idx == 0, "the only render node is selected");
    }

    printf("=== render node: none found ===\n");
    {
        int idx = select_render_node_candidate(NULL, 0, NULL);
        CHECK(idx == -1, "returns -1 when no render node exists (no GPU driver loaded)");
    }

    printf("=== render node: forced override ===\n");
    {
        RenderNodeCandidate cands[] = { { "/dev/dri/renderD128" }, { "/dev/dri/renderD129" } };
        int idx = select_render_node_candidate(cands, 2, "/dev/dri/renderD129");
        CHECK(idx == 1, "forced_path selects the second render node");
    }

    printf("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
