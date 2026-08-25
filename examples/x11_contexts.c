#define _POSIX_C_SOURCE 199309L
#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"
#include <time.h>

static void render(MesaGLPortContext *context, float r, float g, float b, float offset)
{
    mesaGLPortMakeCurrent(context);
    glClearColor(r * 0.15f, g * 0.15f, b * 0.15f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(r, g, b);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.8f + offset, -0.7f);
    glVertex2f(0.8f + offset, -0.7f);
    glVertex2f(offset, 0.8f);
    glEnd();
    mesaGLPortPresent(context);
}

int main(void)
{
    static const struct timespec delay = {0, 16666667};
    MesaGLX11 *a = mesaGLX11Create(400, 300, "MesaGL context A: red");
    MesaGLX11 *b = mesaGLX11Create(400, 300, "MesaGL context B: cyan");
    MesaGLPortContext *ca, *cb;
    if (!a || !b)
        return 1;
    ca = mesaGLPortCreate(mesaGLX11GetPortConfig(a));
    cb = mesaGLPortCreate(mesaGLX11GetPortConfig(b));
    if (!ca || !cb)
        return 2;
    mesaGLPortMakeCurrent(ca);
    glViewport(0, 0, 400, 300);
    mesaGLPortMakeCurrent(cb);
    glViewport(0, 0, 400, 300);
    while (mesaGLX11PollEvents(a) && mesaGLX11PollEvents(b)) {
        render(ca, 1, 0.1f, 0.1f, -0.08f);
        render(cb, 0.1f, 0.9f, 1, 0.08f);
        nanosleep(&delay, 0);
    }
    mesaGLPortDestroy(ca);
    mesaGLPortDestroy(cb);
    mesaGLX11Destroy(a);
    mesaGLX11Destroy(b);
    return 0;
}
