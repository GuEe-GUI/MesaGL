#include "mesaGL/ntgl.h"
#include "mesaGL/config.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NTGL_MATRIX_STACK 16
#define NTGL_EPSILON 1.0e-7f
#define NTGL_DEPTH_MAX 65535.0f

void mesaGLReleaseCurrentContext(void);

typedef struct Vertex {
    float p[4];
    float c[4];
    float uv[2];
    float fog;
    float point_size;
    float varying[MESAGL_MAX_VARYING_INTERPOLATORS][4];
} Vertex;

typedef struct ScreenVertex {
    float x, y, z, iw;
    float c[4];
    float uv[2];
    float fog;
    float point_size;
    float varying[MESAGL_MAX_VARYING_INTERPOLATORS][4];
} ScreenVertex;

struct NTGLcontext {
    NTGLallocator allocator;
    NTGLpixelOps pixel_ops;
    NTGLframebuffer framebuffer;
    int owns_color;
    float *depth;
    unsigned char *stencil;
    int owns_depth;
    int owns_stencil;
    int external_aux;
    float clear_color[4];
    float clear_depth;
    float depth_near, depth_far;
    int viewport[4];
    int scissor[4];
    unsigned enabled;
    NTGLdepthFunc depth_func;
    int depth_mask;
    float polygon_offset_factor, polygon_offset_units;
    unsigned color_mask;
    unsigned char clear_stencil, stencil_ref[2], stencil_value_mask[2], stencil_write_mask[2];
    NTGLdepthFunc stencil_func[2];
    NTGLstencilOp stencil_fail[2], stencil_depth_fail[2], stencil_pass[2];
    int stencil_face;
    NTGLdepthFunc alpha_func;
    float alpha_ref;
    int smooth_shading;
    NTGLblendFactor blend_src, blend_dst, blend_src_alpha, blend_dst_alpha;
    NTGLblendEquation blend_equation_rgb, blend_equation_alpha;
    float blend_color[4];
    int front_ccw, cull_front;
    NTGLpolygonMode polygon_mode[2];
    float point_size, line_width;
    NTGLfragmentFn fragment_function;
    void *fragment_user;
    NTGLprogramFragmentFn program_fragment;
    void *program_fragment_user;
    int varying_count;
    int program_derivatives_required;
    int program_fast_xrgb_blend;
    int program_output_clamped;
    unsigned char *program_fast_row;
    float varying_dfdx[MESAGL_MAX_VARYING_INTERPOLATORS][4];
    float varying_dfdy[MESAGL_MAX_VARYING_INTERPOLATORS][4];
    int program_front_facing;
    float program_point_coord[2];
    float modelview[NTGL_MATRIX_STACK][16];
    float projection[NTGL_MATRIX_STACK][16];
    float texture_matrix[NTGL_MATRIX_STACK][16];
    int modelview_top, projection_top, texture_top;
    NTGLmatrixMode matrix_mode;
    float color[4], uv[2], normal[3];
    float material_ambient[4], material_diffuse[4], material_specular[4], material_shininess;
    float light_ambient[8][4], light_diffuse[8][4], light_specular[8][4];
    float light_position[8][4];
    float light_model_ambient[4];
    NTGLfogMode fog_mode;
    float fog_color[4], fog_density, fog_start, fog_end;
    NTGLtexture texture;
    int has_texture;
    int in_begin;
    NTGLprimitive primitive;
    Vertex vertices[MESAGL_MAX_VERTICES];
    int vertex_count;
    NTGLresult error;
};

static NTGLcontext *current_context;

enum {
    CAP_DEPTH = 1u << 0,
    CAP_BLEND = 1u << 1,
    CAP_CULL = 1u << 2,
    CAP_TEXTURE = 1u << 3,
    CAP_SCISSOR = 1u << 4
};

static void *default_alloc(void *user, size_t size)
{
    (void)user;
    return malloc(size);
}

static void default_free(void *user, void *pointer)
{
    (void)user;
    free(pointer);
}

static int bytes_per_pixel(NTGLformat format)
{
    switch (format) {
    case NTGL_RGB565:
    case NTGL_RGBA4444:
    case NTGL_RGBA5551:
        return 2;
    case NTGL_RGB888:
    case NTGL_BGR888:
        return 3;
    case NTGL_XRGB8888:
    case NTGL_ARGB8888:
    case NTGL_RGBA8888:
    case NTGL_BGRA8888:
        return 4;
    default:
        return 0;
    }
}

static float clamp01(float value)
{
    if (!(value >= 0.0f))
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

static float quantize_depth(float depth)
{
    return floorf(clamp01(depth) * NTGL_DEPTH_MAX + 0.5f) / NTGL_DEPTH_MAX;
}

static void set_error(NTGLcontext *ctx, NTGLresult error)
{
    if (ctx && ctx->error == NTGL_OK)
        ctx->error = error;
}

static void identity(float *m)
{
    int i;
    memset(m, 0, 16 * sizeof(float));
    for (i = 0; i < 4; ++i)
        m[i * 5] = 1.0f;
}

static void multiply(float *out, const float *a, const float *b)
{
    float r[16];
    int row, col, k;
    for (col = 0; col < 4; ++col)
        for (row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (k = 0; k < 4; ++k)
                value += a[k * 4 + row] * b[col * 4 + k];
            r[col * 4 + row] = value;
        }
    memcpy(out, r, sizeof(r));
}

static void transform(float *out, const float *m, const float *v)
{
    int row;
    for (row = 0; row < 4; ++row)
        out[row] = m[row] * v[0] + m[4 + row] * v[1] + m[8 + row] * v[2] + m[12 + row] * v[3];
}

static float *active_matrix(NTGLcontext *ctx)
{
    if (ctx->matrix_mode == NTGL_MODELVIEW)
        return ctx->modelview[ctx->modelview_top];
    if (ctx->matrix_mode == NTGL_PROJECTION)
        return ctx->projection[ctx->projection_top];
    return ctx->texture_matrix[ctx->texture_top];
}

static int *active_matrix_top(NTGLcontext *ctx)
{
    if (ctx->matrix_mode == NTGL_MODELVIEW)
        return &ctx->modelview_top;
    if (ctx->matrix_mode == NTGL_PROJECTION)
        return &ctx->projection_top;
    return &ctx->texture_top;
}

static float (*active_matrix_stack(NTGLcontext *ctx))[16]
{
    if (ctx->matrix_mode == NTGL_MODELVIEW)
        return ctx->modelview;
    if (ctx->matrix_mode == NTGL_PROJECTION)
        return ctx->projection;
    return ctx->texture_matrix;
}

static unsigned char to_u8(float value)
{
    return (unsigned char)(clamp01(value) * 255.0f + 0.5f);
}

static unsigned char *pixel_address(const NTGLframebuffer *fb, int x, int y)
{
    int stride = fb->stride ? fb->stride : fb->width * bytes_per_pixel(fb->format);
    if (fb->origin == NTGL_ORIGIN_TOP_LEFT)
        y = fb->height - 1 - y;
    return (unsigned char *)fb->pixels + (ptrdiff_t)y * stride + x * bytes_per_pixel(fb->format);
}

static void unpack_pixel(const unsigned char *p, NTGLformat format, float *c)
{
    switch (format) {
    case NTGL_RGB565: {
        uint16_t v;
        memcpy(&v, p, sizeof(v));
        c[0] = ((v >> 11) & 31) / 31.0f;
        c[1] = ((v >> 5) & 63) / 63.0f;
        c[2] = (v & 31) / 31.0f;
        c[3] = 1.0f;
        break;
    }
    case NTGL_RGBA4444: {
        uint16_t v;

        memcpy(&v, p, sizeof(v));
        c[0] = ((v >> 12) & 15) / 15.0f;
        c[1] = ((v >> 8) & 15) / 15.0f;
        c[2] = ((v >> 4) & 15) / 15.0f;
        c[3] = (v & 15) / 15.0f;
        break;
    }
    case NTGL_RGBA5551: {
        uint16_t v;

        memcpy(&v, p, sizeof(v));
        c[0] = ((v >> 11) & 31) / 31.0f;
        c[1] = ((v >> 6) & 31) / 31.0f;
        c[2] = ((v >> 1) & 31) / 31.0f;
        c[3] = (v & 1) ? 1.0f : 0.0f;
        break;
    }
    case NTGL_RGB888:
        c[0] = p[0] / 255.0f;
        c[1] = p[1] / 255.0f;
        c[2] = p[2] / 255.0f;
        c[3] = 1;
        break;
    case NTGL_BGR888:
        c[2] = p[0] / 255.0f;
        c[1] = p[1] / 255.0f;
        c[0] = p[2] / 255.0f;
        c[3] = 1;
        break;
    case NTGL_XRGB8888:
    case NTGL_ARGB8888:
        c[2] = p[0] / 255.0f;
        c[1] = p[1] / 255.0f;
        c[0] = p[2] / 255.0f;
        c[3] = format == NTGL_ARGB8888 ? p[3] / 255.0f : 1;
        break;
    case NTGL_RGBA8888:
        c[0] = p[0] / 255.0f;
        c[1] = p[1] / 255.0f;
        c[2] = p[2] / 255.0f;
        c[3] = p[3] / 255.0f;
        break;
    case NTGL_BGRA8888:
        c[2] = p[0] / 255.0f;
        c[1] = p[1] / 255.0f;
        c[0] = p[2] / 255.0f;
        c[3] = p[3] / 255.0f;
        break;
    }
}

static void pack_pixel(unsigned char *p, NTGLformat format, const float *c)
{
    unsigned char r = to_u8(c[0]), g = to_u8(c[1]), b = to_u8(c[2]), a = to_u8(c[3]);
    switch (format) {
    case NTGL_RGB565: {
        uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        memcpy(p, &v, sizeof(v));
        break;
    }
    case NTGL_RGBA4444: {
        uint16_t v = (uint16_t)(((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) |
                                (a >> 4));

        memcpy(p, &v, sizeof(v));
        break;
    }
    case NTGL_RGBA5551: {
        uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) |
                                (a >= 128));

        memcpy(p, &v, sizeof(v));
        break;
    }
    case NTGL_RGB888:
        p[0] = r;
        p[1] = g;
        p[2] = b;
        break;
    case NTGL_BGR888:
        p[0] = b;
        p[1] = g;
        p[2] = r;
        break;
    case NTGL_XRGB8888:
        p[0] = b;
        p[1] = g;
        p[2] = r;
        p[3] = 255;
        break;
    case NTGL_ARGB8888:
        p[0] = b;
        p[1] = g;
        p[2] = r;
        p[3] = a;
        break;
    case NTGL_RGBA8888:
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = a;
        break;
    case NTGL_BGRA8888:
        p[0] = b;
        p[1] = g;
        p[2] = r;
        p[3] = a;
        break;
    }
}

static unsigned quantize_dithered(float value, unsigned levels, int x, int y)
{
    static const unsigned char bayer[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5},
    };
    float scaled = clamp01(value) * levels;
    float threshold = (bayer[y & 3][x & 3] + 0.5f) / 16.0f;
    unsigned result = (unsigned)floorf(scaled + threshold);

    return result > levels ? levels : result;
}

static void pack_fragment_pixel(unsigned char *p, NTGLformat format, const float *color,
                                int x, int y, int dither)
{
    uint16_t value;

    if (!dither || (format != NTGL_RGB565 && format != NTGL_RGBA4444 &&
                    format != NTGL_RGBA5551)) {
        pack_pixel(p, format, color);
        return;
    }
    if (format == NTGL_RGB565) {
        value = (uint16_t)((quantize_dithered(color[0], 31, x, y) << 11) |
                           (quantize_dithered(color[1], 63, x, y) << 5) |
                           quantize_dithered(color[2], 31, x, y));
    } else if (format == NTGL_RGBA4444) {
        value = (uint16_t)((quantize_dithered(color[0], 15, x, y) << 12) |
                           (quantize_dithered(color[1], 15, x, y) << 8) |
                           (quantize_dithered(color[2], 15, x, y) << 4) |
                           quantize_dithered(color[3], 15, x, y));
    } else {
        value = (uint16_t)((quantize_dithered(color[0], 31, x, y) << 11) |
                           (quantize_dithered(color[1], 31, x, y) << 6) |
                           (quantize_dithered(color[2], 31, x, y) << 1) |
                           quantize_dithered(color[3], 1, x, y));
    }
    memcpy(p, &value, sizeof(value));
}

static int inside_scissor(const NTGLcontext *ctx, int x, int y)
{
    int64_t right;
    int64_t top;

    if (!(ctx->enabled & CAP_SCISSOR))
        return 1;
    right = (int64_t)ctx->scissor[0] + ctx->scissor[2];
    top = (int64_t)ctx->scissor[1] + ctx->scissor[3];
    return x >= ctx->scissor[0] && y >= ctx->scissor[1] &&
           (int64_t)x < right && (int64_t)y < top;
}

