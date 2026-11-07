/* The draw device: see gl_dev.h. */
#include "gl_dev.h"

#include "persist.h"
#include "platform.h"
#include "txmp.h"

#include <GLES3/gl3.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static GLuint prog, vao, stream_vbo, stream_ibo, white;
static struct { GLint proj, view, tex, alpha_ref, fog, fog_on, fog_colour; } u;
static float fog_start, fog_end, fog_rgb[3], cur_proj[16], cur_view[16];

static const char *VS =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "layout(location=2) in vec4 a_colour;\n"
    "uniform mat4 u_proj, u_view;\n"
    "out vec2 v_uv; out vec4 v_colour;\n"
    "void main() {\n"
    "  vec4 e = u_view * vec4(a_pos, 1.0);\n"
    "  v_uv = a_uv; v_colour = a_colour;\n"
    "  gl_Position = u_proj * e;\n"
    "}\n";

static const char *FS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform float u_alpha_ref;\n"
    "uniform highp vec2 u_fog; uniform float u_fog_on; uniform vec3 u_fog_colour;\n"
    "in vec2 v_uv; in vec4 v_colour;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  vec4 c = texture(u_tex, v_uv) * v_colour;\n"
    "  if (c.a < u_alpha_ref) discard;\n"
    "  highp float z = gl_FragCoord.z * 2.0 - 1.0;\n"
    "  if (u_fog_on > 0.5 && u_fog.y > u_fog.x) c.rgb = mix(c.rgb, u_fog_colour, float(clamp((z - u_fog.x) / (u_fog.y - u_fog.x), 0.0, 1.0)));\n"
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

static bool dry;      /* a frame not shown: its drawing walked, nothing sent to GL */
void gl_dev_set_dry(bool on) { dry = on; }

/* ---- the display's gamma (0x407a60): a ramp of the display's own (taken as the identity) to the power
   (1 - gamma) 1.2 + 0.4, 0.5 the identity. Off the identity the frame is drawn into a target of its own
   and the ramp applied on the way to the screen, each 8-bit value through the 16-bit ramp's entry and
   back to its high byte. ---- */

static const char *GAMMA_VS =
    "#version 300 es\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  vec2 p = vec2(float((gl_VertexID & 1) << 2) - 1.0, float((gl_VertexID & 2) << 1) - 1.0);\n"
    "  v_uv = p * 0.5 + 0.5;\n"
    "  gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

static const char *GAMMA_FS =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform sampler2D u_frame;\n"
    "uniform float u_power;\n"
    "in vec2 v_uv;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  vec3 c = floor(texture(u_frame, v_uv).rgb * 255.0 + 0.5) / 255.0;\n"
    "  vec3 r = clamp(floor(pow(c, vec3(u_power)) * 65535.0 + 0.5), 0.0, 65535.0);\n"
    "  o = vec4(floor(r / 256.0) / 255.0, 1.0);\n"
    "}\n";

static GLuint gamma_prog, gamma_fbo, gamma_tex, gamma_depth, gamma_vao;
static GLint gamma_u_frame, gamma_u_power;
static int gamma_w, gamma_h;
static float gamma_power = 1.0f;
static bool gamma_on;       /* this frame is drawn into the target */

