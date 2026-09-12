#ifndef UI_SCALE_H
#define UI_SCALE_H

#define UI_REFERENCE_WIDTH  480.0f
#define UI_REFERENCE_HEIGHT 320.0f

static inline float compute_ui_scale(int screen_w, int screen_h) {
    float scale_w = (float)screen_w / UI_REFERENCE_WIDTH;
    float scale_h = (float)screen_h / UI_REFERENCE_HEIGHT;
    return (scale_w < scale_h) ? scale_w : scale_h;
}

#endif
