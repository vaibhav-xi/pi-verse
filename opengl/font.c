#define _DEFAULT_SOURCE
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GfxShader g_gfx_shader;

/* ---- tiny UTF-8 decoder ---- */
static uint32_t utf8_next_codepoint(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    if (p[0] == 0) return 0;

    uint32_t cp;
    int len;
    if ((p[0] & 0x80) == 0)      { cp = p[0];        len = 1; }
    else if ((p[0] & 0xE0) == 0xC0) { cp = p[0] & 0x1F; len = 2; }
    else if ((p[0] & 0xF0) == 0xE0) { cp = p[0] & 0x0F; len = 3; }
    else if ((p[0] & 0xF8) == 0xF0) { cp = p[0] & 0x07; len = 4; }
    else { *s += 1; return 0xFFFD; }

    for (int i = 1; i < len; i++) {
        if (p[i] == 0) { *s += i; return 0xFFFD; }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += len;
    return cp;
}

/* ---- shader ---- */

static const char *VS_SRC =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "uniform vec2 u_screen_size;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 ndc = vec2(\n"
    "        (a_pos.x / u_screen_size.x) * 2.0 - 1.0,\n"
    "        1.0 - (a_pos.y / u_screen_size.y) * 2.0\n"
    "    );\n"
    "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "}\n";

static const char *FS_SRC =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec4 u_color;\n"
    "uniform float u_texture_mode;\n" /* 0=solid color, 1=alpha-mask (text), 2=full RGBA image */
    "void main() {\n"
    "    if (u_texture_mode < 0.5) {\n"
    "        gl_FragColor = u_color;\n"
    "    } else if (u_texture_mode < 1.5) {\n"
    "        float a = texture2D(u_tex, v_uv).a;\n"
    "        gl_FragColor = vec4(u_color.rgb, u_color.a * a);\n"
    "    } else {\n"
    "        vec4 tex = texture2D(u_tex, v_uv);\n"
    "        gl_FragColor = vec4(tex.rgb, tex.a * u_color.a);\n"
    "    }\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "[font] shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool gfx_init_shader(int screen_width, int screen_height) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, VS_SRC);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FS_SRC);
    if (!vs || !fs) return false;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "a_pos");
    glBindAttribLocation(prog, 1, "a_uv");
    glLinkProgram(prog);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "[font] program link error: %s\n", log);
        return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    g_gfx_shader.program = prog;
    g_gfx_shader.a_pos = 0;
    g_gfx_shader.a_uv = 1;
    g_gfx_shader.u_screen_size = glGetUniformLocation(prog, "u_screen_size");
    g_gfx_shader.u_color = glGetUniformLocation(prog, "u_color");
    g_gfx_shader.u_texture_mode = glGetUniformLocation(prog, "u_texture_mode");
    g_gfx_shader.u_tex = glGetUniformLocation(prog, "u_tex");

    glUseProgram(prog);
    glUniform2f(g_gfx_shader.u_screen_size, (float)screen_width, (float)screen_height);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

void gfx_fill_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    float verts[6][4] = {
        { x,     y,     0, 0 },
        { x + w, y,     0, 0 },
        { x,     y + h, 0, 0 },
        { x,     y + h, 0, 0 },
        { x + w, y,     0, 0 },
        { x + w, y + h, 0, 0 },
    };

    glUseProgram(g_gfx_shader.program);
    glUniform4f(g_gfx_shader.u_color, r, g, b, a);
    glUniform1f(g_gfx_shader.u_texture_mode, 0.0f);

    glEnableVertexAttribArray(g_gfx_shader.a_pos);
    glEnableVertexAttribArray(g_gfx_shader.a_uv);
    glVertexAttribPointer(g_gfx_shader.a_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &verts[0][0]);
    glVertexAttribPointer(g_gfx_shader.a_uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &verts[0][2]);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

/* ---- font baking ---- */

bool font_load(const char *font_path, float pixel_height, Font *out) {
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(font_path, "rb");
    if (!f) { fprintf(stderr, "[font] could not open %s\n", font_path); return false; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *ttf_buffer = malloc((size_t)size);
    if (!ttf_buffer || fread(ttf_buffer, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "[font] failed to read %s\n", font_path);
        fclose(f);
        free(ttf_buffer);
        return false;
    }
    fclose(f);

    unsigned char *atlas_bitmap = calloc(1, FONT_ATLAS_SIZE * FONT_ATLAS_SIZE);
    stbtt_packedchar *packed = malloc(sizeof(stbtt_packedchar) * FONT_NUM_CODEPOINTS);
    if (!atlas_bitmap || !packed) { fprintf(stderr, "[font] out of memory\n"); goto fail; }

    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, atlas_bitmap, FONT_ATLAS_SIZE, FONT_ATLAS_SIZE, 0, 1, NULL)) {
        fprintf(stderr, "[font] stbtt_PackBegin failed\n");
        goto fail;
    }
    stbtt_PackSetOversampling(&pc, 2, 2);
    if (!stbtt_PackFontRange(&pc, ttf_buffer, 0, pixel_height,
                              FONT_FIRST_CODEPOINT, FONT_NUM_CODEPOINTS, packed)) {
        fprintf(stderr, "[font] stbtt_PackFontRange failed - atlas too small for "
                        "%d glyphs at %.0fpx (FONT_ATLAS_SIZE=%d). Increase it.\n",
                FONT_NUM_CODEPOINTS, pixel_height, FONT_ATLAS_SIZE);
        stbtt_PackEnd(&pc);
        goto fail;
    }
    stbtt_PackEnd(&pc);

    /* Font-wide vertical metrics at this pixel size, for layout. */
    stbtt_fontinfo info;
    stbtt_InitFont(&info, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0));
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    float scale = stbtt_ScaleForPixelHeight(&info, pixel_height);
    out->ascent = ascent * scale;
    out->descent = descent * scale;
    out->line_gap = line_gap * scale;
    out->pixel_height = pixel_height;
    out->packed_chars = packed;

    glGenTextures(1, &out->texture);
    glBindTexture(GL_TEXTURE_2D, out->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, FONT_ATLAS_SIZE, FONT_ATLAS_SIZE, 0,
                 GL_ALPHA, GL_UNSIGNED_BYTE, atlas_bitmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    free(atlas_bitmap);
    free(ttf_buffer);
    return true;

fail:
    free(atlas_bitmap);
    free(ttf_buffer);
    free(packed);
    return false;
}

