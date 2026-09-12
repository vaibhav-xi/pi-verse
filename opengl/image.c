#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb_image.h"
#include "image.h"

#include <stdio.h>

static bool upload_texture(unsigned char *pixels, int w, int h, Image *out) {
    if (!pixels) {
        fprintf(stderr, "[image] decode failed: %s\n", stbi_failure_reason());
        return false;
    }

    glGenTextures(1, &out->texture);
    glBindTexture(GL_TEXTURE_2D, out->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    out->width = w;
    out->height = h;
    stbi_image_free(pixels);
    return true;
}

bool image_load_from_memory(const uint8_t *data, size_t size, Image *out) {
    int w, h, channels;
    unsigned char *pixels = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
    return upload_texture(pixels, w, h, out);
}

bool image_load_from_file(const char *path, Image *out) {
    int w, h, channels;
    unsigned char *pixels = stbi_load(path, &w, &h, &channels, 4);
    if (!pixels) fprintf(stderr, "[image] could not load %s: %s\n", path, stbi_failure_reason());
    return upload_texture(pixels, w, h, out);
}

void image_free(Image *img) {
    if (img->texture) glDeleteTextures(1, &img->texture);
    img->texture = 0;
    img->width = img->height = 0;
}
