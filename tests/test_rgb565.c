#include "GL/gl.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

int main(void)
{
    uint16_t pixels[16 * 16];
    NTGLframebuffer fb = {pixels, 16, 16, 16 * 2, NTGL_RGB565, NTGL_ORIGIN_TOP_LEFT};
    NTGLcontext *context = ntglCreateContext(&fb, NULL);
    uint16_t center;
    GLubyte readback[4];
    if (!context)
        return 1;
    ntglClearColor(0, 0, 1, 1);
    ntglClear(1, 1);
    ntglEnable(NTGL_BLEND);
    ntglBlendFunc(NTGL_SRC_ALPHA, NTGL_ONE_MINUS_SRC_ALPHA);
    ntglBegin(NTGL_TRIANGLES);
    ntglColor4f(1, 0, 0, 0.5f);
    ntglVertex2f(-1, -1);
    ntglVertex2f(1, -1);
    ntglVertex2f(0, 1);
    ntglEnd();
    center = pixels[8 * 16 + 8];
    /* Half red over blue must contain both red and blue in RGB565. */
    if ((center & 0xf800u) == 0 || (center & 0x001fu) == 0)
        return 2;

    ntglDisable(NTGL_BLEND);
    ntglColorMask(1, 0, 0, 0);
    ntglBegin(NTGL_TRIANGLES);
    ntglColor3f(0, 1, 0);
    ntglVertex2f(-1, -1);
    ntglVertex2f(1, -1);
    ntglVertex2f(0, 1);
    ntglEnd();
    center = pixels[8 * 16 + 8];
    /* Only red was writable: green stays zero and the old blue survives. */
    if ((center & 0x07e0u) != 0 || (center & 0x001fu) == 0)
        return 3;

    ntglColorMask(1, 1, 1, 1);
    ntglClearColor(0, 0, 1, 1);
    ntglClear(1, 0);
    ntglEnable(NTGL_BLEND);
    ntglBlendFunc(NTGL_ONE, NTGL_ONE);
    ntglBlendEquationSeparate(NTGL_FUNC_SUBTRACT, NTGL_FUNC_ADD);
    ntglBegin(NTGL_TRIANGLES);
    ntglColor3f(1, 0, 0);
    ntglVertex2f(-1, -1);
    ntglVertex2f(1, -1);
    ntglVertex2f(0, 1);
    ntglEnd();
    center = pixels[8 * 16 + 8];
    /* Red minus blue clamps to red in RGB565. */
    if ((center & 0xf800u) == 0 || (center & 0x001fu) != 0)
        return 4;

    ntglBlendColor(0, 0, 0, 0.25f);
    ntglBlendFunc(NTGL_CONSTANT_ALPHA, NTGL_ONE_MINUS_CONSTANT_ALPHA);
    ntglBlendEquationSeparate(NTGL_FUNC_ADD, NTGL_FUNC_ADD);
    ntglClearColor(0, 0, 1, 1);
    ntglClear(1, 0);
    ntglBegin(NTGL_TRIANGLES);
    ntglColor3f(1, 0, 0);
    ntglVertex2f(-1, -1);
    ntglVertex2f(1, -1);
    ntglVertex2f(0, 1);
    ntglEnd();
    center = pixels[8 * 16 + 8];
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(8, 7, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, readback);
    if (glGetError() != GL_NO_ERROR || readback[0] < 55 || readback[0] > 75 || readback[2] < 180 ||
        readback[2] > 205) {
        ntglDestroyContext(context);
        return 6;
    }
    ntglDestroyContext(context);
    /* One-quarter red over three-quarter blue. */
    return ((center & 0xf800u) != 0 && (center & 0x001fu) != 0 &&
            (center & 0x001fu) > ((center & 0xf800u) >> 11))
               ? 0
               : 5;
}