void font_free(Font *font) {
    if (font->texture) glDeleteTextures(1, &font->texture);
    free(font->packed_chars);
    memset(font, 0, sizeof(*font));
}

static const stbtt_packedchar *glyph_for(const Font *font, uint32_t codepoint) {
    const stbtt_packedchar *chars = (const stbtt_packedchar *)font->packed_chars;
    if (codepoint >= FONT_FIRST_CODEPOINT && codepoint < FONT_FIRST_CODEPOINT + FONT_NUM_CODEPOINTS)
        return &chars[codepoint - FONT_FIRST_CODEPOINT];
    return &chars['?' - FONT_FIRST_CODEPOINT]; /* fallback for out-of-range codepoints */
}

float font_measure_text(const Font *font, const char *utf8_text) {
    float width = 0.0f;
    const char *s = utf8_text;
    while (*s) {
        uint32_t cp = utf8_next_codepoint(&s);
        if (cp == 0) break;
        width += glyph_for(font, cp)->xadvance;
    }
    return width;
}

float font_draw_text(const Font *font, float x, float y, const char *utf8_text,
                      float r, float g, float b, float a) {
    size_t max_glyphs = strlen(utf8_text);
    if (max_glyphs == 0) return x;

    float *verts = malloc(sizeof(float) * 4 * 6 * max_glyphs); /* 4 floats/vert, 6 verts/glyph */
    size_t glyph_count = 0;

    float xpos = x, ypos = y;
    const char *s = utf8_text;
    while (*s) {
        uint32_t cp = utf8_next_codepoint(&s);
        if (cp == 0) break;
        if (cp == ' ') { xpos += glyph_for(font, cp)->xadvance; continue; }

        stbtt_aligned_quad q;
        stbtt_packedchar pc_copy = *glyph_for(font, cp); /* GetPackedQuad wants non-const array ptr */
        stbtt_GetPackedQuad(&pc_copy, FONT_ATLAS_SIZE, FONT_ATLAS_SIZE, 0, &xpos, &ypos, &q, 1);

        float *v = verts + glyph_count * 6 * 4;
        float quad[6][4] = {
            { q.x0, q.y0, q.s0, q.t0 },
            { q.x1, q.y0, q.s1, q.t0 },
            { q.x0, q.y1, q.s0, q.t1 },
            { q.x0, q.y1, q.s0, q.t1 },
            { q.x1, q.y0, q.s1, q.t0 },
            { q.x1, q.y1, q.s1, q.t1 },
        };
        memcpy(v, quad, sizeof(quad));
        glyph_count++;
    }

    if (glyph_count > 0) {
        glUseProgram(g_gfx_shader.program);
        glUniform4f(g_gfx_shader.u_color, r, g, b, a);
        glUniform1f(g_gfx_shader.u_texture_mode, 1.0f);
        glUniform1i(g_gfx_shader.u_tex, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, font->texture);

        glEnableVertexAttribArray(g_gfx_shader.a_pos);
        glEnableVertexAttribArray(g_gfx_shader.a_uv);
        glVertexAttribPointer(g_gfx_shader.a_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), verts);
        glVertexAttribPointer(g_gfx_shader.a_uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), verts + 2);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(glyph_count * 6));
    }

    free(verts);
    return xpos;
}

void gfx_draw_textured_rect(GLuint texture_id, float x, float y, float w, float h, float alpha) {
    float verts[6][4] = {
        { x,     y,     0, 0 },
        { x + w, y,     1, 0 },
        { x,     y + h, 0, 1 },
        { x,     y + h, 0, 1 },
        { x + w, y,     1, 0 },
        { x + w, y + h, 1, 1 },
    };

    glUseProgram(g_gfx_shader.program);
    glUniform4f(g_gfx_shader.u_color, 1.0f, 1.0f, 1.0f, alpha);
    glUniform1f(g_gfx_shader.u_texture_mode, 2.0f);
    glUniform1i(g_gfx_shader.u_tex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    glEnableVertexAttribArray(g_gfx_shader.a_pos);
    glEnableVertexAttribArray(g_gfx_shader.a_uv);
    glVertexAttribPointer(g_gfx_shader.a_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &verts[0][0]);
    glVertexAttribPointer(g_gfx_shader.a_uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &verts[0][2]);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}
