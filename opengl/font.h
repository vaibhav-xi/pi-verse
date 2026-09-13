#ifndef FONT_H
#define FONT_H

#include <GLES2/gl2.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FONT_FIRST_CODEPOINT 32
#define FONT_NUM_CODEPOINTS  224  /* 32..255 inclusive */
#define FONT_EXTRA_FIRST_CODEPOINT 0x2013 /* EN DASH */
#define FONT_EXTRA_NUM_CODEPOINTS  0x14   /* through 0x2026 HORIZONTAL ELLIPSIS inclusive */
#define FONT_NOTE_CODEPOINT 0x266A         /* EIGHTH NOTE - synthesized, see note_icon.h */
#define FONT_ATLAS_SIZE      512  /* atlas bitmap is FONT_ATLAS_SIZE^2, 1 channel */

typedef struct {
    GLuint texture;
    void *packed_chars;       /* stbtt_packedchar[FONT_NUM_CODEPOINTS] */
    void *extra_packed_chars;  /* stbtt_packedchar[FONT_EXTRA_NUM_CODEPOINTS] */
    GLuint note_texture;        /* small standalone ALPHA texture for the synthesized note icon */
    float note_width, note_height; /* pixel size of the note icon quad, sits on the baseline like a normal glyph */
    float note_advance;          /* how far to move the cursor after drawing the note icon */
    float pixel_height;
    float ascent, descent, line_gap; /* in pixels, from the font's metrics at this size */
} Font;

typedef struct {
    GLuint program;
    GLint a_pos, a_uv;
    GLint u_screen_size, u_color, u_texture_mode, u_tex;
} GfxShader;

extern GfxShader g_gfx_shader; /* defined in font.c */

bool gfx_init_shader(int screen_width, int screen_height);

bool font_load(const char *font_path, float pixel_height, Font *out);
void font_free(Font *font);

float font_draw_text(const Font *font, float x, float y, const char *utf8_text,
                      float r, float g, float b, float a);

float font_measure_text(const Font *font, const char *utf8_text);

void font_truncate_text(const Font *font, const char *utf8_text, float max_width,
                         char *out, size_t out_size);

void gfx_fill_rect(float x, float y, float w, float h, float r, float g, float b, float a);

void gfx_draw_textured_rect(GLuint texture_id, float x, float y, float w, float h, float alpha);

#endif
