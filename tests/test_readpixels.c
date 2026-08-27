#include "GL/gl.h"
#include "mesaGL/ntgl.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static int bytes_per_pixel(NTGLformat format)
{
    if (format == NTGL_RGB565 || format == NTGL_RGBA4444 || format == NTGL_RGBA5551)
        return 2;
    if (format == NTGL_RGB888 || format == NTGL_BGR888)
        return 3;
    return 4;
}

static int color_matches(const unsigned char *rgba, NTGLformat format)
{
    int alpha_matches;

    if (format == NTGL_RGB565 || format == NTGL_RGB888 || format == NTGL_BGR888 ||
        format == NTGL_XRGB8888)
        alpha_matches = rgba[3] == 255;
    else if (format == NTGL_RGBA5551)
        alpha_matches = rgba[3] == 0 || rgba[3] == 255;
    else
        alpha_matches = rgba[3] >= 118 && rgba[3] <= 138;
    return rgba[0] >= 58 && rgba[0] <= 70 && rgba[1] >= 118 && rgba[1] <= 138 &&
           rgba[2] >= 180 && rgba[2] <= 198 && alpha_matches;
}

static int test_format(NTGLformat format, NTGLorigin origin, int negative_stride)
{
    unsigned char framebuffer_memory[4 * 4 * 4];
    unsigned char packed[16];
    unsigned char clipped[3 * 16];
    int stride = 4 * bytes_per_pixel(format);
    NTGLframebuffer framebuffer = {
        negative_stride ? framebuffer_memory + 3 * stride : framebuffer_memory,
        4,
        4,
        negative_stride ? -stride : stride,
        format,
        origin,
    };
    NTGLcontext *context;
    int row;

    memset(framebuffer_memory, 0, sizeof(framebuffer_memory));
    context = ntglCreateContext(&framebuffer, NULL);
    if (!context)
        return 1;
    glClearColor(0.25f, 0.5f, 0.75f, 0.5f);
    glClear(GL_COLOR_BUFFER_BIT);

    memset(packed, 0xa5, sizeof(packed));
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glReadPixels(2, 2, 1, 2, GL_RGBA, GL_UNSIGNED_BYTE, packed);
    if (glGetError() != GL_NO_ERROR || !color_matches(packed, format) ||
        !color_matches(packed + 8, format))
        goto fail;
    if (packed[4] != 0xa5 || packed[5] != 0xa5 || packed[6] != 0xa5 || packed[7] != 0xa5 ||
        packed[12] != 0xa5 || packed[13] != 0xa5 || packed[14] != 0xa5 || packed[15] != 0xa5)
        goto fail;

    memset(clipped, 0xa5, sizeof(clipped));
    glReadPixels(-1, -1, 3, 3, GL_RGBA, GL_UNSIGNED_BYTE, clipped);
    if (glGetError() != GL_NO_ERROR)
        goto fail;
    for (row = 0; row < 3; ++row) {
        int column;

        for (column = 0; column < 3; ++column) {
            const unsigned char *pixel = clipped + row * 16 + column * 4;

            if ((row == 0 || column == 0) ? pixel[0] != 0xa5
                                          : !color_matches(pixel, format))
                goto fail;
        }
        if (clipped[row * 16 + 12] != 0xa5)
            goto fail;
    }

    glReadPixels(0, 0, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, packed);
    if (glGetError() != GL_INVALID_OPERATION)
        goto fail;
    glReadPixels(0, 0, 1, 1, GL_RGBA, 0xdead, packed);
    if (glGetError() != GL_INVALID_OPERATION)
        goto fail;
    glReadPixels(0, 0, -1, 1, 0xdead, 0xdead, packed);
    if (glGetError() != GL_INVALID_VALUE)
        goto fail;
    glReadPixels(0, 0, -1, 1, GL_RGBA, GL_UNSIGNED_BYTE, packed);
    if (glGetError() != GL_INVALID_VALUE)
        goto fail;
    glReadPixels(0, 0, 0, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    if (glGetError() != GL_NO_ERROR)
        goto fail;
    ntglDestroyContext(context);
    return 0;

fail:
    ntglDestroyContext(context);
    return 1;
}

static int test_framebuffer_validation(void)
{
    unsigned char memory[64];
    NTGLframebuffer framebuffer = {
        memory, 4, 4, 15, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT,
    };
    if (ntglCreateContext(&framebuffer, NULL))
        return 1;
    framebuffer.stride = -15;
    if (ntglCreateContext(&framebuffer, NULL))
        return 2;
    framebuffer.stride = 16;
    framebuffer.origin = (NTGLorigin)2;
    if (ntglCreateContext(&framebuffer, NULL))
        return 3;
    framebuffer.origin = NTGL_ORIGIN_BOTTOM_LEFT;
    framebuffer.width = INT_MAX;
    if (ntglCreateContext(&framebuffer, NULL))
        return 4;
    return 0;
}

static int test_nonfinite_clear(void)
{
    unsigned char memory[4] = {0};
    unsigned char pixel[4];
    NTGLframebuffer framebuffer = {
        memory, 1, 1, 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT,
    };
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);

    if (!context)
        return 1;
    glDisable(GL_DITHER);
    glClearColor(NAN, INFINITY, -INFINITY, NAN);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    ntglDestroyContext(context);
    return pixel[0] != 0 || pixel[1] != 255 || pixel[2] != 0 || pixel[3] != 0;
}

int main(void)
{
    static const NTGLformat formats[] = {
        NTGL_RGB565,   NTGL_RGBA4444, NTGL_RGBA5551, NTGL_RGB888,  NTGL_BGR888,
        NTGL_XRGB8888, NTGL_ARGB8888, NTGL_RGBA8888, NTGL_BGRA8888,
    };
    int negative_stride;
    int origin;
    int i;

    if (test_framebuffer_validation())
        return 50;
    if (test_nonfinite_clear())
        return 51;
    for (negative_stride = 0; negative_stride <= 1; ++negative_stride)
        for (origin = NTGL_ORIGIN_BOTTOM_LEFT; origin <= NTGL_ORIGIN_TOP_LEFT; ++origin)
            for (i = 0; i < (int)(sizeof(formats) / sizeof(formats[0])); ++i)
                if (test_format(formats[i], (NTGLorigin)origin, negative_stride))
                    return 1 + negative_stride * 20 + origin * 10 + i;
    return 0;
}
