#define _POSIX_C_SOURCE 199309L
#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"
#include <time.h>

static void triangle(int x, GLenum model)
{
    glViewport(x, 0, 320, 300);
    glShadeModel(model);
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0);
    glVertex2f(-0.8f, -0.75f);
    glColor3f(0, 1, 0);
    glVertex2f(0.8f, -0.75f);
    glColor3f(0, 0.35f, 1);
    glVertex2f(0, 0.8f);
    glEnd();
}

int main(void)
{
    static const struct timespec delay = {0, 16666667};
    MesaGLX11 *x11 = mesaGLX11Create(640, 300, "MesaGL shading: SMOOTH | FLAT (provoking blue)");
    MesaGLPortContext *context;
    if (!x11 || !(context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11))))
        return 1;
    while (mesaGLX11PollEvents(x11)) {
        glViewport(0, 0, 640, 300);
        glClearColor(0.04f, 0.05f, 0.08f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        triangle(0, GL_SMOOTH);
        triangle(320, GL_FLAT);
        mesaGLPortPresent(context);
        nanosleep(&delay, 0);
    }
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