static int depth_pass(NTGLdepthFunc function, float incoming, float stored)
{
    switch (function) {
    case NTGL_NEVER:
        return 0;
    case NTGL_LESS:
        return incoming < stored;
    case NTGL_LEQUAL:
        return incoming <= stored;
    case NTGL_EQUAL:
        return incoming == stored;
    case NTGL_NOTEQUAL:
        return incoming != stored;
    case NTGL_GEQUAL:
        return incoming >= stored;
    case NTGL_GREATER:
        return incoming > stored;
    case NTGL_ALWAYS:
        return 1;
    }
    return 0;
}

static unsigned char stencil_op(NTGLstencilOp operation, unsigned char stored, unsigned char ref)
{
    switch (operation) {
    case NTGL_STENCIL_ZERO:
        return 0;
    case NTGL_REPLACE:
        return ref;
    case NTGL_INCR:
        return stored == 255 ? 255 : (unsigned char)(stored + 1);
    case NTGL_DECR:
        return stored == 0 ? 0 : (unsigned char)(stored - 1);
    case NTGL_INCR_WRAP:
        return (unsigned char)(stored + 1);
    case NTGL_DECR_WRAP:
        return (unsigned char)(stored - 1);
    case NTGL_INVERT:
        return (unsigned char)~stored;
    default:
        return stored;
    }
}

static void update_stencil(NTGLcontext *ctx, int index, NTGLstencilOp operation)
{
    unsigned char old = ctx->stencil[index];
    int face = ctx->stencil_face;
    unsigned char value = stencil_op(operation, old, ctx->stencil_ref[face]);
    ctx->stencil[index] =
        (old & ~ctx->stencil_write_mask[face]) | (value & ctx->stencil_write_mask[face]);
}

static float blend_factor(const NTGLcontext *ctx, NTGLblendFactor factor, const float *source,
                          const float *destination, int channel)
{
    switch (factor) {
    case NTGL_ZERO:
        return 0;
    case NTGL_ONE:
        return 1;
    case NTGL_SRC_COLOR:
        return source[channel];
    case NTGL_ONE_MINUS_SRC_COLOR:
        return 1 - source[channel];
    case NTGL_DST_COLOR:
        return destination[channel];
    case NTGL_ONE_MINUS_DST_COLOR:
        return 1 - destination[channel];
    case NTGL_SRC_ALPHA:
        return source[3];
    case NTGL_ONE_MINUS_SRC_ALPHA:
        return 1 - source[3];
    case NTGL_DST_ALPHA:
        return destination[3];
    case NTGL_ONE_MINUS_DST_ALPHA:
        return 1 - destination[3];
    case NTGL_CONSTANT_COLOR:
        return ctx->blend_color[channel];
    case NTGL_ONE_MINUS_CONSTANT_COLOR:
        return 1 - ctx->blend_color[channel];
    case NTGL_CONSTANT_ALPHA:
        return ctx->blend_color[3];
    case NTGL_ONE_MINUS_CONSTANT_ALPHA:
        return 1 - ctx->blend_color[3];
    case NTGL_SRC_ALPHA_SATURATE:
        return channel == 3 ? 1.0f : fminf(source[3], 1.0f - destination[3]);
    }
    return 1;
}

static float blend(NTGLblendEquation equation, float source, float destination)
{
    if (equation == NTGL_FUNC_SUBTRACT)
        return source - destination;
    if (equation == NTGL_FUNC_REVERSE_SUBTRACT)
        return destination - source;
    if (equation == NTGL_MIN)
        return fminf(source, destination);
    if (equation == NTGL_MAX)
        return fmaxf(source, destination);
    return source + destination;
}

static void write_fragment(NTGLcontext *ctx, int x, int y, float z, float reciprocal_w,
                           const float *source, const float *varyings)
{
    float output[4], destination[4], shaded[4];
    float frag_coord[4];
    float stored_depth;
    unsigned char *address;
    int i, index;
    memcpy(shaded, source, sizeof(shaded));
    if (MESAGL_UNLIKELY(ctx->fragment_function != NULL))
        ctx->fragment_function(ctx->fragment_user, shaded);
    frag_coord[0] = x + 0.5f;
    frag_coord[1] = y + 0.5f;
    frag_coord[2] = z;
    frag_coord[3] = reciprocal_w;
    if (ctx->program_fragment) {
        if (MESAGL_UNLIKELY(!ctx->program_fragment(
                ctx->program_fragment_user, varyings, ctx->varying_count,
                ctx->varying_dfdx[0], ctx->varying_dfdy[0], frag_coord,
                ctx->program_front_facing, ctx->program_point_coord, shaded)))
            return;
    }
    if (!ctx->program_output_clamped)
        for (i = 0; i < 4; ++i)
            shaded[i] = clamp01(shaded[i]);
    source = shaded;
    if (MESAGL_LIKELY(ctx->program_fast_xrgb_blend)) {
        float alpha = source[3];

        address = ctx->program_fast_row + (size_t)x * 4;
        if (MESAGL_UNLIKELY(alpha <= 0.0f))
            return;
        if (MESAGL_UNLIKELY(alpha >= 1.0f)) {
            address[0] = to_u8(source[2]);
            address[1] = to_u8(source[1]);
            address[2] = to_u8(source[0]);
            address[3] = 255;
            return;
        }
        {
            float inverse_alpha = 1.0f - alpha;

            address[0] = (unsigned char)(source[2] * alpha * 255.0f +
                                         address[0] * inverse_alpha + 0.5f);
            address[1] = (unsigned char)(source[1] * alpha * 255.0f +
                                         address[1] * inverse_alpha + 0.5f);
            address[2] = (unsigned char)(source[0] * alpha * 255.0f +
                                         address[2] * inverse_alpha + 0.5f);
        }
        address[3] = 255;
        return;
    }
    if (MESAGL_UNLIKELY(x < 0 || y < 0 || x >= ctx->framebuffer.width ||
                        y >= ctx->framebuffer.height ||
                        !inside_scissor(ctx, x, y)))
        return;
    index = y * ctx->framebuffer.width + x;
    stored_depth = quantize_depth(z);
    if ((ctx->enabled & (1u << NTGL_ALPHA_TEST)) &&
        !depth_pass(ctx->alpha_func, source[3], ctx->alpha_ref))
        return;
    if ((ctx->enabled & (1u << NTGL_STENCIL_TEST)) && ctx->stencil) {
        int face = ctx->stencil_face;
        unsigned ref = ctx->stencil_ref[face] & ctx->stencil_value_mask[face];
        unsigned value = ctx->stencil[index] & ctx->stencil_value_mask[face];
        if (!depth_pass(ctx->stencil_func[face], (float)ref, (float)value)) {
            update_stencil(ctx, index, ctx->stencil_fail[face]);
            return;
        }
    }
    if ((ctx->enabled & CAP_DEPTH) && ctx->depth &&
        !depth_pass(ctx->depth_func, stored_depth, ctx->depth[index])) {
        if ((ctx->enabled & (1u << NTGL_STENCIL_TEST)) && ctx->stencil)
            update_stencil(ctx, index, ctx->stencil_depth_fail[ctx->stencil_face]);
        return;
    }
    if ((ctx->enabled & (1u << NTGL_STENCIL_TEST)) && ctx->stencil)
        update_stencil(ctx, index, ctx->stencil_pass[ctx->stencil_face]);
    if ((ctx->enabled & CAP_DEPTH) && ctx->depth && ctx->depth_mask)
        ctx->depth[index] = stored_depth;
    address = pixel_address(&ctx->framebuffer, x, y);
    if (ctx->enabled & CAP_BLEND) {
        if (MESAGL_LIKELY(ctx->framebuffer.format == NTGL_XRGB8888 &&
                          ctx->color_mask == 0xfu &&
                          ctx->blend_equation_rgb == NTGL_FUNC_ADD &&
                          ctx->blend_equation_alpha == NTGL_FUNC_ADD &&
                          ctx->blend_src == NTGL_SRC_ALPHA &&
                          ctx->blend_dst == NTGL_ONE_MINUS_SRC_ALPHA &&
                          ctx->blend_src_alpha == NTGL_ONE &&
                          ctx->blend_dst_alpha == NTGL_ONE_MINUS_SRC_ALPHA)) {
            float alpha = source[3];
            float inverse_alpha = 1.0f - alpha;

            address[0] = (unsigned char)(source[2] * alpha * 255.0f +
                                         address[0] * inverse_alpha + 0.5f);
            address[1] = (unsigned char)(source[1] * alpha * 255.0f +
                                         address[1] * inverse_alpha + 0.5f);
            address[2] = (unsigned char)(source[0] * alpha * 255.0f +
                                         address[2] * inverse_alpha + 0.5f);
            address[3] = 255;
            return;
        }
        unpack_pixel(address, ctx->framebuffer.format, destination);
        if (ctx->blend_equation_rgb == NTGL_FUNC_ADD &&
            ctx->blend_equation_alpha == NTGL_FUNC_ADD &&
            ctx->blend_src == NTGL_SRC_ALPHA &&
            ctx->blend_dst == NTGL_ONE_MINUS_SRC_ALPHA &&
            ctx->blend_src_alpha == NTGL_ONE &&
            ctx->blend_dst_alpha == NTGL_ONE_MINUS_SRC_ALPHA) {
            float inverse_alpha = 1.0f - source[3];

            for (i = 0; i < 3; ++i)
                output[i] = source[i] * source[3] +
                            destination[i] * inverse_alpha;
            output[3] = source[3] + destination[3] * inverse_alpha;
        } else {
            for (i = 0; i < 3; ++i) {
                float s = source[i] *
                          blend_factor(ctx, ctx->blend_src, source,
                                       destination, i);
                float d = destination[i] *
                          blend_factor(ctx, ctx->blend_dst, source,
                                       destination, i);
                output[i] = blend(ctx->blend_equation_rgb, s, d);
            }
            output[3] = blend(
                ctx->blend_equation_alpha,
                source[3] * blend_factor(ctx, ctx->blend_src_alpha, source,
                                         destination, 3),
                destination[3] *
                    blend_factor(ctx, ctx->blend_dst_alpha, source,
                                 destination, 3));
        }
    } else
        memcpy(output, source, sizeof(output));
    if (ctx->color_mask != 0xfu) {
        unpack_pixel(address, ctx->framebuffer.format, destination);
        for (i = 0; i < 4; ++i)
            if (!(ctx->color_mask & (1u << i)))
                output[i] = destination[i];
    }
    pack_fragment_pixel(address, ctx->framebuffer.format, output, x, y,
                        ctx->enabled & (1u << NTGL_DITHER));
}

