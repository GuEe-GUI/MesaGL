#include "GL/gl.h"
#include "gles2_internal.h"
#include "mesaGL/config.h"
#include "mesaGL/ntgl.h"

#include <string.h>

#define GL_MAX_TEXTURES 256
#define GL_ATTRIB_STACK 16
#define GL_MAX_FRAMEBUFFERS 32
#define GL_MAX_RENDERBUFFERS 32
#define GL_MAX_CONTEXTS 8
#define CA_VERTEX 1u
#define CA_COLOR 2u
#define CA_TEXCOORD 4u
#define CA_NORMAL 8u

typedef struct Array {
    const unsigned char *data;
    GLint size;
    GLenum type;
    GLsizei stride;
} Array;
typedef struct Texture {
    GLuint name;
    unsigned char *rgba;
    int width, height;
    GLint min_filter, mag_filter, wrap_s, wrap_t;
} Texture;
typedef struct Framebuffer {
    GLuint name;
    GLuint color_texture;
    GLuint depth_renderbuffer, stencil_renderbuffer;
} Framebuffer;
typedef struct Renderbuffer {
    GLuint name;
    GLenum format;
    GLsizei width, height;
} Renderbuffer;
typedef struct State {
    unsigned enabled, client_enabled;
    GLenum matrix_mode, shade_model, polygon[2], tex_env;
    GLint viewport[4], scissor[4], unpack_alignment, unpack_row_length, pack_alignment;
    GLenum blend_src, blend_dst;
    GLenum depth_func, cull_face, front_face;
    GLboolean depth_mask, color_mask[4];
    GLuint bound_texture;
    GLfloat point_size, line_width;
    GLfloat fog_start, fog_end;
    GLfloat tex_env_color[4];
    Array vertex, color, texcoord, normal;
} State;

static const State initial_state = {0,
                                    0,
                                    GL_MODELVIEW,
                                    GL_SMOOTH,
                                    {GL_FILL, GL_FILL},
                                    GL_MODULATE,
                                    {0, 0, 0, 0},
                                    {0, 0, 0, 0},
                                    4,
                                    0,
                                    4,
                                    GL_SRC_ALPHA,
                                    GL_ONE_MINUS_SRC_ALPHA,
                                    GL_LESS,
                                    GL_BACK,
                                    GL_CCW,
                                    GL_TRUE,
                                    {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE},
                                    0,
                                    1.0f,
                                    1.0f,
                                    0.0f,
                                    1.0f,
                                    {0, 0, 0, 0},
                                    {0},
                                    {0},
                                    {0},
                                    {0}};
typedef struct CompatContextState {
    NTGLcontext *context;
    State state;
    State attrib_stack[GL_ATTRIB_STACK];
    int attrib_top;
    GLenum error_code;
    Texture textures[GL_MAX_TEXTURES];
    GLuint next_texture;
    Framebuffer framebuffers[GL_MAX_FRAMEBUFFERS];
    GLuint next_framebuffer, bound_framebuffer;
    Renderbuffer renderbuffers[GL_MAX_RENDERBUFFERS];
    GLuint next_renderbuffer, bound_renderbuffer;
    NTGLframebuffer default_framebuffer;
    int have_default_framebuffer;
} CompatContextState;

static CompatContextState context_states[GL_MAX_CONTEXTS];

static CompatContextState *current_compat(void)
{
    NTGLcontext *context = ntglGetCurrent();
    int i, free_slot = -1;
    for (i = 0; i < GL_MAX_CONTEXTS; ++i) {
        if (context_states[i].context == context)
            return &context_states[i];
        if (!context_states[i].context && free_slot < 0)
            free_slot = i;
    }
    if (free_slot < 0) {
        return &context_states[0];
    }
    context_states[free_slot].context = context;
    context_states[free_slot].state = initial_state;
    context_states[free_slot].next_texture = 1;
    context_states[free_slot].next_framebuffer = 1;
    context_states[free_slot].next_renderbuffer = 1;
    return &context_states[free_slot];
}

void mesaGLReleaseCurrentContext(void)
{
    NTGLcontext *context = ntglGetCurrent();
    int i, slot;
    mesaGLGLES2ReleaseCurrentContext();
    for (slot = 0; slot < GL_MAX_CONTEXTS; ++slot)
        if (context_states[slot].context == context) {
            for (i = 0; i < GL_MAX_TEXTURES; ++i)
                ntglFree(context_states[slot].textures[i].rgba);
            memset(&context_states[slot], 0, sizeof(context_states[slot]));
            return;
        }
}

#define state (current_compat()->state)
#define attrib_stack (current_compat()->attrib_stack)
#define attrib_top (current_compat()->attrib_top)
#define error_code (current_compat()->error_code)
#define textures (current_compat()->textures)
#define next_texture (current_compat()->next_texture)
#define framebuffers (current_compat()->framebuffers)
#define next_framebuffer (current_compat()->next_framebuffer)
#define bound_framebuffer (current_compat()->bound_framebuffer)
#define renderbuffers (current_compat()->renderbuffers)
#define next_renderbuffer (current_compat()->next_renderbuffer)
#define bound_renderbuffer (current_compat()->bound_renderbuffer)
#define default_framebuffer (current_compat()->default_framebuffer)
#define have_default_framebuffer (current_compat()->have_default_framebuffer)

static void error(GLenum e)
{
    if (!error_code)
        error_code = e;
}

static unsigned cap_bit(GLenum cap)
{
    switch (cap) {
    case GL_BLEND:
        return 1u << 0;
    case GL_CULL_FACE:
        return 1u << 1;
    case GL_DEPTH_TEST:
        return 1u << 2;
    case GL_SCISSOR_TEST:
        return 1u << 3;
    case GL_TEXTURE_2D:
        return 1u << 4;
    case GL_LIGHTING:
        return 1u << 5;
    case GL_COLOR_MATERIAL:
        return 1u << 6;
    case GL_STENCIL_TEST:
        return 1u << 7;
    case GL_ALPHA_TEST:
        return 1u << 8;
    case GL_FOG:
        return 1u << 9;
    case GL_LIGHT0:
    case GL_LIGHT1:
    case GL_LIGHT2:
    case GL_LIGHT3:
    case GL_LIGHT4:
    case GL_LIGHT5:
    case GL_LIGHT6:
    case GL_LIGHT7:
        return 1u << (10 + cap - GL_LIGHT0);
    case GL_NORMALIZE:
        return 1u << 18;
    default:
        return 0;
    }
}

static Texture *texture(GLuint name)
{
    int i;
    for (i = 0; i < GL_MAX_TEXTURES; ++i)
        if (textures[i].name == name)
            return &textures[i];
    return NULL;
}

static Texture *new_texture(GLuint name)
{
    int i;
    Texture *t = texture(name);
    if (t)
        return t;
    for (i = 0; i < GL_MAX_TEXTURES; ++i)
        if (!textures[i].name) {
            textures[i].name = name;
            textures[i].min_filter = textures[i].mag_filter = GL_LINEAR;
            textures[i].wrap_s = textures[i].wrap_t = GL_REPEAT;
            return &textures[i];
        }
    error(GL_OUT_OF_MEMORY);
    return NULL;
}

