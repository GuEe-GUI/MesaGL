#define _POSIX_C_SOURCE 199309L
#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"

#include <time.h>

static void scene(int x, int mode)
{
    static const float distances[] = {3.0f, 4.75f, 6.5f, 8.25f, 10.0f};
    int i;

    glViewport(x, 0, 300, 300);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -1, 1, 2, 20);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    if (!mode)
        glDisable(GL_FOG);
    else {
        glEnable(GL_FOG);
        glFogf(GL_FOG_MODE, mode == 1 ? GL_LINEAR : GL_EXP2);
    }
    for (i = 0; i < 5; ++i) {
        float distance = distances[i];
        float screen_x = -0.72f + i * 0.36f;
        float center = screen_x * distance * 0.5f;
        float size = distance * 0.12f;

        glColor3f(1.0f, 0.38f + i * 0.08f, 0.08f);
        glBegin(GL_TRIANGLES);
        glVertex3f(center - size, -size, -distance);
        glVertex3f(center + size, -size, -distance);
        glVertex3f(center, size, -distance);
        glEnd();
    }
}

int main(void)
{
    static const GLfloat fog_color[] = {0.08f, 0.12f, 0.2f, 1.0f};
    static const struct timespec delay = {0, 16666667};
    MesaGLX11 *x11 =
        mesaGLX11Create(900, 300, "MesaGL fog: OFF | LINEAR 3..10 | EXP2 density 0.16");
    MesaGLPortContext *context;

    if (!x11 || !(context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11))))
        return 1;
    glEnable(GL_DEPTH_TEST);
    glFogfv(GL_FOG_COLOR, fog_color);
    glFogf(GL_FOG_START, 3.0f);
    glFogf(GL_FOG_END, 10.0f);
    glFogf(GL_FOG_DENSITY, 0.16f);
    while (mesaGLX11PollEvents(x11)) {
        glViewport(0, 0, 900, 300);
        glClearColor(fog_color[0], fog_color[1], fog_color[2], fog_color[3]);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        scene(0, 0);
        scene(300, 1);
        scene(600, 2);
        mesaGLPortPresent(context);
        nanosleep(&delay, NULL);
    }
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
