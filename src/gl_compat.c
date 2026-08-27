#include "GL/gl.h"
#include "gles2_internal.h"
#include "mesaGL/config.h"
#include "mesaGL/ntgl.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define GL_MAX_TEXTURES MESAGL_MAX_TEXTURES
#define GL_ATTRIB_STACK MESAGL_ATTRIB_STACK_DEPTH
#define GL_MAX_FRAMEBUFFERS MESAGL_MAX_FRAMEBUFFERS
#define GL_MAX_RENDERBUFFERS MESAGL_MAX_RENDERBUFFERS
#define GL_MAX_CONTEXTS MESAGL_MAX_CONTEXTS
#define GL_MAX_MIP_LEVELS 13
#define CA_VERTEX 1u
#define CA_COLOR 2u
#define CA_TEXCOORD 4u
#define CA_NORMAL 8u
#define CAP_DITHER (1u << 20)
#define CAP_SAMPLE_ALPHA_TO_COVERAGE (1u << 21)
#define CAP_SAMPLE_COVERAGE (1u << 22)

typedef struct Array {
    const unsigned char *data;
    GLint size;
    GLenum type;
    GLsizei stride;
} Array;
typedef struct CubeStorage {
    unsigned char *pixels[6][GL_MAX_MIP_LEVELS];
    int width[6][GL_MAX_MIP_LEVELS];
    int height[6][GL_MAX_MIP_LEVELS];
    GLenum format[6][GL_MAX_MIP_LEVELS];
    GLenum type[6][GL_MAX_MIP_LEVELS];
} CubeStorage;
typedef struct Texture {
    GLuint name;
    GLenum target;
    int attachment_refs;
    int delete_pending;
    unsigned char *rgba;
    int width, height;
    GLenum format;
    GLenum type;
    int rgb_white_known;
    int rgb_white;
    GLint min_filter, mag_filter, wrap_s, wrap_t;
    int mipmap_complete;
    unsigned char *mipmap[GL_MAX_MIP_LEVELS];
    int mip_width[GL_MAX_MIP_LEVELS], mip_height[GL_MAX_MIP_LEVELS];
    GLenum mip_format[GL_MAX_MIP_LEVELS];
    GLenum mip_type[GL_MAX_MIP_LEVELS];
    CubeStorage *cube;
} Texture;
typedef struct Framebuffer {
    GLuint name;
    int created;
    GLuint color_texture;
    GLenum color_target;
    GLint color_level;
    GLuint color_renderbuffer;
    GLuint depth_texture;
    GLenum depth_target;
    GLint depth_level;
    GLuint depth_renderbuffer, stencil_renderbuffer;
    GLuint stencil_texture;
    GLenum stencil_target;
    GLint stencil_level;
} Framebuffer;
typedef struct Renderbuffer {
    GLuint name;
    int created;
    int attachment_refs;
    int delete_pending;
    GLenum format;
    GLsizei width, height;
    unsigned char *pixels;
} Renderbuffer;
typedef struct State {
    unsigned enabled, client_enabled;
    GLenum matrix_mode, shade_model, polygon[2], tex_env;
    GLint viewport[4], scissor[4], unpack_alignment, unpack_row_length, pack_alignment;
    GLenum blend_src, blend_dst, blend_src_alpha, blend_dst_alpha;
    GLenum blend_equation_rgb, blend_equation_alpha;
    GLenum depth_func, cull_face, front_face;
    GLenum generate_mipmap_hint, derivative_hint;
    GLenum stencil_func[2];
    GLenum stencil_fail[2], stencil_depth_fail[2], stencil_pass[2];
    GLint stencil_ref[2], stencil_clear;
    GLuint stencil_value_mask[2], stencil_write_mask[2];
    GLboolean depth_mask, color_mask[4];
    GLuint bound_texture[MESAGL_MAX_TEXTURE_UNITS];
    GLuint bound_cube_texture[MESAGL_MAX_TEXTURE_UNITS];
    int active_texture;
    GLfloat point_size, line_width, depth_range[2];
    GLfloat clear_color[4], clear_depth;
    GLfloat fog_start, fog_end;
    GLfloat tex_env_color[4];
    GLfloat blend_color[4];
    Array vertex, color, texcoord, normal;
    GLfloat polygon_offset_factor, polygon_offset_units;
    GLfloat sample_coverage;
    GLboolean sample_coverage_invert;
} State;

static const State initial_state = {
    .enabled = CAP_DITHER,
    .matrix_mode = GL_MODELVIEW,
    .shade_model = GL_SMOOTH,
    .polygon = {GL_FILL, GL_FILL},
    .tex_env = GL_MODULATE,
    .unpack_alignment = 4,
    .pack_alignment = 4,
    .blend_src = GL_ONE,
    .blend_dst = GL_ZERO,
    .blend_src_alpha = GL_ONE,
    .blend_dst_alpha = GL_ZERO,
    .blend_equation_rgb = GL_FUNC_ADD,
    .blend_equation_alpha = GL_FUNC_ADD,
    .depth_func = GL_LESS,
    .stencil_func = {GL_ALWAYS, GL_ALWAYS},
    .stencil_fail = {GL_KEEP, GL_KEEP},
    .stencil_depth_fail = {GL_KEEP, GL_KEEP},
    .stencil_pass = {GL_KEEP, GL_KEEP},
    .stencil_value_mask = {0xffu, 0xffu},
    .stencil_write_mask = {0xffu, 0xffu},
    .cull_face = GL_BACK,
    .front_face = GL_CCW,
    .generate_mipmap_hint = GL_DONT_CARE,
    .derivative_hint = GL_DONT_CARE,
    .depth_mask = GL_TRUE,
    .color_mask = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE},
    .point_size = 1.0f,
    .line_width = 1.0f,
    .depth_range = {0.0f, 1.0f},
    .clear_depth = 1.0f,
    .fog_end = 1.0f,
    .sample_coverage = 1.0f,
};
typedef struct CompatContextState {
    NTGLcontext *context;
    State state;
    State attrib_stack[GL_ATTRIB_STACK];
    int attrib_top;
    GLenum error_code;
    Texture textures[GL_MAX_TEXTURES];
    Texture default_texture_2d;
    Texture default_texture_cube;
    GLuint next_texture;
    Framebuffer framebuffers[GL_MAX_FRAMEBUFFERS];
    GLuint next_framebuffer, bound_framebuffer;
    Renderbuffer renderbuffers[GL_MAX_RENDERBUFFERS];
    GLuint next_renderbuffer, bound_renderbuffer;
    NTGLframebuffer default_framebuffer;
    int have_default_framebuffer;
} CompatContextState;

static CompatContextState context_states[GL_MAX_CONTEXTS];

static void init_texture(Texture *texture_object, GLenum target)
{
    memset(texture_object, 0, sizeof(*texture_object));
    texture_object->target = target;
    texture_object->min_filter = GL_NEAREST_MIPMAP_LINEAR;
    texture_object->mag_filter = GL_LINEAR;
    texture_object->wrap_s = GL_REPEAT;
    texture_object->wrap_t = GL_REPEAT;
}

static CompatContextState *current_compat(void)
{
    NTGLcontext *context = ntglGetCurrent();
    const NTGLframebuffer *framebuffer_object;
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
    framebuffer_object = ntglGetFramebuffer(context);
    if (framebuffer_object) {
        context_states[free_slot].state.viewport[2] = framebuffer_object->width;
        context_states[free_slot].state.viewport[3] = framebuffer_object->height;
        context_states[free_slot].state.scissor[2] = framebuffer_object->width;
        context_states[free_slot].state.scissor[3] = framebuffer_object->height;
    }
    context_states[free_slot].next_texture = 1;
    context_states[free_slot].next_framebuffer = 1;
    context_states[free_slot].next_renderbuffer = 1;
    context_states[free_slot].state.sample_coverage = 1.0f;
    init_texture(&context_states[free_slot].default_texture_2d, GL_TEXTURE_2D);
    init_texture(&context_states[free_slot].default_texture_cube, GL_TEXTURE_CUBE_MAP);
    return &context_states[free_slot];
}

static void free_texture_storage(Texture *texture_object)
{
    int face;
    int level;

    ntglFree(texture_object->rgba);
    for (level = 1; level < GL_MAX_MIP_LEVELS; ++level)
        ntglFree(texture_object->mipmap[level]);
    if (texture_object->cube) {
        for (face = 0; face < 6; ++face)
            for (level = 0; level < GL_MAX_MIP_LEVELS; ++level)
                ntglFree(texture_object->cube->pixels[face][level]);
        ntglFree(texture_object->cube);
    }
}

void mesaGLReleaseCurrentContext(void)
{
    NTGLcontext *context = ntglGetCurrent();
    int i, slot;
    mesaGLGLES2ReleaseCurrentContext();
    for (slot = 0; slot < GL_MAX_CONTEXTS; ++slot)
        if (context_states[slot].context == context) {
            for (i = 0; i < GL_MAX_TEXTURES; ++i)
                free_texture_storage(&context_states[slot].textures[i]);
            free_texture_storage(&context_states[slot].default_texture_2d);
            free_texture_storage(&context_states[slot].default_texture_cube);
            for (i = 0; i < GL_MAX_RENDERBUFFERS; ++i)
                ntglFree(context_states[slot].renderbuffers[i].pixels);
            memset(&context_states[slot], 0, sizeof(context_states[slot]));
            return;
        }
}

#define state (current_compat()->state)
#define attrib_stack (current_compat()->attrib_stack)
#define attrib_top (current_compat()->attrib_top)
#define error_code (current_compat()->error_code)
#define textures (current_compat()->textures)
#define default_texture_2d (current_compat()->default_texture_2d)
#define default_texture_cube (current_compat()->default_texture_cube)
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

static Framebuffer *framebuffer(GLuint name);

static int framebuffer_ready(void)
{
    if (bound_framebuffer && glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        error(GL_INVALID_FRAMEBUFFER_OPERATION);
        return 0;
    }
    return 1;
}

static int framebuffer_has_color(void)
{
    Framebuffer *f;

    if (!bound_framebuffer)
        return 1;
    f = framebuffer(bound_framebuffer);
    return f && (f->color_texture || f->color_renderbuffer);
}

void mesaGLSetError(unsigned int error_value)
{
    error((GLenum)error_value);
}

void mesaGLSetBlendEquationState(unsigned int rgb, unsigned int alpha)
{
    state.blend_equation_rgb = (GLenum)rgb;
    state.blend_equation_alpha = (GLenum)alpha;
}

static void sync_texture(void);

static unsigned cap_bit(GLenum cap)
{
#if MESAGL_STRICT_GLES2
    if (cap != GL_BLEND && cap != GL_CULL_FACE && cap != GL_DEPTH_TEST &&
        cap != GL_DITHER && cap != GL_POLYGON_OFFSET_FILL &&
        cap != GL_SAMPLE_ALPHA_TO_COVERAGE && cap != GL_SAMPLE_COVERAGE &&
        cap != GL_SCISSOR_TEST && cap != GL_STENCIL_TEST)
        return 0;
#endif
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
    case GL_POLYGON_OFFSET_FILL:
        return 1u << 19;
    case GL_DITHER:
        return CAP_DITHER;
    case GL_SAMPLE_ALPHA_TO_COVERAGE:
        return CAP_SAMPLE_ALPHA_TO_COVERAGE;
    case GL_SAMPLE_COVERAGE:
        return CAP_SAMPLE_COVERAGE;
    default:
        return 0;
    }
}

void mesaGLSetGLES2TextureState(int enabled)
{
    if (enabled) {
        /* Programmable fragments sample through their sampler uniforms.  The
         * fixed pipeline texture stage would perform a redundant sample whose
         * result is overwritten by gl_FragColor. */
        ntglDisable(NTGL_TEXTURE_2D);
    } else if (state.enabled & (1u << 4)) {
        ntglEnable(NTGL_TEXTURE_2D);
        sync_texture();
    } else {
        ntglDisable(NTGL_TEXTURE_2D);
    }
}

static Texture *texture(GLuint name)
{
    int i;

    if (!name)
        return NULL;
    for (i = 0; i < GL_MAX_TEXTURES; ++i)
        if (textures[i].name == name)
            return &textures[i];
    return NULL;
}

static Texture *new_texture(GLuint name)
{
    int i;
    Texture *t = texture(name);
    if (t && !t->delete_pending)
        return t;
    if (t) {
        error(GL_INVALID_OPERATION);
        return NULL;
    }
    for (i = 0; i < GL_MAX_TEXTURES; ++i)
        if (!textures[i].name) {
            init_texture(&textures[i], 0);
            textures[i].name = name;
            return &textures[i];
        }
    error(GL_OUT_OF_MEMORY);
    return NULL;
}

static void destroy_texture(Texture *texture_object)
{
    if (!texture_object)
        return;
    free_texture_storage(texture_object);
    memset(texture_object, 0, sizeof(*texture_object));
}

static void retain_texture_attachment(GLuint name)
{
    Texture *texture_object = texture(name);

    if (texture_object)
        ++texture_object->attachment_refs;
}

static void release_texture_attachment(GLuint name)
{
    Texture *texture_object = texture(name);

    if (!texture_object)
        return;
    if (texture_object->attachment_refs > 0)
        --texture_object->attachment_refs;
    if (!texture_object->attachment_refs && texture_object->delete_pending)
        destroy_texture(texture_object);
}

static int cube_face(GLenum target)
{
    if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
        return (int)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
    return -1;
}

static Texture *bound_texture_for_target(GLenum target)
{
    if (target == GL_TEXTURE_2D) {
        GLuint name = state.bound_texture[state.active_texture];

        return name ? texture(name) : &default_texture_2d;
    }
    if (target == GL_TEXTURE_CUBE_MAP || cube_face(target) >= 0) {
        GLuint name = state.bound_cube_texture[state.active_texture];

        return name ? texture(name) : &default_texture_cube;
    }
    return NULL;
}

static Texture *texture_2d_for_unit(int unit)
{
    GLuint name = state.bound_texture[unit];

    return name ? texture(name) : &default_texture_2d;
}

static Texture *texture_cube_for_unit(int unit)
{
    GLuint name = state.bound_cube_texture[unit];

    return name ? texture(name) : &default_texture_cube;
}

static Framebuffer *framebuffer(GLuint name)
{
    int i;

    if (!name)
        return NULL;
    for (i = 0; i < GL_MAX_FRAMEBUFFERS; ++i)
        if (framebuffers[i].name == name)
            return &framebuffers[i];
    return NULL;
}

static Renderbuffer *renderbuffer(GLuint name)
{
    int i;

    if (!name)
        return NULL;
    for (i = 0; i < GL_MAX_RENDERBUFFERS; ++i)
        if (renderbuffers[i].name == name)
            return &renderbuffers[i];
    return NULL;
}

static Framebuffer *get_or_create_framebuffer(GLuint name)
{
    Framebuffer *object = framebuffer(name);
    int index;

    if (object || !name)
        return object;
    for (index = 0; index < GL_MAX_FRAMEBUFFERS; ++index)
        if (!framebuffers[index].name) {
            framebuffers[index].name = name;
            return &framebuffers[index];
        }
    error(GL_OUT_OF_MEMORY);
    return NULL;
}