static Framebuffer *framebuffer(GLuint name)
{
    int i;
    for (i = 0; i < GL_MAX_FRAMEBUFFERS; ++i)
        if (framebuffers[i].name == name)
            return &framebuffers[i];
    return NULL;
}

static Renderbuffer *renderbuffer(GLuint name)
{
    int i;
    for (i = 0; i < GL_MAX_RENDERBUFFERS; ++i)
        if (renderbuffers[i].name == name)
            return &renderbuffers[i];
    return NULL;
}

static void attach_bound_framebuffer(void)
{
    Framebuffer *f = framebuffer(bound_framebuffer);
    Texture *t = f ? texture(f->color_texture) : NULL;
    if (t && t->rgba) {
        NTGLframebuffer fb = {t->rgba,      t->width,      t->height,
                              t->width * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
        ntglAttachFramebuffer(ntglGetCurrent(), &fb);
    }
}

static void sync_texture(void)
{
    Texture *t = texture(state.bound_texture);
    if (t && t->rgba) {
        NTGLtextureEnv environment = NTGL_TEXTURE_MODULATE;

        if (state.tex_env == GL_REPLACE)
            environment = NTGL_TEXTURE_REPLACE;
        else if (state.tex_env == GL_DECAL)
            environment = NTGL_TEXTURE_DECAL;
        else if (state.tex_env == GL_ADD)
            environment = NTGL_TEXTURE_ADD;
        else if (state.tex_env == GL_BLEND)
            environment = NTGL_TEXTURE_BLEND;
        NTGLtexture n = {t->rgba,
                         t->width,
                         t->height,
                         t->width * 4,
                         NTGL_RGBA8888,
                         t->mag_filter == GL_NEAREST ? NTGL_NEAREST : NTGL_LINEAR,
                         NTGL_ORIGIN_BOTTOM_LEFT,
                         t->wrap_s == GL_REPEAT ? NTGL_REPEAT : NTGL_CLAMP_TO_EDGE,
                         t->wrap_t == GL_REPEAT ? NTGL_REPEAT : NTGL_CLAMP_TO_EDGE,
                         environment,
                         {state.tex_env_color[0], state.tex_env_color[1], state.tex_env_color[2],
                          state.tex_env_color[3]}};
        ntglBindTexture(&n);
    } else
        ntglBindTexture(NULL);
}

static int elem_size(GLenum type)
{
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        return 1;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        return 2;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
        return 4;
    case GL_DOUBLE:
        return 8;
    default:
        return 0;
    }
}

static float value(const unsigned char *p, GLenum type)
{
    switch (type) {
    case GL_FLOAT:
        return *(const GLfloat *)p;
    case GL_DOUBLE:
        return (float)*(const GLdouble *)p;
    case GL_BYTE:
        return *(const GLbyte *)p;
    case GL_UNSIGNED_BYTE:
        return *(const GLubyte *)p / 255.0f;
    case GL_SHORT:
        return *(const GLshort *)p;
    case GL_UNSIGNED_SHORT:
        return *(const GLushort *)p;
    case GL_INT:
        return *(const GLint *)p;
    case GL_UNSIGNED_INT:
        return (float)*(const GLuint *)p;
    default:
        return 0;
    }
}

static void submit(GLuint i)
{
    Array *a;
    int k, sz;
    if (state.client_enabled & CA_COLOR) {
        a = &state.color;
        sz = a->stride ? a->stride : a->size * elem_size(a->type);
        {
            const unsigned char *p = a->data + (size_t)i * sz;
            float c[4] = {1, 1, 1, 1};
            for (k = 0; k < a->size && k < 4; ++k)
                c[k] = value(p + k * elem_size(a->type), a->type);
            glColor4f(c[0], c[1], c[2], c[3]);
        }
    }
    if (state.client_enabled & CA_TEXCOORD) {
        a = &state.texcoord;
        sz = a->stride ? a->stride : a->size * elem_size(a->type);
        {
            const unsigned char *p = a->data + (size_t)i * sz;
            glTexCoord2f(value(p, a->type),
                         a->size > 1 ? value(p + elem_size(a->type), a->type) : 0);
        }
    }
    if (state.client_enabled & CA_NORMAL) {
        a = &state.normal;
        sz = a->stride ? a->stride : 3 * elem_size(a->type);
        {
            const unsigned char *p = a->data + (size_t)i * sz;

            glNormal3f(value(p, a->type), value(p + elem_size(a->type), a->type),
                       value(p + 2 * elem_size(a->type), a->type));
        }
    }
    a = &state.vertex;
    sz = a->stride ? a->stride : a->size * elem_size(a->type);
    {
        const unsigned char *p = a->data + (size_t)i * sz;
        float x = value(p, a->type), y = a->size > 1 ? value(p + elem_size(a->type), a->type) : 0,
              z = a->size > 2 ? value(p + 2 * elem_size(a->type), a->type) : 0;
        glVertex3f(x, y, z);
    }
}

void glEnable(GLenum c)
{
    unsigned b = cap_bit(c);
    if (!b) {
        error(GL_INVALID_ENUM);
        return;
    }
    state.enabled |= b;
    if (c == GL_BLEND)
        ntglEnable(NTGL_BLEND);
    else if (c == GL_CULL_FACE)
        ntglEnable(NTGL_CULL_FACE);
    else if (c == GL_DEPTH_TEST)
        ntglEnable(NTGL_DEPTH_TEST);
    else if (c == GL_SCISSOR_TEST)
        ntglEnable(NTGL_SCISSOR_TEST);
    else if (c == GL_STENCIL_TEST)
        ntglEnable(NTGL_STENCIL_TEST);
    else if (c == GL_ALPHA_TEST)
        ntglEnable(NTGL_ALPHA_TEST);
    else if (c == GL_TEXTURE_2D) {
        ntglEnable(NTGL_TEXTURE_2D);
        sync_texture();
    } else if (c == GL_LIGHTING)
        ntglEnable(NTGL_LIGHTING);
    else if (c >= GL_LIGHT0 && c <= GL_LIGHT7)
        ntglEnable((NTGLcapability)(NTGL_LIGHT0 + c - GL_LIGHT0));
    else if (c == GL_FOG)
        ntglEnable(NTGL_FOG);
    else if (c == GL_NORMALIZE)
        ntglEnable(NTGL_NORMALIZE);
}

void glDisable(GLenum c)
{
    unsigned b = cap_bit(c);
    if (!b) {
        error(GL_INVALID_ENUM);
        return;
    }
    state.enabled &= ~b;
    if (c == GL_BLEND)
        ntglDisable(NTGL_BLEND);
    else if (c == GL_CULL_FACE)
        ntglDisable(NTGL_CULL_FACE);
    else if (c == GL_DEPTH_TEST)
        ntglDisable(NTGL_DEPTH_TEST);
    else if (c == GL_SCISSOR_TEST)
        ntglDisable(NTGL_SCISSOR_TEST);
    else if (c == GL_STENCIL_TEST)
        ntglDisable(NTGL_STENCIL_TEST);
    else if (c == GL_ALPHA_TEST)
        ntglDisable(NTGL_ALPHA_TEST);
    else if (c == GL_TEXTURE_2D)
        ntglDisable(NTGL_TEXTURE_2D);
    else if (c == GL_LIGHTING)
        ntglDisable(NTGL_LIGHTING);
    else if (c >= GL_LIGHT0 && c <= GL_LIGHT7)
        ntglDisable((NTGLcapability)(NTGL_LIGHT0 + c - GL_LIGHT0));
    else if (c == GL_FOG)
        ntglDisable(NTGL_FOG);
    else if (c == GL_NORMALIZE)
        ntglDisable(NTGL_NORMALIZE);
}

