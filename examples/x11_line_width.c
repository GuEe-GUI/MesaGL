#define _POSIX_C_SOURCE 199309L
#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"

#include <time.h>

static void lines(int x, float width)
{
    glViewport(x, 0, 300, 300);
    glLineWidth(width);
    glBegin(GL_LINE_STRIP);
    glColor3f(1.0f, 0.2f, 0.1f);
    glVertex2f(-0.78f, -0.65f);
    glColor3f(0.2f, 1.0f, 0.3f);
    glVertex2f(-0.25f, 0.6f);
    glColor3f(0.1f, 0.55f, 1.0f);
    glVertex2f(0.25f, -0.35f);
    glColor3f(1.0f, 0.8f, 0.1f);
    glVertex2f(0.78f, 0.65f);
    glEnd();
}

int main(void)
{
    static const struct timespec delay = {0, 16666667};
    MesaGLX11 *x11 = mesaGLX11Create(900, 300, "MesaGL line width: 1 | 5 | 9 pixels");
    MesaGLPortContext *context;

    if (!x11 || !(context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11))))
        return 1;
    while (mesaGLX11PollEvents(x11)) {
        glViewport(0, 0, 900, 300);
        glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        lines(0, 1.0f);
        lines(300, 5.0f);
        lines(600, 9.0f);
        mesaGLPortPresent(context);
        nanosleep(&delay, NULL);
    }
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
