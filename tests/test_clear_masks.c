#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

static void draw_fullscreen(float depth, float red, float green, float blue)
{
    glBegin(GL_TRIANGLES);
    glColor4f(red, green, blue, 1.0f);
    glVertex3f(-1.0f, -1.0f, depth);
    glVertex3f(3.0f, -1.0f, depth);
    glVertex3f(-1.0f, 3.0f, depth);
    glEnd();
}

int main(void)
{
    uint8_t pixels[8 * 8 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 8, 8, 8 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLubyte pixel[4];

    if (!context)
        return 1;
    glViewport(0, 0, 8, 8);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE);
    glClearColor(1.0f, 0.5f, 1.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 0 || pixel[1] < 127 || pixel[1] > 129 || pixel[2] != 0 ||
        pixel[3] != 255)
        return 2;

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glClearDepthf(0.25f);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDepthMask(GL_FALSE);
    glClearDepthf(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
    draw_fullscreen(0.0f, 1.0f, 0.0f, 0.0f);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] || pixel[1] || pixel[2] || pixel[3] != 255)
        return 3;

    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    draw_fullscreen(0.0f, 1.0f, 0.0f, 0.0f);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 255 || pixel[1] || pixel[2] || pixel[3] != 255)
        return 4;

    ntglDestroyContext(context);
    return 0;
}