GLboolean glIsEnabled(GLenum c)
{
    unsigned b = cap_bit(c);
    if (!b) {
        error(GL_INVALID_ENUM);
        return GL_FALSE;
    }
    return (state.enabled & b) ? GL_TRUE : GL_FALSE;
}

void glEnableClientState(GLenum a)
{
    if (a == GL_VERTEX_ARRAY)
        state.client_enabled |= CA_VERTEX;
    else if (a == GL_COLOR_ARRAY)
        state.client_enabled |= CA_COLOR;
    else if (a == GL_TEXTURE_COORD_ARRAY)
        state.client_enabled |= CA_TEXCOORD;
    else if (a == GL_NORMAL_ARRAY)
        state.client_enabled |= CA_NORMAL;
    else
        error(GL_INVALID_ENUM);
}

void glDisableClientState(GLenum a)
{
    if (a == GL_VERTEX_ARRAY)
        state.client_enabled &= ~CA_VERTEX;
    else if (a == GL_COLOR_ARRAY)
        state.client_enabled &= ~CA_COLOR;
    else if (a == GL_TEXTURE_COORD_ARRAY)
        state.client_enabled &= ~CA_TEXCOORD;
    else if (a == GL_NORMAL_ARRAY)
        state.client_enabled &= ~CA_NORMAL;
    else
        error(GL_INVALID_ENUM);
}

static NTGLblendFactor bf(GLenum f)
{
    switch (f) {
    case GL_ZERO:
        return NTGL_ZERO;
    case GL_ONE:
        return NTGL_ONE;
    case GL_SRC_COLOR:
        return NTGL_SRC_COLOR;
    case GL_ONE_MINUS_SRC_COLOR:
        return NTGL_ONE_MINUS_SRC_COLOR;
    case GL_DST_COLOR:
        return NTGL_DST_COLOR;
    case GL_ONE_MINUS_DST_COLOR:
        return NTGL_ONE_MINUS_DST_COLOR;
    case GL_SRC_ALPHA:
        return NTGL_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA:
        return NTGL_ONE_MINUS_SRC_ALPHA;
    case GL_DST_ALPHA:
        return NTGL_DST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA:
        return NTGL_ONE_MINUS_DST_ALPHA;
    case GL_CONSTANT_COLOR:
        return NTGL_CONSTANT_COLOR;
    case GL_ONE_MINUS_CONSTANT_COLOR:
        return NTGL_ONE_MINUS_CONSTANT_COLOR;
    case GL_CONSTANT_ALPHA:
        return NTGL_CONSTANT_ALPHA;
    default:
        return NTGL_ONE_MINUS_CONSTANT_ALPHA;
    }
}

static int valid_blend_factor(GLenum factor)
{
    return factor == GL_ZERO || factor == GL_ONE ||
           (factor >= GL_SRC_COLOR && factor <= GL_ONE_MINUS_DST_COLOR) ||
           (factor >= GL_CONSTANT_COLOR && factor <= GL_ONE_MINUS_CONSTANT_ALPHA);
}

void glBlendFunc(GLenum s, GLenum d)
{
    if (!valid_blend_factor(s) || !valid_blend_factor(d)) {
        error(GL_INVALID_ENUM);
        return;
    }
    state.blend_src = s;
    state.blend_dst = d;
    ntglBlendFunc(bf(s), bf(d));
}

void glBlendFuncSeparate(GLenum sr, GLenum dr, GLenum sa, GLenum da)
{
    if (!valid_blend_factor(sr) || !valid_blend_factor(dr) || !valid_blend_factor(sa) ||
        !valid_blend_factor(da)) {
        error(GL_INVALID_ENUM);
        return;
    }
    state.blend_src = sr;
    state.blend_dst = dr;
    ntglBlendFuncSeparate(bf(sr), bf(dr), bf(sa), bf(da));
}

void glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    ntglBlendColor(red, green, blue, alpha);
}

static int valid_depth_func(GLenum func)
{
    return func >= GL_NEVER && func <= GL_ALWAYS;
}

static NTGLdepthFunc depth_func(GLenum func)
{
    switch (func) {
    case GL_NEVER:
        return NTGL_NEVER;
    case GL_LESS:
        return NTGL_LESS;
    case GL_EQUAL:
        return NTGL_EQUAL;
    case GL_LEQUAL:
        return NTGL_LEQUAL;
    case GL_GREATER:
        return NTGL_GREATER;
    case GL_NOTEQUAL:
        return NTGL_NOTEQUAL;
    case GL_GEQUAL:
        return NTGL_GEQUAL;
    default:
        return NTGL_ALWAYS;
    }
}

void glDepthFunc(GLenum func)
{
    if (!valid_depth_func(func)) {
        error(GL_INVALID_ENUM);
        return;
    }
    state.depth_func = func;
    ntglDepthFunc(depth_func(func));
}

void glDepthMask(GLboolean flag)
{
    state.depth_mask = !!flag;
    ntglDepthMask(state.depth_mask);
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    state.color_mask[0] = !!red;
    state.color_mask[1] = !!green;
    state.color_mask[2] = !!blue;
    state.color_mask[3] = !!alpha;
    ntglColorMask(red, green, blue, alpha);
}

void glClearStencil(GLint value)
{
    ntglClearStencil((unsigned)value);
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
    if (!valid_depth_func(func)) {
        error(GL_INVALID_ENUM);
        return;
    }
    ntglStencilFunc(depth_func(func), (unsigned)ref, mask);
}

void glStencilMask(GLuint mask)
{
    ntglStencilMask(mask);
}

static NTGLstencilOp stencil_op(GLenum op)
{
    switch (op) {
    case GL_ZERO:
        return NTGL_STENCIL_ZERO;
    case GL_REPLACE:
        return NTGL_REPLACE;
    case GL_INCR:
        return NTGL_INCR;
    case GL_DECR:
        return NTGL_DECR;
    case GL_INVERT:
        return NTGL_INVERT;
    default:
        return NTGL_KEEP;
    }
}

void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
    GLenum ops[] = {fail, zfail, zpass};
    int i;
    for (i = 0; i < 3; ++i)
        if (ops[i] != GL_KEEP && ops[i] != GL_ZERO && ops[i] != GL_REPLACE && ops[i] != GL_INCR &&
            ops[i] != GL_DECR && ops[i] != GL_INVERT) {
            error(GL_INVALID_ENUM);
            return;
        }
    ntglStencilOp(stencil_op(fail), stencil_op(zfail), stencil_op(zpass));
}