static void gamma_present(void)
{
    if (!gamma_on) return;
    gamma_on = false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, gamma_w, gamma_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(gamma_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gamma_tex);
    glUniform1i(gamma_u_frame, 0);
    glUniform1f(gamma_u_power, gamma_power);
    glBindVertexArray(gamma_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(vao);
}

/* At a frame's start: into the target when the ramp isn't the identity. */
static void gamma_begin(int w, int h)
{
    float g = persist_gamma();
    gamma_power = (1.0f - g) * 1.2f + 0.4f;    /* 0x529508, 0x529504, 0x529500 */
    if (gamma_power == 1.0f) {
        if (gamma_on) glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gamma_on = false;
        return;
    }
    if (!gamma_prog) {
        gamma_prog = glCreateProgram();
        glAttachShader(gamma_prog, shader(GL_VERTEX_SHADER, GAMMA_VS));
        glAttachShader(gamma_prog, shader(GL_FRAGMENT_SHADER, GAMMA_FS));
        glLinkProgram(gamma_prog);
        gamma_u_frame = glGetUniformLocation(gamma_prog, "u_frame");
        gamma_u_power = glGetUniformLocation(gamma_prog, "u_power");
        glGenVertexArrays(1, &gamma_vao);
        glGenFramebuffers(1, &gamma_fbo);
        glGenTextures(1, &gamma_tex);
        glGenRenderbuffers(1, &gamma_depth);
        plat_present_hook = gamma_present;
    }
    if (w != gamma_w || h != gamma_h) {
        gamma_w = w, gamma_h = h;
        glBindTexture(GL_TEXTURE_2D, gamma_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindRenderbuffer(GL_RENDERBUFFER, gamma_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, gamma_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gamma_tex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gamma_depth);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, gamma_fbo);
    gamma_on = true;
}

bool gl_dev_init(void)
{
    if (prog) return true;      /* once: the next levels keep it */
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
    u.fog_on = glGetUniformLocation(prog, "u_fog_on");
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
    if (dry) return;
    gamma_begin(w, h);
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
    memcpy(cur_proj, proj, sizeof cur_proj);
    memcpy(cur_view, view, sizeof cur_view);
    if (dry) return;
    glUseProgram(prog);
    glUniformMatrix4fv(u.proj, 1, GL_FALSE, proj);
    glUniformMatrix4fv(u.view, 1, GL_FALSE, view);
}

void gl_dev_fog(float start, float end, float r, float g, float b)
{
    fog_start = start;
    fog_end = end;
    fog_rgb[0] = r, fog_rgb[1] = g, fog_rgb[2] = b;
    if (dry) return;
    glUseProgram(prog);
    glUniform2f(u.fog, fog_start, fog_end);
    glUniform3f(u.fog_colour, r, g, b);
}

/* gl_calculate_fog_factor 0x40dcf0: p's depth on the screen (MSrTransform_PointListToFrustumScreen 0x418100,
   z over w) against the fog: 0 without fog (start = end, start not below 1, end not above 0) and at or
   before the start, (end - z) / (end - start) between, and 1 at or past the end (as the original: the
   factor climbs back past the end, where the far plane is). */
float gl_dev_fog_factor(const float p[3])
{
    if (fog_start == fog_end || !(fog_start < 1.0f) || !(fog_end > 0.0f)) return 0.0f;
    float e[4], c[4];
    for (int i = 0; i < 4; i++) e[i] = cur_view[i] * p[0] + cur_view[4 + i] * p[1] + cur_view[8 + i] * p[2] + cur_view[12 + i];
    for (int i = 0; i < 4; i++) c[i] = cur_proj[i] * e[0] + cur_proj[4 + i] * e[1] + cur_proj[8 + i] * e[2] + cur_proj[12 + i] * e[3];
    float z = c[2] * (1.0f / c[3]);
    if (!(z > fog_start)) return 0.0f;
    if (!(z < fog_end)) return 1.0f;
    return (fog_end - z) / (fog_end - fog_start);
}

void gl_dev_cull(int mode)
{
    if (dry) return;
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
        /* the OpenGL upload (0x40cf40): below the graphics quality 2 (0x508530, high-quality textures
           and trilinear) a mipmapped texture over 2 by 2 drops its top level, and its levels are
           filtered nearest between (GL_LINEAR_MIPMAP_NEAREST) instead of linearly; flags 4 and 8 clamp
           s and t (GL_CLAMP, which ES lacks: to the edge) */
        bool hq = persist_quality() >= 2, mips = (in.flags & TXMP_FLAG_MIPMAPS) != 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        unsigned w = in.width, hh = in.height, level = 0;
        int drop = !hq && mips && w > 2 && hh > 2 ? 1 : 0;
        size_t off = 0;
        for (;;) {
            if ((int)level >= drop)
                glTexImage2D(GL_TEXTURE_2D, (GLint)level - drop, GL_RGBA, (GLsizei)w, (GLsizei)hh, 0, GL_RGBA, GL_UNSIGNED_BYTE, px + off);
            off += (size_t)w * hh * 4;
            if (!mips || (w == 1 && hh == 1) || off >= size) break;
            w = w > 1 ? w / 2 : 1;
            hh = hh > 1 ? hh / 2 : 1;
            level++;
        }
        int top = (int)level - drop;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, top > 0 ? top : 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, top > 0 ? (hq ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST) : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (in.flags & 4) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (in.flags & 8) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
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

unsigned gl_dev_texture_rgba(unsigned *name, int w, int h, const uint8_t *rgba)
{
    if (dry || w <= 0 || h <= 0) return *name ? *name : white;
    GLuint t = *name;
    if (!t) {
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        *name = t;
    }
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return t;
}

void gl_dev_texture_free(unsigned *name)
{
    if (*name) {
        GLuint t = *name;
        glDeleteTextures(1, &t);
    }
    *name = 0;
}

void gl_dev_screen(int w, int h)
{
    if (dry) return;
    float proj[16] = { 0 }, view[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    proj[0] = 2.0f / (float)(w > 0 ? w : 1);
    proj[5] = -2.0f / (float)(h > 0 ? h : 1);
    proj[10] = 1.0f;
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[15] = 1.0f;
    gl_dev_matrices(proj, view);
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

void gl_dev_mesh_indices(unsigned m, const uint32_t *idx, uint32_t ni)
{
    if (dry || !m || m >= MAX_MESHES || !meshes[m].vao) return;
    glBindVertexArray(meshes[m].vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshes[m].ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(ni * sizeof *idx), ni ? (const void *)idx : NULL, GL_DYNAMIC_DRAW);
    glBindVertexArray(0);
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
    if (mode == GL_DEV_SCREEN || mode == GL_DEV_SCREEN_ADD || mode == GL_DEV_SKY) glDisable(GL_DEPTH_TEST);
    else glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    /* the fog (the draw state 0xf, on by default): not on the 2D path, nor the sky (ONiGameState_Display_Reflectable
       0x4fb69f), nor additive textures (0x40d640 turns GL_FOG off with flag 0x400's blend) */
    glUniform1f(u.fog_on, mode == GL_DEV_SCREEN || mode == GL_DEV_SCREEN_ADD || mode == GL_DEV_SKY || mode == GL_DEV_ADD ? 0.0f : 1.0f);
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
    case GL_DEV_REFLECT:
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_SRC_ALPHA);
        glDepthMask(GL_TRUE);
        glUniform1f(u.alpha_ref, -1.0f);
        break;
    case GL_DEV_SKY:
        glDisable(GL_BLEND);
        glDepthMask(GL_FALSE);
        glUniform1f(u.alpha_ref, -1.0f);
        break;
    default:
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glUniform1f(u.alpha_ref, mode == GL_DEV_ALPHA_TEST ? 0.5f : -1.0f);
    }
}

void gl_dev_draw_mesh(unsigned m, uint32_t first, uint32_t count, unsigned tex, int mode)
{
    if (dry) return;
    if (!m || m >= MAX_MESHES || !meshes[m].vao || !count) return;
    state(tex, mode);
    glBindVertexArray(meshes[m].vao);
    glDrawElements(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_INT, (void *)(uintptr_t)(first * 4));
    glBindVertexArray(0);
}

void gl_dev_draw(const GlVertex *v, uint32_t nv, const uint16_t *idx, uint32_t ni, unsigned tex, int mode)
{
    if (dry) return;
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

void gl_dev_lines(const GlVertex *v, uint32_t nv, int mode)
{
    if (dry || nv < 2) return;
    state(0, mode);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, stream_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nv * sizeof *v), v, GL_STREAM_DRAW);
    attribs();
    glDrawArrays(GL_LINES, 0, (GLsizei)(nv & ~1u));
    glBindVertexArray(0);
}