static Renderbuffer *get_or_create_renderbuffer(GLuint name)
{
    Renderbuffer *object = renderbuffer(name);
    int index;

    if (object && !object->delete_pending)
        return object;
    if (object) {
        error(GL_INVALID_OPERATION);
        return NULL;
    }
    if (!name)
        return NULL;
    for (index = 0; index < GL_MAX_RENDERBUFFERS; ++index)
        if (!renderbuffers[index].name) {
            renderbuffers[index].name = name;
            return &renderbuffers[index];
        }
    error(GL_OUT_OF_MEMORY);
    return NULL;
}

static void destroy_renderbuffer(Renderbuffer *object)
{
    if (!object)
        return;
    ntglFree(object->pixels);
    memset(object, 0, sizeof(*object));
}

static void retain_renderbuffer_attachment(GLuint name)
{
    Renderbuffer *object = renderbuffer(name);

    if (object)
        ++object->attachment_refs;
}

static void release_renderbuffer_attachment(GLuint name)
{
    Renderbuffer *object = renderbuffer(name);

    if (!object)
        return;
    if (object->attachment_refs > 0)
        --object->attachment_refs;
    if (!object->attachment_refs && object->delete_pending)
        destroy_renderbuffer(object);
}

static void attach_bound_framebuffer(void)
{
    Framebuffer *f = framebuffer(bound_framebuffer);
    Texture *t = f ? texture(f->color_texture) : NULL;
    Renderbuffer *r = f ? renderbuffer(f->color_renderbuffer) : NULL;
    Renderbuffer *depth = f ? renderbuffer(f->depth_renderbuffer) : NULL;
    Renderbuffer *stencil = f ? renderbuffer(f->stencil_renderbuffer) : NULL;
    unsigned char *pixels = NULL;
    int width = 0;
    int height = 0;
    int face = f ? cube_face(f->color_target) : -1;

    if (t && f && f->color_target == GL_TEXTURE_2D && !f->color_level)
        t->rgb_white_known = 0;

    if (t && f && f->color_target == GL_TEXTURE_2D) {
        pixels = f->color_level ? t->mipmap[f->color_level] : t->rgba;
        width = f->color_level ? t->mip_width[f->color_level] : t->width;
        height = f->color_level ? t->mip_height[f->color_level] : t->height;
    } else if (t && t->cube && face >= 0) {
        pixels = t->cube->pixels[face][f->color_level];
        width = t->cube->width[face][f->color_level];
        height = t->cube->height[face][f->color_level];
    } else if (r && r->pixels) {
        pixels = r->pixels;
        width = r->width;
        height = r->height;
    }
    if (!pixels && depth && depth->pixels) {
        width = depth->width;
        height = depth->height;
    } else if (!pixels && stencil && stencil->pixels) {
        width = stencil->width;
        height = stencil->height;
    }
    if (width > 0 && height > 0) {
        NTGLformat format = r && r->pixels
                                ? r->format == GL_RGB565   ? NTGL_RGB565
                                  : r->format == GL_RGBA4 ? NTGL_RGBA4444
                                                         : NTGL_RGBA5551
                                : NTGL_RGBA8888;
        int bytes_per_pixel = r && r->pixels ? 2 : 4;
        NTGLframebuffer fb = {pixels, width, height, width * bytes_per_pixel, format,
                              NTGL_ORIGIN_BOTTOM_LEFT};
        if (ntglAttachFramebuffer(ntglGetCurrent(), &fb) != NTGL_OK) {
            error(GL_OUT_OF_MEMORY);
            return;
        }
        ntglAttachAuxBuffers(ntglGetCurrent(),
                             depth && depth->format == GL_DEPTH_COMPONENT16
                                 ? (float *)depth->pixels
                                 : NULL,
                             stencil && stencil->format == GL_STENCIL_INDEX8
                                 ? stencil->pixels
                                 : NULL);
    }
}

static void attach_bound_or_default(void)
{
    Framebuffer *f = framebuffer(bound_framebuffer);

    if (f && (f->color_texture || f->color_renderbuffer || f->depth_texture ||
              f->depth_renderbuffer || f->stencil_texture || f->stencil_renderbuffer))
        attach_bound_framebuffer();
    else if (have_default_framebuffer)
        ntglAttachFramebuffer(ntglGetCurrent(), &default_framebuffer);
}

static void sync_texture(void)
{
    int saved_unit = state.active_texture;
    Texture *t;

    state.active_texture = 0;
    t = bound_texture_for_target(GL_TEXTURE_2D);
    state.active_texture = saved_unit;
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
                         t->wrap_s == GL_REPEAT
                             ? NTGL_REPEAT
                             : t->wrap_s == GL_MIRRORED_REPEAT ? NTGL_MIRRORED_REPEAT
                                                               : NTGL_CLAMP_TO_EDGE,
                         t->wrap_t == GL_REPEAT
                             ? NTGL_REPEAT
                             : t->wrap_t == GL_MIRRORED_REPEAT ? NTGL_MIRRORED_REPEAT
                                                               : NTGL_CLAMP_TO_EDGE,
                         environment,
                         {state.tex_env_color[0], state.tex_env_color[1], state.tex_env_color[2],
                          state.tex_env_color[3]}};
        ntglBindTexture(&n);
    } else
        ntglBindTexture(NULL);
}

static int wrapped_texture_index(int index, int size, GLint wrap)
{
    if (wrap == GL_REPEAT) {
        index %= size;
        return index < 0 ? index + size : index;
    }
    if (wrap == GL_MIRRORED_REPEAT) {
        int period = size * 2;

        index %= period;
        if (index < 0)
            index += period;
        return index < size ? index : period - index - 1;
    }
    if (index < 0)
        return 0;
    return index < size ? index : size - 1;
}

static float normalized_texture_coordinate(float coordinate, GLint wrap)
{
    if (!isfinite(coordinate))
        return 0.0f;
    if (wrap == GL_REPEAT)
        return coordinate - floorf(coordinate);
    if (wrap == GL_MIRRORED_REPEAT) {
        coordinate = fmodf(coordinate, 2.0f);
        if (coordinate < 0.0f)
            coordinate += 2.0f;
        return coordinate <= 1.0f ? coordinate : 2.0f - coordinate;
    }
    if (coordinate < 0.0f)
        return 0.0f;
    return coordinate > 1.0f ? 1.0f : coordinate;
}

static void sample_texture_level(const Texture *texture_object, const unsigned char *storage,
                                 int width, int height, float s, float t, int nearest,
                                 float color[4])
{
    float x;
    float y;
    int component;

    x = normalized_texture_coordinate(s, texture_object->wrap_s) * width;
    y = normalized_texture_coordinate(t, texture_object->wrap_t) * height;
    if (nearest) {
        int sample_x = wrapped_texture_index((int)floorf(x), width,
                                             texture_object->wrap_s);
        int sample_y = wrapped_texture_index((int)floorf(y), height,
                                             texture_object->wrap_t);
        const GLubyte *pixel = storage +
                               ((size_t)sample_y * width + (size_t)sample_x) *
                                   4;

        for (component = 0; component < 4; ++component)
            color[component] = pixel[component] / 255.0f;
        return;
    }
    {
        int unwrapped_x0 = (int)floorf(x - 0.5f);
        int unwrapped_y0 = (int)floorf(y - 0.5f);
        int x0 = wrapped_texture_index(unwrapped_x0, width,
                                       texture_object->wrap_s);
        int y0 = wrapped_texture_index(unwrapped_y0, height,
                                       texture_object->wrap_t);
        int x1 = wrapped_texture_index(unwrapped_x0 + 1, width,
                                       texture_object->wrap_s);
        int y1 = wrapped_texture_index(unwrapped_y0 + 1, height,
                                       texture_object->wrap_t);
        float alpha_x = x - 0.5f - unwrapped_x0;
        float alpha_y = y - 0.5f - unwrapped_y0;

        for (component = 0; component < 4; ++component) {
            float c00 = storage[((size_t)y0 * width + x0) * 4 + component] / 255.0f;
            float c10 = storage[((size_t)y0 * width + x1) * 4 + component] / 255.0f;
            float c01 = storage[((size_t)y1 * width + x0) * 4 + component] / 255.0f;
            float c11 = storage[((size_t)y1 * width + x1) * 4 + component] / 255.0f;

            color[component] =
                (c00 * (1.0f - alpha_x) + c10 * alpha_x) * (1.0f - alpha_y) +
                (c01 * (1.0f - alpha_x) + c11 * alpha_x) * alpha_y;
        }
    }
}

static int texture_level(const Texture *texture_object, int level, const unsigned char **storage,
                         int *width, int *height)
{
    if (level < 0)
        level = 0;
    if (level >= GL_MAX_MIP_LEVELS)
        level = GL_MAX_MIP_LEVELS - 1;
    while (level > 0 && !texture_object->mipmap[level])
        --level;
    *storage = level ? texture_object->mipmap[level] : texture_object->rgba;
    *width = level ? texture_object->mip_width[level] : texture_object->width;
    *height = level ? texture_object->mip_height[level] : texture_object->height;
    return 1;
}

static int mipmap_filter(GLint filter)
{
    return filter == GL_NEAREST_MIPMAP_NEAREST || filter == GL_LINEAR_MIPMAP_NEAREST ||
           filter == GL_NEAREST_MIPMAP_LINEAR || filter == GL_LINEAR_MIPMAP_LINEAR;
}

static float texture_filter_switch(const Texture *texture_object)
{
    return texture_object->mag_filter == GL_LINEAR &&
                   (texture_object->min_filter == GL_NEAREST_MIPMAP_NEAREST ||
                    texture_object->min_filter == GL_NEAREST_MIPMAP_LINEAR)
               ? 0.5f
               : 0.0f;
}

static int nearest_mipmap_level(float lod)
{
    if (!(lod > 0.5f))
        return 0;
    if (!isfinite(lod) || lod >= GL_MAX_MIP_LEVELS - 0.5f)
        return GL_MAX_MIP_LEVELS - 1;
    return (int)ceilf(lod + 0.5f) - 1;
}

static int linear_mipmap_level(float lod, float *fraction)
{
    float level;

    if (!(lod > 0.0f)) {
        *fraction = 0.0f;
        return 0;
    }
    if (!isfinite(lod) || lod >= GL_MAX_MIP_LEVELS - 1) {
        *fraction = 0.0f;
        return GL_MAX_MIP_LEVELS - 1;
    }
    level = floorf(lod);
    *fraction = lod - level;
    return (int)level;
}

static int power_of_two(int value)
{
    return value > 0 && !(value & (value - 1));
}

static int valid_texture_level(GLint level)
{
    return level >= 0 && level < GL_MAX_MIP_LEVELS &&
           (MESAGL_MAX_TEXTURE_SIZE >> level) > 0;
}

static int valid_texture_dimensions(GLenum target, GLint level, GLsizei width,
                                    GLsizei height)
{
    int maximum;

    if (!valid_texture_level(level) || width < 0 || height < 0)
        return 0;
    if (level > 0 && (!power_of_two(width) || !power_of_two(height)))
        return 0;
    maximum = MESAGL_MAX_TEXTURE_SIZE >> level;
    return width <= maximum && height <= maximum &&
           (cube_face(target) < 0 || width == height);
}

static int texture_2d_complete(const Texture *texture_object)
{
    int width;
    int height;
    int level;

    if (!texture_object->rgba || texture_object->width <= 0 || texture_object->height <= 0)
        return 0;
    if ((!power_of_two(texture_object->width) || !power_of_two(texture_object->height)) &&
        (mipmap_filter(texture_object->min_filter) || texture_object->wrap_s != GL_CLAMP_TO_EDGE ||
         texture_object->wrap_t != GL_CLAMP_TO_EDGE))
        return 0;
    if (!mipmap_filter(texture_object->min_filter))
        return 1;
    width = texture_object->width;
    height = texture_object->height;
    for (level = 1; width > 1 || height > 1; ++level) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        if (level >= GL_MAX_MIP_LEVELS || !texture_object->mipmap[level] ||
            texture_object->mip_width[level] != width ||
            texture_object->mip_height[level] != height ||
            texture_object->mip_format[level] != texture_object->format ||
            texture_object->mip_type[level] != texture_object->type)
            return 0;
    }
    return 1;
}

static int texture_cube_complete(const Texture *texture_object)
{
    GLenum format;
    GLenum type;
    int size;
    int face;
    int level;

    if (!texture_object->cube || !texture_object->cube->pixels[0][0])
        return 0;
    size = texture_object->cube->width[0][0];
    format = texture_object->cube->format[0][0];
    type = texture_object->cube->type[0][0];
    if (size <= 0 || texture_object->cube->height[0][0] != size)
        return 0;
    for (face = 1; face < 6; ++face)
        if (!texture_object->cube->pixels[face][0] ||
            texture_object->cube->width[face][0] != size ||
            texture_object->cube->height[face][0] != size ||
            texture_object->cube->format[face][0] != format ||
            texture_object->cube->type[face][0] != type)
            return 0;
    if (!power_of_two(size) &&
        (mipmap_filter(texture_object->min_filter) || texture_object->wrap_s != GL_CLAMP_TO_EDGE ||
         texture_object->wrap_t != GL_CLAMP_TO_EDGE))
        return 0;
    if (!mipmap_filter(texture_object->min_filter))
        return 1;
    for (level = 1; size > 1; ++level) {
        size /= 2;
        for (face = 0; face < 6; ++face)
            if (level >= GL_MAX_MIP_LEVELS || !texture_object->cube->pixels[face][level] ||
                texture_object->cube->width[face][level] != size ||
                texture_object->cube->height[face][level] != size ||
                texture_object->cube->format[face][level] != format ||
                texture_object->cube->type[face][level] != type)
                return 0;
    }
    return 1;
}

static int incomplete_texture_color(float color[4])
{
    color[0] = 0.0f;
    color[1] = 0.0f;
    color[2] = 0.0f;
    color[3] = 1.0f;
    return 1;
}

int mesaGLPrepareTexture2D(int unit, MesaGLPreparedTexture2D *sampler)
{
    Texture *texture_object;
    int min_nearest;
    int mag_nearest;

    if (!sampler || unit < 0 || unit >= MESAGL_MAX_TEXTURE_UNITS)
        return 0;
    texture_object = texture_2d_for_unit(unit);
    if (!texture_object || !texture_2d_complete(texture_object) ||
        mipmap_filter(texture_object->min_filter))
        return 0;
    min_nearest = texture_object->min_filter == GL_NEAREST;
    mag_nearest = texture_object->mag_filter == GL_NEAREST;
    if (min_nearest != mag_nearest)
        return 0;
    if (!texture_object->rgb_white_known) {
        size_t pixel_count = (size_t)texture_object->width *
                             texture_object->height;
        size_t pixel;

        texture_object->rgb_white = 1;
        for (pixel = 0; pixel < pixel_count; ++pixel) {
            const unsigned char *rgba = texture_object->rgba + pixel * 4;

            if (rgba[0] != 255 || rgba[1] != 255 || rgba[2] != 255) {
                texture_object->rgb_white = 0;
                break;
            }
        }
        texture_object->rgb_white_known = 1;
    }
    sampler->texture = texture_object;
    sampler->pixels = texture_object->rgba;
    sampler->width = texture_object->width;
    sampler->height = texture_object->height;
    sampler->nearest = min_nearest;
    sampler->wrap_s = texture_object->wrap_s;
    sampler->wrap_t = texture_object->wrap_t;
    sampler->rgb_white = texture_object->rgb_white;
    return 1;
}