void glAlphaFunc(GLenum func, GLclampf ref)
{
    if (!valid_depth_func(func)) {
        error(GL_INVALID_ENUM);
        return;
    }
    ntglAlphaFunc(depth_func(func), ref);
}

void glFrontFace(GLenum mode)
{
    if (mode != GL_CW && mode != GL_CCW) {
        error(GL_INVALID_ENUM);
        return;
    }
    state.front_face = mode;
    ntglFrontFace(mode == GL_CCW);
}

void glCullFace(GLenum mode)
{
    if (mode != GL_FRONT && mode != GL_BACK) {
        error(GL_INVALID_ENUM);
        return;
    }
    state.cull_face = mode;
    ntglCullFace(mode == GL_FRONT);
}

void glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    state.viewport[0] = x;
    state.viewport[1] = y;
    state.viewport[2] = w;
    state.viewport[3] = h;
    ntglViewport(x, y, w, h);
}

void glScissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    state.scissor[0] = x;
    state.scissor[1] = y;
    state.scissor[2] = w;
    state.scissor[3] = h;
    ntglScissor(x, y, w, h);
}

void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    ntglClearColor(r, g, b, a);
}

void glClearDepth(GLdouble d)
{
    ntglClearDepth((float)d);
}

void glClear(GLbitfield m)
{
    ntglClear(!!(m & GL_COLOR_BUFFER_BIT), !!(m & GL_DEPTH_BUFFER_BIT));
    if (m & GL_STENCIL_BUFFER_BIT)
        ntglClearStencilBuffer();
}

void glMatrixMode(GLenum m)
{
    if (m == GL_MODELVIEW)
        ntglMatrixMode(NTGL_MODELVIEW);
    else if (m == GL_PROJECTION)
        ntglMatrixMode(NTGL_PROJECTION);
    else if (m == GL_TEXTURE)
        ntglMatrixMode(NTGL_TEXTURE);
    else {
        error(GL_INVALID_ENUM);
        return;
    }
    state.matrix_mode = m;
}

void glLoadIdentity(void)
{
    ntglLoadIdentity();
}

void glPushMatrix(void)
{
    ntglPushMatrix();
}

void glPopMatrix(void)
{
    ntglPopMatrix();
}

void glLoadMatrixf(const GLfloat *m)
{
    ntglLoadMatrixf(m);
}

void glMultMatrixf(const GLfloat *m)
{
    ntglMultMatrixf(m);
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    ntglTranslatef(x, y, z);
}

void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    ntglScalef(x, y, z);
}

void glRotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z)
{
    ntglRotatef(a, x, y, z);
}

void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    ntglOrtho((float)l, (float)r, (float)b, (float)t, (float)n, (float)f);
}

void glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    ntglFrustum((float)l, (float)r, (float)b, (float)t, (float)n, (float)f);
}

static NTGLprimitive prim(GLenum m)
{
    return m == GL_POINTS           ? NTGL_POINTS
           : m == GL_LINES          ? NTGL_LINES
           : m == GL_LINE_LOOP      ? NTGL_LINE_LOOP
           : m == GL_LINE_STRIP     ? NTGL_LINE_STRIP
           : m == GL_TRIANGLE_STRIP ? NTGL_TRIANGLE_STRIP
           : m == GL_TRIANGLE_FAN   ? NTGL_TRIANGLE_FAN
           : m == GL_QUADS          ? NTGL_QUADS
                                    : NTGL_TRIANGLES;
}

void glBegin(GLenum m)
{
    ntglBegin(prim(m));
}

void glEnd(void)
{
    ntglEnd();
}

void glVertex2f(GLfloat x, GLfloat y)
{
    ntglVertex2f(x, y);
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    ntglVertex3f(x, y, z);
}

void glColor3f(GLfloat r, GLfloat g, GLfloat b)
{
    glColor4f(r, g, b, 1.0f);
}

void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    GLfloat color[4] = {r, g, b, a};

    ntglColor4f(r, g, b, a);
    if (state.enabled & cap_bit(GL_COLOR_MATERIAL))
        ntglMaterial(color, color);
}

void glNormal3f(GLfloat x, GLfloat y, GLfloat z)
{
    ntglNormal3f(x, y, z);
}

void glLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    int index = (int)(light - GL_LIGHT0);

    if (light < GL_LIGHT0 || light > GL_LIGHT7 || !params) {
        error(!params ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_AMBIENT)
        ntglLightAmbient(index, params);
    else if (pname == GL_DIFFUSE)
        ntglLightDiffuse(index, params);
    else if (pname == GL_SPECULAR)
        ntglLightSpecular(index, params);
    else if (pname == GL_POSITION)
        ntglLightPosition(index, params);
    else
        error(GL_INVALID_ENUM);
}

void glLightModelfv(GLenum pname, const GLfloat *params)
{
    if (pname != GL_LIGHT_MODEL_AMBIENT || !params) {
        error(!params ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }
    ntglLightModelAmbient(params);
}

void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) || !params) {
        error(!params ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_AMBIENT)
        ntglMaterial(params, NULL);
    else if (pname == GL_DIFFUSE)
        ntglMaterial(NULL, params);
    else if (pname == GL_AMBIENT_AND_DIFFUSE)
        ntglMaterial(params, params);
    else if (pname == GL_SPECULAR)
        ntglMaterialSpecular(params);
    else if (pname == GL_SHININESS) {
        if (params[0] < 0.0f || params[0] > 128.0f)
            error(GL_INVALID_VALUE);
        else
            ntglMaterialShininess(params[0]);
    } else
        error(GL_INVALID_ENUM);
}

void glMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    glMaterialfv(face, pname, &param);
}

void glColorMaterial(GLenum face, GLenum mode)
{
    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) ||
        mode != GL_AMBIENT_AND_DIFFUSE)
        error(GL_INVALID_ENUM);
}

void glFogf(GLenum pname, GLfloat param)
{
    if (pname == GL_FOG_MODE) {
        if ((GLenum)param == GL_LINEAR)
            ntglFogMode(NTGL_FOG_LINEAR);
        else if ((GLenum)param == GL_EXP)
            ntglFogMode(NTGL_FOG_EXP);
        else if ((GLenum)param == GL_EXP2)
            ntglFogMode(NTGL_FOG_EXP2);
        else
            error(GL_INVALID_ENUM);
    } else if (pname == GL_FOG_DENSITY) {
        if (param < 0.0f)
            error(GL_INVALID_VALUE);
        else
            ntglFogDensity(param);
    } else if (pname == GL_FOG_START) {
        state.fog_start = param;
        ntglFogRange(state.fog_start, state.fog_end);
    } else if (pname == GL_FOG_END) {
        state.fog_end = param;
        ntglFogRange(state.fog_start, state.fog_end);
    } else
        error(GL_INVALID_ENUM);
}

void glFogfv(GLenum pname, const GLfloat *params)
{
    if (!params) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (pname == GL_FOG_COLOR)
        ntglFogColor(params);
    else
        glFogf(pname, params[0]);
}

void glTexCoord2f(GLfloat s, GLfloat t)
{
    ntglTexCoord2f(s, t);
}

