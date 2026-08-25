#define _POSIX_C_SOURCE 199309L
#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"
#include <stdio.h>
#include <time.h>

static void quad(float l, float b, float r, float t)
{
    glBegin(GL_QUADS);
    glVertex2f(l, b);
    glVertex2f(r, b);
    glVertex2f(r, t);
    glVertex2f(l, t);
    glEnd();
}

int main(void)
{
    static const struct timespec delay = {0, 16666667};
    MesaGLX11 *x11 =
        mesaGLX11Create(640, 480, "MesaGL stencil test: green only inside diamond mask");
    MesaGLPortContext *context;
    if (!x11)
        return 1;
    context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11));
    if (!context)
        return 2;
    glViewport(0, 0, 640, 480);
    while (mesaGLX11PollEvents(x11)) {
        glClearColor(0.05f, 0.07f, 0.12f, 1);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glEnable(GL_STENCIL_TEST);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glStencilFunc(GL_ALWAYS, 1, 0xff);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(0, 0.8f);
        glVertex2f(0.8f, 0);
        glVertex2f(0, -0.8f);
        glVertex2f(-0.8f, 0);
        glEnd();
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilFunc(GL_EQUAL, 1, 0xff);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glColor3f(0.1f, 0.9f, 0.3f);
        quad(-0.95f, -0.65f, 0.95f, 0.65f);
        glDisable(GL_STENCIL_TEST);
        mesaGLPortPresent(context);
        nanosleep(&delay, NULL);
    }
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
