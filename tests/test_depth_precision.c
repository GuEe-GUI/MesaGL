#include "GL/gl.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

static void draw_fullscreen(float depth, float red, float green)
{
    glColor3f(red, green, 0.0f);
    glBegin(GL_TRIANGLES);
    glVertex3f(-1.0f, -1.0f, depth);
    glVertex3f(3.0f, -1.0f, depth);
    glVertex3f(-1.0f, 3.0f, depth);
    glEnd();
}

int main(void)
{
    uint32_t pixels[16 * 16] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 16, 16, 16 * (int)sizeof(*pixels), NTGL_XRGB8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    unsigned char pixel[4];

    if (!context)
        return 1;
    glViewport(0, 0, 16, 16);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Both values map to the same DEPTH_COMPONENT16 storage value. */
    draw_fullscreen(-0.2f, 1.0f, 0.0f);
    draw_fullscreen(-0.200002f, 0.0f, 1.0f);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 240 || pixel[1] > 15 || glGetError() != GL_NO_ERROR) {
        ntglDestroyContext(context);
        return 2;
    }

    /* A separation larger than one 16-bit depth unit must remain visible. */
    draw_fullscreen(-0.2001f, 0.0f, 1.0f);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] > 15 || pixel[1] < 240 || glGetError() != GL_NO_ERROR) {
        ntglDestroyContext(context);
        return 3;
    }

    /* EQUAL compares stored depth values exactly, without a floating epsilon. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDepthFunc(GL_ALWAYS);
    draw_fullscreen(0.0f, 1.0f, 0.0f);
    glDepthFunc(GL_EQUAL);
    draw_fullscreen(0.00004f, 0.0f, 1.0f);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 240 || pixel[1] > 15 || glGetError() != GL_NO_ERROR) {
        ntglDestroyContext(context);
        return 4;
    }

    glDepthFunc(GL_NOTEQUAL);
    draw_fullscreen(0.00004f, 0.0f, 1.0f);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] > 15 || pixel[1] < 240 || glGetError() != GL_NO_ERROR) {
        ntglDestroyContext(context);
        return 5;
    }

    ntglDestroyContext(context);
    return 0;
}