static void pointer(Array *a, GLint n, GLenum t, GLsizei s, const GLvoid *p)
{
    if (!elem_size(t) || n < 1 || n > 4 || s < 0) {
        error(GL_INVALID_VALUE);
        return;
    }
    a->data = p;
    a->size = n;
    a->type = t;
    a->stride = s;
}

void glVertexPointer(GLint n, GLenum t, GLsizei s, const GLvoid *p)
{
    pointer(&state.vertex, n, t, s, p);
}

void glColorPointer(GLint n, GLenum t, GLsizei s, const GLvoid *p)
{
    pointer(&state.color, n, t, s, p);
}

void glTexCoordPointer(GLint n, GLenum t, GLsizei s, const GLvoid *p)
{
    pointer(&state.texcoord, n, t, s, p);
}

void glNormalPointer(GLenum t, GLsizei s, const GLvoid *p)
{
    pointer(&state.normal, 3, t, s, p);
}

void glDrawArrays(GLenum m, GLint first, GLsizei count)
{
    int i;
    mesaGLPrepareGLES2Draw();
    if (!(state.client_enabled & CA_VERTEX) || count < 0) {
        error(GL_INVALID_OPERATION);
        return;
    }
    glBegin(m);
    for (i = 0; i < count; ++i)
        submit((GLuint)(first + i));
    glEnd();
}

void glDrawElements(GLenum m, GLsizei count, GLenum type, const GLvoid *idx)
{
    int i, batch = 0;
    mesaGLPrepareGLES2Draw();
    idx = mesaGLResolveElementPointer(idx);
    if (!(state.client_enabled & CA_VERTEX) || count < 0 || !idx) {
        error(GL_INVALID_OPERATION);
        return;
    }
    if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT && type != GL_UNSIGNED_BYTE) {
        error(GL_INVALID_ENUM);
        return;
    }
    glBegin(m);
    for (i = 0; i < count; ++i) {
        GLuint n = type == GL_UNSIGNED_SHORT ? ((const GLushort *)idx)[i]
                   : type == GL_UNSIGNED_INT ? ((const GLuint *)idx)[i]
                                             : ((const GLubyte *)idx)[i];
        submit(n);
        ++batch;
        if (m == GL_TRIANGLES && batch == MESAGL_MAX_VERTICES - 1 && i + 1 < count) {
            glEnd();
            glBegin(m);
            batch = 0;
        }
    }
    glEnd();
}

void glGenTextures(GLsizei n, GLuint *out)
{
    int i;
    for (i = 0; i < n; ++i) {
        while (texture(next_texture))
            ++next_texture;
        out[i] = next_texture++;
    }
}

void glDeleteTextures(GLsizei n, const GLuint *names)
{
    int i, slot;
    for (i = 0; i < n; ++i) {
        Texture *t = texture(names[i]);
        for (slot = 0; slot < GL_MAX_FRAMEBUFFERS; ++slot)
            if (framebuffers[slot].color_texture == names[i]) {
                if (bound_framebuffer == framebuffers[slot].name)
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                framebuffers[slot].color_texture = 0;
            }
        if (t) {
            ntglFree(t->rgba);
            memset(t, 0, sizeof(*t));
        }
        if (state.bound_texture == names[i]) {
            state.bound_texture = 0;
            sync_texture();
        }
    }
}

void glBindTexture(GLenum target, GLuint name)
{
    if (target != GL_TEXTURE_2D) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (name && !new_texture(name))
        return;
    state.bound_texture = name;
    sync_texture();
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    Texture *t;
    if (target != GL_TEXTURE_2D || (t = texture(state.bound_texture)) == NULL) {
        error(GL_INVALID_OPERATION);
        return;
    }
    if ((pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_MAG_FILTER) && param != GL_NEAREST &&
        param != GL_LINEAR) {
        error(GL_INVALID_ENUM);
        return;
    }
    if ((pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T) && param != GL_REPEAT &&
        param != GL_CLAMP && param != GL_CLAMP_TO_EDGE) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_TEXTURE_MIN_FILTER)
        t->min_filter = param;
    else if (pname == GL_TEXTURE_MAG_FILTER)
        t->mag_filter = param;
    else if (pname == GL_TEXTURE_WRAP_S)
        t->wrap_s = param;
    else if (pname == GL_TEXTURE_WRAP_T)
        t->wrap_t = param;
    else
        error(GL_INVALID_ENUM);
    sync_texture();
}

void glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    if (target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_MODE) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (param != GL_MODULATE && param != GL_REPLACE && param != GL_DECAL && param != GL_ADD &&
        param != GL_BLEND) {
        error(GL_INVALID_ENUM);
        return;
    }
    state.tex_env = (GLenum)param;
    sync_texture();
}

void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    glTexEnvi(target, pname, (GLint)param);
}

void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    int i;

    if (target != GL_TEXTURE_ENV || !params) {
        error(target != GL_TEXTURE_ENV ? GL_INVALID_ENUM : GL_INVALID_VALUE);
        return;
    }
    if (pname == GL_TEXTURE_ENV_MODE) {
        glTexEnvi(target, pname, (GLint)params[0]);
        return;
    }
    if (pname != GL_TEXTURE_ENV_COLOR) {
        error(GL_INVALID_ENUM);
        return;
    }
    for (i = 0; i < 4; ++i) {
        GLfloat value = params[i];

        state.tex_env_color[i] = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }
    sync_texture();
}

void glGetTexEnviv(GLenum t, GLenum p, GLint *v)
{
    int i;

    if (t == GL_TEXTURE_ENV && p == GL_TEXTURE_ENV_MODE)
        *v = (GLint)state.tex_env;
    else if (t == GL_TEXTURE_ENV && p == GL_TEXTURE_ENV_COLOR)
        for (i = 0; i < 4; ++i)
            v[i] = (GLint)state.tex_env_color[i];
    else
        error(GL_INVALID_ENUM);
}

void glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params)
{
    if (target != GL_TEXTURE_ENV || !params) {
        error(target != GL_TEXTURE_ENV ? GL_INVALID_ENUM : GL_INVALID_VALUE);
        return;
    }
    if (pname == GL_TEXTURE_ENV_MODE)
        params[0] = (GLfloat)state.tex_env;
    else if (pname == GL_TEXTURE_ENV_COLOR)
        memcpy(params, state.tex_env_color, sizeof(state.tex_env_color));
    else
        error(GL_INVALID_ENUM);
}

void glPixelStorei(GLenum p, GLint v)
{
    if (p == GL_UNPACK_ALIGNMENT) {
        if (v != 1 && v != 2 && v != 4 && v != 8) {
            error(GL_INVALID_VALUE);
            return;
        }
        state.unpack_alignment = v;
    } else if (p == GL_PACK_ALIGNMENT) {
        if (v != 1 && v != 2 && v != 4 && v != 8) {
            error(GL_INVALID_VALUE);
            return;
        }
        state.pack_alignment = v;
    } else if (p == GL_UNPACK_ROW_LENGTH) {
        if (v < 0) {
            error(GL_INVALID_VALUE);
            return;
        }
        state.unpack_row_length = v;
    } else
        error(GL_INVALID_ENUM);
}