int mesaGLSamplePreparedTexture2D(const MesaGLPreparedTexture2D *sampler,
                                  float s, float t, float color[4])
{
    if (!sampler || !sampler->texture || !sampler->pixels)
        return 0;
    sample_texture_level((const Texture *)sampler->texture, sampler->pixels,
                         sampler->width, sampler->height, s, t,
                         sampler->nearest, color);
    return 1;
}

int mesaGLSampleTexture2DLod(int unit, float s, float coordinate_t, float lod, float color[4])
{
    Texture *texture_object;
    const unsigned char *storage0;
    const unsigned char *storage1;
    int width0;
    int height0;
    int width1;
    int height1;
    int uses_mipmaps;
    int linear_mipmap;
    int nearest_texels;
    int level0;
    int level1;
    float fraction;
    float switch_level;

    if (unit < 0 || unit >= MESAGL_MAX_TEXTURE_UNITS)
        return 0;
    texture_object = texture_2d_for_unit(unit);
    if (!texture_object || !texture_2d_complete(texture_object))
        return incomplete_texture_color(color);
    uses_mipmaps = texture_object->min_filter == GL_NEAREST_MIPMAP_NEAREST ||
                   texture_object->min_filter == GL_LINEAR_MIPMAP_NEAREST ||
                   texture_object->min_filter == GL_NEAREST_MIPMAP_LINEAR ||
                   texture_object->min_filter == GL_LINEAR_MIPMAP_LINEAR;
    linear_mipmap = texture_object->min_filter == GL_NEAREST_MIPMAP_LINEAR ||
                    texture_object->min_filter == GL_LINEAR_MIPMAP_LINEAR;
    switch_level = texture_filter_switch(texture_object);
    if (lod <= switch_level || !uses_mipmaps) {
        int nearest = lod <= switch_level ? texture_object->mag_filter == GL_NEAREST
                                          : texture_object->min_filter == GL_NEAREST;

        sample_texture_level(texture_object, texture_object->rgba, texture_object->width,
                             texture_object->height, s, coordinate_t, nearest, color);
        return 1;
    }
    nearest_texels = texture_object->min_filter == GL_NEAREST_MIPMAP_NEAREST ||
                     texture_object->min_filter == GL_NEAREST_MIPMAP_LINEAR;
    level0 = linear_mipmap ? linear_mipmap_level(lod, &fraction)
                           : nearest_mipmap_level(lod);
    level1 = linear_mipmap ? level0 + 1 : level0;
    if (!linear_mipmap)
        fraction = 0.0f;
    texture_level(texture_object, level0, &storage0, &width0, &height0);
    sample_texture_level(texture_object, storage0, width0, height0, s, coordinate_t,
                         nearest_texels, color);
    if (linear_mipmap && fraction > 0.0f) {
        float second[4];
        int component;

        texture_level(texture_object, level1, &storage1, &width1, &height1);
        sample_texture_level(texture_object, storage1, width1, height1, s, coordinate_t,
                             nearest_texels, second);
        for (component = 0; component < 4; ++component)
            color[component] = color[component] * (1.0f - fraction) +
                               second[component] * fraction;
    }
    return 1;
}

int mesaGLSampleTexture2D(int unit, float s, float t, float color[4])
{
    return mesaGLSampleTexture2DLod(unit, s, t, 0.0f, color);
}

int mesaGLSampleTexture2DGrad(int unit, float s, float t, float dsdx, float dtdx, float dsdy,
                              float dtdy, float color[4])
{
    return mesaGLSampleTexture2DGradBias(unit, s, t, dsdx, dtdx, dsdy, dtdy, 0.0f, color);
}

int mesaGLSampleTexture2DGradBias(int unit, float s, float t, float dsdx, float dtdx, float dsdy,
                                  float dtdy, float bias, float color[4])
{
    Texture *texture_object;
    float gradient_x;
    float gradient_y;
    float rho;
    float lod;

    if (unit < 0 || unit >= MESAGL_MAX_TEXTURE_UNITS)
        return 0;
    texture_object = texture_2d_for_unit(unit);
    if (!texture_object || !texture_2d_complete(texture_object))
        return incomplete_texture_color(color);
    gradient_x = sqrtf(dsdx * dsdx * texture_object->width * texture_object->width +
                       dtdx * dtdx * texture_object->height * texture_object->height);
    gradient_y = sqrtf(dsdy * dsdy * texture_object->width * texture_object->width +
                       dtdy * dtdy * texture_object->height * texture_object->height);
    rho = fmaxf(gradient_x, gradient_y);
    lod = rho > 0.0f ? log2f(rho) + bias : -INFINITY;
    return mesaGLSampleTexture2DLod(unit, s, t, lod, color);
}

static int cube_coordinates(float x, float y, float z, int *face, float *s, float *t)
{
    float absolute_x = fabsf(x);
    float absolute_y = fabsf(y);
    float absolute_z = fabsf(z);

    if (!isfinite(x) || !isfinite(y) || !isfinite(z) ||
        (absolute_x == 0.0f && absolute_y == 0.0f && absolute_z == 0.0f))
        return 0;
    if (absolute_x >= absolute_y && absolute_x >= absolute_z) {
        *face = x >= 0.0f ? 0 : 1;
        *s = (x >= 0.0f ? -z : z) / absolute_x;
        *t = -y / absolute_x;
    } else if (absolute_y >= absolute_z) {
        *face = y >= 0.0f ? 2 : 3;
        *s = x / absolute_y;
        *t = (y >= 0.0f ? z : -z) / absolute_y;
    } else {
        *face = z >= 0.0f ? 4 : 5;
        *s = (z >= 0.0f ? x : -x) / absolute_z;
        *t = -y / absolute_z;
    }
    *s = (*s + 1.0f) * 0.5f;
    *t = (*t + 1.0f) * 0.5f;
    return 1;
}

static int cube_level(const Texture *texture_object, int face, int level,
                      const unsigned char **storage, int *width, int *height)
{
    if (level < 0)
        level = 0;
    if (level >= GL_MAX_MIP_LEVELS)
        level = GL_MAX_MIP_LEVELS - 1;
    while (level > 0 && !texture_object->cube->pixels[face][level])
        --level;
    *storage = texture_object->cube->pixels[face][level];
    *width = texture_object->cube->width[face][level];
    *height = texture_object->cube->height[face][level];
    return *storage && *width > 0 && *height > 0;
}

int mesaGLSampleTextureCubeLod(int unit, float x, float y, float z, float lod, float color[4])
{
    Texture *texture_object;
    const unsigned char *storage0;
    const unsigned char *storage1;
    float s;
    float t;
    float fraction;
    int face;
    int width0;
    int height0;
    int width1;
    int height1;
    int uses_mipmaps;
    int linear_mipmap;
    int nearest_texels;
    int level0;
    int level1;
    float switch_level;

    if (unit < 0 || unit >= MESAGL_MAX_TEXTURE_UNITS ||
        !cube_coordinates(x, y, z, &face, &s, &t))
        return 0;
    texture_object = texture_cube_for_unit(unit);
    if (!texture_object || !texture_cube_complete(texture_object))
        return incomplete_texture_color(color);
    uses_mipmaps = texture_object->min_filter == GL_NEAREST_MIPMAP_NEAREST ||
                   texture_object->min_filter == GL_LINEAR_MIPMAP_NEAREST ||
                   texture_object->min_filter == GL_NEAREST_MIPMAP_LINEAR ||
                   texture_object->min_filter == GL_LINEAR_MIPMAP_LINEAR;
    linear_mipmap = texture_object->min_filter == GL_NEAREST_MIPMAP_LINEAR ||
                    texture_object->min_filter == GL_LINEAR_MIPMAP_LINEAR;
    switch_level = texture_filter_switch(texture_object);
    if (lod <= switch_level || !uses_mipmaps) {
        int nearest = lod <= switch_level ? texture_object->mag_filter == GL_NEAREST
                                          : texture_object->min_filter == GL_NEAREST;

        if (!cube_level(texture_object, face, 0, &storage0, &width0, &height0))
            return 0;
        sample_texture_level(texture_object, storage0, width0, height0, s, t, nearest, color);
        return 1;
    }
    nearest_texels = texture_object->min_filter == GL_NEAREST_MIPMAP_NEAREST ||
                     texture_object->min_filter == GL_NEAREST_MIPMAP_LINEAR;
    level0 = linear_mipmap ? linear_mipmap_level(lod, &fraction)
                           : nearest_mipmap_level(lod);
    level1 = linear_mipmap ? level0 + 1 : level0;
    if (!linear_mipmap)
        fraction = 0.0f;
    if (!cube_level(texture_object, face, level0, &storage0, &width0, &height0))
        return 0;
    sample_texture_level(texture_object, storage0, width0, height0, s, t, nearest_texels, color);
    if (linear_mipmap && fraction > 0.0f &&
        cube_level(texture_object, face, level1, &storage1, &width1, &height1)) {
        float second[4];
        int component;

        sample_texture_level(texture_object, storage1, width1, height1, s, t, nearest_texels,
                             second);
        for (component = 0; component < 4; ++component)
            color[component] = color[component] * (1.0f - fraction) +
                               second[component] * fraction;
    }
    return 1;
}

int mesaGLSampleTextureCube(int unit, float x, float y, float z, float color[4])
{
    return mesaGLSampleTextureCubeLod(unit, x, y, z, 0.0f, color);
}

int mesaGLSampleTextureCubeGradBias(int unit, float x, float y, float z, float dxdx, float dydx,
                                    float dzdx, float dxdy, float dydy, float dzdy, float bias,
                                    float color[4])
{
    Texture *texture_object;
    float s;
    float t;
    float sx;
    float tx;
    float sy;
    float ty;
    float rho = 1.0f;
    int face;
    int face_x;
    int face_y;

    if (unit < 0 || unit >= MESAGL_MAX_TEXTURE_UNITS ||
        !cube_coordinates(x, y, z, &face, &s, &t))
        return 0;
    texture_object = texture_cube_for_unit(unit);
    if (!texture_object || !texture_cube_complete(texture_object))
        return incomplete_texture_color(color);
    if (cube_coordinates(x + dxdx, y + dydx, z + dzdx, &face_x, &sx, &tx) &&
        cube_coordinates(x + dxdy, y + dydy, z + dzdy, &face_y, &sy, &ty) &&
        face_x == face && face_y == face) {
        float width = (float)texture_object->cube->width[face][0];
        float height = (float)texture_object->cube->height[face][0];
        float gradient_x = hypotf((sx - s) * width, (tx - t) * height);
        float gradient_y = hypotf((sy - s) * width, (ty - t) * height);

        rho = fmaxf(gradient_x, gradient_y);
    }
    return mesaGLSampleTextureCubeLod(unit, x, y, z,
                                      rho > 0.0f ? log2f(rho) + bias : -INFINITY, color);
}

int mesaGLSetActiveTextureUnit(int unit)
{
    if (unit < 0 || unit >= MESAGL_MAX_TEXTURE_UNITS)
        return 0;
    state.active_texture = unit;
    return 1;
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
    case GL_FIXED:
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
    case GL_FIXED:
        return *(const GLint *)p / 65536.0f;
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
    else if (c == GL_POLYGON_OFFSET_FILL)
        ntglEnable(NTGL_POLYGON_OFFSET_FILL);
    else if (c == GL_DITHER)
        ntglEnable(NTGL_DITHER);
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
    else if (c == GL_POLYGON_OFFSET_FILL)
        ntglDisable(NTGL_POLYGON_OFFSET_FILL);
    else if (c == GL_DITHER)
        ntglDisable(NTGL_DITHER);
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
    case GL_SRC_ALPHA_SATURATE:
        return NTGL_SRC_ALPHA_SATURATE;
    default:
        return NTGL_ONE_MINUS_CONSTANT_ALPHA;
    }
}

static int valid_blend_factor(GLenum factor)
{
    return factor == GL_ZERO || factor == GL_ONE ||
           (factor >= GL_SRC_COLOR && factor <= GL_ONE_MINUS_DST_COLOR) ||
           (factor >= GL_CONSTANT_COLOR && factor <= GL_ONE_MINUS_CONSTANT_ALPHA) ||
           factor == GL_SRC_ALPHA_SATURATE;
}

static int valid_alpha_blend_factor(GLenum factor)
{
    return factor == GL_ZERO || factor == GL_ONE || factor == GL_SRC_ALPHA ||
           factor == GL_ONE_MINUS_SRC_ALPHA || factor == GL_DST_ALPHA ||
           factor == GL_ONE_MINUS_DST_ALPHA || factor == GL_CONSTANT_ALPHA ||
           factor == GL_ONE_MINUS_CONSTANT_ALPHA;
}

void glBlendFunc(GLenum s, GLenum d)
{
    if (!valid_blend_factor(s) || !valid_blend_factor(d) || d == GL_SRC_ALPHA_SATURATE) {
        error(GL_INVALID_ENUM);
        return;
    }
    state.blend_src = s;
    state.blend_dst = d;
    state.blend_src_alpha = s;
    state.blend_dst_alpha = d;
    ntglBlendFunc(bf(s), bf(d));
}

void glBlendFuncSeparate(GLenum sr, GLenum dr, GLenum sa, GLenum da)
{
    if (!valid_blend_factor(sr) || !valid_blend_factor(dr) ||
        !valid_alpha_blend_factor(sa) || !valid_alpha_blend_factor(da) ||
        dr == GL_SRC_ALPHA_SATURATE) {
        error(GL_INVALID_ENUM);
        return;
    }
    state.blend_src = sr;
    state.blend_dst = dr;
    state.blend_src_alpha = sa;
    state.blend_dst_alpha = da;
    ntglBlendFuncSeparate(bf(sr), bf(dr), bf(sa), bf(da));
}

void glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    state.blend_color[0] = red < 0.0f ? 0.0f : red > 1.0f ? 1.0f : red;
    state.blend_color[1] = green < 0.0f ? 0.0f : green > 1.0f ? 1.0f : green;
    state.blend_color[2] = blue < 0.0f ? 0.0f : blue > 1.0f ? 1.0f : blue;
    state.blend_color[3] = alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha;
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
    state.stencil_clear = value;
    ntglClearStencil((unsigned)value);
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
    if (!valid_depth_func(func)) {
        error(GL_INVALID_ENUM);
        return;
    }
    state.stencil_func[0] = state.stencil_func[1] = func;
    state.stencil_ref[0] = state.stencil_ref[1] = ref < 0 ? 0 : ref > 255 ? 255 : ref;
    state.stencil_value_mask[0] = state.stencil_value_mask[1] = mask & 0xffu;
    ntglStencilFunc(depth_func(func), (unsigned)state.stencil_ref[0], mask);
}

void glStencilMask(GLuint mask)
{
    state.stencil_write_mask[0] = state.stencil_write_mask[1] = mask & 0xffu;
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
    case GL_INCR_WRAP:
        return NTGL_INCR_WRAP;
    case GL_DECR_WRAP:
        return NTGL_DECR_WRAP;
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
            ops[i] != GL_DECR && ops[i] != GL_INCR_WRAP && ops[i] != GL_DECR_WRAP &&
            ops[i] != GL_INVERT) {
            error(GL_INVALID_ENUM);
            return;
        }
    ntglStencilOp(stencil_op(fail), stencil_op(zfail), stencil_op(zpass));
    state.stencil_fail[0] = state.stencil_fail[1] = fail;
    state.stencil_depth_fail[0] = state.stencil_depth_fail[1] = zfail;
    state.stencil_pass[0] = state.stencil_pass[1] = zpass;
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
    if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK) {
        error(GL_INVALID_ENUM);
        return;
    }
    state.cull_face = mode;
    ntglCullFace(mode == GL_FRONT ? 1 : mode == GL_BACK ? 0 : 2);
}

void glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    if (w < 0 || h < 0) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (w > MESAGL_MAX_VIEWPORT_DIMENSION)
        w = MESAGL_MAX_VIEWPORT_DIMENSION;
    if (h > MESAGL_MAX_VIEWPORT_DIMENSION)
        h = MESAGL_MAX_VIEWPORT_DIMENSION;
    state.viewport[0] = x;
    state.viewport[1] = y;
    state.viewport[2] = w;
    state.viewport[3] = h;
    ntglViewport(x, y, w, h);
}

void glScissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    if (w < 0 || h < 0) {
        error(GL_INVALID_VALUE);
        return;
    }
    state.scissor[0] = x;
    state.scissor[1] = y;
    state.scissor[2] = w;
    state.scissor[3] = h;
    ntglScissor(x, y, w, h);
}

void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    state.clear_color[0] = r < 0.0f ? 0.0f : r > 1.0f ? 1.0f : r;
    state.clear_color[1] = g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g;
    state.clear_color[2] = b < 0.0f ? 0.0f : b > 1.0f ? 1.0f : b;
    state.clear_color[3] = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
    ntglClearColor(r, g, b, a);
}

void glClearDepth(GLdouble d)
{
    state.clear_depth = d < 0.0 ? 0.0f : d > 1.0 ? 1.0f : (GLfloat)d;
    ntglClearDepth((float)d);
}

void glClearDepthf(GLclampf depth)
{
    glClearDepth(depth);
}

void glDepthRangef(GLclampf near_value, GLclampf far_value)
{
    state.depth_range[0] = near_value < 0.0f ? 0.0f : near_value > 1.0f ? 1.0f : near_value;
    state.depth_range[1] = far_value < 0.0f ? 0.0f : far_value > 1.0f ? 1.0f : far_value;
    ntglDepthRange(state.depth_range[0], state.depth_range[1]);
}

void glClear(GLbitfield m)
{
    if (m & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (!framebuffer_ready())
        return;
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

    if (first < 0 || count < 0) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (m != GL_POINTS && m != GL_LINES && m != GL_LINE_LOOP && m != GL_LINE_STRIP &&
        m != GL_TRIANGLES && m != GL_TRIANGLE_STRIP && m != GL_TRIANGLE_FAN) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!framebuffer_ready())
        return;
    if (mesaGLDrawGLES2Arrays(m, first, count))
        return;
    if (!count)
        return;
    mesaGLPrepareGLES2Draw();
    if (!(state.client_enabled & CA_VERTEX) || count < 0) {
        error(GL_INVALID_OPERATION);
        return;
    }
    glBegin(m);
    for (i = 0; i < count; ++i)
        submit((GLuint)first + (GLuint)i);
    glEnd();
}

void glDrawElements(GLenum m, GLsizei count, GLenum type, const GLvoid *idx)
{
    int i, batch = 0;

    if (count < 0) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (m != GL_POINTS && m != GL_LINES && m != GL_LINE_LOOP && m != GL_LINE_STRIP &&
        m != GL_TRIANGLES && m != GL_TRIANGLE_STRIP && m != GL_TRIANGLE_FAN) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_BYTE &&
        (!MESAGL_ENABLE_UINT_ELEMENT_INDICES || type != GL_UNSIGNED_INT)) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!framebuffer_ready())
        return;
    if (mesaGLDrawGLES2Elements(m, count, type, idx))
        return;
    if (!count)
        return;
    mesaGLPrepareGLES2Draw();
    idx = mesaGLResolveElementPointer(idx);
    if (!(state.client_enabled & CA_VERTEX) || count < 0 || !idx) {
        error(GL_INVALID_OPERATION);
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

    if (n < 0 || (n && !out)) {
        error(GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < n; ++i) {
        while (texture(next_texture))
            ++next_texture;
        out[i] = next_texture++;
    }
}

void glDeleteTextures(GLsizei n, const GLuint *names)
{
    int i;

    if (n < 0 || (n && !names)) {
        error(GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < n; ++i) {
        Texture *t = texture(names[i]);
        Framebuffer *f = framebuffer(bound_framebuffer);

        if (t)
            t->delete_pending = 1;
        if (f && f->color_texture == names[i]) {
            f->color_texture = 0;
            f->color_target = 0;
            f->color_level = 0;
            release_texture_attachment(names[i]);
        }
        if (f && f->depth_texture == names[i]) {
            f->depth_texture = 0;
            f->depth_target = 0;
            f->depth_level = 0;
            release_texture_attachment(names[i]);
        }
        if (f && f->stencil_texture == names[i]) {
            f->stencil_texture = 0;
            f->stencil_target = 0;
            f->stencil_level = 0;
            release_texture_attachment(names[i]);
        }
        {
            int unit;

            for (unit = 0; unit < MESAGL_MAX_TEXTURE_UNITS; ++unit) {
                if (state.bound_texture[unit] == names[i])
                    state.bound_texture[unit] = 0;
                if (state.bound_cube_texture[unit] == names[i])
                    state.bound_cube_texture[unit] = 0;
            }
            sync_texture();
        }
        t = texture(names[i]);
        if (t && !t->attachment_refs)
            destroy_texture(t);
        if (f)
            attach_bound_or_default();
    }
}

void glBindTexture(GLenum target, GLuint name)
{
    Texture *t;

    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        error(GL_INVALID_ENUM);
        return;
    }
    t = name ? new_texture(name) : NULL;
    if (name && !t)
        return;
    if (t && t->target && t->target != target) {
        error(GL_INVALID_OPERATION);
        return;
    }
    if (t)
        t->target = target;
    if (target == GL_TEXTURE_2D)
        state.bound_texture[state.active_texture] = name;
    else
        state.bound_cube_texture[state.active_texture] = name;
    if (!state.active_texture && target == GL_TEXTURE_2D)
        sync_texture();
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    Texture *t;

    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        error(GL_INVALID_ENUM);
        return;
    }
    t = bound_texture_for_target(target);
    if (pname == GL_TEXTURE_MAG_FILTER && param != GL_NEAREST && param != GL_LINEAR) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_TEXTURE_MIN_FILTER && param != GL_NEAREST && param != GL_LINEAR &&
        param != GL_NEAREST_MIPMAP_NEAREST && param != GL_LINEAR_MIPMAP_NEAREST &&
        param != GL_NEAREST_MIPMAP_LINEAR && param != GL_LINEAR_MIPMAP_LINEAR) {
        error(GL_INVALID_ENUM);
        return;
    }
    if ((pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T) && param != GL_REPEAT &&
#if !MESAGL_STRICT_GLES2
        param != GL_CLAMP &&
#endif
        param != GL_CLAMP_TO_EDGE && param != GL_MIRRORED_REPEAT) {
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

void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    int valid;

    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_TEXTURE_MAG_FILTER)
        valid = param == GL_NEAREST || param == GL_LINEAR;
    else if (pname == GL_TEXTURE_MIN_FILTER)
        valid = param == GL_NEAREST || param == GL_LINEAR ||
                param == GL_NEAREST_MIPMAP_NEAREST || param == GL_LINEAR_MIPMAP_NEAREST ||
                param == GL_NEAREST_MIPMAP_LINEAR || param == GL_LINEAR_MIPMAP_LINEAR;
    else if (pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T)
        valid = param == GL_REPEAT ||
#if !MESAGL_STRICT_GLES2
                param == GL_CLAMP ||
#endif
                param == GL_CLAMP_TO_EDGE || param == GL_MIRRORED_REPEAT;
    else
        valid = 0;
    if (!valid) {
        error(GL_INVALID_ENUM);
        return;
    }
    glTexParameteri(target, pname, (GLint)param);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
    if (!params) {
        error(GL_INVALID_VALUE);
        return;
    }
    glTexParameterf(target, pname, params[0]);
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
    if (!params) {
        error(GL_INVALID_VALUE);
        return;
    }
    glTexParameteri(target, pname, params[0]);
}

void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params)
{
    Texture *t;

    if (!params)
        return;
    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        error(GL_INVALID_ENUM);
        return;
    }
    t = bound_texture_for_target(target);
    if (pname == GL_TEXTURE_MIN_FILTER)
        *params = t->min_filter;
    else if (pname == GL_TEXTURE_MAG_FILTER)
        *params = t->mag_filter;
    else if (pname == GL_TEXTURE_WRAP_S)
        *params = t->wrap_s;
    else if (pname == GL_TEXTURE_WRAP_T)
        *params = t->wrap_t;
    else
        error(GL_INVALID_ENUM);
}

void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
    GLint value = 0;

    if (!params)
        return;
    glGetTexParameteriv(target, pname, &value);
    *params = (GLfloat)value;
}

GLboolean glIsTexture(GLuint name)
{
    Texture *texture_object = texture(name);

    return texture_object && !texture_object->delete_pending ? GL_TRUE : GL_FALSE;
}

static int generate_mipmap_levels(const unsigned char *base, int base_width, int base_height,
                                  GLenum base_format, GLenum base_type,
                                  unsigned char **levels, int *widths,
                                  int *heights, GLenum *formats, GLenum *types)
{
    const unsigned char *source = base;
    int source_width = base_width;
    int source_height = base_height;
    int level;

    for (level = 1; level < GL_MAX_MIP_LEVELS && (source_width > 1 || source_height > 1);
         ++level) {
        int width = source_width > 1 ? source_width / 2 : 1;
        int height = source_height > 1 ? source_height / 2 : 1;
        unsigned char *destination = (unsigned char *)ntglAlloc((size_t)width * height * 4);
        int x;
        int y;
        int component;

        if (!destination)
            return 0;
        for (y = 0; y < height; ++y)
            for (x = 0; x < width; ++x)
                for (component = 0; component < 4; ++component) {
                    int source_x = x * 2;
                    int source_y = y * 2;
                    int source_x1 = source_x + 1 < source_width ? source_x + 1 : source_x;
                    int source_y1 = source_y + 1 < source_height ? source_y + 1 : source_y;
                    unsigned sum =
                        source[((size_t)source_y * source_width + source_x) * 4 + component] +
                        source[((size_t)source_y * source_width + source_x1) * 4 + component] +
                        source[((size_t)source_y1 * source_width + source_x) * 4 + component] +
                        source[((size_t)source_y1 * source_width + source_x1) * 4 + component];

                    destination[((size_t)y * width + x) * 4 + component] =
                        (unsigned char)((sum + 2) / 4);
                }
        ntglFree(levels[level]);
        levels[level] = destination;
        widths[level] = width;
        heights[level] = height;
        formats[level] = base_format;
        types[level] = base_type;
        source = destination;
        source_width = width;
        source_height = height;
    }
    return 1;
}

void glGenerateMipmap(GLenum target)
{
    Texture *t;
    const unsigned char *source;
    int source_width, source_height, level;

    if (target == GL_TEXTURE_CUBE_MAP) {
        int face;
        int size;
        GLenum format;
        GLenum type;

        t = bound_texture_for_target(target);
        if (!t || !t->cube || !t->cube->pixels[0][0]) {
            error(GL_INVALID_OPERATION);
            return;
        }
        size = t->cube->width[0][0];
        format = t->cube->format[0][0];
        type = t->cube->type[0][0];
        for (face = 0; face < 6; ++face)
            if (!t->cube->pixels[face][0] || t->cube->width[face][0] != size ||
                t->cube->height[face][0] != size ||
                t->cube->format[face][0] != format ||
                t->cube->type[face][0] != type) {
                error(GL_INVALID_OPERATION);
                return;
            }
        if (!power_of_two(size)) {
            error(GL_INVALID_OPERATION);
            return;
        }
        for (face = 0; face < 6; ++face) {
            if (!generate_mipmap_levels(t->cube->pixels[face][0], t->cube->width[face][0],
                                        t->cube->height[face][0], t->cube->format[face][0],
                                        t->cube->type[face][0],
                                        t->cube->pixels[face], t->cube->width[face],
                                        t->cube->height[face], t->cube->format[face],
                                        t->cube->type[face])) {
                error(GL_OUT_OF_MEMORY);
                return;
            }
        }
        t->mipmap_complete = 1;
        return;
    }
    if (target != GL_TEXTURE_2D) {
        error(GL_INVALID_ENUM);
        return;
    }
    t = bound_texture_for_target(target);
    if (!t || !t->rgba || t->width <= 0 || t->height <= 0 || !power_of_two(t->width) ||
        !power_of_two(t->height)) {
        error(GL_INVALID_OPERATION);
        return;
    }
    source = t->rgba;
    source_width = t->width;
    source_height = t->height;
    for (level = 1; level < GL_MAX_MIP_LEVELS && (source_width > 1 || source_height > 1);
         ++level) {
        int width = source_width > 1 ? source_width / 2 : 1;
        int height = source_height > 1 ? source_height / 2 : 1;
        unsigned char *destination = (unsigned char *)ntglAlloc((size_t)width * height * 4);
        int x, y, component;

        if (!destination) {
            error(GL_OUT_OF_MEMORY);
            return;
        }
        for (y = 0; y < height; ++y)
            for (x = 0; x < width; ++x)
                for (component = 0; component < 4; ++component) {
                    int sx = x * 2, sy = y * 2;
                    int sx1 = sx + 1 < source_width ? sx + 1 : sx;
                    int sy1 = sy + 1 < source_height ? sy + 1 : sy;
                    unsigned sum = source[((size_t)sy * source_width + sx) * 4 + component] +
                                   source[((size_t)sy * source_width + sx1) * 4 + component] +
                                   source[((size_t)sy1 * source_width + sx) * 4 + component] +
                                   source[((size_t)sy1 * source_width + sx1) * 4 + component];

                    destination[((size_t)y * width + x) * 4 + component] =
                        (unsigned char)((sum + 2) / 4);
                }
        ntglFree(t->mipmap[level]);
        t->mipmap[level] = destination;
        t->mip_width[level] = width;
        t->mip_height[level] = height;
        t->mip_format[level] = t->format;
        t->mip_type[level] = t->type;
        source = destination;
        source_width = width;
        source_height = height;
    }
    t->mipmap_complete = 1;
}

static int copy_texture_components(GLenum format)
{
    if (format == GL_ALPHA || format == GL_LUMINANCE)
        return 1;
    if (format == GL_LUMINANCE_ALPHA)
        return 2;
    if (format == GL_RGB)
        return 3;
    if (format == GL_RGBA)
        return 4;
    return 0;
}

static int framebuffer_color_has_alpha(void)
{
    if (bound_framebuffer) {
        Framebuffer *framebuffer_object = framebuffer(bound_framebuffer);

        if (framebuffer_object && framebuffer_object->color_texture) {
            Texture *texture_object = texture(framebuffer_object->color_texture);
            int face = cube_face(framebuffer_object->color_target);
            GLenum format = 0;

            if (texture_object && framebuffer_object->color_target == GL_TEXTURE_2D)
                format = framebuffer_object->color_level
                             ? texture_object->mip_format[framebuffer_object->color_level]
                             : texture_object->format;
            else if (texture_object && texture_object->cube && face >= 0)
                format = texture_object->cube->format[face]
                                                     [framebuffer_object->color_level];
            return format == GL_RGBA;
        }
        if (framebuffer_object && framebuffer_object->color_renderbuffer) {
            Renderbuffer *renderbuffer_object =
                renderbuffer(framebuffer_object->color_renderbuffer);

            return renderbuffer_object &&
                   (renderbuffer_object->format == GL_RGBA4 ||
                    renderbuffer_object->format == GL_RGB5_A1);
        }
        return 0;
    }
    {
        const NTGLframebuffer *framebuffer_object =
            ntglGetFramebuffer(ntglGetCurrent());

        return framebuffer_object &&
               (framebuffer_object->format == NTGL_ARGB8888 ||
                framebuffer_object->format == NTGL_RGBA8888 ||
                framebuffer_object->format == NTGL_BGRA8888 ||
                framebuffer_object->format == NTGL_RGBA4444 ||
                framebuffer_object->format == NTGL_RGBA5551);
    }
}

