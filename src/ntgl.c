#include "mesaGL/ntgl.h"
#include "mesaGL/config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NTGL_MATRIX_STACK 16
#define NTGL_EPSILON 1.0e-7f

void mesaGLReleaseCurrentContext(void);

typedef struct Vertex {
    float p[4];
    float c[4];
    float uv[2];
    float fog;
} Vertex;

typedef struct ScreenVertex {
    float x, y, z, iw;
    float c[4];
    float uv[2];
    float fog;
} ScreenVertex;

struct NTGLcontext {
    NTGLallocator allocator;
    NTGLframebuffer framebuffer;
    int owns_color;
    float *depth;
    unsigned char *stencil;
    float clear_color[4];
    float clear_depth;
    int viewport[4];
    int scissor[4];
    unsigned enabled;
    NTGLdepthFunc depth_func;
    int depth_mask;
    unsigned color_mask;
    unsigned char clear_stencil, stencil_ref, stencil_value_mask, stencil_write_mask;
    NTGLdepthFunc stencil_func;
    NTGLstencilOp stencil_fail, stencil_depth_fail, stencil_pass;
    NTGLdepthFunc alpha_func;
    float alpha_ref;
    int smooth_shading;
    NTGLblendFactor blend_src, blend_dst, blend_src_alpha, blend_dst_alpha;
    NTGLblendEquation blend_equation_rgb, blend_equation_alpha;
    float blend_color[4];
    int front_ccw, cull_front;
    NTGLpolygonMode polygon_mode[2];
    float point_size, line_width;
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
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
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

static int inside_scissor(const NTGLcontext *ctx, int x, int y)
{
    if (!(ctx->enabled & CAP_SCISSOR))
        return 1;
    return x >= ctx->scissor[0] && y >= ctx->scissor[1] && x < ctx->scissor[0] + ctx->scissor[2] &&
           y < ctx->scissor[1] + ctx->scissor[3];
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
        return fabsf(incoming - stored) < NTGL_EPSILON;
    case NTGL_NOTEQUAL:
        return fabsf(incoming - stored) >= NTGL_EPSILON;
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
    case NTGL_INVERT:
        return (unsigned char)~stored;
    default:
        return stored;
    }
}