static void upload(Texture *t, int xo, int yo, int w, int h, GLenum fmt, const void *data)
{
    int y, x,
        src_bpp = fmt == GL_ALPHA || fmt == GL_LUMINANCE ? 1
                  : fmt == GL_LUMINANCE_ALPHA            ? 2
                  : fmt == GL_RGB                        ? 3
                                                         : 4,
        row = state.unpack_row_length ? state.unpack_row_length : w;
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x) {
            const unsigned char *s = (const unsigned char *)data + (size_t)(y * row + x) * src_bpp;
            unsigned char *d = t->rgba + (size_t)((yo + y) * t->width + xo + x) * 4;
            if (fmt == GL_ALPHA) {
                d[0] = d[1] = d[2] = 255;
                d[3] = s[0];
            } else if (fmt == GL_LUMINANCE) {
                d[0] = d[1] = d[2] = s[0];
                d[3] = 255;
            } else if (fmt == GL_LUMINANCE_ALPHA) {
                d[0] = d[1] = d[2] = s[0];
                d[3] = s[1];
            } else if (fmt == GL_BGRA) {
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = s[3];
            } else {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = src_bpp == 4 ? s[3] : 255;
            }
        }
}

static void upload_packed(Texture *t, int w, int h, GLenum type, const void *data)
{
    const GLushort *source = (const GLushort *)data;
    int i;
    for (i = 0; i < w * h; ++i) {
        GLushort value = source[i];
        GLubyte *d = t->rgba + (size_t)i * 4;
        if (type == GL_UNSIGNED_SHORT_5_6_5) {
            d[0] = (GLubyte)(((value >> 11) & 31) * 255 / 31);
            d[1] = (GLubyte)(((value >> 5) & 63) * 255 / 63);
            d[2] = (GLubyte)((value & 31) * 255 / 31);
            d[3] = 255;
        } else if (type == GL_UNSIGNED_SHORT_4_4_4_4) {
            d[0] = (GLubyte)(((value >> 12) & 15) * 17);
            d[1] = (GLubyte)(((value >> 8) & 15) * 17);
            d[2] = (GLubyte)(((value >> 4) & 15) * 17);
            d[3] = (GLubyte)((value & 15) * 17);
        } else {
            d[0] = (GLubyte)(((value >> 11) & 31) * 255 / 31);
            d[1] = (GLubyte)(((value >> 6) & 31) * 255 / 31);
            d[2] = (GLubyte)(((value >> 1) & 31) * 255 / 31);
            d[3] = (value & 1) ? 255 : 0;
        }
    }
}

void glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,
                  GLenum fmt, GLenum type, const GLvoid *data)
{
    Texture *t;
    unsigned char *rgba;
    (void)internal;
    if (target != GL_TEXTURE_2D || level || border ||
        (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT_5_6_5 &&
         type != GL_UNSIGNED_SHORT_4_4_4_4 && type != GL_UNSIGNED_SHORT_5_5_5_1) ||
        (type == GL_UNSIGNED_SHORT_5_6_5 && fmt != GL_RGB) ||
        ((type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1) &&
         fmt != GL_RGBA) ||
        (fmt != GL_RGBA && fmt != GL_RGB && fmt != GL_BGRA && fmt != GL_ALPHA &&
         fmt != GL_LUMINANCE && fmt != GL_LUMINANCE_ALPHA) ||
        (t = texture(state.bound_texture)) == NULL || w <= 0 || h <= 0) {
        error(GL_INVALID_VALUE);
        return;
    }
    rgba = (unsigned char *)ntglAlloc((size_t)w * h * 4);
    if (!rgba) {
        error(GL_OUT_OF_MEMORY);
        return;
    }
    ntglFree(t->rgba);
    t->rgba = rgba;
    t->width = w;
    t->height = h;
    if (data && type == GL_UNSIGNED_BYTE)
        upload(t, 0, 0, w, h, fmt, data);
    else if (data)
        upload_packed(t, w, h, type, data);
    else
        memset(t->rgba, 0, (size_t)w * h * 4);
    sync_texture();
}

void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt,
                     GLenum type, const GLvoid *data)
{
    Texture *t = texture(state.bound_texture);
    if (target != GL_TEXTURE_2D || level || type != GL_UNSIGNED_BYTE || !t || !t->rgba || !data ||
        x < 0 || y < 0 || x + w > t->width || y + h > t->height) {
        error(GL_INVALID_VALUE);
        return;
    }
    upload(t, x, y, w, h, fmt, data);
    sync_texture();
}

void glPolygonMode(GLenum face, GLenum mode)
{
    NTGLpolygonMode nt_mode;

    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (mode == GL_POINT)
        nt_mode = NTGL_POLYGON_POINT;
    else if (mode == GL_LINE)
        nt_mode = NTGL_POLYGON_LINE;
    else if (mode == GL_FILL)
        nt_mode = NTGL_POLYGON_FILL;
    else {
        error(GL_INVALID_ENUM);
        return;
    }
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        state.polygon[0] = mode;
        ntglPolygonMode(1, nt_mode);
    }
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        state.polygon[1] = mode;
        ntglPolygonMode(0, nt_mode);
    }
}

void glPointSize(GLfloat size)
{
    if (size <= 0.0f) {
        error(GL_INVALID_VALUE);
        return;
    }
    state.point_size = size;
    ntglPointSize(size);
}

void glLineWidth(GLfloat width)
{
    if (width <= 0.0f) {
        error(GL_INVALID_VALUE);
        return;
    }
    state.line_width = width;
    ntglLineWidth(width);
}

void glShadeModel(GLenum m)
{
    if (m != GL_SMOOTH && m != GL_FLAT) {
        error(GL_INVALID_ENUM);
        return;
    }
    state.shade_model = m;
    ntglShadeModel(m == GL_SMOOTH);
}

void glPushAttrib(GLbitfield mask)
{
    (void)mask;
    if (attrib_top >= GL_ATTRIB_STACK) {
        error(GL_INVALID_OPERATION);
        return;
    }
    attrib_stack[attrib_top++] = state;
}

void glPopAttrib(void)
{
    State saved;
    if (!attrib_top) {
        error(GL_INVALID_OPERATION);
        return;
    }
    saved = attrib_stack[--attrib_top];
    state = saved;
    glBlendFunc(state.blend_src, state.blend_dst);
    glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
    glScissor(state.scissor[0], state.scissor[1], state.scissor[2], state.scissor[3]);
    glPointSize(state.point_size);
    glLineWidth(state.line_width);
    glDepthFunc(state.depth_func);
    glDepthMask(state.depth_mask);
    glColorMask(state.color_mask[0], state.color_mask[1], state.color_mask[2], state.color_mask[3]);
    glFrontFace(state.front_face);
    glCullFace(state.cull_face);
    glPolygonMode(GL_FRONT, state.polygon[0]);
    glPolygonMode(GL_BACK, state.polygon[1]);
    glShadeModel(state.shade_model);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, state.tex_env);
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, state.tex_env_color);
    {
        GLenum caps[] = {GL_BLEND,          GL_CULL_FACE,    GL_DEPTH_TEST, GL_SCISSOR_TEST,
                         GL_TEXTURE_2D,     GL_STENCIL_TEST, GL_ALPHA_TEST, GL_LIGHTING,
                         GL_COLOR_MATERIAL, GL_FOG,          GL_NORMALIZE,  GL_LIGHT0,
                         GL_LIGHT1,         GL_LIGHT2,       GL_LIGHT3,     GL_LIGHT4,
                         GL_LIGHT5,         GL_LIGHT6,       GL_LIGHT7};
        int i;
        for (i = 0; i < (int)(sizeof(caps) / sizeof(caps[0])); ++i)
            if (state.enabled & cap_bit(caps[i]))
                glEnable(caps[i]);
            else
                glDisable(caps[i]);
    }
    glBindTexture(GL_TEXTURE_2D, state.bound_texture);
}

