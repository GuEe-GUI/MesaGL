#include "GL/gl.h"
#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

static void left_triangle(void)
{
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.9f, -0.8f);
    glVertex2f(-0.1f, -0.8f);
    glVertex2f(-0.5f, 0.8f);
    glEnd();
}

static void right_triangle(void)
{
    glBegin(GL_TRIANGLES);
    glVertex2f(0.1f, -0.8f);
    glVertex2f(0.5f, 0.8f);
    glVertex2f(0.9f, -0.8f);
    glEnd();
}

static void left_line(void)
{
    glBegin(GL_LINES);
    glVertex2f(-0.9f, -0.2f);
    glVertex2f(-0.1f, -0.2f);
    glEnd();
}

int main(void)
{
    uint8_t pixels[64 * 64 * 4] = {0};
    uint8_t left[4], right[4];
    NTGLframebuffer framebuffer = {pixels, 64, 64, 64 * 4, NTGL_RGBA8888,
                                   NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLint value;

    if (!context)
        return 1;
    glViewport(0, 0, 64, 64);
    glEnable(GL_STENCIL_TEST);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFuncSeparate(GL_FRONT, GL_ALWAYS, 1, 0xff);
    glStencilFuncSeparate(GL_BACK, GL_ALWAYS, 2, 0xff);
    glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_REPLACE);
    glGetIntegerv(GL_STENCIL_REF, &value);
    if (value != 1)
        return 6;
    glGetIntegerv(GL_STENCIL_BACK_REF, &value);
    if (value != 2)
        return 7;
    glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_PASS, &value);
    if (value != GL_REPLACE)
        return 8;
    left_triangle();
    right_triangle();

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilOpSeparate(GL_FRONT_AND_BACK, GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilFuncSeparate(GL_FRONT, GL_EQUAL, 1, 0xff);
    glStencilFuncSeparate(GL_BACK, GL_EQUAL, 2, 0xff);
    glColor3f(0.0f, 1.0f, 0.0f);
    left_triangle();
    right_triangle();

    glStencilFuncSeparate(GL_FRONT, GL_EQUAL, 2, 0xff);
    glStencilFuncSeparate(GL_BACK, GL_EQUAL, 1, 0xff);
    glColor3f(1.0f, 0.0f, 0.0f);
    left_triangle();
    right_triangle();
    glReadPixels(16, 28, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);
    glReadPixels(48, 28, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, right);
    if (left[1] < 240 || left[0] > 8 || right[1] < 240 || right[0] > 8)
        return 2;

    glClearStencil(255);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 0, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR_WRAP);
    left_triangle();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilFunc(GL_EQUAL, 0, 0xff);
    glColor3f(0.0f, 1.0f, 0.0f);
    left_triangle();
    glReadPixels(16, 28, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);
    if (left[1] < 240 || left[0] > 8)
        return 3;

    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 0, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_DECR_WRAP);
    right_triangle();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilFunc(GL_EQUAL, 255, 0xff);
    glColor3f(0.0f, 1.0f, 0.0f);
    right_triangle();
    glReadPixels(48, 28, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, right);
    if (right[1] < 240 || right[0] > 8)
        return 4;

    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 300, 0xff);
    glGetIntegerv(GL_STENCIL_REF, &value);
    if (value != 255)
        return 9;
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    left_triangle();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilFunc(GL_EQUAL, 255, 0xff);
    glColor3f(1.0f, 0.0f, 0.0f);
    left_triangle();
    glReadPixels(16, 28, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);
    if (left[0] < 240 || left[1] > 8)
        return 5;

    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFuncSeparate(GL_FRONT, GL_ALWAYS, 3, 0xff);
    glStencilFuncSeparate(GL_BACK, GL_NEVER, 7, 0xff);
    glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_KEEP);
    right_triangle();
    glLineWidth(3.0f);
    left_line();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFuncSeparate(GL_FRONT, GL_EQUAL, 3, 0xff);
    glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_KEEP);
    glColor3f(0.0f, 1.0f, 0.0f);
    left_line();
    glReadPixels(16, 25, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);
    if (left[1] < 240 || left[0] > 8)
        return 10;

    glStencilMask(0xff);
    glClearStencil(255);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 0, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    left_triangle();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilFunc(GL_EQUAL, 255, 0xff);
    glColor3f(0.0f, 1.0f, 0.0f);
    left_triangle();
    glReadPixels(16, 28, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);
    if (left[1] < 240 || left[0] > 8)
        return 11;

    glClearStencil(0xa5);
    glStencilMask(0xff);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glStencilMask(0x0f);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 0, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
    left_triangle();
    glStencilMask(0xff);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilFunc(GL_EQUAL, 0xa0, 0xff);
    glColor3f(0.0f, 1.0f, 0.0f);
    left_triangle();
    glReadPixels(16, 28, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);
    if (left[1] < 240 || left[0] > 8)
        return 12;
    ntglDestroyContext(context);
    return 0;
}