static void update_stencil(NTGLcontext *ctx, int index, NTGLstencilOp operation)
{
    unsigned char old = ctx->stencil[index];
    unsigned char value = stencil_op(operation, old, ctx->stencil_ref);
    ctx->stencil[index] = (old & ~ctx->stencil_write_mask) | (value & ctx->stencil_write_mask);
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

static void write_fragment(NTGLcontext *ctx, int x, int y, float z, const float *source)
{
    float output[4], destination[4];
    unsigned char *address;
    int i, index;
    if (x < 0 || y < 0 || x >= ctx->framebuffer.width || y >= ctx->framebuffer.height ||
        !inside_scissor(ctx, x, y))
        return;
    index = y * ctx->framebuffer.width + x;
    if ((ctx->enabled & (1u << NTGL_ALPHA_TEST)) &&
        !depth_pass(ctx->alpha_func, source[3], ctx->alpha_ref))
        return;
    if (ctx->enabled & (1u << NTGL_STENCIL_TEST)) {
        unsigned ref = ctx->stencil_ref & ctx->stencil_value_mask;
        unsigned value = ctx->stencil[index] & ctx->stencil_value_mask;
        if (!depth_pass(ctx->stencil_func, (float)ref, (float)value)) {
            update_stencil(ctx, index, ctx->stencil_fail);
            return;
        }
    }
    if ((ctx->enabled & CAP_DEPTH) && !depth_pass(ctx->depth_func, z, ctx->depth[index])) {
        if (ctx->enabled & (1u << NTGL_STENCIL_TEST))
            update_stencil(ctx, index, ctx->stencil_depth_fail);
        return;
    }
    if (ctx->enabled & (1u << NTGL_STENCIL_TEST))
        update_stencil(ctx, index, ctx->stencil_pass);
    if ((ctx->enabled & CAP_DEPTH) && ctx->depth_mask)
        ctx->depth[index] = z;
    address = pixel_address(&ctx->framebuffer, x, y);
    if (ctx->enabled & CAP_BLEND) {
        unpack_pixel(address, ctx->framebuffer.format, destination);
        for (i = 0; i < 3; ++i) {
            float s = source[i] * blend_factor(ctx, ctx->blend_src, source, destination, i);
            float d = destination[i] * blend_factor(ctx, ctx->blend_dst, source, destination, i);
            output[i] = blend(ctx->blend_equation_rgb, s, d);
        }
        output[3] =
            blend(ctx->blend_equation_alpha,
                  source[3] * blend_factor(ctx, ctx->blend_src_alpha, source, destination, 3),
                  destination[3] * blend_factor(ctx, ctx->blend_dst_alpha, source, destination, 3));
    } else
        memcpy(output, source, sizeof(output));
    if (ctx->color_mask != 0xfu) {
        unpack_pixel(address, ctx->framebuffer.format, destination);
        for (i = 0; i < 4; ++i)
            if (!(ctx->color_mask & (1u << i)))
                output[i] = destination[i];
    }
    pack_pixel(address, ctx->framebuffer.format, output);
}

static void sample_texture(const NTGLtexture *texture, float s, float t, float *color)
{
    float fx, fy;
    int x0, y0, x1, y1, i, stride;
    float c00[4], c10[4], c01[4], c11[4], ax, ay;
    if (texture->wrap_s == NTGL_REPEAT)
        s -= floorf(s);
    else
        s = clamp01(s);
    if (texture->wrap_t == NTGL_REPEAT)
        t -= floorf(t);
    else
        t = clamp01(t);
    if (texture->origin == NTGL_ORIGIN_TOP_LEFT)
        t = 1.0f - t;
    fx = s * (texture->width - 1);
    fy = t * (texture->height - 1);
    if (texture->filter == NTGL_NEAREST) {
        x0 = (int)floorf(fx + 0.5f);
        y0 = (int)floorf(fy + 0.5f);
        stride =
            texture->stride ? texture->stride : texture->width * bytes_per_pixel(texture->format);
        unpack_pixel((const unsigned char *)texture->pixels + y0 * stride +
                         x0 * bytes_per_pixel(texture->format),
                     texture->format, color);
        return;
    }
    x0 = (int)floorf(fx);
    y0 = (int)floorf(fy);
    x1 = x0 + 1;
    x1 = x1 >= texture->width ? x0 : x1;
    y1 = y0 + 1;
    y1 = y1 >= texture->height ? y0 : y1;
    stride = texture->stride ? texture->stride : texture->width * bytes_per_pixel(texture->format);
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
    ax = fx - x0;
    ay = fy - y0;
    for (i = 0; i < 4; ++i)
        color[i] =
            (c00[i] * (1 - ax) + c10[i] * ax) * (1 - ay) + (c01[i] * (1 - ax) + c11[i] * ax) * ay;
}

static float edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static int to_screen(const NTGLcontext *ctx, const Vertex *v, ScreenVertex *s)

{
    int i;
    if (fabsf(v->p[3]) < NTGL_EPSILON)
        return 0;
    s->iw = 1.0f / v->p[3];
    s->x = ctx->viewport[0] + (v->p[0] * s->iw * 0.5f + 0.5f) * ctx->viewport[2];
    s->y = ctx->viewport[1] + (v->p[1] * s->iw * 0.5f + 0.5f) * ctx->viewport[3];
    s->z = v->p[2] * s->iw * 0.5f + 0.5f;
    for (i = 0; i < 4; ++i)
        s->c[i] = v->c[i];
    s->uv[0] = v->uv[0];
    s->uv[1] = v->uv[1];
    s->fog = v->fog;
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

static void raster_screen_line(NTGLcontext *ctx, const ScreenVertex *a, const ScreenVertex *b)
{
    float dx = b->x - a->x, dy = b->y - a->y;
    int steps = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
    int width = (int)ceilf(ctx->line_width), i, channel, px, py;
    int offset = (int)floorf((ctx->line_width - 1.0f) * 0.5f);

    if (steps < 1)
        steps = 1;
    for (i = 0; i <= steps; ++i) {
        float t = (float)i / steps, color[4];

        for (channel = 0; channel < 4; ++channel)
            color[channel] = ctx->smooth_shading
                                 ? a->c[channel] + (b->c[channel] - a->c[channel]) * t
                                 : b->c[channel];
        apply_fog(ctx, a->fog + (b->fog - a->fog) * t, color);
        px = (int)floorf(a->x + dx * t) - offset;
        py = (int)floorf(a->y + dy * t) - offset;
        for (channel = 0; channel < width; ++channel) {
            int k;

            for (k = 0; k < width; ++k)
                write_fragment(ctx, px + k, py + channel, a->z + (b->z - a->z) * t, color);
        }
    }
}

static void raster_screen_point(NTGLcontext *ctx, const ScreenVertex *vertex)
{
    float color[4];
    int size = (int)ceilf(ctx->point_size), x, y;
    int x0 = (int)floorf(vertex->x - ctx->point_size * 0.5f + 0.5f);
    int y0 = (int)floorf(vertex->y - ctx->point_size * 0.5f + 0.5f);

    memcpy(color, vertex->c, sizeof(color));
    apply_fog(ctx, vertex->fog, color);
    for (y = 0; y < size; ++y)
        for (x = 0; x < size; ++x)
            write_fragment(ctx, x0 + x, y0 + y, vertex->z, color);
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
    if ((ctx->enabled & CAP_CULL) && front == ctx->cull_front)
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
        float area, minxf, maxxf, minyf, maxyf;
        int minx, maxx, miny, maxy, x, y;
        a = screen[0];
        b = screen[tri];
        c = screen[tri + 1];
        area = edge(a.x, a.y, b.x, b.y, c.x, c.y);
        if (fabsf(area) < NTGL_EPSILON)
            continue;
        minxf = fminf(a.x, fminf(b.x, c.x));
        maxxf = fmaxf(a.x, fmaxf(b.x, c.x));
        minyf = fminf(a.y, fminf(b.y, c.y));
        maxyf = fmaxf(a.y, fmaxf(b.y, c.y));
        minx = (int)floorf(minxf);
        maxx = (int)ceilf(maxxf);
        miny = (int)floorf(minyf);
        maxy = (int)ceilf(maxyf);
        for (y = miny; y <= maxy; ++y)
            for (x = minx; x <= maxx; ++x) {
                float w0 = edge(b.x, b.y, c.x, c.y, x + 0.5f, y + 0.5f) / area,
                      w1 = edge(c.x, c.y, a.x, a.y, x + 0.5f, y + 0.5f) / area, w2 = 1 - w0 - w1,
                      den, color[4], tex[4], u, v, fog;
                int i;
                if (w0 < -NTGL_EPSILON || w1 < -NTGL_EPSILON || w2 < -NTGL_EPSILON)
                    continue;
                den = w0 * a.iw + w1 * b.iw + w2 * c.iw;
                if (fabsf(den) < NTGL_EPSILON)
                    continue;
                for (i = 0; i < 4; ++i)
                    color[i] =
                        ctx->smooth_shading
                            ? (w0 * a.c[i] * a.iw + w1 * b.c[i] * b.iw + w2 * c.c[i] * c.iw) / den
                            : c.c[i];
                if ((ctx->enabled & CAP_TEXTURE) && ctx->has_texture) {
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
                fog = (w0 * a.fog * a.iw + w1 * b.fog * b.iw + w2 * c.fog * c.iw) / den;
                apply_fog(ctx, fog, color);
                write_fragment(ctx, x, y, w0 * a.z + w1 * b.z + w2 * c.z, color);
            }
    }
}

static void raster_line(NTGLcontext *ctx, const Vertex *va, const Vertex *vb)
{
    Vertex ca = *va, cb = *vb;
    ScreenVertex a, b;
    if (!clip_line(&ca, &cb) || !to_screen(ctx, &ca, &a) || !to_screen(ctx, &cb, &b))
        return;
    raster_screen_line(ctx, &a, &b);
}

static void draw_vertices(NTGLcontext *ctx)
{
    int i, n = ctx->vertex_count;
    Vertex *v = ctx->vertices;
    switch (ctx->primitive) {
    case NTGL_POINTS:
        for (i = 0; i < n; ++i) {
            ScreenVertex s;
            if (vertex_inside(&v[i]) && to_screen(ctx, &v[i], &s))
                write_fragment(ctx, (int)s.x, (int)s.y, s.z, s.c);
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
    if (fb->width <= 0 || fb->height <= 0 || !bpp)
        return 0;
    if (!pixels) {
        color_size = (size_t)fb->width * fb->height * bpp;
        pixels = ctx->allocator.alloc(ctx->allocator.user, color_size);
        if (!pixels)
            return 0;
        ctx->owns_color = 1;
    }
    ctx->framebuffer = *fb;
    ctx->framebuffer.pixels = pixels;
    if (!ctx->framebuffer.stride)
        ctx->framebuffer.stride = fb->width * bpp;
    return 1;
}

static int allocate_depth(NTGLcontext *ctx)
{
    size_t i, count;
    if (ctx->depth)
        return 1;
    count = (size_t)ctx->framebuffer.width * ctx->framebuffer.height;
    ctx->depth = (float *)ctx->allocator.alloc(ctx->allocator.user, count * sizeof(float));
    if (!ctx->depth)
        return 0;
    for (i = 0; i < count; ++i)
        ctx->depth[i] = ctx->clear_depth;
    return 1;
}

static int allocate_stencil(NTGLcontext *ctx)
{
    size_t count = (size_t)ctx->framebuffer.width * ctx->framebuffer.height;
    if (ctx->stencil)
        return 1;
    ctx->stencil = (unsigned char *)ctx->allocator.alloc(ctx->allocator.user, count);
    if (!ctx->stencil)
        return 0;
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
    ctx->depth_func = NTGL_LESS;
    ctx->depth_mask = 1;
    ctx->color_mask = 0xfu;
    ctx->stencil_value_mask = ctx->stencil_write_mask = 0xffu;
    ctx->stencil_func = NTGL_ALWAYS;
    ctx->stencil_fail = ctx->stencil_depth_fail = ctx->stencil_pass = NTGL_KEEP;
    ctx->alpha_func = NTGL_ALWAYS;
    ctx->smooth_shading = 1;
    ctx->blend_src = ctx->blend_src_alpha = NTGL_SRC_ALPHA;
    ctx->blend_dst = ctx->blend_dst_alpha = NTGL_ONE_MINUS_SRC_ALPHA;
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
    ctx->allocator.free(ctx->allocator.user, ctx->depth);
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
    if (!ctx || !fb || !fb->pixels)
        return NTGL_INVALID_ARGUMENT;
    old_depth = ctx->depth;
    old_stencil = ctx->stencil;
    old_color = ctx->framebuffer.pixels;
    old_owned = ctx->owns_color;
    ctx->depth = NULL;
    ctx->stencil = NULL;
    ctx->owns_color = 0;
    if (!allocate_buffers(ctx, fb)) {
        ctx->depth = old_depth;
        ctx->stencil = old_stencil;
        ctx->framebuffer.pixels = old_color;
        ctx->owns_color = old_owned;
        return NTGL_OUT_OF_MEMORY;
    }
    if (old_owned)
        ctx->allocator.free(ctx->allocator.user, old_color);
    ctx->allocator.free(ctx->allocator.user, old_depth);
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
        ctx->depth = NULL;
        ctx->stencil = NULL;
        ctx->owns_color = 0;
        if (!allocate_buffers(ctx, &fb)) {
            ctx->depth = old_depth;
            ctx->stencil = old_stencil;
            ctx->framebuffer.pixels = old_color;
            ctx->owns_color = 1;
            return NTGL_OUT_OF_MEMORY;
        }
        ctx->allocator.free(ctx->allocator.user, old_depth);
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
    int x, y;
    if (!c)
        return;
    if (depth && !allocate_depth(c)) {
        set_error(c, NTGL_OUT_OF_MEMORY);
        return;
    }
    for (y = 0; y < c->framebuffer.height; ++y)
        for (x = 0; x < c->framebuffer.width; ++x)
            if (inside_scissor(c, x, y)) {
                int i = y * c->framebuffer.width + x;
                if (color)
                    pack_pixel(pixel_address(&c->framebuffer, x, y), c->framebuffer.format,
                               c->clear_color);
                if (depth)
                    c->depth[i] = c->clear_depth;
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
    if (!c || !allocate_stencil(c))
        return;
    for (y = 0; y < c->framebuffer.height; ++y)
        for (x = 0; x < c->framebuffer.width; ++x)
            if (inside_scissor(c, x, y)) {
                int index = y * c->framebuffer.width + x;
                unsigned char old = c->stencil[index];
                c->stencil[index] =
                    (old & ~c->stencil_write_mask) | (c->clear_stencil & c->stencil_write_mask);
            }
}

void ntglStencilFunc(NTGLdepthFunc function, unsigned reference, unsigned mask)
{
    if (current_context) {
        current_context->stencil_func = function;
        current_context->stencil_ref = (unsigned char)reference;
        current_context->stencil_value_mask = (unsigned char)mask;
    }
}

void ntglStencilMask(unsigned mask)
{
    if (current_context)
        current_context->stencil_write_mask = (unsigned char)mask;
}

void ntglStencilOp(NTGLstencilOp fail, NTGLstencilOp depth_fail, NTGLstencilOp pass)
{
    if (current_context) {
        current_context->stencil_fail = fail;
        current_context->stencil_depth_fail = depth_fail;
        current_context->stencil_pass = pass;
    }
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