void glGetIntegerv(GLenum p, GLint *v)
{
    int handled = 0;
    mesaGLGLES2GetIntegerv(p, v, &handled);
    if (handled)
        return;
    if (p == GL_TEXTURE_BINDING_2D)
        *v = (GLint)state.bound_texture;
    else if (p == GL_POLYGON_MODE) {
        v[0] = (GLint)state.polygon[0];
        v[1] = (GLint)state.polygon[1];
    } else if (p == GL_VIEWPORT)
        memcpy(v, state.viewport, sizeof(state.viewport));
    else if (p == GL_SCISSOR_BOX)
        memcpy(v, state.scissor, sizeof(state.scissor));
    else if (p == GL_SHADE_MODEL)
        *v = (GLint)state.shade_model;
    else if (p == GL_MATRIX_MODE)
        *v = (GLint)state.matrix_mode;
    else if (p == GL_UNPACK_ALIGNMENT)
        *v = state.unpack_alignment;
    else if (p == GL_UNPACK_ROW_LENGTH)
        *v = state.unpack_row_length;
    else if (p == GL_PACK_ALIGNMENT)
        *v = state.pack_alignment;
    else if (p == GL_FRAMEBUFFER_BINDING)
        *v = (GLint)bound_framebuffer;
    else if (p == GL_RENDERBUFFER_BINDING)
        *v = (GLint)bound_renderbuffer;
    else if (p == GL_DEPTH_FUNC)
        *v = (GLint)state.depth_func;
    else if (p == GL_DEPTH_WRITEMASK)
        *v = state.depth_mask;
    else if (p == GL_COLOR_WRITEMASK) {
        v[0] = state.color_mask[0];
        v[1] = state.color_mask[1];
        v[2] = state.color_mask[2];
        v[3] = state.color_mask[3];
    } else if (p == GL_CULL_FACE_MODE)
        *v = (GLint)state.cull_face;
    else if (p == GL_FRONT_FACE)
        *v = (GLint)state.front_face;
    else if (p == GL_POINT_SIZE)
        *v = (GLint)state.point_size;
    else if (p == GL_LINE_WIDTH)
        *v = (GLint)state.line_width;
    else if (p == 0x84E0)
        *v = 0x84C0;
    else if (p == 0x80C9)
        *v = (GLint)state.blend_src;
    else if (p == 0x80C8)
        *v = (GLint)state.blend_dst;
    else if (p == 0x80CB)
        *v = (GLint)state.blend_src;
    else if (p == 0x80CA)
        *v = (GLint)state.blend_dst;
    else if (p == 0x8009 || p == 0x883D)
        *v = 0x8006;
    else if (p == 0x0D33)
        *v = 4096;
    else
        error(GL_INVALID_ENUM);
}

static int query_count(GLenum pname)
{
    if (pname == GL_VIEWPORT || pname == GL_SCISSOR_BOX || pname == GL_COLOR_WRITEMASK)
        return 4;
    if (pname == GL_POLYGON_MODE)
        return 2;
    return 1;
}

void glGetBooleanv(GLenum pname, GLboolean *params)
{
    GLint values[4];
    int i, count;
    if (!params) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (cap_bit(pname)) {
        params[0] = glIsEnabled(pname);
        return;
    }
    glGetIntegerv(pname, values);
    count = query_count(pname);
    for (i = 0; i < count; ++i)
        params[i] = values[i] ? GL_TRUE : GL_FALSE;
}

void glGetFloatv(GLenum pname, GLfloat *params)
{
    GLint values[4];
    int i, count;
    if (!params) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (pname == GL_POINT_SIZE || pname == GL_LINE_WIDTH) {
        params[0] = pname == GL_POINT_SIZE ? state.point_size : state.line_width;
        return;
    }
    if (pname == GL_MODELVIEW_MATRIX || pname == GL_PROJECTION_MATRIX ||
        pname == GL_TEXTURE_MATRIX) {
        NTGLmatrixMode mode = pname == GL_MODELVIEW_MATRIX    ? NTGL_MODELVIEW
                              : pname == GL_PROJECTION_MATRIX ? NTGL_PROJECTION
                                                              : NTGL_TEXTURE;

        ntglGetMatrix(mode, params);
        return;
    }
    glGetIntegerv(pname, values);
    count = query_count(pname);
    for (i = 0; i < count; ++i)
        params[i] = (GLfloat)values[i];
}

GLenum glGetError(void)
{
    GLenum e = error_code;
    error_code = 0;
    return e;
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                  GLvoid *pixels)
{
    const NTGLframebuffer *fb = ntglGetFramebuffer(ntglGetCurrent());
    int row, column, source_bpp, destination_bpp, destination_stride;

    if (format != GL_RGB && format != GL_RGBA) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (type != GL_UNSIGNED_BYTE) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!fb || !pixels || width < 0 || height < 0 || x < 0 || y < 0 || x + width > fb->width ||
        y + height > fb->height) {
        error(GL_INVALID_VALUE);
        return;
    }
    source_bpp = fb->format == NTGL_RGB565                                ? 2
                 : fb->format == NTGL_RGB888 || fb->format == NTGL_BGR888 ? 3
                                                                          : 4;
    destination_bpp = format == GL_RGB ? 3 : 4;
    destination_stride = width * destination_bpp;
    destination_stride =
        (destination_stride + state.pack_alignment - 1) & ~(state.pack_alignment - 1);
    for (row = 0; row < height; ++row)
        for (column = 0; column < width; ++column) {
            int source_y = y + row;
            int stride = fb->stride ? fb->stride : fb->width * source_bpp;
            const GLubyte *source;
            GLubyte rgba[4];
            GLubyte *destination = (GLubyte *)pixels + (size_t)row * destination_stride +
                                   (size_t)column * destination_bpp;

            if (fb->origin == NTGL_ORIGIN_TOP_LEFT)
                source_y = fb->height - 1 - source_y;
            source = (const GLubyte *)fb->pixels + (ptrdiff_t)source_y * stride +
                     (size_t)(x + column) * source_bpp;
            if (fb->format == NTGL_RGB565) {
                uint16_t packed;

                memcpy(&packed, source, sizeof(packed));
                rgba[0] = (GLubyte)(((packed >> 11) & 31) * 255 / 31);
                rgba[1] = (GLubyte)(((packed >> 5) & 63) * 255 / 63);
                rgba[2] = (GLubyte)((packed & 31) * 255 / 31);
                rgba[3] = 255;
            } else if (fb->format == NTGL_RGB888) {
                rgba[0] = source[0];
                rgba[1] = source[1];
                rgba[2] = source[2];
                rgba[3] = 255;
            } else if (fb->format == NTGL_BGR888) {
                rgba[0] = source[2];
                rgba[1] = source[1];
                rgba[2] = source[0];
                rgba[3] = 255;
            } else if (fb->format == NTGL_RGBA8888) {
                memcpy(rgba, source, 4);
            } else if (fb->format == NTGL_BGRA8888) {
                rgba[0] = source[2];
                rgba[1] = source[1];
                rgba[2] = source[0];
                rgba[3] = source[3];
            } else {
                rgba[0] = source[2];
                rgba[1] = source[1];
                rgba[2] = source[0];
                rgba[3] = fb->format == NTGL_ARGB8888 ? source[3] : 255;
            }
            memcpy(destination, rgba, (size_t)destination_bpp);
        }
}

