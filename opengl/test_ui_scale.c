#include "ui_scale.h"
#include <stdio.h>
#include <math.h>

static int failures = 0;
#define CHECK_NEAR(actual, expected, msg) do { \
    float a = (actual), e = (expected); \
    if (fabsf(a - e) > 0.001f) { printf("  FAIL: %s (got %.4f, expected %.4f)\n", msg, a, e); failures++; } \
    else printf("  ok:   %s (%.4f)\n", msg, a); \
} while (0)

int main(void) {
    printf("=== exact reference resolution: scale must be exactly 1.0 ===\n");
    CHECK_NEAR(compute_ui_scale(480, 320), 1.0f, "480x320 (the original SPI panel) -> scale 1.0, zero regression risk");

    printf("=== common 5-inch HDMI resolution (800x480) ===\n");
    CHECK_NEAR(compute_ui_scale(800, 480), 1.5f, "800x480 -> constrained by height (480/320=1.5 < 800/480=1.667)");

    printf("=== larger HDMI display (1920x1080, 16:9) ===\n");
    CHECK_NEAR(compute_ui_scale(1920, 1080), 3.375f, "1920x1080 -> constrained by height (1080/320=3.375 < 1920/480=4.0)");

    printf("=== smaller-than-reference display doesn't produce a negative/zero scale ===\n");
    {
        float s = compute_ui_scale(240, 160);
        printf("  240x160 -> scale=%.4f\n", s);
        if (s > 0) printf("  ok:   scale is positive for a smaller display\n");
        else { printf("  FAIL: scale should be positive\n"); failures++; }
        CHECK_NEAR(s, 0.5f, "240x160 is exactly half the reference -> scale 0.5");
    }

    printf("=== portrait orientation doesn't overflow horizontally ===\n");
    {
        /* 320 wide x 480 tall: scale_w=320/480=0.667, scale_h=480/320=1.5 -> min=0.667 */
        float s = compute_ui_scale(320, 480);
        CHECK_NEAR(s, 0.6667f, "320x480 portrait -> constrained by the narrower width");
    }

    printf("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