static int framebuffer_copy_format_compatible(GLenum internal_format)
{
    return framebuffer_color_has_alpha() || internal_format == GL_LUMINANCE ||
           internal_format == GL_RGB;
}

static void convert_copy_pixels(GLubyte *destination, const GLubyte *source, size_t pixel_count,
                                GLenum format)
{
    size_t pixel;

    for (pixel = 0; pixel < pixel_count; ++pixel) {
        const GLubyte *rgba = source + pixel * 4;

        if (format == GL_ALPHA)
            destination[pixel] = rgba[3];
        else if (format == GL_LUMINANCE)
            destination[pixel] = rgba[0];
        else if (format == GL_LUMINANCE_ALPHA) {
            destination[pixel * 2] = rgba[0];
            destination[pixel * 2 + 1] = rgba[3];
        } else if (format == GL_RGB) {
            memcpy(destination + pixel * 3, rgba, 3);
        } else {
            memcpy(destination + pixel * 4, rgba, 4);
        }
    }
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum internal_format, GLint x, GLint y,
                      GLsizei width, GLsizei height, GLint border)
{
    GLubyte *pixels;
    GLubyte *upload_pixels_buffer;
    size_t pixel_count;
    int components;
    GLint saved_pack_alignment;
    GLint saved_unpack_alignment;
    GLint saved_unpack_row_length;

    if (target != GL_TEXTURE_2D && cube_face(target) < 0) {
        error(GL_INVALID_ENUM);
        return;
    }
    components = copy_texture_components(internal_format);
    if (!components) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!valid_texture_dimensions(target, level, width, height) || border != 0 ||
        (width && (size_t)height > SIZE_MAX / (size_t)width / 4)) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (!framebuffer_ready())
        return;
    if (!framebuffer_copy_format_compatible(internal_format)) {
        error(GL_INVALID_OPERATION);
        return;
    }
    pixel_count = (size_t)width * height;
    pixels = pixel_count ? (GLubyte *)ntglAlloc(pixel_count * 4) : NULL;
    if (pixel_count && !pixels) {
        error(GL_OUT_OF_MEMORY);
        return;
    }
    if (pixel_count) {
        memset(pixels, 0, pixel_count * 4);
        saved_pack_alignment = state.pack_alignment;
        state.pack_alignment = 1;
        glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        state.pack_alignment = saved_pack_alignment;
    }
    upload_pixels_buffer = pixels;
    if (components != 4 && pixel_count) {
        upload_pixels_buffer = (GLubyte *)ntglAlloc(pixel_count * (size_t)components);
        if (!upload_pixels_buffer) {
            ntglFree(pixels);
            error(GL_OUT_OF_MEMORY);
            return;
        }
        convert_copy_pixels(upload_pixels_buffer, pixels, pixel_count, internal_format);
    }
    saved_unpack_alignment = state.unpack_alignment;
    saved_unpack_row_length = state.unpack_row_length;
    state.unpack_alignment = 1;
    state.unpack_row_length = 0;
    glTexImage2D(target, level, (GLint)internal_format, width, height, border, internal_format,
                 GL_UNSIGNED_BYTE, upload_pixels_buffer);
    state.unpack_alignment = saved_unpack_alignment;
    state.unpack_row_length = saved_unpack_row_length;
    if (upload_pixels_buffer != pixels)
        ntglFree(upload_pixels_buffer);
    ntglFree(pixels);
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x,
                         GLint y, GLsizei width, GLsizei height)
{
    GLubyte *pixels;
    GLubyte *upload_pixels_buffer;
    Texture *texture_object;
    GLenum format;
    size_t pixel_count;
    int components;
    int face;
    GLint saved_pack_alignment;
    GLint saved_unpack_alignment;
    GLint saved_unpack_row_length;

    face = cube_face(target);
    if (target != GL_TEXTURE_2D && face < 0) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!valid_texture_level(level) || xoffset < 0 || yoffset < 0 || width < 0 ||
        height < 0 || (width && (size_t)height > SIZE_MAX / (size_t)width / 4)) {
        error(GL_INVALID_VALUE);
        return;
    }
    texture_object = bound_texture_for_target(target);
    format = face >= 0 && texture_object->cube ? texture_object->cube->format[face][level]
             : level                           ? texture_object->mip_format[level]
                                               : texture_object->format;
    components = copy_texture_components(format);
    if (!components) {
        error(GL_INVALID_OPERATION);
        return;
    }
    {
        int destination_width = face >= 0 && texture_object->cube
                                    ? texture_object->cube->width[face][level]
                                : level ? texture_object->mip_width[level]
                                        : texture_object->width;
        int destination_height = face >= 0 && texture_object->cube
                                     ? texture_object->cube->height[face][level]
                                 : level ? texture_object->mip_height[level]
                                         : texture_object->height;

        if (xoffset > destination_width || yoffset > destination_height ||
            width > destination_width - xoffset ||
            height > destination_height - yoffset) {
            error(GL_INVALID_VALUE);
            return;
        }
    }
    if (!framebuffer_ready())
        return;
    if (!framebuffer_copy_format_compatible(format)) {
        error(GL_INVALID_OPERATION);
        return;
    }
    pixel_count = (size_t)width * height;
    pixels = pixel_count ? (GLubyte *)ntglAlloc(pixel_count * 4) : NULL;
    if (pixel_count && !pixels) {
        error(GL_OUT_OF_MEMORY);
        return;
    }
    if (pixel_count) {
        memset(pixels, 0, pixel_count * 4);
        saved_pack_alignment = state.pack_alignment;
        state.pack_alignment = 1;
        glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        state.pack_alignment = saved_pack_alignment;
    }
    upload_pixels_buffer = pixels;
    if (components != 4 && pixel_count) {
        upload_pixels_buffer = (GLubyte *)ntglAlloc(pixel_count * (size_t)components);
        if (!upload_pixels_buffer) {
            ntglFree(pixels);
            error(GL_OUT_OF_MEMORY);
            return;
        }
        convert_copy_pixels(upload_pixels_buffer, pixels, pixel_count, format);
    }
    saved_unpack_alignment = state.unpack_alignment;
    saved_unpack_row_length = state.unpack_row_length;
    state.unpack_alignment = 1;
    state.unpack_row_length = 0;
    glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, GL_UNSIGNED_BYTE,
                    upload_pixels_buffer);
    state.unpack_alignment = saved_unpack_alignment;
    state.unpack_row_length = saved_unpack_row_length;
    if (upload_pixels_buffer != pixels)
        ntglFree(upload_pixels_buffer);
    ntglFree(pixels);
}

void glCompressedTexImage2D(GLenum target, GLint level, GLenum internal_format, GLsizei width,
                            GLsizei height, GLint border, GLsizei image_size, const void *data)
{
    (void)target;
    (void)level;
    (void)internal_format;
    (void)width;
    (void)height;
    (void)border;
    (void)image_size;
    (void)data;
    error(GL_INVALID_ENUM);
}

void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                               GLsizei width, GLsizei height, GLenum format, GLsizei image_size,
                               const void *data)
{
    (void)target;
    (void)level;
    (void)xoffset;
    (void)yoffset;
    (void)width;
    (void)height;
    (void)format;
    (void)image_size;
    (void)data;
    error(GL_INVALID_ENUM);
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
    }
#if !MESAGL_STRICT_GLES2
    else if (p == GL_UNPACK_ROW_LENGTH) {
        if (v < 0) {
            error(GL_INVALID_VALUE);
            return;
        }
        state.unpack_row_length = v;
    }
#endif
    else
        error(GL_INVALID_ENUM);
}

static int valid_texture_format(GLenum format)
{
    return format == GL_ALPHA || format == GL_LUMINANCE || format == GL_LUMINANCE_ALPHA ||
           format == GL_RGB || format == GL_RGBA || format == GL_BGRA;
}

static int valid_texture_type(GLenum type)
{
    return type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT_5_6_5 ||
           type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1;
}

static int valid_texture_pair(GLenum format, GLenum type)
{
    if (type == GL_UNSIGNED_SHORT_5_6_5)
        return format == GL_RGB;
    if (type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1)
        return format == GL_RGBA;
    return type == GL_UNSIGNED_BYTE && valid_texture_format(format);
}

static int texture_upload_layout(int width, int height, GLenum format, GLenum type,
                                 size_t *row_bytes)
{
    size_t alignment = (size_t)state.unpack_alignment;
    size_t bytes_per_pixel;
    size_t row_pixels;
    size_t bytes;
    size_t final_row_bytes;

    bytes_per_pixel = type == GL_UNSIGNED_BYTE
                          ? (format == GL_ALPHA || format == GL_LUMINANCE
                                 ? 1
                                 : format == GL_LUMINANCE_ALPHA ? 2
                                 : format == GL_RGB             ? 3
                                                                : 4)
                          : 2;
    row_pixels = state.unpack_row_length ? (size_t)state.unpack_row_length : (size_t)width;
    if (row_pixels > SIZE_MAX / bytes_per_pixel)
        return 0;
    bytes = row_pixels * bytes_per_pixel;
    if (bytes > SIZE_MAX - (alignment - 1))
        return 0;
    *row_bytes = (bytes + alignment - 1) & ~(alignment - 1);
    if ((size_t)width > SIZE_MAX / bytes_per_pixel)
        return 0;
    final_row_bytes = (size_t)width * bytes_per_pixel;
    if (height > 1 && *row_bytes > (SIZE_MAX - final_row_bytes) / (size_t)(height - 1))
        return 0;
    return 1;
}

static void upload_pixels(unsigned char *destination, int destination_width, int x_offset,
                          int y_offset, int width, int height, GLenum format, GLenum type,
                          size_t row_bytes, const void *data)
{
    int bytes_per_pixel = type == GL_UNSIGNED_BYTE
                              ? (format == GL_ALPHA || format == GL_LUMINANCE
                                     ? 1
                                     : format == GL_LUMINANCE_ALPHA ? 2
                                     : format == GL_RGB             ? 3
                                                                    : 4)
                              : 2;
    int x;
    int y;

    for (y = 0; y < height; ++y) {
        const unsigned char *row = (const unsigned char *)data + (size_t)y * row_bytes;

        for (x = 0; x < width; ++x) {
            const unsigned char *source = row + (size_t)x * bytes_per_pixel;
            unsigned char *dest = destination +
                                  (size_t)((y_offset + y) * destination_width + x_offset + x) * 4;

            if (type != GL_UNSIGNED_BYTE) {
                GLushort value;

                memcpy(&value, source, sizeof(value));
                if (type == GL_UNSIGNED_SHORT_5_6_5) {
                    dest[0] = (GLubyte)(((value >> 11) & 31) * 255 / 31);
                    dest[1] = (GLubyte)(((value >> 5) & 63) * 255 / 63);
                    dest[2] = (GLubyte)((value & 31) * 255 / 31);
                    dest[3] = 255;
                } else if (type == GL_UNSIGNED_SHORT_4_4_4_4) {
                    dest[0] = (GLubyte)(((value >> 12) & 15) * 17);
                    dest[1] = (GLubyte)(((value >> 8) & 15) * 17);
                    dest[2] = (GLubyte)(((value >> 4) & 15) * 17);
                    dest[3] = (GLubyte)((value & 15) * 17);
                } else {
                    dest[0] = (GLubyte)(((value >> 11) & 31) * 255 / 31);
                    dest[1] = (GLubyte)(((value >> 6) & 31) * 255 / 31);
                    dest[2] = (GLubyte)(((value >> 1) & 31) * 255 / 31);
                    dest[3] = (value & 1) ? 255 : 0;
                }
            } else if (format == GL_ALPHA) {
                dest[0] = dest[1] = dest[2] = 255;
                dest[3] = source[0];
            } else if (format == GL_LUMINANCE) {
                dest[0] = dest[1] = dest[2] = source[0];
                dest[3] = 255;
            } else if (format == GL_LUMINANCE_ALPHA) {
                dest[0] = dest[1] = dest[2] = source[0];
                dest[3] = source[1];
            } else if (format == GL_BGRA) {
                dest[0] = source[2];
                dest[1] = source[1];
                dest[2] = source[0];
                dest[3] = source[3];
            } else {
                dest[0] = source[0];
                dest[1] = source[1];
                dest[2] = source[2];
                dest[3] = bytes_per_pixel == 4 ? source[3] : 255;
            }
        }
    }
}

void glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,
                  GLenum fmt, GLenum type, const GLvoid *data)
{
    Texture *t;
    unsigned char *rgba;
    size_t row_bytes = 0;
    int face = cube_face(target);
    if (target != GL_TEXTURE_2D && face < 0) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!valid_texture_dimensions(target, level, w, h) || border != 0 ||
        (w && (size_t)h > SIZE_MAX / (size_t)w / 4)) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (!valid_texture_format((GLenum)internal) || !valid_texture_format(fmt) ||
        !valid_texture_type(type)) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (internal != (GLint)fmt || !valid_texture_pair(fmt, type)) {
        error(GL_INVALID_OPERATION);
        return;
    }
    t = bound_texture_for_target(target);
    if (data && w && h && !texture_upload_layout(w, h, fmt, type, &row_bytes)) {
        error(GL_INVALID_VALUE);
        return;
    }
    rgba = w && h ? (unsigned char *)ntglAlloc((size_t)w * h * 4) : NULL;
    if (w && h && !rgba) {
        error(GL_OUT_OF_MEMORY);
        return;
    }
    {
        Texture view = *t;

        view.rgba = rgba;
        view.width = w;
        view.height = h;
        if (data)
            upload_pixels(view.rgba, view.width, 0, 0, w, h, fmt, type, row_bytes, data);
        else if (rgba)
            memset(rgba, 0, (size_t)w * h * 4);
    }
    if (face >= 0) {
        if (!t->cube) {
            t->cube = (CubeStorage *)ntglAlloc(sizeof(*t->cube));
            if (!t->cube) {
                ntglFree(rgba);
                error(GL_OUT_OF_MEMORY);
                return;
            }
            memset(t->cube, 0, sizeof(*t->cube));
        }
        ntglFree(t->cube->pixels[face][level]);
        t->cube->pixels[face][level] = rgba;
        t->cube->width[face][level] = w;
        t->cube->height[face][level] = h;
        t->cube->format[face][level] = (GLenum)internal;
        t->cube->type[face][level] = type;
    } else if (!level) {
        int mip_level;

        ntglFree(t->rgba);
        t->rgba = rgba;
        t->width = w;
        t->height = h;
        t->format = (GLenum)internal;
        t->type = type;
        t->rgb_white_known = 0;
        for (mip_level = 1; mip_level < GL_MAX_MIP_LEVELS; ++mip_level) {
            ntglFree(t->mipmap[mip_level]);
            t->mipmap[mip_level] = NULL;
            t->mip_width[mip_level] = 0;
            t->mip_height[mip_level] = 0;
            t->mip_format[mip_level] = 0;
            t->mip_type[mip_level] = 0;
        }
        t->mipmap_complete = 0;
    } else {
        ntglFree(t->mipmap[level]);
        t->mipmap[level] = rgba;
        t->mip_width[level] = w;
        t->mip_height[level] = h;
        t->mip_format[level] = (GLenum)internal;
        t->mip_type[level] = type;
    }
    if (!level && face < 0)
        sync_texture();
    if (bound_framebuffer) {
        Framebuffer *f = framebuffer(bound_framebuffer);

        if (f && f->color_texture == t->name && f->color_target == target &&
            f->color_level == level)
            attach_bound_framebuffer();
    }
}

