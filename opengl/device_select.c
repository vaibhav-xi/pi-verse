#include "device_select.h"
#include <string.h>

int select_display_candidate(const DisplayCandidate *candidates, int count, const char *forced_path) {
    if (forced_path && forced_path[0]) {
        for (int i = 0; i < count; i++)
            if (strcmp(candidates[i].path, forced_path) == 0) return i;
    }

    int first_connected = -1;
    int connected_count = 0;
    for (int i = 0; i < count; i++) {
        if (candidates[i].connected) {
            connected_count++;
            if (first_connected < 0) first_connected = i;
        }
    }

    if (connected_count >= 1) return first_connected;
    return -1;
}

int select_render_node_candidate(const RenderNodeCandidate *candidates, int count, const char *forced_path) {
    if (forced_path && forced_path[0]) {
        for (int i = 0; i < count; i++)
            if (strcmp(candidates[i].path, forced_path) == 0) return i;
    }
    return (count > 0) ? 0 : -1;
}