const GLubyte *glGetString(GLenum n)
{
    if (n == GL_VERSION)
        return (const GLubyte *)"1.2 mesaGL";
    if (n == GL_VENDOR)
        return (const GLubyte *)"mesaGL";
    if (n == GL_RENDERER)
        return (const GLubyte *)"mesaGL software framebuffer";
    if (n == GL_EXTENSIONS)
        return (const GLubyte *)"GL_EXT_blend_func_separate";
    error(GL_INVALID_ENUM);
    return NULL;
}

void glFlush(void)
{
}

void glGenFramebuffers(GLsizei n, GLuint *names)
{
    int i, slot;
    if (n < 0 || (n && !names)) {
        error(GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < n; ++i) {
        for (slot = 0; slot < GL_MAX_FRAMEBUFFERS && framebuffers[slot].name; ++slot) {
        }
        if (slot == GL_MAX_FRAMEBUFFERS) {
            error(GL_OUT_OF_MEMORY);
            names[i] = 0;
            continue;
        }
        framebuffers[slot].name = next_framebuffer++;
        names[i] = framebuffers[slot].name;
    }
}

void glDeleteFramebuffers(GLsizei n, const GLuint *names)
{
    int i;
    if (n < 0 || (n && !names)) {
        error(GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < n; ++i) {
        Framebuffer *f = framebuffer(names[i]);
        if (f) {
            if (bound_framebuffer == names[i])
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            memset(f, 0, sizeof(*f));
        }
    }
}

void glBindFramebuffer(GLenum target, GLuint name)
{
    const NTGLframebuffer *current;
    if (target != GL_FRAMEBUFFER) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (name && !framebuffer(name)) {
        error(GL_INVALID_OPERATION);
        return;
    }
    current = ntglGetFramebuffer(ntglGetCurrent());
    if (name && !have_default_framebuffer && current) {
        default_framebuffer = *current;
        have_default_framebuffer = 1;
    }
    bound_framebuffer = name;
    if (!name) {
        if (have_default_framebuffer)
            ntglAttachFramebuffer(ntglGetCurrent(), &default_framebuffer);
    } else {
        attach_bound_framebuffer();
    }
}

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint name,
                            GLint level)
{
    Framebuffer *f;
    if (target != GL_FRAMEBUFFER || attachment != GL_COLOR_ATTACHMENT0 ||
        textarget != GL_TEXTURE_2D) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!bound_framebuffer || level != 0 || (name && !texture(name))) {
        error(GL_INVALID_OPERATION);
        return;
    }
    f = framebuffer(bound_framebuffer);
    f->color_texture = name;
    attach_bound_framebuffer();
}

GLenum glCheckFramebufferStatus(GLenum target)
{
    Framebuffer *f;
    Texture *t;
    if (target != GL_FRAMEBUFFER) {
        error(GL_INVALID_ENUM);
        return 0;
    }
    if (!bound_framebuffer)
        return GL_FRAMEBUFFER_COMPLETE;
    f = framebuffer(bound_framebuffer);
    t = f ? texture(f->color_texture) : NULL;
    if (!t || !t->rgba || t->width <= 0 || t->height <= 0)
        return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    if (f->depth_renderbuffer) {
        Renderbuffer *r = renderbuffer(f->depth_renderbuffer);
        if (!r || r->format != GL_DEPTH_COMPONENT16 || r->width != t->width ||
            r->height != t->height)
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    }
    if (f->stencil_renderbuffer) {
        Renderbuffer *r = renderbuffer(f->stencil_renderbuffer);
        if (!r || r->format != GL_STENCIL_INDEX8 || r->width != t->width || r->height != t->height)
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    }
    return GL_FRAMEBUFFER_COMPLETE;
}

void glGenRenderbuffers(GLsizei n, GLuint *names)
{
    int i, slot;
    if (n < 0 || (n && !names)) {
        error(GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < n; ++i) {
        for (slot = 0; slot < GL_MAX_RENDERBUFFERS && renderbuffers[slot].name; ++slot) {
        }
        if (slot == GL_MAX_RENDERBUFFERS) {
            error(GL_OUT_OF_MEMORY);
            names[i] = 0;
            continue;
        }
        renderbuffers[slot].name = next_renderbuffer++;
        names[i] = renderbuffers[slot].name;
    }
}

void glDeleteRenderbuffers(GLsizei n, const GLuint *names)
{
    int i, slot;
    for (i = 0; i < n; ++i) {
        Renderbuffer *r = renderbuffer(names[i]);
        for (slot = 0; slot < GL_MAX_FRAMEBUFFERS; ++slot) {
            if (framebuffers[slot].depth_renderbuffer == names[i])
                framebuffers[slot].depth_renderbuffer = 0;
            if (framebuffers[slot].stencil_renderbuffer == names[i])
                framebuffers[slot].stencil_renderbuffer = 0;
        }
        if (r) {
            if (bound_renderbuffer == r->name)
                bound_renderbuffer = 0;
            memset(r, 0, sizeof(*r));
        }
    }
}

void glBindRenderbuffer(GLenum target, GLuint name)
{
    if (target != GL_RENDERBUFFER) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (name && !renderbuffer(name)) {
        error(GL_INVALID_OPERATION);
        return;
    }
    bound_renderbuffer = name;
}

void glRenderbufferStorage(GLenum target, GLenum format, GLsizei width, GLsizei height)
{
    Renderbuffer *r = renderbuffer(bound_renderbuffer);
    if (target != GL_RENDERBUFFER || !r || width <= 0 || height <= 0 ||
        (format != GL_DEPTH_COMPONENT16 && format != GL_STENCIL_INDEX8)) {
        error(GL_INVALID_VALUE);
        return;
    }
    r->format = format;
    r->width = width;
    r->height = height;
}

void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum rbtarget, GLuint name)
{
    Framebuffer *f = framebuffer(bound_framebuffer);
    if (target != GL_FRAMEBUFFER || rbtarget != GL_RENDERBUFFER || !f ||
        (name && !renderbuffer(name))) {
        error(GL_INVALID_OPERATION);
        return;
    }
    if (attachment == GL_DEPTH_ATTACHMENT)
        f->depth_renderbuffer = name;
    else if (attachment == GL_STENCIL_ATTACHMENT)
        f->stencil_renderbuffer = name;
    else
        error(GL_INVALID_ENUM);
}