void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt,
                     GLenum type, const GLvoid *data)
{
    Texture *t;
    Texture view;
    unsigned char *storage;
    int width, height;
    GLenum internal_format;
    size_t row_bytes = 0;
    int face = cube_face(target);

    if (target != GL_TEXTURE_2D && face < 0) {
        error(GL_INVALID_ENUM);
        return;
    }
    t = bound_texture_for_target(target);
    if (!t || !valid_texture_level(level) || x < 0 || y < 0 || w < 0 || h < 0) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (!valid_texture_format(fmt) || !valid_texture_type(type)) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!valid_texture_pair(fmt, type)) {
        error(GL_INVALID_OPERATION);
        return;
    }
    if (data && w && h && !texture_upload_layout(w, h, fmt, type, &row_bytes)) {
        error(GL_INVALID_VALUE);
        return;
    }
    storage = face >= 0 && t->cube ? t->cube->pixels[face][level]
                                  : level ? t->mipmap[level] : t->rgba;
    width = face >= 0 && t->cube ? t->cube->width[face][level]
                                : level ? t->mip_width[level] : t->width;
    height = face >= 0 && t->cube ? t->cube->height[face][level]
                                 : level ? t->mip_height[level] : t->height;
    internal_format = face >= 0 && t->cube ? t->cube->format[face][level]
                                          : level ? t->mip_format[level] : t->format;
    if (!internal_format || internal_format != fmt) {
        error(GL_INVALID_OPERATION);
        return;
    }
    if ((!data && w && h) || x > width || y > height ||
        w > width - x || h > height - y) {
        error(GL_INVALID_VALUE);
        return;
    }
    view = *t;
    view.rgba = storage;
    view.width = width;
    view.height = height;
    if (w && h)
        upload_pixels(view.rgba, view.width, x, y, w, h, fmt, type, row_bytes, data);
    if (!level && face < 0) {
        t->rgb_white_known = 0;
        sync_texture();
    }
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
    if (!(size > 0.0f)) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (size > MESAGL_MAX_POINT_SIZE)
        size = MESAGL_MAX_POINT_SIZE;
    state.point_size = size;
    ntglPointSize(size);
}

void glLineWidth(GLfloat width)
{
    GLfloat raster_width;

    if (width <= 0.0f) {
        error(GL_INVALID_VALUE);
        return;
    }
    state.line_width = width;
    raster_width = isnan(width) ? 1.0f
                                : width > MESAGL_MAX_LINE_WIDTH
                                      ? MESAGL_MAX_LINE_WIDTH
                                      : width;
    ntglLineWidth(raster_width);
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
    glBlendFuncSeparate(state.blend_src, state.blend_dst, state.blend_src_alpha,
                        state.blend_dst_alpha);
    glBlendEquationSeparate(state.blend_equation_rgb, state.blend_equation_alpha);
    glBlendColor(state.blend_color[0], state.blend_color[1], state.blend_color[2],
                 state.blend_color[3]);
    glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
    glScissor(state.scissor[0], state.scissor[1], state.scissor[2], state.scissor[3]);
    glPointSize(state.point_size);
    glLineWidth(state.line_width);
    glDepthFunc(state.depth_func);
    glDepthMask(state.depth_mask);
    glClearStencil(state.stencil_clear);
    glStencilFuncSeparate(GL_FRONT, state.stencil_func[0], state.stencil_ref[0],
                          state.stencil_value_mask[0]);
    glStencilFuncSeparate(GL_BACK, state.stencil_func[1], state.stencil_ref[1],
                          state.stencil_value_mask[1]);
    glStencilMaskSeparate(GL_FRONT, state.stencil_write_mask[0]);
    glStencilMaskSeparate(GL_BACK, state.stencil_write_mask[1]);
    glStencilOpSeparate(GL_FRONT, state.stencil_fail[0], state.stencil_depth_fail[0],
                        state.stencil_pass[0]);
    glStencilOpSeparate(GL_BACK, state.stencil_fail[1], state.stencil_depth_fail[1],
                        state.stencil_pass[1]);
    glDepthRangef(state.depth_range[0], state.depth_range[1]);
    glColorMask(state.color_mask[0], state.color_mask[1], state.color_mask[2], state.color_mask[3]);
    glFrontFace(state.front_face);
    glCullFace(state.cull_face);
    glPolygonMode(GL_FRONT, state.polygon[0]);
    glPolygonMode(GL_BACK, state.polygon[1]);
    glPolygonOffset(state.polygon_offset_factor, state.polygon_offset_units);
    glShadeModel(state.shade_model);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, state.tex_env);
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, state.tex_env_color);
    {
        GLenum caps[] = {GL_BLEND,          GL_CULL_FACE,    GL_DEPTH_TEST, GL_SCISSOR_TEST,
                         GL_TEXTURE_2D,     GL_STENCIL_TEST, GL_ALPHA_TEST, GL_LIGHTING,
                         GL_COLOR_MATERIAL, GL_FOG,          GL_NORMALIZE,  GL_LIGHT0,
                         GL_LIGHT1,         GL_LIGHT2,       GL_LIGHT3,     GL_LIGHT4,
                         GL_LIGHT5,         GL_LIGHT6,       GL_LIGHT7,
                         GL_POLYGON_OFFSET_FILL, GL_DITHER};
        int i;
        for (i = 0; i < (int)(sizeof(caps) / sizeof(caps[0])); ++i)
            if (state.enabled & cap_bit(caps[i]))
                glEnable(caps[i]);
            else
                glDisable(caps[i]);
    }
    sync_texture();
}

static int get_integer(GLenum p, GLint *v)
{
    int handled = 0;

#if MESAGL_STRICT_GLES2
    if (p == GL_POLYGON_MODE || p == GL_POINT_SIZE || p == GL_SHADE_MODEL ||
        p == GL_MATRIX_MODE)
        return 0;
#endif
    mesaGLGLES2GetIntegerv(p, v, &handled);
    if (handled)
        return 1;
    if (cap_bit(p))
        *v = (state.enabled & cap_bit(p)) ? GL_TRUE : GL_FALSE;
    else if (p == GL_TEXTURE_BINDING_2D)
        *v = (GLint)state.bound_texture[state.active_texture];
    else if (p == GL_TEXTURE_BINDING_CUBE_MAP)
        *v = (GLint)state.bound_cube_texture[state.active_texture];
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
    else if (p == GL_UNPACK_ROW_LENGTH) {
#if MESAGL_STRICT_GLES2
        error(GL_INVALID_ENUM);
#else
        *v = state.unpack_row_length;
#endif
    }
    else if (p == GL_PACK_ALIGNMENT)
        *v = state.pack_alignment;
    else if (p == GL_FRAMEBUFFER_BINDING)
        *v = (GLint)bound_framebuffer;
    else if (p == GL_RENDERBUFFER_BINDING)
        *v = (GLint)bound_renderbuffer;
    else if (p == GL_DEPTH_FUNC)
        *v = (GLint)state.depth_func;
    else if (p == GL_DEPTH_CLEAR_VALUE)
        *v = (GLint)((double)state.clear_depth * 2147483647.0);
    else if (p == GL_DEPTH_RANGE) {
        v[0] = (GLint)((double)state.depth_range[0] * 2147483647.0);
        v[1] = (GLint)((double)state.depth_range[1] * 2147483647.0);
    }
    else if (p == GL_COLOR_CLEAR_VALUE) {
        v[0] = (GLint)((double)state.clear_color[0] * 2147483647.0);
        v[1] = (GLint)((double)state.clear_color[1] * 2147483647.0);
        v[2] = (GLint)((double)state.clear_color[2] * 2147483647.0);
        v[3] = (GLint)((double)state.clear_color[3] * 2147483647.0);
    } else if (p == GL_STENCIL_CLEAR_VALUE)
        *v = state.stencil_clear;
    else if (p == GL_STENCIL_FUNC || p == GL_STENCIL_BACK_FUNC)
        *v = (GLint)state.stencil_func[p == GL_STENCIL_FUNC ? 0 : 1];
    else if (p == GL_STENCIL_REF || p == GL_STENCIL_BACK_REF)
        *v = state.stencil_ref[p == GL_STENCIL_REF ? 0 : 1];
    else if (p == GL_STENCIL_VALUE_MASK || p == GL_STENCIL_BACK_VALUE_MASK)
        *v = (GLint)state.stencil_value_mask[p == GL_STENCIL_VALUE_MASK ? 0 : 1];
    else if (p == GL_STENCIL_WRITEMASK || p == GL_STENCIL_BACK_WRITEMASK)
        *v = (GLint)state.stencil_write_mask[p == GL_STENCIL_WRITEMASK ? 0 : 1];
    else if (p == GL_STENCIL_FAIL || p == GL_STENCIL_BACK_FAIL)
        *v = (GLint)state.stencil_fail[p == GL_STENCIL_FAIL ? 0 : 1];
    else if (p == GL_STENCIL_PASS_DEPTH_FAIL || p == GL_STENCIL_BACK_PASS_DEPTH_FAIL)
        *v = (GLint)state.stencil_depth_fail[p == GL_STENCIL_PASS_DEPTH_FAIL ? 0 : 1];
    else if (p == GL_STENCIL_PASS_DEPTH_PASS || p == GL_STENCIL_BACK_PASS_DEPTH_PASS)
        *v = (GLint)state.stencil_pass[p == GL_STENCIL_PASS_DEPTH_PASS ? 0 : 1];
    else if (p == GL_DEPTH_WRITEMASK)
        *v = state.depth_mask;
    else if (p == GL_COLOR_WRITEMASK) {
        v[0] = state.color_mask[0];
        v[1] = state.color_mask[1];
        v[2] = state.color_mask[2];
        v[3] = state.color_mask[3];
    } else if (p == GL_BLEND_COLOR) {
        v[0] = (GLint)((double)state.blend_color[0] * 2147483647.0);
        v[1] = (GLint)((double)state.blend_color[1] * 2147483647.0);
        v[2] = (GLint)((double)state.blend_color[2] * 2147483647.0);
        v[3] = (GLint)((double)state.blend_color[3] * 2147483647.0);
    } else if (p == GL_CULL_FACE_MODE)
        *v = (GLint)state.cull_face;
    else if (p == GL_FRONT_FACE)
        *v = (GLint)state.front_face;
    else if (p == GL_POINT_SIZE)
        *v = (GLint)lroundf(state.point_size);
    else if (p == GL_LINE_WIDTH)
        *v = (GLint)lroundf(state.line_width);
    else if (p == GL_ACTIVE_TEXTURE)
        *v = (GLint)(GL_TEXTURE0 + state.active_texture);
    else if (p == GL_GENERATE_MIPMAP_HINT)
        *v = (GLint)state.generate_mipmap_hint;
    else if (p == GL_FRAGMENT_SHADER_DERIVATIVE_HINT_OES)
        *v = (GLint)state.derivative_hint;
    else if (p == GL_BLEND_SRC_RGB)
        *v = (GLint)state.blend_src;
    else if (p == GL_BLEND_DST_RGB)
        *v = (GLint)state.blend_dst;
    else if (p == GL_BLEND_SRC_ALPHA)
        *v = (GLint)state.blend_src_alpha;
    else if (p == GL_BLEND_DST_ALPHA)
        *v = (GLint)state.blend_dst_alpha;
    else if (p == GL_BLEND_EQUATION_RGB)
        *v = (GLint)state.blend_equation_rgb;
    else if (p == GL_BLEND_EQUATION_ALPHA)
        *v = (GLint)state.blend_equation_alpha;
    else if (p == GL_POLYGON_OFFSET_FACTOR)
        *v = (GLint)lroundf(state.polygon_offset_factor);
    else if (p == GL_POLYGON_OFFSET_UNITS)
        *v = (GLint)lroundf(state.polygon_offset_units);
    else if (p == GL_SAMPLE_COVERAGE_VALUE)
        *v = (GLint)lroundf(state.sample_coverage);
    else if (p == GL_MAX_TEXTURE_SIZE || p == GL_MAX_CUBE_MAP_TEXTURE_SIZE)
        *v = MESAGL_MAX_TEXTURE_SIZE;
    else if (p == GL_MAX_RENDERBUFFER_SIZE)
        *v = MESAGL_MAX_RENDERBUFFER_SIZE;
    else if (p == GL_MAX_VIEWPORT_DIMS) {
        v[0] = MESAGL_MAX_VIEWPORT_DIMENSION;
        v[1] = MESAGL_MAX_VIEWPORT_DIMENSION;
    } else if (p == GL_ALIASED_POINT_SIZE_RANGE || p == GL_ALIASED_LINE_WIDTH_RANGE) {
        v[0] = 1;
        v[1] = p == GL_ALIASED_POINT_SIZE_RANGE ? MESAGL_MAX_POINT_SIZE
                                                : MESAGL_MAX_LINE_WIDTH;
    } else if (p == GL_RED_BITS || p == GL_GREEN_BITS || p == GL_BLUE_BITS ||
               p == GL_ALPHA_BITS) {
        const NTGLframebuffer *fb = ntglGetFramebuffer(ntglGetCurrent());
        int red = 8;
        int green = 8;
        int blue = 8;
        int alpha = 8;

        if (bound_framebuffer) {
            Framebuffer *f = framebuffer(bound_framebuffer);
            Renderbuffer *r = f ? renderbuffer(f->color_renderbuffer) : NULL;
            Texture *t = f ? texture(f->color_texture) : NULL;
            GLenum format = 0;
            int face = f ? cube_face(f->color_target) : -1;

            if (r)
                format = r->format;
            else if (t && f && f->color_target == GL_TEXTURE_2D)
                format = f->color_level ? t->mip_format[f->color_level] : t->format;
            else if (t && t->cube && face >= 0)
                format = t->cube->format[face][f->color_level];
            if (format == GL_RGB565) {
                red = blue = 5;
                green = 6;
                alpha = 0;
            } else if (format == GL_RGBA4) {
                red = green = blue = alpha = 4;
            } else if (format == GL_RGB5_A1) {
                red = green = blue = 5;
                alpha = 1;
            } else if (format == GL_RGB || format == GL_LUMINANCE) {
                alpha = 0;
            } else if (format == GL_ALPHA) {
                red = green = blue = 0;
            } else if (format == GL_LUMINANCE_ALPHA) {
                red = green = blue = 8;
            } else if (!format) {
                red = green = blue = alpha = 0;
            }
        } else if (fb && fb->format == NTGL_RGB565) {
            red = blue = 5;
            green = 6;
            alpha = 0;
        } else if (fb && fb->format == NTGL_RGBA4444) {
            red = green = blue = alpha = 4;
        } else if (fb && fb->format == NTGL_RGBA5551) {
            red = green = blue = 5;
            alpha = 1;
        } else if (fb && (fb->format == NTGL_RGB888 || fb->format == NTGL_BGR888 ||
                          fb->format == NTGL_XRGB8888)) {
            alpha = 0;
        }
        *v = p == GL_RED_BITS     ? red
             : p == GL_GREEN_BITS ? green
             : p == GL_BLUE_BITS  ? blue
                                  : alpha;
    } else if (p == GL_DEPTH_BITS) {
        Framebuffer *f = bound_framebuffer ? framebuffer(bound_framebuffer) : NULL;
        Renderbuffer *r = f ? renderbuffer(f->depth_renderbuffer) : NULL;

        *v = bound_framebuffer ? (r && r->format == GL_DEPTH_COMPONENT16 ? 16 : 0) : 16;
    } else if (p == GL_STENCIL_BITS) {
        Framebuffer *f = bound_framebuffer ? framebuffer(bound_framebuffer) : NULL;
        Renderbuffer *r = f ? renderbuffer(f->stencil_renderbuffer) : NULL;

        *v = bound_framebuffer ? (r && r->format == GL_STENCIL_INDEX8 ? 8 : 0) : 8;
    } else if (p == GL_SUBPIXEL_BITS)
        *v = 4;
    else if (p == GL_SAMPLE_BUFFERS || p == GL_SAMPLES)
        *v = 0;
    else if (p == GL_IMPLEMENTATION_COLOR_READ_TYPE)
        *v = GL_UNSIGNED_BYTE;
    else if (p == GL_IMPLEMENTATION_COLOR_READ_FORMAT)
        *v = GL_RGBA;
    else if (p == GL_SHADER_COMPILER)
        *v = GL_TRUE;
    else if (p == GL_NUM_SHADER_BINARY_FORMATS ||
             p == GL_NUM_COMPRESSED_TEXTURE_FORMATS)
        *v = 0;
    else if (p == GL_SHADER_BINARY_FORMATS || p == GL_COMPRESSED_TEXTURE_FORMATS) {
    }
    else if (p == GL_SAMPLE_COVERAGE_INVERT)
        *v = state.sample_coverage_invert;
    else
        return 0;
    return 1;
}

