/* The draw device: see gl_dev.h. */
#include "gl_dev.h"

#include "platform.h"
#include "txmp.h"

#include <GLES3/gl3.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static GLuint prog, vao, stream_vbo, stream_ibo, white;
static struct { GLint proj, view, tex, alpha_ref, fog, fog_colour; } u;
static float fog_start, fog_end, fog_rgb[3];

static const char *VS =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "layout(location=2) in vec4 a_colour;\n"
    "uniform mat4 u_proj, u_view;\n"
    "out vec2 v_uv; out vec4 v_colour; out float v_dist;\n"
    "void main() {\n"
    "  vec4 e = u_view * vec4(a_pos, 1.0);\n"
    "  v_dist = length(e.xyz);\n"
    "  v_uv = a_uv; v_colour = a_colour;\n"
    "  gl_Position = u_proj * e;\n"
    "}\n";

static const char *FS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform float u_alpha_ref;\n"
    "uniform vec2 u_fog; uniform vec3 u_fog_colour;\n"
    "in vec2 v_uv; in vec4 v_colour; in float v_dist;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  vec4 c = texture(u_tex, v_uv) * v_colour;\n"
    "  if (c.a < u_alpha_ref) discard;\n"
    "  if (u_fog.y > u_fog.x) c.rgb = mix(c.rgb, u_fog_colour, clamp((v_dist - u_fog.x) / (u_fog.y - u_fog.x), 0.0, 1.0));\n"
    "  o = c;\n"
    "}\n";

static GLuint shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        plat_log(log);
    }
    return s;
}

static void attribs(void)
{
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GlVertex), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GlVertex), (void *)offsetof(GlVertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GlVertex), (void *)offsetof(GlVertex, r));
}

bool gl_dev_init(void)
{
    prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER, VS));
    glAttachShader(prog, shader(GL_FRAGMENT_SHADER, FS));
    glLinkProgram(prog);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) return false;
    u.proj = glGetUniformLocation(prog, "u_proj");
    u.view = glGetUniformLocation(prog, "u_view");
    u.tex = glGetUniformLocation(prog, "u_tex");
    u.alpha_ref = glGetUniformLocation(prog, "u_alpha_ref");
    u.fog = glGetUniformLocation(prog, "u_fog");
    u.fog_colour = glGetUniformLocation(prog, "u_fog_colour");
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &stream_vbo);
    glGenBuffers(1, &stream_ibo);
    static const uint8_t px[4] = { 255, 255, 255, 255 };
    glGenTextures(1, &white);
    glBindTexture(GL_TEXTURE_2D, white);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return true;
}

void gl_dev_begin(int w, int h, float r, float g, float b)
{
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(r, g, b, 1.0f);
    glClearDepthf(1.0f);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    /* The frame stays opaque: texel alpha (a shine mask on many character textures) mustn't reach the
       framebuffer, which the runner composites. */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glUseProgram(prog);
    glUniform1i(u.tex, 0);
    glUniform2f(u.fog, fog_start, fog_end);
    glUniform3f(u.fog_colour, fog_rgb[0], fog_rgb[1], fog_rgb[2]);
}

void gl_dev_matrices(const float proj[16], const float view[16])
{
    glUseProgram(prog);
    glUniformMatrix4fv(u.proj, 1, GL_FALSE, proj);
    glUniformMatrix4fv(u.view, 1, GL_FALSE, view);
}

void gl_dev_fog(float start, float end, float r, float g, float b)
{
    fog_start = start;
    fog_end = end;
    fog_rgb[0] = r, fog_rgb[1] = g, fog_rgb[2] = b;
    glUseProgram(prog);
    glUniform2f(u.fog, fog_start, fog_end);
    glUniform3f(u.fog_colour, r, g, b);
}

void gl_dev_cull(int mode)
{
    if (!mode) {
        glDisable(GL_CULL_FACE);
        return;
    }
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(mode > 0 ? GL_CCW : GL_CW);
}

/* ---- textures: a small open-addressing map from the TXMP's data to its GL name ---- */

enum { TEX_SLOTS = 8192 };
static struct { const void *txmp; GLuint name; } texs[TEX_SLOTS];

unsigned gl_dev_white(void) { return white; }

unsigned gl_dev_texture(const void *txmp)
{
    if (!txmp) return white;
    size_t h = ((uintptr_t)txmp >> 3) % TEX_SLOTS;
    while (texs[h].txmp && texs[h].txmp != txmp) h = (h + 1) % TEX_SLOTS;
    if (texs[h].txmp) return texs[h].name;
    TxmpInfo in;
    txmp_info(txmp, &in);
    uint32_t size;
    uint8_t *px = txmp_rgba(txmp, &size);
    GLuint t = 0;
    if (px) {
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        unsigned w = in.width, hh = in.height, level = 0;
        size_t off = 0;
        for (;;) {
            glTexImage2D(GL_TEXTURE_2D, (GLint)level, GL_RGBA, (GLsizei)w, (GLsizei)hh, 0, GL_RGBA, GL_UNSIGNED_BYTE, px + off);
            off += (size_t)w * hh * 4;
            if (!(in.flags & TXMP_FLAG_MIPMAPS) || (w == 1 && hh == 1) || off >= size) break;
            w = w > 1 ? w / 2 : 1;
            hh = hh > 1 ? hh / 2 : 1;
            level++;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)level);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, level ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        free(px);
    }
    texs[h].txmp = txmp;
    texs[h].name = t ? t : white;
    return texs[h].name;
}

