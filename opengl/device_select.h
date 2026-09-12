#ifndef DEVICE_SELECT_H
#define DEVICE_SELECT_H

#include <stdbool.h>

#define MAX_DEVICE_CANDIDATES 8

typedef struct {
    char path[256];
    bool connected;
    int width, height;   /* preferred mode resolution, for logging */
    char driver_name[64]; /* e.g. "vc4", "v3d", "ili9486drmfb" - for logging */
} DisplayCandidate;

int select_display_candidate(const DisplayCandidate *candidates, int count, const char *forced_path);

typedef struct {
    char path[256];
} RenderNodeCandidate;

int select_render_node_candidate(const RenderNodeCandidate *candidates, int count, const char *forced_path);

#endif