void glGetIntegerv(GLenum pname, GLint *params)
{
    GLint ignored[16];

    if (!params) {
        error(get_integer(pname, ignored) ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }
    if (!get_integer(pname, params))
        error(GL_INVALID_ENUM);
}

static int query_count(GLenum pname)
{
    if (pname == GL_SHADER_BINARY_FORMATS || pname == GL_COMPRESSED_TEXTURE_FORMATS)
        return 0;
    if (pname == GL_VIEWPORT || pname == GL_SCISSOR_BOX || pname == GL_COLOR_WRITEMASK ||
        pname == GL_BLEND_COLOR || pname == GL_COLOR_CLEAR_VALUE)
        return 4;
    if (pname == GL_POLYGON_MODE || pname == GL_DEPTH_RANGE ||
        pname == GL_MAX_VIEWPORT_DIMS ||
        pname == GL_ALIASED_POINT_SIZE_RANGE || pname == GL_ALIASED_LINE_WIDTH_RANGE)
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
#if MESAGL_STRICT_GLES2
    if (pname == GL_POLYGON_MODE || pname == GL_POINT_SIZE ||
        pname == GL_SHADE_MODEL || pname == GL_MATRIX_MODE ||
        pname == GL_MODELVIEW_MATRIX || pname == GL_PROJECTION_MATRIX ||
        pname == GL_TEXTURE_MATRIX) {
        error(GL_INVALID_ENUM);
        return;
    }
#endif
    if (cap_bit(pname)) {
        params[0] = glIsEnabled(pname);
        return;
    }
    if (pname == GL_POINT_SIZE || pname == GL_LINE_WIDTH ||
        pname == GL_POLYGON_OFFSET_FACTOR || pname == GL_POLYGON_OFFSET_UNITS ||
        pname == GL_SAMPLE_COVERAGE_VALUE || pname == GL_DEPTH_CLEAR_VALUE) {
        GLfloat value;

        if (pname == GL_POINT_SIZE)
            value = state.point_size;
        else if (pname == GL_LINE_WIDTH)
            value = state.line_width;
        else if (pname == GL_POLYGON_OFFSET_FACTOR)
            value = state.polygon_offset_factor;
        else if (pname == GL_POLYGON_OFFSET_UNITS)
            value = state.polygon_offset_units;
        else if (pname == GL_SAMPLE_COVERAGE_VALUE)
            value = state.sample_coverage;
        else
            value = state.clear_depth;

        params[0] = value != 0.0f ? GL_TRUE : GL_FALSE;
        return;
    }
    if (pname == GL_DEPTH_RANGE) {
        params[0] = state.depth_range[0] != 0.0f ? GL_TRUE : GL_FALSE;
        params[1] = state.depth_range[1] != 0.0f ? GL_TRUE : GL_FALSE;
        return;
    }
    if (pname == GL_BLEND_COLOR || pname == GL_COLOR_CLEAR_VALUE) {
        const GLfloat *values = pname == GL_BLEND_COLOR ? state.blend_color
                                                        : state.clear_color;

        for (i = 0; i < 4; ++i)
            params[i] = values[i] != 0.0f ? GL_TRUE : GL_FALSE;
        return;
    }
    if (!get_integer(pname, values)) {
        error(GL_INVALID_ENUM);
        return;
    }
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
#if MESAGL_STRICT_GLES2
    if (pname == GL_POLYGON_MODE || pname == GL_POINT_SIZE ||
        pname == GL_SHADE_MODEL || pname == GL_MATRIX_MODE ||
        pname == GL_MODELVIEW_MATRIX || pname == GL_PROJECTION_MATRIX ||
        pname == GL_TEXTURE_MATRIX) {
        error(GL_INVALID_ENUM);
        return;
    }
#endif
    if (pname == GL_DEPTH_RANGE) {
        params[0] = state.depth_range[0];
        params[1] = state.depth_range[1];
        return;
    }
    if (pname == GL_BLEND_COLOR) {
        memcpy(params, state.blend_color, sizeof(state.blend_color));
        return;
    }
    if (pname == GL_COLOR_CLEAR_VALUE) {
        memcpy(params, state.clear_color, sizeof(state.clear_color));
        return;
    }
    if (pname == GL_DEPTH_CLEAR_VALUE) {
        params[0] = state.clear_depth;
        return;
    }
    if (pname == GL_ALIASED_POINT_SIZE_RANGE || pname == GL_ALIASED_LINE_WIDTH_RANGE) {
        params[0] = 1.0f;
        params[1] = pname == GL_ALIASED_POINT_SIZE_RANGE ? MESAGL_MAX_POINT_SIZE
                                                         : MESAGL_MAX_LINE_WIDTH;
        return;
    }
    if (pname == GL_POINT_SIZE || pname == GL_LINE_WIDTH || pname == 0x8038 ||
        pname == 0x2A00 || pname == 0x80AA) {
        params[0] = pname == GL_POINT_SIZE   ? state.point_size
                    : pname == GL_LINE_WIDTH ? state.line_width
                    : pname == 0x8038        ? state.polygon_offset_factor
                    : pname == 0x2A00        ? state.polygon_offset_units
                                             : state.sample_coverage;
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
    if (!get_integer(pname, values)) {
        error(GL_INVALID_ENUM);
        return;
    }
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
    size_t destination_stride;
    size_t destination_row_bytes;
    size_t last_destination_column;
    size_t last_destination_row;
    int64_t requested_last_x, requested_last_y;
    int first_x, first_y, last_x, last_y;
    int row, column, source_bpp;

    if (width < 0 || height < 0) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (!framebuffer_ready())
        return;
    if (!framebuffer_has_color()) {
        error(GL_INVALID_OPERATION);
        return;
    }
    if (format != GL_RGBA || type != GL_UNSIGNED_BYTE) {
        error(GL_INVALID_OPERATION);
        return;
    }
    if (!width || !height)
        return;
    if (!fb || !pixels) {
        error(GL_INVALID_OPERATION);
        return;
    }
    source_bpp = fb->format == NTGL_RGB565 || fb->format == NTGL_RGBA4444 ||
                         fb->format == NTGL_RGBA5551
                     ? 2
                 : fb->format == NTGL_RGB888 || fb->format == NTGL_BGR888 ? 3
                                                                          : 4;
    if ((size_t)width > SIZE_MAX / 4) {
        error(GL_INVALID_VALUE);
        return;
    }
    destination_row_bytes = (size_t)width * 4;
    if (destination_row_bytes > SIZE_MAX - (size_t)(state.pack_alignment - 1)) {
        error(GL_INVALID_VALUE);
        return;
    }
    destination_stride = (destination_row_bytes + (size_t)state.pack_alignment - 1) &
                         ~(size_t)(state.pack_alignment - 1);
    first_x = x < 0 ? 0 : x;
    first_y = y < 0 ? 0 : y;
    requested_last_x = (int64_t)x + width;
    requested_last_y = (int64_t)y + height;
    last_x = requested_last_x > fb->width ? fb->width : (int)requested_last_x;
    last_y = requested_last_y > fb->height ? fb->height : (int)requested_last_y;
    if (first_x >= last_x || first_y >= last_y)
        return;
    if ((uint64_t)((int64_t)last_y - 1 - y) > SIZE_MAX ||
        (uint64_t)((int64_t)last_x - 1 - x) > SIZE_MAX) {
        error(GL_INVALID_VALUE);
        return;
    }
    last_destination_row = (size_t)((int64_t)last_y - 1 - y);
    last_destination_column = (size_t)((int64_t)last_x - 1 - x);
    if (last_destination_column > SIZE_MAX / 4 ||
        last_destination_row > SIZE_MAX / destination_stride ||
        last_destination_row * destination_stride > SIZE_MAX - last_destination_column * 4) {
        error(GL_INVALID_VALUE);
        return;
    }
    for (row = first_y; row < last_y; ++row)
        for (column = first_x; column < last_x; ++column) {
            int source_y = row;
            int stride = fb->stride ? fb->stride : fb->width * source_bpp;
            const GLubyte *source;
            GLubyte rgba[4];
            GLubyte *destination =
                (GLubyte *)pixels + (size_t)((int64_t)row - y) * destination_stride +
                (size_t)((int64_t)column - x) * 4;

            if (fb->origin == NTGL_ORIGIN_TOP_LEFT)
                source_y = fb->height - 1 - source_y;
            source = (const GLubyte *)fb->pixels + (ptrdiff_t)source_y * stride +
                     (size_t)column * source_bpp;
            if (fb->format == NTGL_RGB565) {
                uint16_t packed;

                memcpy(&packed, source, sizeof(packed));
                rgba[0] = (GLubyte)(((packed >> 11) & 31) * 255 / 31);
                rgba[1] = (GLubyte)(((packed >> 5) & 63) * 255 / 63);
                rgba[2] = (GLubyte)((packed & 31) * 255 / 31);
                rgba[3] = 255;
            } else if (fb->format == NTGL_RGBA4444) {
                uint16_t packed;

                memcpy(&packed, source, sizeof(packed));
                rgba[0] = (GLubyte)(((packed >> 12) & 15) * 17);
                rgba[1] = (GLubyte)(((packed >> 8) & 15) * 17);
                rgba[2] = (GLubyte)(((packed >> 4) & 15) * 17);
                rgba[3] = (GLubyte)((packed & 15) * 17);
            } else if (fb->format == NTGL_RGBA5551) {
                uint16_t packed;

                memcpy(&packed, source, sizeof(packed));
                rgba[0] = (GLubyte)(((packed >> 11) & 31) * 255 / 31);
                rgba[1] = (GLubyte)(((packed >> 6) & 31) * 255 / 31);
                rgba[2] = (GLubyte)(((packed >> 1) & 31) * 255 / 31);
                rgba[3] = (packed & 1) ? 255 : 0;
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
            memcpy(destination, rgba, sizeof(rgba));
        }
}

const GLubyte *glGetString(GLenum n)
{
    if (n == GL_VERSION)
        return (const GLubyte *)"OpenGL ES 2.0 mesaGL";
    if (n == GL_VENDOR)
        return (const GLubyte *)"mesaGL";
    if (n == GL_RENDERER)
        return (const GLubyte *)"mesaGL software framebuffer";
    if (n == GL_EXTENSIONS)
        return (const GLubyte *)
            "GL_EXT_texture_format_BGRA8888 "
            "GL_OES_standard_derivatives";
    if (n == GL_SHADING_LANGUAGE_VERSION)
        return (const GLubyte *)"OpenGL ES GLSL ES 1.00 mesaGL";
    error(GL_INVALID_ENUM);
    return NULL;
}

void glFlush(void)
{
}

void glFinish(void)
{
}

void glHint(GLenum target, GLenum mode)
{
    if ((target != GL_GENERATE_MIPMAP_HINT &&
         target != GL_FRAGMENT_SHADER_DERIVATIVE_HINT_OES) ||
        (mode != GL_DONT_CARE && mode != GL_FASTEST && mode != GL_NICEST)) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (target == GL_GENERATE_MIPMAP_HINT)
        state.generate_mipmap_hint = mode;
    else
        state.derivative_hint = mode;
}

void glPolygonOffset(GLfloat factor, GLfloat units)
{
    state.polygon_offset_factor = factor;
    state.polygon_offset_units = units;
    ntglPolygonOffset(factor, units);
}

void glSampleCoverage(GLclampf value, GLboolean invert)
{
    state.sample_coverage = value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
    state.sample_coverage_invert = !!invert;
}

void glStencilFuncSeparate(GLenum face, GLenum function, GLint reference, GLuint mask)
{
    unsigned clamped_reference;

    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) ||
        !valid_depth_func(function)) {
        error(GL_INVALID_ENUM);
        return;
    }
    clamped_reference = (unsigned)(reference < 0 ? 0 : reference > 255 ? 255 : reference);
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        state.stencil_func[0] = function;
        state.stencil_ref[0] = (GLint)clamped_reference;
        state.stencil_value_mask[0] = mask & 0xffu;
        ntglStencilFuncSeparate(1, depth_func(function), clamped_reference, mask);
    }
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        state.stencil_func[1] = function;
        state.stencil_ref[1] = (GLint)clamped_reference;
        state.stencil_value_mask[1] = mask & 0xffu;
        ntglStencilFuncSeparate(0, depth_func(function), clamped_reference, mask);
    }
}

void glStencilMaskSeparate(GLenum face, GLuint mask)
{
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        state.stencil_write_mask[0] = mask & 0xffu;
        ntglStencilMaskSeparate(1, mask);
    }
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        state.stencil_write_mask[1] = mask & 0xffu;
        ntglStencilMaskSeparate(0, mask);
    }
}