unsigned gl_dev_texture_alpha(const void *key, int w, int h, const uint8_t *alpha)
{
    if (!key || w <= 0 || h <= 0) return white;
    size_t k = ((uintptr_t)key >> 3) % TEX_SLOTS;
    while (texs[k].txmp && texs[k].txmp != key) k = (k + 1) % TEX_SLOTS;
    if (texs[k].txmp) return texs[k].name;
    uint8_t *px = malloc((size_t)w * h * 4);
    GLuint t = 0;
    if (px) {
        for (int i = 0; i < w * h; i++) px[4 * i] = px[4 * i + 1] = px[4 * i + 2] = 255, px[4 * i + 3] = alpha[i];
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        free(px);
    }
    texs[k].txmp = key;
    texs[k].name = t ? t : white;
    return texs[k].name;
}

void gl_dev_screen(int w, int h)
{
    float proj[16] = { 0 }, view[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    proj[0] = 2.0f / (float)(w > 0 ? w : 1);
    proj[5] = -2.0f / (float)(h > 0 ? h : 1);
    proj[10] = 1.0f;
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[15] = 1.0f;
    gl_dev_matrices(proj, view);
    gl_dev_fog(0, 0, 0, 0, 0);
    gl_dev_cull(0);
}

void gl_dev_forget_textures(void)
{
    for (int i = 0; i < TEX_SLOTS; i++) {
        if (texs[i].txmp && texs[i].name != white) glDeleteTextures(1, &texs[i].name);
        texs[i].txmp = NULL;
    }
}

/* ---- meshes ---- */

enum { MAX_MESHES = 64 };
static struct { GLuint vao, vbo, ibo; } meshes[MAX_MESHES];

unsigned gl_dev_mesh(const GlVertex *v, uint32_t nv, const uint32_t *idx, uint32_t ni)
{
    for (unsigned m = 1; m < MAX_MESHES; m++) {
        if (meshes[m].vao) continue;
        glGenVertexArrays(1, &meshes[m].vao);
        glBindVertexArray(meshes[m].vao);
        glGenBuffers(1, &meshes[m].vbo);
        glBindBuffer(GL_ARRAY_BUFFER, meshes[m].vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nv * sizeof *v), v, GL_STATIC_DRAW);
        glGenBuffers(1, &meshes[m].ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshes[m].ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(ni * sizeof *idx), idx, GL_STATIC_DRAW);
        attribs();
        glBindVertexArray(0);
        return m;
    }
    return 0;
}

void gl_dev_mesh_free(unsigned m)
{
    if (!m || m >= MAX_MESHES || !meshes[m].vao) return;
    glDeleteBuffers(1, &meshes[m].vbo);
    glDeleteBuffers(1, &meshes[m].ibo);
    glDeleteVertexArrays(1, &meshes[m].vao);
    memset(&meshes[m], 0, sizeof meshes[m]);
}

static void state(unsigned tex, int mode)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex ? tex : white);
    if (mode == GL_DEV_SCREEN || mode == GL_DEV_SCREEN_ADD) glDisable(GL_DEPTH_TEST);
    else glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    switch (mode) {
    case GL_DEV_SCREEN:
    case GL_DEV_SCREEN_ADD:
    case GL_DEV_BLEND:
    case GL_DEV_ADD:
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, mode == GL_DEV_ADD || mode == GL_DEV_SCREEN_ADD ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glUniform1f(u.alpha_ref, 0.0f);
        break;
    default:
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glUniform1f(u.alpha_ref, mode == GL_DEV_ALPHA_TEST ? 0.5f : -1.0f);
    }
}

void gl_dev_draw_mesh(unsigned m, uint32_t first, uint32_t count, unsigned tex, int mode)
{
    if (!m || m >= MAX_MESHES || !meshes[m].vao || !count) return;
    state(tex, mode);
    glBindVertexArray(meshes[m].vao);
    glDrawElements(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_INT, (void *)(uintptr_t)(first * 4));
    glBindVertexArray(0);
}

void gl_dev_draw(const GlVertex *v, uint32_t nv, const uint16_t *idx, uint32_t ni, unsigned tex, int mode)
{
    if (!ni) return;
    state(tex, mode);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, stream_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nv * sizeof *v), v, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, stream_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(ni * sizeof *idx), idx, GL_STREAM_DRAW);
    attribs();
    glDrawElements(GL_TRIANGLES, (GLsizei)ni, GL_UNSIGNED_SHORT, 0);
    glBindVertexArray(0);
}
