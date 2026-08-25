#define _POSIX_C_SOURCE 199309L

#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"

#include <stdio.h>
#include <time.h>

static double monotonic_seconds(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (double)value.tv_sec + (double)value.tv_nsec / 1000000000.0;
}

int main(void)
{
    MesaGLX11 *x11 = mesaGLX11Create(640, 480, "mesaGL X11 port");
    MesaGLPortContext *context;
    const double start = monotonic_seconds();
    struct timespec frame_delay = {0, 16666667};
    if (!x11) {
        fprintf(stderr, "Unable to create X11 framebuffer\n");
        return 1;
    }
    context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11));
    if (!context) {
        mesaGLX11Destroy(x11);
        return 2;
    }

    glViewport(0, 0, 640, 480);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.333333, 1.333333, -1.0, 1.0, -10.0, 10.0);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glClearDepth(1.0);

    while (mesaGLX11PollEvents(x11)) {
        float angle = (float)((monotonic_seconds() - start) * 60.0);
        glClearColor(0.08f, 0.10f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();
        glRotatef(angle, 0.0f, 1.0f, 0.0f);
        glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.2f, 0.1f);
        glVertex3f(-0.85f, -0.7f, 0.0f);
        glColor3f(0.1f, 1.0f, 0.3f);
        glVertex3f(0.85f, -0.7f, 0.0f);
        glColor3f(0.2f, 0.4f, 1.0f);
        glVertex3f(0.0f, 0.85f, 0.0f);
        glEnd();
        mesaGLPortPresent(context);
        nanosleep(&frame_delay, NULL);
    }
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
