
#ifndef IMAGE_H
#define IMAGE_H

#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    GLuint texture;
    int width, height;
} Image;

bool image_load_from_memory(const uint8_t *data, size_t size, Image *out);

/* Convenience for testing/development: loads straight from a file path. */
bool image_load_from_file(const char *path, Image *out);

void image_free(Image *img);

#endif
