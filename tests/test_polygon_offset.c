#include "GL/gl.h"
#include "mesaGL/ntgl.h"

#include <math.h>
#include <stdint.h>

static void triangle(float red, float green, float blue)
{
    glColor3f(red, green, blue);
    glBegin(GL_TRIANGLES);
    glVertex3f(-0.9f, -0.9f, -0.4f);
    glVertex3f(0.9f, -0.9f, 0.4f);
    glVertex3f(0.0f, 0.9f, 0.0f);
    glEnd();
}

static int center_is(const unsigned char rgba[4], int red, int green)
{
    return (red ? rgba[0] > 240 : rgba[0] < 16) &&
           (green ? rgba[1] > 240 : rgba[1] < 16) && rgba[2] < 16;
}

int main(void)
{
    uint32_t pixels[64 * 64] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 64, 64, 64 * (int)sizeof(*pixels), NTGL_XRGB8888, NTGL_ORIGIN_TOP_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    unsigned char rgba[4];
    GLfloat factor = 0.0f;
    GLfloat units = 0.0f;

    if (!context)
        return 1;
    glViewport(0, 0, 64, 64);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    triangle(1.0f, 0.0f, 0.0f);
    triangle(0.0f, 1.0f, 0.0f);
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (!center_is(rgba, 1, 0)) {
        ntglDestroyContext(context);
        return 2;
    }

    glPolygonOffset(-1.0f, -2.0f);
    glEnable(GL_POLYGON_OFFSET_FILL);
    triangle(0.0f, 1.0f, 0.0f);
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (!center_is(rgba, 0, 1)) {
        ntglDestroyContext(context);
        return 3;
    }

    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &factor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &units);
    if (fabsf(factor + 1.0f) > 0.0001f || fabsf(units + 2.0f) > 0.0001f ||
        !glIsEnabled(GL_POLYGON_OFFSET_FILL) || glGetError() != GL_NO_ERROR) {
        ntglDestroyContext(context);
        return 4;
    }

    ntglDestroyContext(context);
    return 0;
}
