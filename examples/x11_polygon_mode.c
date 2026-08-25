#define _POSIX_C_SOURCE 199309L
#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"

#include <time.h>

static void triangle(int x, GLenum mode)
{
    glViewport(x, 0, 300, 300);
    glPolygonMode(GL_FRONT_AND_BACK, mode);
    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.2f, 0.1f);
    glVertex2f(-0.78f, -0.72f);
    glColor3f(0.1f, 1.0f, 0.3f);
    glVertex2f(0.78f, -0.72f);
    glColor3f(0.1f, 0.55f, 1.0f);
    glVertex2f(0.0f, 0.78f);
    glEnd();
}

int main(void)
{
    static const struct timespec delay = {0, 16666667};
    MesaGLX11 *x11 = mesaGLX11Create(900, 300, "MesaGL polygon mode: FILL | LINE | POINT");
    MesaGLPortContext *context;

    if (!x11 || !(context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11))))
        return 1;
    glPointSize(7.0f);
    while (mesaGLX11PollEvents(x11)) {
        glViewport(0, 0, 900, 300);
        glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        triangle(0, GL_FILL);
        triangle(300, GL_LINE);
        triangle(600, GL_POINT);
        mesaGLPortPresent(context);
        nanosleep(&delay, NULL);
    }
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
