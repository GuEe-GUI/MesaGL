#include "GL/gl.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <string.h>

static int bytes_per_pixel(NTGLformat format)
{
    if (format == NTGL_RGB565)
        return 2;
    if (format == NTGL_RGB888 || format == NTGL_BGR888)
        return 3;
    return 4;
}

int main(void)
{
    static const NTGLformat formats[] = {NTGL_RGB565,   NTGL_RGB888,   NTGL_BGR888,  NTGL_XRGB8888,
                                         NTGL_ARGB8888, NTGL_RGBA8888, NTGL_BGRA8888};
    unsigned char framebuffer_memory[4 * 4 * 4];
    unsigned char rgba[4], rgb[3];
    int i;

    for (i = 0; i < (int)(sizeof(formats) / sizeof(formats[0])); ++i) {
        NTGLframebuffer framebuffer = {
            framebuffer_memory,  4, 4, 4 * bytes_per_pixel(formats[i]), formats[i],
            NTGL_ORIGIN_TOP_LEFT};
        NTGLcontext *context;
        int opaque = formats[i] == NTGL_RGB565 || formats[i] == NTGL_RGB888 ||
                     formats[i] == NTGL_BGR888 || formats[i] == NTGL_XRGB8888;

        memset(framebuffer_memory, 0, sizeof(framebuffer_memory));
        context = ntglCreateContext(&framebuffer, NULL);
        if (!context)
            return 1;
        glClearColor(0.25f, 0.5f, 0.75f, 0.5f);
        glClear(GL_COLOR_BUFFER_BIT);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glReadPixels(2, 2, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb);
        if (glGetError() != GL_NO_ERROR || rgba[0] < 60 || rgba[0] > 68 || rgba[1] < 124 ||
            rgba[1] > 132 || rgba[2] < 188 || rgba[2] > 196 ||
            (opaque ? rgba[3] != 255 : (rgba[3] < 126 || rgba[3] > 130)) ||
            memcmp(rgb, rgba, 3) != 0) {
            ntglDestroyContext(context);
            return 2 + i;
        }
        ntglDestroyContext(context);
    }
    return 0;
}