static int texture_sample_index(int index, int size, NTGLwrap wrap)
{
    if (wrap == NTGL_REPEAT) {
        index %= size;
        return index < 0 ? index + size : index;
    }
    if (wrap == NTGL_MIRRORED_REPEAT) {
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

static float normalized_ntgl_texture_coordinate(float coordinate, NTGLwrap wrap)
{
    if (!isfinite(coordinate))
        return 0.0f;
    if (wrap == NTGL_REPEAT)
        return coordinate - floorf(coordinate);
    if (wrap == NTGL_MIRRORED_REPEAT) {
        coordinate = fmodf(coordinate, 2.0f);
        if (coordinate < 0.0f)
            coordinate += 2.0f;
        return coordinate <= 1.0f ? coordinate : 2.0f - coordinate;
    }
    if (coordinate < 0.0f)
        return 0.0f;
    return coordinate > 1.0f ? 1.0f : coordinate;
}

static void sample_texture(const NTGLtexture *texture, float s, float t, float *color)
{
    float fx, fy;
    int x0, y0, x1, y1, i, stride;
    float c00[4], c10[4], c01[4], c11[4], ax, ay;
    fx = normalized_ntgl_texture_coordinate(s, texture->wrap_s) * texture->width;
    fy = normalized_ntgl_texture_coordinate(t, texture->wrap_t) * texture->height;
    if (texture->filter == NTGL_NEAREST) {
        x0 = texture_sample_index((int)floorf(fx), texture->width,
                                  texture->wrap_s);
        y0 = texture_sample_index((int)floorf(fy), texture->height,
                                  texture->wrap_t);
        if (texture->origin == NTGL_ORIGIN_TOP_LEFT)
            y0 = texture->height - y0 - 1;
        stride =
            texture->stride ? texture->stride : texture->width * bytes_per_pixel(texture->format);
        unpack_pixel((const unsigned char *)texture->pixels + y0 * stride +
                         x0 * bytes_per_pixel(texture->format),
                     texture->format, color);
        return;
    }
    x0 = (int)floorf(fx - 0.5f);
    y0 = (int)floorf(fy - 0.5f);
    ax = fx - 0.5f - x0;
    ay = fy - 0.5f - y0;
    x1 = texture_sample_index(x0 + 1, texture->width, texture->wrap_s);
    y1 = texture_sample_index(y0 + 1, texture->height, texture->wrap_t);
    x0 = texture_sample_index(x0, texture->width, texture->wrap_s);
    y0 = texture_sample_index(y0, texture->height, texture->wrap_t);
    if (texture->origin == NTGL_ORIGIN_TOP_LEFT) {
        y0 = texture->height - y0 - 1;
        y1 = texture->height - y1 - 1;
    }
    stride = texture->stride ? texture->stride : texture->width * bytes_per_pixel(texture->format);
    if (texture->format == NTGL_RGBA8888) {
        const unsigned char *base = (const unsigned char *)texture->pixels;
        const unsigned char *p00 = base + y0 * stride + x0 * 4;
        const unsigned char *p10 = base + y0 * stride + x1 * 4;
        const unsigned char *p01 = base + y1 * stride + x0 * 4;
        const unsigned char *p11 = base + y1 * stride + x1 * 4;
        float one_minus_x = 1.0f - ax;
        float one_minus_y = 1.0f - ay;

        for (i = 0; i < 4; ++i)
            color[i] = ((p00[i] * one_minus_x + p10[i] * ax) *
                            one_minus_y +
                        (p01[i] * one_minus_x + p11[i] * ax) * ay) /
                       255.0f;
        return;
    }
    unpack_pixel((const unsigned char *)texture->pixels + y0 * stride +
                     x0 * bytes_per_pixel(texture->format),
                 texture->format, c00);
    unpack_pixel((const unsigned char *)texture->pixels + y0 * stride +
                     x1 * bytes_per_pixel(texture->format),
                 texture->format, c10);
    unpack_pixel((const unsigned char *)texture->pixels + y1 * stride +
                     x0 * bytes_per_pixel(texture->format),
                 texture->format, c01);
    unpack_pixel((const unsigned char *)texture->pixels + y1 * stride +
                     x1 * bytes_per_pixel(texture->format),
                 texture->format, c11);
    for (i = 0; i < 4; ++i)
        color[i] =
            (c00[i] * (1 - ax) + c10[i] * ax) * (1 - ay) + (c01[i] * (1 - ax) + c11[i] * ax) * ay;
}

static float edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static int top_left_edge(float ax, float ay, float bx, float by)
{
    float dx = bx - ax;
    float dy = by - ay;

    return dy > 0.0f || (dy == 0.0f && dx < 0.0f);
}

static int triangle_edges_inside(const ScreenVertex *a, const ScreenVertex *b,
                                 const ScreenVertex *c, float area,
                                 float e0, float e1, float e2)
{
    float sign = area < 0.0f ? -1.0f : 1.0f;
    int edge0 = sign > 0.0f ? top_left_edge(b->x, b->y, c->x, c->y)
                            : top_left_edge(c->x, c->y, b->x, b->y);
    int edge1 = sign > 0.0f ? top_left_edge(c->x, c->y, a->x, a->y)
                            : top_left_edge(a->x, a->y, c->x, c->y);
    int edge2 = sign > 0.0f ? top_left_edge(a->x, a->y, b->x, b->y)
                            : top_left_edge(b->x, b->y, a->x, a->y);

    e0 *= sign;
    e1 *= sign;
    e2 *= sign;
    if (e0 < -NTGL_EPSILON || e1 < -NTGL_EPSILON || e2 < -NTGL_EPSILON)
        return 0;
    if ((fabsf(e0) <= NTGL_EPSILON && !edge0) ||
        (fabsf(e1) <= NTGL_EPSILON && !edge1) ||
        (fabsf(e2) <= NTGL_EPSILON && !edge2))
        return 0;
    return 1;
}

static int interpolate_program_varyings(const NTGLcontext *ctx, const ScreenVertex *a,
                                        const ScreenVertex *b, const ScreenVertex *c, float area,
                                        float x, float y,
                                        float output[MESAGL_MAX_VARYING_INTERPOLATORS][4])
{
    float w0 = edge(b->x, b->y, c->x, c->y, x, y) / area;
    float w1 = edge(c->x, c->y, a->x, a->y, x, y) / area;
    float w2 = 1.0f - w0 - w1;
    float denominator = w0 * a->iw + w1 * b->iw + w2 * c->iw;
    int varying;

    if (fabsf(denominator) < NTGL_EPSILON)
        return 0;
    for (varying = 0; varying < ctx->varying_count; ++varying) {
        int component;

        for (component = 0; component < 4; ++component)
            output[varying][component] =
                (w0 * a->varying[varying][component] * a->iw +
                 w1 * b->varying[varying][component] * b->iw +
                 w2 * c->varying[varying][component] * c->iw) /
                denominator;
    }
    return 1;
}

static int to_screen(const NTGLcontext *ctx, const Vertex *v, ScreenVertex *s)
{
    int i;

    for (i = 0; i < 4; ++i)
        if (!isfinite(v->p[i]))
            return 0;
    if (fabsf(v->p[3]) < NTGL_EPSILON)
        return 0;
    s->iw = 1.0f / v->p[3];
    s->x = ctx->viewport[0] + (v->p[0] * s->iw * 0.5f + 0.5f) * ctx->viewport[2];
    s->y = ctx->viewport[1] + (v->p[1] * s->iw * 0.5f + 0.5f) * ctx->viewport[3];
    s->z = ctx->depth_near + (v->p[2] * s->iw * 0.5f + 0.5f) *
                                     (ctx->depth_far - ctx->depth_near);
    for (i = 0; i < 4; ++i)
        s->c[i] = v->c[i];
    s->uv[0] = v->uv[0];
    s->uv[1] = v->uv[1];
    s->fog = v->fog;
    s->point_size = v->point_size;
    memcpy(s->varying, v->varying, sizeof(s->varying));
    return 1;
}

static float clip_distance(const Vertex *v, int plane)
{
    switch (plane) {
    case 0:
        return v->p[3] + v->p[0];
    case 1:
        return v->p[3] - v->p[0];
    case 2:
        return v->p[3] + v->p[1];
    case 3:
        return v->p[3] - v->p[1];
    case 4:
        return v->p[3] + v->p[2];
    default:
        return v->p[3] - v->p[2];
    }
}

static Vertex interpolate_vertex(const Vertex *a, const Vertex *b, float t)
{
    Vertex r;
    int i;
    for (i = 0; i < 4; ++i) {
        r.p[i] = a->p[i] + (b->p[i] - a->p[i]) * t;
        r.c[i] = a->c[i] + (b->c[i] - a->c[i]) * t;
    }
    for (i = 0; i < 2; ++i)
        r.uv[i] = a->uv[i] + (b->uv[i] - a->uv[i]) * t;
    r.fog = a->fog + (b->fog - a->fog) * t;
    r.point_size = a->point_size + (b->point_size - a->point_size) * t;
    for (i = 0; i < MESAGL_MAX_VARYING_INTERPOLATORS; ++i) {
        int component;

        for (component = 0; component < 4; ++component)
            r.varying[i][component] =
                a->varying[i][component] + (b->varying[i][component] - a->varying[i][component]) * t;
    }
    return r;
}

static int clip_triangle(const Vertex *a, const Vertex *b, const Vertex *c, Vertex *output)
{
    Vertex buffers[2][12];
    int count = 3, plane, i, out_count, src = 0;
    buffers[0][0] = *a;
    buffers[0][1] = *b;
    buffers[0][2] = *c;
    for (plane = 0; plane < 6; ++plane) {
        out_count = 0;
        for (i = 0; i < count; ++i) {
            const Vertex *p = &buffers[src][i], *q = &buffers[src][(i + 1) % count];
            float dp = clip_distance(p, plane), dq = clip_distance(q, plane);
            int pin = dp >= 0, qin = dq >= 0;
            if (pin)
                buffers[1 - src][out_count++] = *p;
            if (pin != qin)
                buffers[1 - src][out_count++] = interpolate_vertex(p, q, dp / (dp - dq));
        }
        count = out_count;
        src = 1 - src;
        if (!count)
            break;
    }
    memcpy(output, buffers[src], (size_t)count * sizeof(Vertex));
    return count;
}

static int clip_line(Vertex *a, Vertex *b)
{
    Vertex original_a = *a, original_b = *b;
    float enter = 0.0f, leave = 1.0f;
    int plane;
    for (plane = 0; plane < 6; ++plane) {
        float da = clip_distance(&original_a, plane), db = clip_distance(&original_b, plane), t;
        if (da < 0 && db < 0)
            return 0;
        if (da >= 0 && db >= 0)
            continue;
        t = da / (da - db);
        if (da < 0) {
            if (t > enter)
                enter = t;
        } else if (t < leave)
            leave = t;
        if (enter > leave)
            return 0;
    }
    *a = interpolate_vertex(&original_a, &original_b, enter);
    *b = interpolate_vertex(&original_a, &original_b, leave);
    return 1;
}

static int vertex_inside(const Vertex *v)
{
    int plane;
    for (plane = 0; plane < 6; ++plane)
        if (clip_distance(v, plane) < 0)
            return 0;
    return 1;
}

static void apply_fog(NTGLcontext *ctx, float factor, float *color)
{
    int i;

    if (!(ctx->enabled & (1u << NTGL_FOG)))
        return;
    factor = clamp01(factor);
    for (i = 0; i < 3; ++i)
        color[i] = factor * color[i] + (1.0f - factor) * ctx->fog_color[i];
}

static void write_line_fragment(NTGLcontext *ctx, const ScreenVertex *a,
                                const ScreenVertex *b, float dx, float dy,
                                float length_squared, int x, int y)
{
    float sample_x = x + 0.5f;
    float sample_y = y + 0.5f;
    float t = ((sample_x - a->x) * dx + (sample_y - a->y) * dy) /
              length_squared;
    float denominator = (1.0f - t) * a->iw + t * b->iw;
    float color[4];
    float varying[MESAGL_MAX_VARYING_INTERPOLATORS][4];
    int channel;
    int slot;

    if (fabsf(denominator) < NTGL_EPSILON)
        return;
    for (channel = 0; channel < 4; ++channel)
        color[channel] =
            ctx->smooth_shading
                ? ((1.0f - t) * a->c[channel] * a->iw +
                   t * b->c[channel] * b->iw) /
                      denominator
                : b->c[channel];
    for (slot = 0; slot < ctx->varying_count; ++slot)
        for (channel = 0; channel < 4; ++channel)
            varying[slot][channel] =
                ((1.0f - t) * a->varying[slot][channel] * a->iw +
                 t * b->varying[slot][channel] * b->iw) /
                denominator;
    memset(ctx->varying_dfdx, 0, sizeof(ctx->varying_dfdx));
    memset(ctx->varying_dfdy, 0, sizeof(ctx->varying_dfdy));
    apply_fog(ctx, ((1.0f - t) * a->fog * a->iw + t * b->fog * b->iw) /
                       denominator,
              color);
    write_fragment(ctx, x, y, a->z + (b->z - a->z) * t, denominator, color,
                   varying[0]);
}

static void raster_screen_line(NTGLcontext *ctx, const ScreenVertex *a, const ScreenVertex *b)
{
    float dx = b->x - a->x;
    float dy = b->y - a->y;
    float length_squared = dx * dx + dy * dy;
    float width_value = ctx->line_width;
    int first;
    int last;
    int major;
    int width;

    ctx->program_point_coord[0] = 0.0f;
    ctx->program_point_coord[1] = 0.0f;
    if (length_squared <= NTGL_EPSILON || !isfinite(width_value))
        return;
    if (width_value > MESAGL_MAX_LINE_WIDTH)
        width_value = MESAGL_MAX_LINE_WIDTH;
    width = (int)floorf(width_value + 0.5f);
    if (width < 1)
        width = 1;

    if (fabsf(dx) >= fabsf(dy)) {
        float first_value = floorf(fminf(a->x, b->x));
        float last_value = ceilf(fmaxf(a->x, b->x));

        if (last_value < 0.0f || first_value >= ctx->framebuffer.width)
            return;
        first = first_value < 0.0f ? 0 : (int)first_value;
        last = last_value >= ctx->framebuffer.width
                   ? ctx->framebuffer.width - 1
                   : (int)last_value;
        for (major = first; major <= last; ++major) {
            float t = (major + 0.5f - a->x) / dx;
            float minor_position;
            int offset;

            if (t < 0.0f || t >= 1.0f)
                continue;
            minor_position = a->y + dy * t - (width - 1) * 0.5f;
            minor_position = floorf(minor_position);
            for (offset = 0; offset < width; ++offset) {
                float y = minor_position + offset;

                if (y >= 0.0f && y < ctx->framebuffer.height)
                    write_line_fragment(ctx, a, b, dx, dy, length_squared,
                                        major, (int)y);
            }
        }
    } else {
        float first_value = floorf(fminf(a->y, b->y));
        float last_value = ceilf(fmaxf(a->y, b->y));

        if (last_value < 0.0f || first_value >= ctx->framebuffer.height)
            return;
        first = first_value < 0.0f ? 0 : (int)first_value;
        last = last_value >= ctx->framebuffer.height
                   ? ctx->framebuffer.height - 1
                   : (int)last_value;
        for (major = first; major <= last; ++major) {
            float t = (major + 0.5f - a->y) / dy;
            float minor_position;
            int offset;

            if (t < 0.0f || t >= 1.0f)
                continue;
            minor_position = a->x + dx * t - (width - 1) * 0.5f;
            minor_position = floorf(minor_position);
            for (offset = 0; offset < width; ++offset) {
                float x = minor_position + offset;

                if (x >= 0.0f && x < ctx->framebuffer.width)
                    write_line_fragment(ctx, a, b, dx, dy, length_squared,
                                        (int)x, major);
            }
        }
    }
}

static void raster_screen_point(NTGLcontext *ctx, const ScreenVertex *vertex)
{
    float color[4];
    float point_size = ctx->program_fragment ? vertex->point_size : ctx->point_size;
    float half_size;
    float first_x;
    float first_y;
    float last_x;
    float last_y;
    int max_x;
    int max_y;
    int min_x;
    int min_y;
    int x;
    int y;

    if (!(point_size > 0.0f))
        return;
    if (point_size < 1.0f)
        point_size = 1.0f;
    if (point_size > MESAGL_MAX_POINT_SIZE)
        point_size = MESAGL_MAX_POINT_SIZE;
    half_size = point_size * 0.5f;
    first_x = ceilf(vertex->x - half_size - 0.5f);
    last_x = ceilf(vertex->x + half_size - 0.5f) - 1.0f;
    first_y = ceilf(vertex->y - half_size - 0.5f);
    last_y = ceilf(vertex->y + half_size - 0.5f) - 1.0f;
    if (last_x < 0.0f || last_y < 0.0f ||
        first_x >= ctx->framebuffer.width || first_y >= ctx->framebuffer.height)
        return;
    min_x = first_x < 0.0f ? 0 : (int)first_x;
    max_x = last_x >= ctx->framebuffer.width ? ctx->framebuffer.width - 1
                                             : (int)last_x;
    min_y = first_y < 0.0f ? 0 : (int)first_y;
    max_y = last_y >= ctx->framebuffer.height ? ctx->framebuffer.height - 1
                                              : (int)last_y;

    memcpy(color, vertex->c, sizeof(color));
    apply_fog(ctx, vertex->fog, color);
    memset(ctx->varying_dfdx, 0, sizeof(ctx->varying_dfdx));
    memset(ctx->varying_dfdy, 0, sizeof(ctx->varying_dfdy));
    for (y = min_y; y <= max_y; ++y)
        for (x = min_x; x <= max_x; ++x) {
            ctx->program_point_coord[0] =
                0.5f + (x + 0.5f - vertex->x) / point_size;
            ctx->program_point_coord[1] =
                0.5f - (y + 0.5f - vertex->y) / point_size;
            write_fragment(ctx, x, y, vertex->z, vertex->iw, color,
                           vertex->varying[0]);
        }
}

static void raster_triangle(NTGLcontext *ctx, const Vertex *va, const Vertex *vb, const Vertex *vc)
{
    Vertex polygon[12];
    ScreenVertex screen[12], a, b, c;
    NTGLpolygonMode mode;
    float polygon_area;
    int count = clip_triangle(va, vb, vc, polygon), front, i, tri;

    if (count < 3)
        return;
    for (i = 0; i < count; ++i)
        if (!to_screen(ctx, &polygon[i], &screen[i]))
            return;
    polygon_area =
        edge(screen[0].x, screen[0].y, screen[1].x, screen[1].y, screen[2].x, screen[2].y);
    if (fabsf(polygon_area) < NTGL_EPSILON)
        return;
    front = ctx->front_ccw ? (polygon_area < 0) : (polygon_area > 0);
    ctx->program_front_facing = front;
    ctx->program_point_coord[0] = 0.0f;
    ctx->program_point_coord[1] = 0.0f;
    ctx->stencil_face = front ? 0 : 1;
    if ((ctx->enabled & CAP_CULL) &&
        (ctx->cull_front == 2 || front == ctx->cull_front))
        return;
    mode = ctx->polygon_mode[front ? 0 : 1];
    if (mode == NTGL_POLYGON_POINT) {
        for (i = 0; i < count; ++i)
            raster_screen_point(ctx, &screen[i]);
        return;
    }
    if (mode == NTGL_POLYGON_LINE) {
        for (i = 0; i < count; ++i)
            raster_screen_line(ctx, &screen[i], &screen[(i + 1) % count]);
        return;
    }
    for (tri = 1; tri + 1 < count; ++tri) {
        float area, depth_offset, minxf, maxxf, minyf, maxyf;
        int minx, maxx, miny, maxy, x, y;
        int constant_program_varyings;
        a = screen[0];
        b = screen[tri];
        c = screen[tri + 1];
        area = edge(a.x, a.y, b.x, b.y, c.x, c.y);
        if (fabsf(area) < NTGL_EPSILON)
            continue;
        constant_program_varyings =
            ctx->program_fragment && !ctx->program_derivatives_required &&
            memcmp(a.varying, b.varying,
                   (size_t)ctx->varying_count * sizeof(a.varying[0])) == 0 &&
            memcmp(a.varying, c.varying,
                   (size_t)ctx->varying_count * sizeof(a.varying[0])) == 0;
        depth_offset = 0.0f;
        if (ctx->enabled & (1u << NTGL_POLYGON_OFFSET_FILL)) {
            float dzdx = ((b.z - a.z) * (c.y - a.y) -
                          (c.z - a.z) * (b.y - a.y)) /
                         area;
            float dzdy = ((b.x - a.x) * (c.z - a.z) -
                          (c.x - a.x) * (b.z - a.z)) /
                         area;
            float slope = fmaxf(fabsf(dzdx), fabsf(dzdy));

            depth_offset = slope * ctx->polygon_offset_factor +
                           ctx->polygon_offset_units / NTGL_DEPTH_MAX;
        }
        minxf = fminf(a.x, fminf(b.x, c.x));
        maxxf = fmaxf(a.x, fmaxf(b.x, c.x));
        minyf = fminf(a.y, fminf(b.y, c.y));
        maxyf = fmaxf(a.y, fmaxf(b.y, c.y));
        if (maxxf < 0.0f || maxyf < 0.0f ||
            minxf >= ctx->framebuffer.width || minyf >= ctx->framebuffer.height)
            continue;
        if (minxf < 0.0f)
            minxf = 0.0f;
        if (minyf < 0.0f)
            minyf = 0.0f;
        if (maxxf >= ctx->framebuffer.width)
            maxxf = ctx->framebuffer.width - 1.0f;
        if (maxyf >= ctx->framebuffer.height)
            maxyf = ctx->framebuffer.height - 1.0f;
        minx = (int)floorf(minxf);
        maxx = (int)ceilf(maxxf);
        miny = (int)floorf(minyf);
        maxy = (int)ceilf(maxyf);
        if (ctx->enabled & CAP_SCISSOR) {
            int64_t scissor_max_x = (int64_t)ctx->scissor[0] +
                                    ctx->scissor[2] - 1;
            int64_t scissor_max_y = (int64_t)ctx->scissor[1] +
                                    ctx->scissor[3] - 1;

            if ((int64_t)maxx < ctx->scissor[0] ||
                (int64_t)maxy < ctx->scissor[1] ||
                (int64_t)minx > scissor_max_x ||
                (int64_t)miny > scissor_max_y)
                continue;
            if (minx < ctx->scissor[0])
                minx = ctx->scissor[0];
            if (miny < ctx->scissor[1])
                miny = ctx->scissor[1];
            if (maxx > scissor_max_x)
                maxx = (int)scissor_max_x;
            if (maxy > scissor_max_y)
                maxy = (int)scissor_max_y;
            if (minx > maxx || miny > maxy)
                continue;
        }
        for (y = miny; y <= maxy; ++y) {
            if (ctx->program_fast_xrgb_blend)
                ctx->program_fast_row = pixel_address(&ctx->framebuffer, 0, y);
            for (x = minx; x <= maxx; ++x) {
                float e0 = edge(b.x, b.y, c.x, c.y,
                                x + 0.5f, y + 0.5f);
                float e1 = edge(c.x, c.y, a.x, a.y,
                                x + 0.5f, y + 0.5f);
                float e2 = edge(a.x, a.y, b.x, b.y,
                                x + 0.5f, y + 0.5f);
                float w0 = e0 / area, w1 = e1 / area, w2 = 1 - w0 - w1,
                      den, color[4], tex[4], u, v, fog,
                      varying[MESAGL_MAX_VARYING_INTERPOLATORS][4],
                      varying_x[MESAGL_MAX_VARYING_INTERPOLATORS][4],
                      varying_y[MESAGL_MAX_VARYING_INTERPOLATORS][4];
                const float *fragment_varyings = varying[0];
                int i;
                if (!triangle_edges_inside(&a, &b, &c, area, e0, e1, e2))
                    continue;
                den = w0 * a.iw + w1 * b.iw + w2 * c.iw;
                if (MESAGL_UNLIKELY(fabsf(den) < NTGL_EPSILON))
                    continue;
                if (ctx->program_fragment) {
                    memset(color, 0, sizeof(color));
                } else {
                    for (i = 0; i < 4; ++i)
                        color[i] =
                            ctx->smooth_shading
                                ? (w0 * a.c[i] * a.iw + w1 * b.c[i] * b.iw +
                                   w2 * c.c[i] * c.iw) /
                                      den
                                : c.c[i];
                }
                if (!ctx->program_fragment &&
                    (ctx->enabled & CAP_TEXTURE) && ctx->has_texture) {
                    u = (w0 * a.uv[0] * a.iw + w1 * b.uv[0] * b.iw + w2 * c.uv[0] * c.iw) / den;
                    v = (w0 * a.uv[1] * a.iw + w1 * b.uv[1] * b.iw + w2 * c.uv[1] * c.iw) / den;
                    sample_texture(&ctx->texture, u, v, tex);
                    if (ctx->texture.environment == NTGL_TEXTURE_REPLACE)
                        memcpy(color, tex, sizeof(color));
                    else if (ctx->texture.environment == NTGL_TEXTURE_DECAL) {
                        for (i = 0; i < 3; ++i)
                            color[i] = color[i] * (1.0f - tex[3]) + tex[i] * tex[3];
                    } else if (ctx->texture.environment == NTGL_TEXTURE_ADD) {
                        for (i = 0; i < 3; ++i)
                            color[i] = clamp01(color[i] + tex[i]);
                        color[3] *= tex[3];
                    } else if (ctx->texture.environment == NTGL_TEXTURE_BLEND) {
                        for (i = 0; i < 3; ++i)
                            color[i] = color[i] * (1.0f - tex[i]) +
                                       ctx->texture.environment_color[i] * tex[i];
                        color[3] *= tex[3];
                    } else
                        for (i = 0; i < 4; ++i)
                            color[i] *= tex[i];
                }
                if (!ctx->program_fragment) {
                    fog = (w0 * a.fog * a.iw + w1 * b.fog * b.iw +
                           w2 * c.fog * c.iw) /
                          den;
                    apply_fog(ctx, fog, color);
                }
                if (ctx->program_fragment) {
                    if (constant_program_varyings) {
                        fragment_varyings = a.varying[0];
                    } else {
                        for (i = 0; i < ctx->varying_count; ++i) {
                            int component;

                            for (component = 0; component < 4; ++component)
                                varying[i][component] =
                                    (w0 * a.varying[i][component] * a.iw +
                                     w1 * b.varying[i][component] * b.iw +
                                     w2 * c.varying[i][component] * c.iw) /
                                    den;
                        }
                    }
                    if (MESAGL_UNLIKELY(ctx->program_derivatives_required)) {
                        if (!interpolate_program_varyings(
                                ctx, &a, &b, &c, area, x + 1.5f,
                                y + 0.5f, varying_x) ||
                            !interpolate_program_varyings(
                                ctx, &a, &b, &c, area, x + 0.5f,
                                y + 1.5f, varying_y))
                            continue;
                        for (i = 0; i < ctx->varying_count; ++i) {
                            int component;

                            for (component = 0; component < 4; ++component) {
                                ctx->varying_dfdx[i][component] =
                                    varying_x[i][component] -
                                    varying[i][component];
                                ctx->varying_dfdy[i][component] =
                                    varying_y[i][component] -
                                    varying[i][component];
                            }
                        }
                    }
                }
                write_fragment(ctx, x, y,
                               clamp01(w0 * a.z + w1 * b.z + w2 * c.z + depth_offset), den,
                               color, ctx->program_fragment ? fragment_varyings : NULL);
            }
        }
    }
}

static void raster_line(NTGLcontext *ctx, const Vertex *va, const Vertex *vb)
{
    Vertex ca = *va, cb = *vb;
    ScreenVertex a, b;

    ctx->program_front_facing = 1;
    ctx->stencil_face = 0;
    if (!clip_line(&ca, &cb) || !to_screen(ctx, &ca, &a) || !to_screen(ctx, &cb, &b))
        return;
    raster_screen_line(ctx, &a, &b);
}

static int raster_textured_rectangle(NTGLcontext *ctx, const Vertex *vertices)
{
    const unsigned forbidden = (1u << NTGL_DEPTH_TEST) |
                               (1u << NTGL_BLEND) |
                               (1u << NTGL_CULL_FACE) |
                               (1u << NTGL_SCISSOR_TEST) |
                               (1u << NTGL_STENCIL_TEST) |
                               (1u << NTGL_ALPHA_TEST) |
                               (1u << NTGL_POLYGON_OFFSET_FILL) |
                               (1u << NTGL_FOG);
    ScreenVertex screen[4];
    float left;
    float right;
    float bottom;
    float top;
    int min_x;
    int max_x;
    int min_y;
    int max_y;
    int x;
    int y;
    int i;
    int fast_xrgb_linear;
    NTGLlinearColumn *columns = NULL;

    if (ctx->program_fragment || !(ctx->enabled & CAP_TEXTURE) ||
        (ctx->enabled & forbidden) || !ctx->has_texture ||
        ctx->texture.environment != NTGL_TEXTURE_REPLACE ||
        ctx->polygon_mode[0] != NTGL_POLYGON_FILL ||
        ctx->polygon_mode[1] != NTGL_POLYGON_FILL ||
        ctx->color_mask != 0xfu)
        return 0;
    for (i = 0; i < 4; ++i)
        if (!vertex_inside(&vertices[i]) ||
            !to_screen(ctx, &vertices[i], &screen[i]))
            return 0;
    if (fabsf(screen[0].y - screen[1].y) > NTGL_EPSILON ||
        fabsf(screen[1].x - screen[2].x) > NTGL_EPSILON ||
        fabsf(screen[2].y - screen[3].y) > NTGL_EPSILON ||
        fabsf(screen[3].x - screen[0].x) > NTGL_EPSILON ||
        fabsf(screen[0].iw - screen[1].iw) > NTGL_EPSILON ||
        fabsf(screen[0].iw - screen[2].iw) > NTGL_EPSILON ||
        fabsf(screen[0].iw - screen[3].iw) > NTGL_EPSILON)
        return 0;
    left = screen[0].x;
    right = screen[1].x;
    bottom = screen[0].y;
    top = screen[3].y;
    if (!(right > left) || !(top > bottom))
        return 0;
    min_x = (int)ceilf(left - 0.5f);
    max_x = (int)ceilf(right - 0.5f) - 1;
    min_y = (int)ceilf(bottom - 0.5f);
    max_y = (int)ceilf(top - 0.5f) - 1;
    if (min_x < 0)
        min_x = 0;
    if (min_y < 0)
        min_y = 0;
    if (max_x >= ctx->framebuffer.width)
        max_x = ctx->framebuffer.width - 1;
    if (max_y >= ctx->framebuffer.height)
        max_y = ctx->framebuffer.height - 1;
    fast_xrgb_linear = ctx->texture.format == NTGL_RGBA8888 &&
                       ctx->texture.filter == NTGL_LINEAR &&
                       ctx->framebuffer.format == NTGL_XRGB8888;
    if (fast_xrgb_linear) {
        int column_count = max_x - min_x + 1;
        float column_u = screen[0].uv[0] +
                         (min_x + 0.5f - left) *
                             ((screen[1].uv[0] - screen[0].uv[0]) /
                              (right - left));
        float column_du = (screen[1].uv[0] - screen[0].uv[0]) /
                          (right - left);

        columns = ctx->allocator.alloc(ctx->allocator.user,
                                       (size_t)column_count * sizeof(*columns));
        if (!columns) {
            fast_xrgb_linear = 0;
        } else {
            for (i = 0; i < column_count; ++i) {
                float fx = normalized_ntgl_texture_coordinate(
                               column_u, ctx->texture.wrap_s) *
                           ctx->texture.width;
                int unwrapped_x0 = (int)floorf(fx - 0.5f);

                columns[i].x0 = texture_sample_index(unwrapped_x0,
                                                     ctx->texture.width,
                                                     ctx->texture.wrap_s);
                columns[i].x1 = texture_sample_index(unwrapped_x0 + 1,
                                                     ctx->texture.width,
                                                     ctx->texture.wrap_s);
                columns[i].alpha = fx - 0.5f - unwrapped_x0;
                column_u += column_du;
            }
        }
    }
    for (y = min_y; y <= max_y; ++y) {
        float dv = (screen[3].uv[1] - screen[0].uv[1]) /
                   (top - bottom);
        float du = (screen[1].uv[0] - screen[0].uv[0]) /
                   (right - left);
        float v = screen[0].uv[1] + (y + 0.5f - bottom) * dv;
        float u = screen[0].uv[0] + (min_x + 0.5f - left) * du;
        int row_y0 = 0;
        int row_y1 = 0;
        int texture_stride = 0;
        float row_alpha = 0.0f;
        const unsigned char *texture_base = ctx->texture.pixels;

        if (fast_xrgb_linear) {
            float fy = normalized_ntgl_texture_coordinate(
                           v, ctx->texture.wrap_t) *
                       ctx->texture.height;
            int unwrapped_y0 = (int)floorf(fy - 0.5f);

            row_y0 = texture_sample_index(unwrapped_y0,
                                          ctx->texture.height,
                                          ctx->texture.wrap_t);
            row_y1 = texture_sample_index(unwrapped_y0 + 1,
                                          ctx->texture.height,
                                          ctx->texture.wrap_t);
            if (ctx->texture.origin == NTGL_ORIGIN_TOP_LEFT) {
                row_y0 = ctx->texture.height - row_y0 - 1;
                row_y1 = ctx->texture.height - row_y1 - 1;
            }
            row_alpha = fy - 0.5f - unwrapped_y0;
            texture_stride = ctx->texture.stride ? ctx->texture.stride
                                                  : ctx->texture.width * 4;
            if (ctx->pixel_ops.linear_rgba8888_to_xrgb8888) {
                unsigned char *destination =
                    pixel_address(&ctx->framebuffer, min_x, y);
                const unsigned char *source_row0 =
                    texture_base + row_y0 * texture_stride;
                const unsigned char *source_row1 =
                    texture_base + row_y1 * texture_stride;

                if (ctx->pixel_ops.linear_rgba8888_to_xrgb8888(
                        ctx->pixel_ops.user, destination, source_row0,
                        source_row1, columns, max_x - min_x + 1,
                        row_alpha))
                    continue;
            }
        }

        for (x = min_x; x <= max_x; ++x) {
            float color[4];
            unsigned char *address;

            address = pixel_address(&ctx->framebuffer, x, y);
            if (fast_xrgb_linear) {
                const NTGLlinearColumn *column = &columns[x - min_x];
                const unsigned char *p00;
                const unsigned char *p10;
                const unsigned char *p01;
                const unsigned char *p11;

                p00 = texture_base + row_y0 * texture_stride + column->x0 * 4;
                p10 = texture_base + row_y0 * texture_stride + column->x1 * 4;
                p01 = texture_base + row_y1 * texture_stride + column->x0 * 4;
                p11 = texture_base + row_y1 * texture_stride + column->x1 * 4;
                for (i = 0; i < 3; ++i)
                    color[i] =
                        ((p00[i] * (1.0f - column->alpha) +
                          p10[i] * column->alpha) *
                             (1.0f - row_alpha) +
                         (p01[i] * (1.0f - column->alpha) +
                          p11[i] * column->alpha) *
                             row_alpha) /
                        255.0f;
                address[0] = to_u8(color[2]);
                address[1] = to_u8(color[1]);
                address[2] = to_u8(color[0]);
                address[3] = 255;
            } else {
                sample_texture(&ctx->texture, u, v, color);
                pack_fragment_pixel(address, ctx->framebuffer.format, color,
                                    x, y,
                                    ctx->enabled & (1u << NTGL_DITHER));
            }
            u += du;
        }
    }
    if (columns)
        ctx->allocator.free(ctx->allocator.user, columns);
    return 1;
}

static void draw_vertices(NTGLcontext *ctx)
{
    int i, n = ctx->vertex_count;
    Vertex *v = ctx->vertices;
    switch (ctx->primitive) {
    case NTGL_POINTS:
        ctx->program_front_facing = 1;
        ctx->stencil_face = 0;
        for (i = 0; i < n; ++i) {
            ScreenVertex s;
            if (vertex_inside(&v[i]) && to_screen(ctx, &v[i], &s))
                raster_screen_point(ctx, &s);
        }
        break;
    case NTGL_LINES:
        for (i = 0; i + 1 < n; i += 2)
            raster_line(ctx, &v[i], &v[i + 1]);
        break;
    case NTGL_LINE_STRIP:
        for (i = 0; i + 1 < n; ++i)
            raster_line(ctx, &v[i], &v[i + 1]);
        break;
    case NTGL_LINE_LOOP:
        for (i = 0; i + 1 < n; ++i)
            raster_line(ctx, &v[i], &v[i + 1]);
        if (n > 1)
            raster_line(ctx, &v[n - 1], &v[0]);
        break;
    case NTGL_TRIANGLES:
        for (i = 0; i + 2 < n; i += 3)
            raster_triangle(ctx, &v[i], &v[i + 1], &v[i + 2]);
        break;
    case NTGL_TRIANGLE_STRIP:
        for (i = 0; i + 2 < n; ++i)
            if (i & 1)
                raster_triangle(ctx, &v[i + 1], &v[i], &v[i + 2]);
            else
                raster_triangle(ctx, &v[i], &v[i + 1], &v[i + 2]);
        break;
    case NTGL_TRIANGLE_FAN:
        for (i = 1; i + 1 < n; ++i)
            raster_triangle(ctx, &v[0], &v[i], &v[i + 1]);
        break;
    case NTGL_QUADS:
        for (i = 0; i + 3 < n; i += 4) {
            if (raster_textured_rectangle(ctx, &v[i]))
                continue;
            raster_triangle(ctx, &v[i], &v[i + 1], &v[i + 2]);
            raster_triangle(ctx, &v[i], &v[i + 2], &v[i + 3]);
        }
        break;
    }
}

static int allocate_buffers(NTGLcontext *ctx, const NTGLframebuffer *fb)
{
    size_t color_size;
    int bpp = bytes_per_pixel(fb->format);
    void *pixels = fb->pixels;
    int64_t row_bytes;
    uint64_t stride_bytes;
    uint64_t address_span;

    if (fb->width <= 0 || fb->height <= 0 || !bpp ||
        (fb->origin != NTGL_ORIGIN_BOTTOM_LEFT && fb->origin != NTGL_ORIGIN_TOP_LEFT))
        return 0;
    row_bytes = (int64_t)fb->width * bpp;
    if (row_bytes > INT_MAX ||
        (fb->stride && (int64_t)fb->stride > -row_bytes &&
         (int64_t)fb->stride < row_bytes))
        return 0;
    stride_bytes = fb->stride < 0 ? (uint64_t)(-(int64_t)fb->stride)
                                  : (uint64_t)(fb->stride ? fb->stride : row_bytes);
    if ((uint64_t)(fb->height - 1) >
        ((uint64_t)PTRDIFF_MAX - (uint64_t)row_bytes) / stride_bytes)
        return 0;
    address_span = (uint64_t)(fb->height - 1) * stride_bytes + (uint64_t)row_bytes;
    if (address_span > SIZE_MAX)
        return 0;
    if (!pixels) {
        if ((size_t)fb->height > SIZE_MAX / (size_t)row_bytes)
            return 0;
        color_size = (size_t)fb->height * (size_t)row_bytes;
        pixels = ctx->allocator.alloc(ctx->allocator.user, color_size);
        if (!pixels)
            return 0;
        ctx->owns_color = 1;
    }
    ctx->framebuffer = *fb;
    ctx->framebuffer.pixels = pixels;
    if (!ctx->framebuffer.stride)
        ctx->framebuffer.stride = (int)row_bytes;
    return 1;
}

static int allocate_depth(NTGLcontext *ctx)
{
    size_t i, count;
    if (ctx->depth)
        return 1;
    if (ctx->external_aux)
        return 1;
    count = (size_t)ctx->framebuffer.width * ctx->framebuffer.height;
    ctx->depth = (float *)ctx->allocator.alloc(ctx->allocator.user, count * sizeof(float));
    if (!ctx->depth)
        return 0;
    ctx->owns_depth = 1;
    for (i = 0; i < count; ++i)
        ctx->depth[i] = quantize_depth(ctx->clear_depth);
    return 1;
}

static int allocate_stencil(NTGLcontext *ctx)
{
    size_t count = (size_t)ctx->framebuffer.width * ctx->framebuffer.height;
    if (ctx->stencil)
        return 1;
    if (ctx->external_aux)
        return 1;
    ctx->stencil = (unsigned char *)ctx->allocator.alloc(ctx->allocator.user, count);
    if (!ctx->stencil)
        return 0;
    ctx->owns_stencil = 1;
    memset(ctx->stencil, ctx->clear_stencil, count);
    return 1;
}

NTGLcontext *ntglCreateContext(const NTGLframebuffer *fb, const NTGLallocator *allocator)
{
    NTGLallocator a;
    NTGLcontext *ctx;
    if (!fb)
        return NULL;
    a.alloc = allocator && allocator->alloc ? allocator->alloc : default_alloc;
    a.free = allocator && allocator->free ? allocator->free : default_free;
    a.user = allocator ? allocator->user : NULL;
    ctx = (NTGLcontext *)a.alloc(a.user, sizeof(*ctx));
    if (!ctx)
        return NULL;
    memset(ctx, 0, sizeof(*ctx));
    ctx->allocator = a;
    if (!allocate_buffers(ctx, fb)) {
        a.free(a.user, ctx);
        return NULL;
    }
    identity(ctx->modelview[0]);
    identity(ctx->projection[0]);
    identity(ctx->texture_matrix[0]);
    ctx->matrix_mode = NTGL_MODELVIEW;
    ctx->color[0] = ctx->color[1] = ctx->color[2] = ctx->color[3] = 1;
    ctx->normal[2] = 1.0f;
    ctx->material_ambient[0] = ctx->material_ambient[1] = ctx->material_ambient[2] = 0.2f;
    ctx->material_ambient[3] = 1.0f;
    ctx->material_diffuse[0] = ctx->material_diffuse[1] = ctx->material_diffuse[2] = 0.8f;
    ctx->material_diffuse[3] = 1.0f;
    ctx->light_diffuse[0][0] = ctx->light_diffuse[0][1] = ctx->light_diffuse[0][2] =
        ctx->light_diffuse[0][3] = 1.0f;
    ctx->light_specular[0][0] = ctx->light_specular[0][1] = ctx->light_specular[0][2] =
        ctx->light_specular[0][3] = 1.0f;
    ctx->light_position[0][2] = 1.0f;
    ctx->light_model_ambient[0] = ctx->light_model_ambient[1] = ctx->light_model_ambient[2] = 0.2f;
    ctx->light_model_ambient[3] = 1.0f;
    ctx->fog_density = 1.0f;
    ctx->fog_end = 1.0f;
    ctx->clear_depth = 1;
    ctx->depth_near = 0.0f;
    ctx->depth_far = 1.0f;
    ctx->depth_func = NTGL_LESS;
    ctx->depth_mask = 1;
    ctx->enabled = 1u << NTGL_DITHER;
    ctx->color_mask = 0xfu;
    ctx->stencil_value_mask[0] = ctx->stencil_value_mask[1] = 0xffu;
    ctx->stencil_write_mask[0] = ctx->stencil_write_mask[1] = 0xffu;
    ctx->stencil_func[0] = ctx->stencil_func[1] = NTGL_ALWAYS;
    ctx->stencil_fail[0] = ctx->stencil_fail[1] = NTGL_KEEP;
    ctx->stencil_depth_fail[0] = ctx->stencil_depth_fail[1] = NTGL_KEEP;
    ctx->stencil_pass[0] = ctx->stencil_pass[1] = NTGL_KEEP;
    ctx->alpha_func = NTGL_ALWAYS;
    ctx->smooth_shading = 1;
    ctx->blend_src = ctx->blend_src_alpha = NTGL_ONE;
    ctx->blend_dst = ctx->blend_dst_alpha = NTGL_ZERO;
    ctx->blend_equation_rgb = ctx->blend_equation_alpha = NTGL_FUNC_ADD;
    ctx->front_ccw = 1;
    ctx->polygon_mode[0] = ctx->polygon_mode[1] = NTGL_POLYGON_FILL;
    ctx->point_size = 1.0f;
    ctx->line_width = 1.0f;
    ctx->viewport[2] = ctx->scissor[2] = fb->width;
    ctx->viewport[3] = ctx->scissor[3] = fb->height;
    ntglMakeCurrent(ctx);
    ntglClear(1, 0);
    return ctx;
}

void ntglDestroyContext(NTGLcontext *ctx)
{
    NTGLcontext *previous;
    if (!ctx)
        return;
    previous = current_context;
    current_context = ctx;
    mesaGLReleaseCurrentContext();
    if (current_context == ctx)
        current_context = NULL;
    if (ctx->owns_color)
        ctx->allocator.free(ctx->allocator.user, ctx->framebuffer.pixels);
    if (ctx->owns_depth)
        ctx->allocator.free(ctx->allocator.user, ctx->depth);
    if (ctx->owns_stencil)
        ctx->allocator.free(ctx->allocator.user, ctx->stencil);
    ctx->allocator.free(ctx->allocator.user, ctx);
    if (previous != ctx)
        current_context = previous;
}

NTGLresult ntglMakeCurrent(NTGLcontext *ctx)
{
    current_context = ctx;
    return NTGL_OK;
}

NTGLcontext *ntglGetCurrent(void)
{
    return current_context;
}

void ntglSetPixelOps(NTGLcontext *context, const NTGLpixelOps *operations)
{
    if (!context)
        return;
    if (operations)
        context->pixel_ops = *operations;
    else
        memset(&context->pixel_ops, 0, sizeof(context->pixel_ops));
}

void *ntglAlloc(size_t size)
{
    NTGLcontext *c = current_context;
    return c ? c->allocator.alloc(c->allocator.user, size) : NULL;
}

void ntglFree(void *pointer)
{
    NTGLcontext *c = current_context;
    if (c && pointer)
        c->allocator.free(c->allocator.user, pointer);
}

const NTGLframebuffer *ntglGetFramebuffer(const NTGLcontext *ctx)
{
    return ctx ? &ctx->framebuffer : NULL;
}

NTGLresult ntglAttachFramebuffer(NTGLcontext *ctx, const NTGLframebuffer *fb)
{
    float *old_depth;
    unsigned char *old_stencil;
    void *old_color;
    int old_owned;
    int old_owns_depth;
    int old_owns_stencil;
    int old_external_aux;
    if (!ctx || !fb)
        return NTGL_INVALID_ARGUMENT;
    old_depth = ctx->depth;
    old_stencil = ctx->stencil;
    old_color = ctx->framebuffer.pixels;
    old_owned = ctx->owns_color;
    old_owns_depth = ctx->owns_depth;
    old_owns_stencil = ctx->owns_stencil;
    old_external_aux = ctx->external_aux;
    ctx->depth = NULL;
    ctx->stencil = NULL;
    ctx->owns_depth = 0;
    ctx->owns_stencil = 0;
    ctx->external_aux = 0;
    ctx->owns_color = 0;
    if (!allocate_buffers(ctx, fb)) {
        ctx->depth = old_depth;
        ctx->stencil = old_stencil;
        ctx->framebuffer.pixels = old_color;
        ctx->owns_color = old_owned;
        ctx->owns_depth = old_owns_depth;
        ctx->owns_stencil = old_owns_stencil;
        ctx->external_aux = old_external_aux;
        return NTGL_OUT_OF_MEMORY;
    }
    if (old_owned)
        ctx->allocator.free(ctx->allocator.user, old_color);
    if (old_owns_depth)
        ctx->allocator.free(ctx->allocator.user, old_depth);
    if (old_owns_stencil)
        ctx->allocator.free(ctx->allocator.user, old_stencil);
    ctx->viewport[0] = ctx->viewport[1] = ctx->scissor[0] = ctx->scissor[1] = 0;
    ctx->viewport[2] = ctx->scissor[2] = fb->width;
    ctx->viewport[3] = ctx->scissor[3] = fb->height;
    if ((ctx->enabled & CAP_DEPTH) && !allocate_depth(ctx)) {
        set_error(ctx, NTGL_OUT_OF_MEMORY);
        return NTGL_OUT_OF_MEMORY;
    }
    if ((ctx->enabled & (1u << NTGL_STENCIL_TEST)) && !allocate_stencil(ctx)) {
        set_error(ctx, NTGL_OUT_OF_MEMORY);
        return NTGL_OUT_OF_MEMORY;
    }
    return NTGL_OK;
}

NTGLresult ntglAttachAuxBuffers(NTGLcontext *ctx, float *depth, unsigned char *stencil)
{
    if (!ctx)
        return NTGL_INVALID_ARGUMENT;
    if (ctx->owns_depth)
        ctx->allocator.free(ctx->allocator.user, ctx->depth);
    if (ctx->owns_stencil)
        ctx->allocator.free(ctx->allocator.user, ctx->stencil);
    ctx->depth = depth;
    ctx->stencil = stencil;
    ctx->owns_depth = 0;
    ctx->owns_stencil = 0;
    ctx->external_aux = 1;
    return NTGL_OK;
}

NTGLresult ntglResize(NTGLcontext *ctx, int width, int height)
{
    NTGLframebuffer fb;
    if (!ctx || width <= 0 || height <= 0)
        return NTGL_INVALID_ARGUMENT;
    if (!ctx->owns_color)
        return NTGL_INVALID_OPERATION;
    fb = ctx->framebuffer;
    fb.pixels = NULL;
    fb.width = width;
    fb.height = height;
    fb.stride = 0;
    {
        float *old_depth = ctx->depth;
        unsigned char *old_stencil = ctx->stencil;
        void *old_color = ctx->framebuffer.pixels;
        int old_owns_depth = ctx->owns_depth;
        int old_owns_stencil = ctx->owns_stencil;
        int old_external_aux = ctx->external_aux;
        ctx->depth = NULL;
        ctx->stencil = NULL;
        ctx->owns_depth = 0;
        ctx->owns_stencil = 0;
        ctx->external_aux = 0;
        ctx->owns_color = 0;
        if (!allocate_buffers(ctx, &fb)) {
            ctx->depth = old_depth;
            ctx->stencil = old_stencil;
            ctx->framebuffer.pixels = old_color;
            ctx->owns_color = 1;
            ctx->owns_depth = old_owns_depth;
            ctx->owns_stencil = old_owns_stencil;
            ctx->external_aux = old_external_aux;
            return NTGL_OUT_OF_MEMORY;
        }
        if (old_owns_depth)
            ctx->allocator.free(ctx->allocator.user, old_depth);
        if (old_owns_stencil)
            ctx->allocator.free(ctx->allocator.user, old_stencil);
        ctx->allocator.free(ctx->allocator.user, old_color);
    }
    ctx->viewport[0] = ctx->viewport[1] = ctx->scissor[0] = ctx->scissor[1] = 0;
    ctx->viewport[2] = ctx->scissor[2] = width;
    ctx->viewport[3] = ctx->scissor[3] = height;
    if ((ctx->enabled & CAP_DEPTH) && !allocate_depth(ctx)) {
        set_error(ctx, NTGL_OUT_OF_MEMORY);
        return NTGL_OUT_OF_MEMORY;
    }
    if ((ctx->enabled & (1u << NTGL_STENCIL_TEST)) && !allocate_stencil(ctx)) {
        set_error(ctx, NTGL_OUT_OF_MEMORY);
        return NTGL_OUT_OF_MEMORY;
    }
    return NTGL_OK;
}

void ntglViewport(int x, int y, int width, int height)
{
    NTGLcontext *c = current_context;
    if (!c)
        return;
    if (width < 0 || height < 0) {
        set_error(c, NTGL_INVALID_ARGUMENT);
        return;
    }
    c->viewport[0] = x;
    c->viewport[1] = y;
    c->viewport[2] = width;
    c->viewport[3] = height;
}

void ntglDepthRange(float near_value, float far_value)
{
    if (!current_context)
        return;
    current_context->depth_near = clamp01(near_value);
    current_context->depth_far = clamp01(far_value);
}

void ntglPolygonOffset(float factor, float units)
{
    if (!current_context)
        return;
    current_context->polygon_offset_factor = factor;
    current_context->polygon_offset_units = units;
}

void ntglScissor(int x, int y, int width, int height)
{
    NTGLcontext *c = current_context;
    if (!c)
        return;
    if (width < 0 || height < 0) {
        set_error(c, NTGL_INVALID_ARGUMENT);
        return;
    }
    c->scissor[0] = x;
    c->scissor[1] = y;
    c->scissor[2] = width;
    c->scissor[3] = height;
}

void ntglClearColor(float r, float g, float b, float a)
{
    NTGLcontext *c = current_context;
    if (c) {
        c->clear_color[0] = clamp01(r);
        c->clear_color[1] = clamp01(g);
        c->clear_color[2] = clamp01(b);
        c->clear_color[3] = clamp01(a);
    }
}

void ntglClearDepth(float d)
{
    if (current_context)
        current_context->clear_depth = clamp01(d);
}

void ntglClear(int color, int depth)
{
    NTGLcontext *c = current_context;
    int color_done = 0;
    int x, y;
    if (!c)
        return;
    if (depth && c->depth_mask && !allocate_depth(c)) {
        set_error(c, NTGL_OUT_OF_MEMORY);
        return;
    }
    if (color && c->color_mask == 0xfu && !(c->enabled & CAP_SCISSOR)) {
        unsigned char packed[4];
        unsigned char *first_row;
        int bpp = bytes_per_pixel(c->framebuffer.format);
        size_t row_bytes = (size_t)c->framebuffer.width * (size_t)bpp;

        pack_pixel(packed, c->framebuffer.format, c->clear_color);
        first_row = pixel_address(&c->framebuffer, 0, 0);
        for (x = 0; x < c->framebuffer.width; ++x)
            memcpy(first_row + (size_t)x * bpp, packed, (size_t)bpp);
        for (y = 1; y < c->framebuffer.height; ++y)
            memcpy(pixel_address(&c->framebuffer, 0, y), first_row,
                   row_bytes);
        color_done = 1;
    }
    if (color_done && (!depth || !c->depth || !c->depth_mask))
        return;
    for (y = 0; y < c->framebuffer.height; ++y)
        for (x = 0; x < c->framebuffer.width; ++x)
            if (inside_scissor(c, x, y)) {
                int i = y * c->framebuffer.width + x;
                if (color && !color_done) {
                    unsigned char *address = pixel_address(&c->framebuffer, x, y);

                    if (c->color_mask == 0xf) {
                        pack_pixel(address, c->framebuffer.format, c->clear_color);
                    } else if (c->color_mask) {
                        float masked_color[4];
                        int channel;

                        unpack_pixel(address, c->framebuffer.format, masked_color);
                        for (channel = 0; channel < 4; ++channel)
                            if (c->color_mask & (1 << channel))
                                masked_color[channel] = c->clear_color[channel];
                        pack_pixel(address, c->framebuffer.format, masked_color);
                    }
                }
                if (depth && c->depth && c->depth_mask)
                    c->depth[i] = quantize_depth(c->clear_depth);
            }
}

void ntglEnable(NTGLcapability cap)
{
    NTGLcontext *c = current_context;
    if (!c)
        return;
    if (cap == NTGL_DEPTH_TEST && !allocate_depth(c)) {
        set_error(c, NTGL_OUT_OF_MEMORY);
        return;
    }
    if (cap == NTGL_STENCIL_TEST && !allocate_stencil(c)) {
        set_error(c, NTGL_OUT_OF_MEMORY);
        return;
    }
    c->enabled |= 1u << (unsigned)cap;
}

void ntglDisable(NTGLcapability cap)
{
    NTGLcontext *c = current_context;
    if (c)
        c->enabled &= ~(1u << (unsigned)cap);
}

void ntglDepthFunc(NTGLdepthFunc f)
{
    if (current_context)
        current_context->depth_func = f;
}

void ntglDepthMask(int e)
{
    if (current_context)
        current_context->depth_mask = !!e;
}

void ntglColorMask(int red, int green, int blue, int alpha)
{
    if (current_context)
        current_context->color_mask =
            (!!red << 0) | (!!green << 1) | (!!blue << 2) | (!!alpha << 3);
}

void ntglClearStencil(unsigned value)
{
    if (current_context)
        current_context->clear_stencil = (unsigned char)value;
}

void ntglClearStencilBuffer(void)
{
    NTGLcontext *c = current_context;
    int x, y;
    if (!c || !allocate_stencil(c) || !c->stencil)
        return;
    for (y = 0; y < c->framebuffer.height; ++y)
        for (x = 0; x < c->framebuffer.width; ++x)
            if (inside_scissor(c, x, y)) {
                int index = y * c->framebuffer.width + x;
                unsigned char old = c->stencil[index];
                c->stencil[index] =
                    (old & ~c->stencil_write_mask[0]) |
                    (c->clear_stencil & c->stencil_write_mask[0]);
            }
}

void ntglStencilFunc(NTGLdepthFunc function, unsigned reference, unsigned mask)
{
    ntglStencilFuncSeparate(1, function, reference, mask);
    ntglStencilFuncSeparate(0, function, reference, mask);
}

void ntglStencilFuncSeparate(int front, NTGLdepthFunc function, unsigned reference, unsigned mask)
{
    int face = front ? 0 : 1;

    if (!current_context)
        return;
    current_context->stencil_func[face] = function;
    current_context->stencil_ref[face] = (unsigned char)reference;
    current_context->stencil_value_mask[face] = (unsigned char)mask;
}

void ntglStencilMask(unsigned mask)
{
    ntglStencilMaskSeparate(1, mask);
    ntglStencilMaskSeparate(0, mask);
}

void ntglStencilMaskSeparate(int front, unsigned mask)
{
    if (current_context)
        current_context->stencil_write_mask[front ? 0 : 1] = (unsigned char)mask;
}

void ntglStencilOp(NTGLstencilOp fail, NTGLstencilOp depth_fail, NTGLstencilOp pass)
{
    ntglStencilOpSeparate(1, fail, depth_fail, pass);
    ntglStencilOpSeparate(0, fail, depth_fail, pass);
}

void ntglStencilOpSeparate(int front, NTGLstencilOp fail, NTGLstencilOp depth_fail,
                           NTGLstencilOp pass)
{
    int face = front ? 0 : 1;

    if (!current_context)
        return;
    current_context->stencil_fail[face] = fail;
    current_context->stencil_depth_fail[face] = depth_fail;
    current_context->stencil_pass[face] = pass;
}

void ntglAlphaFunc(NTGLdepthFunc function, float reference)
{
    if (current_context) {
        current_context->alpha_func = function;
        current_context->alpha_ref = clamp01(reference);
    }
}

void ntglShadeModel(int smooth)
{
    if (current_context)
        current_context->smooth_shading = !!smooth;
}

void ntglBlendFunc(NTGLblendFactor s, NTGLblendFactor d)
{
    ntglBlendFuncSeparate(s, d, s, d);
}

void ntglBlendFuncSeparate(NTGLblendFactor sr, NTGLblendFactor dr, NTGLblendFactor sa,
                           NTGLblendFactor da)
{
    if (current_context) {
        current_context->blend_src = sr;
        current_context->blend_dst = dr;
        current_context->blend_src_alpha = sa;
        current_context->blend_dst_alpha = da;
    }
}

void ntglBlendEquationSeparate(NTGLblendEquation rgb, NTGLblendEquation alpha)
{
    if (current_context) {
        current_context->blend_equation_rgb = rgb;
        current_context->blend_equation_alpha = alpha;
    }
}

void ntglBlendColor(float red, float green, float blue, float alpha)
{
    if (current_context) {
        current_context->blend_color[0] = clamp01(red);
        current_context->blend_color[1] = clamp01(green);
        current_context->blend_color[2] = clamp01(blue);
        current_context->blend_color[3] = clamp01(alpha);
    }
}

void ntglFrontFace(int ccw)
{
    if (current_context)
        current_context->front_ccw = !!ccw;
}

void ntglCullFace(int front)
{
    if (current_context)
        current_context->cull_front = !!front;
}

void ntglPolygonMode(int front, NTGLpolygonMode mode)
{
    if (!current_context)
        return;
    if (mode < NTGL_POLYGON_POINT || mode > NTGL_POLYGON_FILL) {
        set_error(current_context, NTGL_INVALID_ARGUMENT);
        return;
    }
    current_context->polygon_mode[front ? 0 : 1] = mode;
}

void ntglPointSize(float size)
{
    if (!current_context)
        return;
    if (size <= 0.0f) {
        set_error(current_context, NTGL_INVALID_ARGUMENT);
        return;
    }
    current_context->point_size = size;
}

void ntglLineWidth(float width)
{
    if (!current_context)
        return;
    if (width <= 0.0f) {
        set_error(current_context, NTGL_INVALID_ARGUMENT);
        return;
    }
    current_context->line_width = width;
}

void ntglSetFragmentFunction(NTGLfragmentFn function, void *user)
{
    if (!current_context)
        return;
    current_context->fragment_function = function;
    current_context->fragment_user = user;
}

static void draw_programmable(NTGLprimitive primitive,
                              const NTGLprogramVertex *vertices, int count,
                              int varying_count, NTGLprogramFragmentFn fragment,
                              void *user, int derivatives_required,
                              int output_clamped)
{
    NTGLcontext *ctx = current_context;
    int i;

    if (!ctx || !vertices || count < 0 || count > MESAGL_MAX_VERTICES || varying_count < 0 ||
        varying_count > MESAGL_MAX_VARYING_INTERPOLATORS) {
        set_error(ctx, NTGL_INVALID_ARGUMENT);
        return;
    }
    ctx->primitive = primitive;
    ctx->vertex_count = count;
    ctx->varying_count = varying_count;
    ctx->program_derivatives_required = derivatives_required;
    ctx->program_fragment = fragment;
    ctx->program_fragment_user = user;
    ctx->program_output_clamped = output_clamped;
    ctx->program_fast_xrgb_blend =
        !derivatives_required && primitive == NTGL_TRIANGLES &&
        ctx->fragment_function == NULL &&
        ctx->framebuffer.format == NTGL_XRGB8888 &&
        !(ctx->enabled & ((1u << NTGL_ALPHA_TEST) |
                          (1u << NTGL_STENCIL_TEST) | CAP_DEPTH)) &&
        (ctx->enabled & CAP_BLEND) && ctx->color_mask == 0xfu &&
        ctx->blend_equation_rgb == NTGL_FUNC_ADD &&
        ctx->blend_equation_alpha == NTGL_FUNC_ADD &&
        ctx->blend_src == NTGL_SRC_ALPHA &&
        ctx->blend_dst == NTGL_ONE_MINUS_SRC_ALPHA &&
        ctx->blend_src_alpha == NTGL_ONE &&
        ctx->blend_dst_alpha == NTGL_ONE_MINUS_SRC_ALPHA;
    memset(ctx->varying_dfdx, 0, sizeof(ctx->varying_dfdx));
    memset(ctx->varying_dfdy, 0, sizeof(ctx->varying_dfdy));
    for (i = 0; i < count; ++i) {
        Vertex *destination = &ctx->vertices[i];

        memcpy(destination->p, vertices[i].position, sizeof(destination->p));
        destination->point_size = vertices[i].point_size > 0.0f ? vertices[i].point_size : 1.0f;
        memcpy(destination->varying, vertices[i].varying,
               (size_t)varying_count * sizeof(destination->varying[0]));
        destination->uv[0] = 0.0f;
        destination->uv[1] = 0.0f;
        destination->c[0] = 1.0f;
        destination->c[1] = 1.0f;
        destination->c[2] = 1.0f;
        destination->c[3] = 1.0f;
        destination->fog = 1.0f;
    }
    draw_vertices(ctx);
    ctx->program_fragment = NULL;
    ctx->program_fragment_user = NULL;
    ctx->program_fast_xrgb_blend = 0;
    ctx->program_fast_row = NULL;
    ctx->program_output_clamped = 0;
    ctx->varying_count = 0;
    ctx->program_derivatives_required = 1;
    ctx->vertex_count = 0;
}

void ntglDrawProgrammable(NTGLprimitive primitive,
                          const NTGLprogramVertex *vertices, int count,
                          int varying_count, NTGLprogramFragmentFn fragment,
                          void *user)
{
    draw_programmable(primitive, vertices, count, varying_count, fragment,
                      user, 1, 0);
}

void ntglDrawProgrammableNoDerivatives(
    NTGLprimitive primitive, const NTGLprogramVertex *vertices, int count,
    int varying_count, NTGLprogramFragmentFn fragment, void *user)
{
    draw_programmable(primitive, vertices, count, varying_count, fragment,
                      user, 0, 0);
}

void ntglDrawProgrammableNoDerivativesClamped(
    NTGLprimitive primitive, const NTGLprogramVertex *vertices, int count,
    int varying_count, NTGLprogramFragmentFn fragment, void *user)
{
    draw_programmable(primitive, vertices, count, varying_count, fragment,
                      user, 0, 1);
}

void ntglMatrixMode(NTGLmatrixMode mode)
{
    if (!current_context)
        return;
    if (mode != NTGL_MODELVIEW && mode != NTGL_PROJECTION && mode != NTGL_TEXTURE) {
        set_error(current_context, NTGL_INVALID_ARGUMENT);
        return;
    }
    current_context->matrix_mode = mode;
}

void ntglLoadIdentity(void)
{
    if (current_context)
        identity(active_matrix(current_context));
}

void ntglLoadMatrixf(const float *m)
{
    if (current_context && m)
        memcpy(active_matrix(current_context), m, 16 * sizeof(float));
}

void ntglMultMatrixf(const float *m)
{
    if (current_context && m)
        multiply(active_matrix(current_context), active_matrix(current_context), m);
}

void ntglPushMatrix(void)
{
    NTGLcontext *c = current_context;
    int *top;
    float (*stack)[16];
    if (!c)
        return;
    top = active_matrix_top(c);
    stack = active_matrix_stack(c);
    if (*top + 1 >= NTGL_MATRIX_STACK) {
        set_error(c, NTGL_STACK_OVERFLOW);
        return;
    }
    memcpy(stack[*top + 1], stack[*top], 16 * sizeof(float));
    ++*top;
}

void ntglPopMatrix(void)
{
    NTGLcontext *c = current_context;
    int *top;
    if (!c)
        return;
    top = active_matrix_top(c);
    if (!*top) {
        set_error(c, NTGL_STACK_UNDERFLOW);
        return;
    }
    --*top;
}

void ntglTranslatef(float x, float y, float z)
{
    float m[16];
    identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
    ntglMultMatrixf(m);
}

void ntglScalef(float x, float y, float z)
{
    float m[16];
    identity(m);
    m[0] = x;
    m[5] = y;
    m[10] = z;
    ntglMultMatrixf(m);
}

void ntglRotatef(float degrees, float x, float y, float z)
{
    float m[16], len = sqrtf(x * x + y * y + z * z), c, s, ic;
    if (len < NTGL_EPSILON)
        return;
    x /= len;
    y /= len;
    z /= len;
    c = cosf(degrees * 0.017453292519943295f);
    s = sinf(degrees * 0.017453292519943295f);
    ic = 1 - c;
    identity(m);
    m[0] = x * x * ic + c;
    m[4] = x * y * ic - z * s;
    m[8] = x * z * ic + y * s;
    m[1] = y * x * ic + z * s;
    m[5] = y * y * ic + c;
    m[9] = y * z * ic - x * s;
    m[2] = z * x * ic - y * s;
    m[6] = z * y * ic + x * s;
    m[10] = z * z * ic + c;
    ntglMultMatrixf(m);
}

void ntglGetMatrix(NTGLmatrixMode mode, float *matrix)
{
    NTGLcontext *ctx = current_context;

    if (!ctx || !matrix)
        return;
    if (mode == NTGL_MODELVIEW)
        memcpy(matrix, ctx->modelview[ctx->modelview_top], 16 * sizeof(float));
    else if (mode == NTGL_PROJECTION)
        memcpy(matrix, ctx->projection[ctx->projection_top], 16 * sizeof(float));
    else if (mode == NTGL_TEXTURE)
        memcpy(matrix, ctx->texture_matrix[ctx->texture_top], 16 * sizeof(float));
    else
        set_error(ctx, NTGL_INVALID_ARGUMENT);
}

void ntglFrustum(float l, float r, float b, float t, float n, float f)
{
    float m[16];
    if (r == l || t == b || f == n || n <= 0 || f <= 0) {
        set_error(current_context, NTGL_INVALID_ARGUMENT);
        return;
    }
    memset(m, 0, sizeof(m));
    m[0] = 2 * n / (r - l);
    m[5] = 2 * n / (t - b);
    m[8] = (r + l) / (r - l);
    m[9] = (t + b) / (t - b);
    m[10] = -(f + n) / (f - n);
    m[11] = -1;
    m[14] = -2 * f * n / (f - n);
    ntglMultMatrixf(m);
}

void ntglOrtho(float l, float r, float b, float t, float n, float f)
{
    float m[16];
    if (r == l || t == b || f == n) {
        set_error(current_context, NTGL_INVALID_ARGUMENT);
        return;
    }
    identity(m);
    m[0] = 2 / (r - l);
    m[5] = 2 / (t - b);
    m[10] = -2 / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    ntglMultMatrixf(m);
}

void ntglBegin(NTGLprimitive p)
{
    NTGLcontext *c = current_context;
    if (!c)
        return;
    if (c->in_begin) {
        set_error(c, NTGL_INVALID_OPERATION);
        return;
    }
    c->in_begin = 1;
    c->primitive = p;
    c->vertex_count = 0;
}

void ntglEnd(void)
{
    NTGLcontext *c = current_context;
    if (!c)
        return;
    if (!c->in_begin) {
        set_error(c, NTGL_INVALID_OPERATION);
        return;
    }
    draw_vertices(c);
    c->in_begin = 0;
    c->vertex_count = 0;
}

void ntglColor4f(float r, float g, float b, float a)
{
    NTGLcontext *c = current_context;
    if (c) {
        c->color[0] = r;
        c->color[1] = g;
        c->color[2] = b;
        c->color[3] = a;
    }
}

void ntglColor3f(float r, float g, float b)
{
    ntglColor4f(r, g, b, 1);
}

void ntglNormal3f(float x, float y, float z)
{
    if (current_context) {
        current_context->normal[0] = x;
        current_context->normal[1] = y;
        current_context->normal[2] = z;
    }
}

void ntglMaterial(const float *ambient, const float *diffuse)
{
    if (!current_context)
        return;
    if (ambient)
        memcpy(current_context->material_ambient, ambient, 4 * sizeof(float));
    if (diffuse)
        memcpy(current_context->material_diffuse, diffuse, 4 * sizeof(float));
}

void ntglMaterialSpecular(const float *color)
{
    if (current_context && color)
        memcpy(current_context->material_specular, color, 4 * sizeof(float));
}

void ntglMaterialShininess(float shininess)
{
    if (current_context)
        current_context->material_shininess = shininess;
}

void ntglLightAmbient(int light, const float *color)
{
    if (current_context && color && light >= 0 && light < 8)
        memcpy(current_context->light_ambient[light], color, 4 * sizeof(float));
}

void ntglLightDiffuse(int light, const float *color)
{
    if (current_context && color && light >= 0 && light < 8)
        memcpy(current_context->light_diffuse[light], color, 4 * sizeof(float));
}

void ntglLightSpecular(int light, const float *color)
{
    if (current_context && color && light >= 0 && light < 8)
        memcpy(current_context->light_specular[light], color, 4 * sizeof(float));
}

void ntglLightPosition(int light, const float *position)
{
    if (current_context && position && light >= 0 && light < 8)
        transform(current_context->light_position[light],
                  current_context->modelview[current_context->modelview_top], position);
}

void ntglLightModelAmbient(const float *color)
{
    if (current_context && color)
        memcpy(current_context->light_model_ambient, color, 4 * sizeof(float));
}

void ntglFogMode(NTGLfogMode mode)
{
    if (current_context)
        current_context->fog_mode = mode;
}

void ntglFogColor(const float *color)
{
    if (current_context && color)
        memcpy(current_context->fog_color, color, 4 * sizeof(float));
}

void ntglFogDensity(float density)
{
    if (current_context)
        current_context->fog_density = density;
}

void ntglFogRange(float start, float end)
{
    if (current_context) {
        current_context->fog_start = start;
        current_context->fog_end = end;
    }
}

void ntglTexCoord2f(float s, float t)
{
    if (current_context) {
        current_context->uv[0] = s;
        current_context->uv[1] = t;
    }
}

static void accumulate_light(NTGLcontext *ctx, int light, const float *eye, const float *normal,
                             float *color)
{
    float direction[3], view[3], half_vector[3], length, diffuse, specular = 0.0f;
    int i;

    if (ctx->light_position[light][3] == 0.0f) {
        memcpy(direction, ctx->light_position[light], 3 * sizeof(float));
    } else {
        for (i = 0; i < 3; ++i)
            direction[i] =
                ctx->light_position[light][i] / ctx->light_position[light][3] - eye[i] / eye[3];
    }
    length = sqrtf(direction[0] * direction[0] + direction[1] * direction[1] +
                   direction[2] * direction[2]);
    if (length > NTGL_EPSILON)
        for (i = 0; i < 3; ++i)
            direction[i] /= length;
    diffuse =
        fmaxf(0.0f, normal[0] * direction[0] + normal[1] * direction[1] + normal[2] * direction[2]);
    if (diffuse > 0.0f) {
        for (i = 0; i < 3; ++i)
            view[i] = -eye[i];
        length = sqrtf(view[0] * view[0] + view[1] * view[1] + view[2] * view[2]);
        if (length > NTGL_EPSILON)
            for (i = 0; i < 3; ++i)
                view[i] /= length;
        for (i = 0; i < 3; ++i)
            half_vector[i] = direction[i] + view[i];
        length = sqrtf(half_vector[0] * half_vector[0] + half_vector[1] * half_vector[1] +
                       half_vector[2] * half_vector[2]);
        if (length > NTGL_EPSILON) {
            float dot;

            for (i = 0; i < 3; ++i)
                half_vector[i] /= length;
            dot = fmaxf(0.0f, normal[0] * half_vector[0] + normal[1] * half_vector[1] +
                                  normal[2] * half_vector[2]);
            specular = powf(dot, ctx->material_shininess);
        }
    }
    for (i = 0; i < 3; ++i) {
        color[i] += ctx->light_ambient[light][i] * ctx->material_ambient[i] +
                    ctx->light_diffuse[light][i] * ctx->material_diffuse[i] * diffuse +
                    ctx->light_specular[light][i] * ctx->material_specular[i] * specular;
    }
}

static void light_vertex(NTGLcontext *ctx, const float *eye, float *color)
{
    const float *m = ctx->modelview[ctx->modelview_top];
    float normal[3], cofactor[9], determinant, length;
    int i, light;

    cofactor[0] = m[5] * m[10] - m[9] * m[6];
    cofactor[1] = m[9] * m[2] - m[1] * m[10];
    cofactor[2] = m[1] * m[6] - m[5] * m[2];
    cofactor[3] = m[8] * m[6] - m[4] * m[10];
    cofactor[4] = m[0] * m[10] - m[8] * m[2];
    cofactor[5] = m[4] * m[2] - m[0] * m[6];
    cofactor[6] = m[4] * m[9] - m[8] * m[5];
    cofactor[7] = m[8] * m[1] - m[0] * m[9];
    cofactor[8] = m[0] * m[5] - m[4] * m[1];
    determinant = m[0] * cofactor[0] + m[4] * cofactor[1] + m[8] * cofactor[2];
    if (fabsf(determinant) > NTGL_EPSILON) {
        normal[0] = (cofactor[0] * ctx->normal[0] + cofactor[1] * ctx->normal[1] +
                     cofactor[2] * ctx->normal[2]) /
                    determinant;
        normal[1] = (cofactor[3] * ctx->normal[0] + cofactor[4] * ctx->normal[1] +
                     cofactor[5] * ctx->normal[2]) /
                    determinant;
        normal[2] = (cofactor[6] * ctx->normal[0] + cofactor[7] * ctx->normal[1] +
                     cofactor[8] * ctx->normal[2]) /
                    determinant;
    } else {
        memcpy(normal, ctx->normal, sizeof(normal));
    }
    if (ctx->enabled & (1u << NTGL_NORMALIZE)) {
        length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        if (length > NTGL_EPSILON)
            for (i = 0; i < 3; ++i)
                normal[i] /= length;
    }
    for (i = 0; i < 3; ++i)
        color[i] = ctx->light_model_ambient[i] * ctx->material_ambient[i];
    for (light = 0; light < 8; ++light)
        if (ctx->enabled & (1u << (NTGL_LIGHT0 + light)))
            accumulate_light(ctx, light, eye, normal, color);
    for (i = 0; i < 3; ++i)
        color[i] = clamp01(color[i]);
    color[3] = ctx->material_diffuse[3];
}

static float fog_factor(NTGLcontext *ctx, const float *eye)
{
    float distance = fabsf(eye[3]) > NTGL_EPSILON ? fabsf(eye[2] / eye[3]) : fabsf(eye[2]);
    float value;

    if (ctx->fog_mode == NTGL_FOG_EXP)
        value = expf(-ctx->fog_density * distance);
    else if (ctx->fog_mode == NTGL_FOG_EXP2) {
        value = ctx->fog_density * distance;
        value = expf(-(value * value));
    } else if (fabsf(ctx->fog_end - ctx->fog_start) > NTGL_EPSILON)
        value = (ctx->fog_end - distance) / (ctx->fog_end - ctx->fog_start);
    else
        value = distance < ctx->fog_end ? 1.0f : 0.0f;
    return clamp01(value);
}

void ntglVertex4f(float x, float y, float z, float w)
{
    NTGLcontext *c = current_context;
    float in[4], eye[4], texture_in[4], texture_out[4];
    Vertex *v;
    if (!c || !c->in_begin)
        return;
    if (c->vertex_count >= MESAGL_MAX_VERTICES) {
        set_error(c, NTGL_OUT_OF_MEMORY);
        return;
    }
    v = &c->vertices[c->vertex_count++];
    in[0] = x;
    in[1] = y;
    in[2] = z;
    in[3] = w;
    transform(eye, c->modelview[c->modelview_top], in);
    transform(v->p, c->projection[c->projection_top], eye);
    v->fog = fog_factor(c, eye);
    if (c->enabled & (1u << NTGL_LIGHTING))
        light_vertex(c, eye, v->c);
    else
        memcpy(v->c, c->color, sizeof(v->c));
    texture_in[0] = c->uv[0];
    texture_in[1] = c->uv[1];
    texture_in[2] = 0.0f;
    texture_in[3] = 1.0f;
    transform(texture_out, c->texture_matrix[c->texture_top], texture_in);
    if (fabsf(texture_out[3]) > NTGL_EPSILON) {
        v->uv[0] = texture_out[0] / texture_out[3];
        v->uv[1] = texture_out[1] / texture_out[3];
    } else {
        v->uv[0] = texture_out[0];
        v->uv[1] = texture_out[1];
    }
}

void ntglVertex3f(float x, float y, float z)
{
    ntglVertex4f(x, y, z, 1);
}

void ntglVertex2f(float x, float y)
{
    ntglVertex4f(x, y, 0, 1);
}

NTGLresult ntglBindTexture(const NTGLtexture *t)
{
    NTGLcontext *c = current_context;
    if (!c)
        return NTGL_INVALID_OPERATION;
    if (!t) {
        c->has_texture = 0;
        return NTGL_OK;
    }
    if (!t->pixels || t->width <= 0 || t->height <= 0 || !bytes_per_pixel(t->format))
        return NTGL_INVALID_ARGUMENT;
    c->texture = *t;
    c->has_texture = 1;
    return NTGL_OK;
}

NTGLresult ntglGetError(void)
{
    NTGLcontext *c = current_context;
    NTGLresult e;
    if (!c)
        return NTGL_INVALID_OPERATION;
    e = c->error;
    c->error = NTGL_OK;
    return e;
}

const char *ntglGetString(void)
{
    return "mesaGL software framebuffer renderer";
}
