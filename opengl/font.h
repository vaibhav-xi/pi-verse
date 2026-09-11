
#ifndef FONT_H
#define FONT_H

#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdbool.h>

#define FONT_FIRST_CODEPOINT 32
#define FONT_NUM_CODEPOINTS  224  /* 32..255 inclusive */
#define FONT_ATLAS_SIZE      512  /* atlas bitmap is FONT_ATLAS_SIZE^2, 1 channel */

typedef struct {
    GLuint texture;
    void *packed_chars;   /* stbtt_packedchar[FONT_NUM_CODEPOINTS], opaque here to avoid pulling stb into every TU */
    float pixel_height;
    float ascent, descent, line_gap; /* in pixels, from the font's metrics at this size */
} Font;

typedef struct {
    GLuint program;
    GLint a_pos, a_uv;
    GLint u_screen_size, u_color, u_use_texture, u_tex;
} GfxShader;

extern GfxShader g_gfx_shader; /* defined in font.c */

bool gfx_init_shader(int screen_width, int screen_height);

bool font_load(const char *font_path, float pixel_height, Font *out);
void font_free(Font *font);

float font_draw_text(const Font *font, float x, float y, const char *utf8_text,
                      float r, float g, float b, float a);

/* Width in pixels of utf8_text if drawn with this font. Doesn't touch GL. */
float font_measure_text(const Font *font, const char *utf8_text);

/* Fills an axis-aligned rectangle with a solid color (no texture). */
void gfx_fill_rect(float x, float y, float w, float h, float r, float g, float b, float a);

#endif
