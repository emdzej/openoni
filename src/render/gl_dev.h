/* The draw device on OpenGL ES 3.0 (gasm:gl, WebGL 2 rules): what the Motoko draw engine's OpenGL back
   end did with OpenGL 1.1 (textured, vertex-coloured triangles, alpha test, blending, depth, fog), done
   with one shader. Matrices are column-major, as GL's. Only the renderer calls it. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float x, y, z, u, v;
    uint8_t r, g, b, a;
} GlVertex;

enum {
    GL_DEV_OPAQUE,          /* depth test and write, no blending */
    GL_DEV_ALPHA_TEST,      /* as opaque, texels with alpha below one half dropped */
    GL_DEV_BLEND,           /* depth test without write, source alpha blending */
    GL_DEV_ADD,             /* depth test without write, additive */
    GL_DEV_SCREEN,          /* no depth test, source alpha blending: the 2D path (gl_dev_screen) */
    GL_DEV_SCREEN_ADD,      /* the same, added to what is there */
};

bool gl_dev_init(void);
/* Starts a frame: the viewport over the drawable, cleared to the colour and the far depth. */
void gl_dev_begin(int w, int h, float r, float g, float b);
/* A frame the platform doesn't show: the drawing is walked (what it changes, as the textures' random
   frames do, still happens) but nothing is drawn. Textures and meshes are still made. */
void gl_dev_set_dry(bool on);
void gl_dev_matrices(const float proj[16], const float view[16]);
/* Linear fog from start to end (distances from the eye), off if end <= start. */
void gl_dev_fog(float start, float end, float r, float g, float b);
/* The GL texture of a TXMP (all its mip levels), uploaded on first use; 0 if it doesn't decode. */
unsigned gl_dev_texture(const void *txmp);
/* The texture of white texels, for untextured draws. */
unsigned gl_dev_white(void);
/* A texture of white texels with the given alpha (w * h bytes), kept under key (a glyph, say) and
   uploaded on first use. */
unsigned gl_dev_texture_alpha(const void *key, int w, int h, const uint8_t *alpha);
/* A texture of w × h RGBA pixels the caller refreshes (a movie's frames): made when *name is 0, its
   pixels replaced by rgba. gl_dev_texture_free deletes it. */
unsigned gl_dev_texture_rgba(unsigned *name, int w, int h, const uint8_t *rgba);
void gl_dev_texture_free(unsigned *name);
/* Screen space for the 2D path: x right and y down in pixels of a w by h drawable, z ignored. */
void gl_dev_screen(int w, int h);
/* Frees every texture uploaded (when a level goes). */
void gl_dev_forget_textures(void);

/* A static mesh: vertices and triangle indices kept on the GPU. Returns an id > 0, 0 on failure. */
unsigned gl_dev_mesh(const GlVertex *v, uint32_t nv, const uint32_t *idx, uint32_t ni);
void gl_dev_mesh_free(unsigned mesh);
/* Draws count indices from first of a static mesh. */
void gl_dev_draw_mesh(unsigned mesh, uint32_t first, uint32_t count, unsigned tex, int mode);
/* Draws triangles from client memory (streamed). */
void gl_dev_draw(const GlVertex *v, uint32_t nv, const uint16_t *idx, uint32_t ni, unsigned tex, int mode);
/* Draws lines (pairs of vertices) from client memory, untextured: the draw engine's line (0x41f440). */
void gl_dev_lines(const GlVertex *v, uint32_t nv, int mode);
/* Back-face culling: 0 none, 1 counter-clockwise front faces kept, -1 clockwise kept. */
void gl_dev_cull(int mode);
