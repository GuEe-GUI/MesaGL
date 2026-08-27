#include "GL/gl.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <string.h>

static void draw_gray(void)
{
    glColor4f(0.5f, 0.5f, 0.5f, 0.5f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(3.0f, -1.0f);
    glVertex2f(-1.0f, 3.0f);
    glEnd();
}

static int count_rgb565_levels(const uint16_t *pixels, uint16_t low, uint16_t high)
{
    int low_count = 0;
    int high_count = 0;
    int i;

    for (i = 0; i < 16; ++i) {
        if (pixels[i] == low)
            ++low_count;
        else if (pixels[i] == high)
            ++high_count;
        else
            return 0;
    }
    return low_count == 8 && high_count == 8;
}

static int test_packed_format(NTGLformat format, uint16_t low, uint16_t high)
{
    uint16_t pixels[16];
    NTGLframebuffer framebuffer = {
        pixels, 4, 4, 4 * (int)sizeof(uint16_t), format, NTGL_ORIGIN_BOTTOM_LEFT,
    };
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    int result;

    if (!context)
        return 0;
    memset(pixels, 0, sizeof(pixels));
    draw_gray();
    result = count_rgb565_levels(pixels, low, high);
    ntglDestroyContext(context);
    return result;
}

int main(void)
{
    uint16_t pixels[16];
    NTGLframebuffer framebuffer = {
        pixels, 4, 4, 4 * (int)sizeof(uint16_t), NTGL_RGB565, NTGL_ORIGIN_BOTTOM_LEFT,
    };
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLubyte readback[16 * 4];
    uint16_t undithered;
    int low_count;
    int high_count;
    int i;

    if (!context)
        return 1;
    if (glIsEnabled(GL_DITHER) != GL_TRUE) {
        ntglDestroyContext(context);
        return 2;
    }

    glDisable(GL_DITHER);
    memset(pixels, 0, sizeof(pixels));
    draw_gray();
    undithered = pixels[0];
    for (i = 1; i < 16; ++i)
        if (pixels[i] != undithered) {
            ntglDestroyContext(context);
            return 3;
        }
    if (undithered != 0x8410u) {
        ntglDestroyContext(context);
        return 4;
    }

    glEnable(GL_DITHER);
    memset(pixels, 0, sizeof(pixels));
    draw_gray();
    if (!count_rgb565_levels(pixels, 0x7befu, 0x8410u)) {
        ntglDestroyContext(context);
        return 5;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, readback);
    low_count = 0;
    high_count = 0;
    for (i = 0; i < 16; ++i) {
        if (readback[i * 4] == 123 && readback[i * 4 + 1] == 125 &&
            readback[i * 4 + 2] == 123 && readback[i * 4 + 3] == 255)
            ++low_count;
        else if (readback[i * 4] == 131 && readback[i * 4 + 1] == 129 &&
                 readback[i * 4 + 2] == 131 && readback[i * 4 + 3] == 255)
            ++high_count;
        else {
            ntglDestroyContext(context);
            return 6;
        }
    }
    if (low_count != 8 || high_count != 8) {
        ntglDestroyContext(context);
        return 7;
    }

    glClearColor(0.5f, 0.5f, 0.5f, 0.5f);
    glClear(GL_COLOR_BUFFER_BIT);
    for (i = 1; i < 16; ++i)
        if (pixels[i] != pixels[0]) {
            ntglDestroyContext(context);
            return 8;
        }

    ntglDestroyContext(context);
    if (glGetError() != GL_NO_ERROR)
        return 9;
    if (!test_packed_format(NTGL_RGBA4444, 0x7777u, 0x8888u))
        return 10;
    if (!test_packed_format(NTGL_RGBA5551, 0x7bdeu, 0x8421u))
        return 11;
    return 0;
}