void glStencilOpSeparate(GLenum face, GLenum fail, GLenum depth_fail, GLenum depth_pass)
{
    GLenum operations[] = {fail, depth_fail, depth_pass};
    int i;

    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) {
        error(GL_INVALID_ENUM);
        return;
    }
    for (i = 0; i < 3; ++i)
        if (operations[i] != GL_KEEP && operations[i] != GL_ZERO &&
            operations[i] != GL_REPLACE && operations[i] != GL_INCR &&
            operations[i] != GL_DECR && operations[i] != GL_INCR_WRAP &&
            operations[i] != GL_DECR_WRAP && operations[i] != GL_INVERT) {
            error(GL_INVALID_ENUM);
            return;
        }
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        state.stencil_fail[0] = fail;
        state.stencil_depth_fail[0] = depth_fail;
        state.stencil_pass[0] = depth_pass;
        ntglStencilOpSeparate(1, stencil_op(fail), stencil_op(depth_fail), stencil_op(depth_pass));
    }
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        state.stencil_fail[1] = fail;
        state.stencil_depth_fail[1] = depth_fail;
        state.stencil_pass[1] = depth_pass;
        ntglStencilOpSeparate(0, stencil_op(fail), stencil_op(depth_fail), stencil_op(depth_pass));
    }
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
        while (framebuffer(next_framebuffer))
            ++next_framebuffer;
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
            release_texture_attachment(f->color_texture);
            release_texture_attachment(f->depth_texture);
            release_texture_attachment(f->stencil_texture);
            release_renderbuffer_attachment(f->color_renderbuffer);
            release_renderbuffer_attachment(f->depth_renderbuffer);
            release_renderbuffer_attachment(f->stencil_renderbuffer);
            memset(f, 0, sizeof(*f));
        }
    }
}

GLboolean glIsFramebuffer(GLuint name)
{
    Framebuffer *f = framebuffer(name);

    return f && f->created ? GL_TRUE : GL_FALSE;
}

void glBindFramebuffer(GLenum target, GLuint name)
{
    const NTGLframebuffer *current;
    Framebuffer *object;
    if (target != GL_FRAMEBUFFER) {
        error(GL_INVALID_ENUM);
        return;
    }
    object = get_or_create_framebuffer(name);
    if (name && !object)
        return;
    if (name)
        object->created = 1;
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
    Texture *t = name ? texture(name) : NULL;

    if (target != GL_FRAMEBUFFER ||
        (attachment != GL_COLOR_ATTACHMENT0 && attachment != GL_DEPTH_ATTACHMENT &&
         attachment != GL_STENCIL_ATTACHMENT) ||
        (name && textarget != GL_TEXTURE_2D && cube_face(textarget) < 0)) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (name && level != 0) {
        error(GL_INVALID_VALUE);
        return;
    }
    if (!bound_framebuffer || (name && (!t || t->delete_pending)) ||
        (t && ((textarget == GL_TEXTURE_2D && t->target != GL_TEXTURE_2D) ||
               (cube_face(textarget) >= 0 && t->target != GL_TEXTURE_CUBE_MAP)))) {
        error(GL_INVALID_OPERATION);
        return;
    }
    f = framebuffer(bound_framebuffer);
    if (attachment == GL_COLOR_ATTACHMENT0) {
        if (f->color_texture != name) {
            retain_texture_attachment(name);
            release_texture_attachment(f->color_texture);
        }
        f->color_texture = name;
        f->color_target = name ? textarget : 0;
        f->color_level = name ? level : 0;
        release_renderbuffer_attachment(f->color_renderbuffer);
        f->color_renderbuffer = 0;
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        if (f->depth_texture != name) {
            retain_texture_attachment(name);
            release_texture_attachment(f->depth_texture);
        }
        f->depth_texture = name;
        f->depth_target = name ? textarget : 0;
        f->depth_level = name ? level : 0;
        release_renderbuffer_attachment(f->depth_renderbuffer);
        f->depth_renderbuffer = 0;
    } else {
        if (f->stencil_texture != name) {
            retain_texture_attachment(name);
            release_texture_attachment(f->stencil_texture);
        }
        f->stencil_texture = name;
        f->stencil_target = name ? textarget : 0;
        f->stencil_level = name ? level : 0;
        release_renderbuffer_attachment(f->stencil_renderbuffer);
        f->stencil_renderbuffer = 0;
    }
    attach_bound_framebuffer();
}

GLenum glCheckFramebufferStatus(GLenum target)
{
    Framebuffer *f;
    Texture *t;
    int width = 0;
    int height = 0;
    int face;
    int attachment_count;
    GLenum color_format = 0;
    if (target != GL_FRAMEBUFFER) {
        error(GL_INVALID_ENUM);
        return 0;
    }
    if (!bound_framebuffer)
        return GL_FRAMEBUFFER_COMPLETE;
    f = framebuffer(bound_framebuffer);
    attachment_count = !!f->color_texture + !!f->color_renderbuffer +
                       !!f->depth_texture + !!f->depth_renderbuffer +
                       !!f->stencil_texture + !!f->stencil_renderbuffer;
    if (!attachment_count)
        return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    t = f ? texture(f->color_texture) : NULL;
    face = f ? cube_face(f->color_target) : -1;
    if (t && f && f->color_target == GL_TEXTURE_2D) {
        width = f->color_level ? t->mip_width[f->color_level] : t->width;
        height = f->color_level ? t->mip_height[f->color_level] : t->height;
        color_format = f->color_level ? t->mip_format[f->color_level] : t->format;
    } else if (t && t->cube && face >= 0) {
        width = t->cube->width[face][f->color_level];
        height = t->cube->height[face][f->color_level];
        color_format = t->cube->format[face][f->color_level];
    } else if (f && f->color_renderbuffer) {
        Renderbuffer *r = renderbuffer(f->color_renderbuffer);

        if (r && (r->format == GL_RGBA4 || r->format == GL_RGB5_A1 ||
                  r->format == GL_RGB565)) {
            width = r->width;
            height = r->height;
            color_format = r->format;
        }
    }
    if ((f->color_texture || f->color_renderbuffer) &&
        (width <= 0 || height <= 0 ||
         (color_format != GL_RGB && color_format != GL_RGBA && color_format != GL_RGBA4 &&
          color_format != GL_RGB5_A1 && color_format != GL_RGB565)))
        return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    if (f->depth_texture || f->stencil_texture)
        return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    if (f->depth_renderbuffer) {
        Renderbuffer *r = renderbuffer(f->depth_renderbuffer);
        if (!r || r->format != GL_DEPTH_COMPONENT16 || r->width <= 0 || r->height <= 0)
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        if (!width && !height) {
            width = r->width;
            height = r->height;
        } else if (r->width != width || r->height != height) {
            return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
        }
    }
    if (f->stencil_renderbuffer) {
        Renderbuffer *r = renderbuffer(f->stencil_renderbuffer);
        if (!r || r->format != GL_STENCIL_INDEX8 || r->width <= 0 || r->height <= 0)
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        if (!width && !height) {
            width = r->width;
            height = r->height;
        } else if (r->width != width || r->height != height) {
            return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
        }
    }
    return GL_FRAMEBUFFER_COMPLETE;
}

void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname,
                                           GLint *params)
{
    Framebuffer *f;
    GLuint object = 0;
    GLenum object_type = GL_NONE;
    GLenum texture_target = 0;
    GLint texture_level = 0;

    if (!params)
        return;
    if (target != GL_FRAMEBUFFER) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!bound_framebuffer || !(f = framebuffer(bound_framebuffer))) {
        error(GL_INVALID_OPERATION);
        return;
    }
    if (attachment == GL_COLOR_ATTACHMENT0) {
        if (f->color_texture) {
            object = f->color_texture;
            object_type = GL_TEXTURE;
            texture_target = f->color_target;
            texture_level = f->color_level;
        } else if (f->color_renderbuffer) {
            object = f->color_renderbuffer;
            object_type = GL_RENDERBUFFER;
        }
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        if (f->depth_texture) {
            object = f->depth_texture;
            object_type = GL_TEXTURE;
            texture_target = f->depth_target;
            texture_level = f->depth_level;
        } else {
            object = f->depth_renderbuffer;
            object_type = object ? GL_RENDERBUFFER : GL_NONE;
        }
    } else if (attachment == GL_STENCIL_ATTACHMENT) {
        if (f->stencil_texture) {
            object = f->stencil_texture;
            object_type = GL_TEXTURE;
            texture_target = f->stencil_target;
            texture_level = f->stencil_level;
        } else {
            object = f->stencil_renderbuffer;
            object_type = object ? GL_RENDERBUFFER : GL_NONE;
        }
    } else {
        error(GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE) {
        *params = (GLint)object_type;
        return;
    }
    if (object_type == GL_NONE) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME) {
        *params = (GLint)object;
        return;
    }
    if (object_type != GL_TEXTURE &&
        (pname == GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL ||
         pname == GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE)) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL &&
             object_type == GL_TEXTURE)
        *params = texture_level;
    else if (pname == GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE &&
             object_type == GL_TEXTURE)
        *params = cube_face(texture_target) >= 0 ? (GLint)texture_target : 0;
    else
        error(GL_INVALID_ENUM);
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
        while (renderbuffer(next_renderbuffer))
            ++next_renderbuffer;
        renderbuffers[slot].name = next_renderbuffer++;
        names[i] = renderbuffers[slot].name;
    }
}

void glDeleteRenderbuffers(GLsizei n, const GLuint *names)
{
    int i;

    if (n < 0 || (n && !names)) {
        error(GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < n; ++i) {
        Renderbuffer *r = renderbuffer(names[i]);
        Framebuffer *f = framebuffer(bound_framebuffer);

        if (bound_renderbuffer == names[i])
            bound_renderbuffer = 0;
        if (r)
            r->delete_pending = 1;
        if (f && f->color_renderbuffer == names[i]) {
            f->color_renderbuffer = 0;
            release_renderbuffer_attachment(names[i]);
        }
        if (f && f->depth_renderbuffer == names[i]) {
            f->depth_renderbuffer = 0;
            release_renderbuffer_attachment(names[i]);
        }
        if (f && f->stencil_renderbuffer == names[i]) {
            f->stencil_renderbuffer = 0;
            release_renderbuffer_attachment(names[i]);
        }
        r = renderbuffer(names[i]);
        if (r && !r->attachment_refs)
            destroy_renderbuffer(r);
        if (f)
            attach_bound_or_default();
    }
}

GLboolean glIsRenderbuffer(GLuint name)
{
    Renderbuffer *r = renderbuffer(name);

    return r && r->created && !r->delete_pending ? GL_TRUE : GL_FALSE;
}

void glBindRenderbuffer(GLenum target, GLuint name)
{
    Renderbuffer *object;

    if (target != GL_RENDERBUFFER) {
        error(GL_INVALID_ENUM);
        return;
    }
    object = get_or_create_renderbuffer(name);
    if (name && !object)
        return;
    if (name)
        object->created = 1;
    bound_renderbuffer = name;
}

void glRenderbufferStorage(GLenum target, GLenum format, GLsizei width, GLsizei height)
{
    Renderbuffer *r = renderbuffer(bound_renderbuffer);

    if (target != GL_RENDERBUFFER) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!r) {
        error(GL_INVALID_OPERATION);
        return;
    }
    if (format != GL_DEPTH_COMPONENT16 && format != GL_STENCIL_INDEX8 && format != GL_RGBA4 &&
        format != GL_RGB5_A1 && format != GL_RGB565) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (width < 0 || height < 0 || width > MESAGL_MAX_RENDERBUFFER_SIZE ||
        height > MESAGL_MAX_RENDERBUFFER_SIZE ||
        (width && (size_t)height > SIZE_MAX / (size_t)width / 4)) {
        error(GL_INVALID_VALUE);
        return;
    }
    {
        size_t bytes_per_pixel = format == GL_STENCIL_INDEX8 ? 1
                                 : format == GL_DEPTH_COMPONENT16 ? sizeof(float)
                                                                  : 2;
        unsigned char *pixels = width && height
                                    ? (unsigned char *)ntglAlloc((size_t)width * height *
                                                                 bytes_per_pixel)
                                    : NULL;

        if (width && height && !pixels) {
            error(GL_OUT_OF_MEMORY);
            return;
        }
        if (pixels)
            memset(pixels, 0, (size_t)width * height * bytes_per_pixel);
        ntglFree(r->pixels);
        r->pixels = pixels;
    }
    r->format = format;
    r->width = width;
    r->height = height;
    if (bound_framebuffer) {
        Framebuffer *f = framebuffer(bound_framebuffer);

        if (f && (f->color_renderbuffer == r->name || f->depth_renderbuffer == r->name ||
                  f->stencil_renderbuffer == r->name))
            attach_bound_framebuffer();
    }
}

void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
    Renderbuffer *r = renderbuffer(bound_renderbuffer);

    if (!params)
        return;
    if (target != GL_RENDERBUFFER) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!r) {
        error(GL_INVALID_OPERATION);
        return;
    }
    if (pname == GL_RENDERBUFFER_WIDTH)
        *params = r->width;
    else if (pname == GL_RENDERBUFFER_HEIGHT)
        *params = r->height;
    else if (pname == GL_RENDERBUFFER_INTERNAL_FORMAT)
        *params = (GLint)r->format;
    else if (pname == GL_RENDERBUFFER_RED_SIZE)
        *params = r->format == GL_RGBA4 ? 4
                  : r->format == GL_RGB5_A1 || r->format == GL_RGB565 ? 5
                                                                      : 0;
    else if (pname == GL_RENDERBUFFER_GREEN_SIZE)
        *params = r->format == GL_RGBA4 ? 4
                  : r->format == GL_RGB5_A1 ? 5
                  : r->format == GL_RGB565  ? 6
                                            : 0;
    else if (pname == GL_RENDERBUFFER_BLUE_SIZE)
        *params = r->format == GL_RGBA4 ? 4
                  : r->format == GL_RGB5_A1 || r->format == GL_RGB565 ? 5
                                                                      : 0;
    else if (pname == GL_RENDERBUFFER_ALPHA_SIZE)
        *params = r->format == GL_RGBA4 ? 4 : r->format == GL_RGB5_A1 ? 1 : 0;
    else if (pname == GL_RENDERBUFFER_DEPTH_SIZE)
        *params = r->format == GL_DEPTH_COMPONENT16 ? 16 : 0;
    else if (pname == GL_RENDERBUFFER_STENCIL_SIZE)
        *params = r->format == GL_STENCIL_INDEX8 ? 8 : 0;
    else
        error(GL_INVALID_ENUM);
}

void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum rbtarget, GLuint name)
{
    Framebuffer *f = framebuffer(bound_framebuffer);

    if (target != GL_FRAMEBUFFER || (name && rbtarget != GL_RENDERBUFFER)) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (attachment != GL_COLOR_ATTACHMENT0 && attachment != GL_DEPTH_ATTACHMENT &&
        attachment != GL_STENCIL_ATTACHMENT) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (!f || (name && (!renderbuffer(name) || !renderbuffer(name)->created ||
                        renderbuffer(name)->delete_pending))) {
        error(GL_INVALID_OPERATION);
        return;
    }
    if (attachment == GL_COLOR_ATTACHMENT0) {
        if (f->color_renderbuffer != name) {
            retain_renderbuffer_attachment(name);
            release_renderbuffer_attachment(f->color_renderbuffer);
        }
        release_texture_attachment(f->color_texture);
        f->color_renderbuffer = name;
        f->color_texture = 0;
        f->color_target = 0;
        f->color_level = 0;
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        if (f->depth_renderbuffer != name) {
            retain_renderbuffer_attachment(name);
            release_renderbuffer_attachment(f->depth_renderbuffer);
        }
        release_texture_attachment(f->depth_texture);
        f->depth_renderbuffer = name;
        f->depth_texture = 0;
        f->depth_target = 0;
        f->depth_level = 0;
    } else {
        if (f->stencil_renderbuffer != name) {
            retain_renderbuffer_attachment(name);
            release_renderbuffer_attachment(f->stencil_renderbuffer);
        }
        release_texture_attachment(f->stencil_texture);
        f->stencil_renderbuffer = name;
        f->stencil_texture = 0;
        f->stencil_target = 0;
        f->stencil_level = 0;
    }
    attach_bound_framebuffer();
}
