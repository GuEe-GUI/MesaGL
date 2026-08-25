#define _POSIX_C_SOURCE 199309L
#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"
#include <time.h>

static void panel(int x, int alpha_test)
{
    glViewport(x, 0, 320, 300);
    if (alpha_test) {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);
    } else {
        glDisable(GL_ALPHA_TEST);
    }
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(-0.85f, -0.75f);
    glTexCoord2f(1, 0);
    glVertex2f(0.85f, -0.75f);
    glTexCoord2f(1, 1);
    glVertex2f(0.85f, 0.75f);
    glTexCoord2f(0, 1);
    glVertex2f(-0.85f, 0.75f);
    glEnd();
}

int main(void)
{
    static const GLubyte alpha[] = {0, 255, 0, 255};
    static const struct timespec delay = {0, 16666667};
    MesaGLX11 *x11 = mesaGLX11Create(640, 300, "MesaGL alpha: BLEND | ALPHA_TEST > 0.5");
    MesaGLPortContext *context;
    GLuint texture;
    if (!x11 || !(context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11))))
        return 1;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 2, 2, 0, GL_ALPHA, GL_UNSIGNED_BYTE, alpha);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    while (mesaGLX11PollEvents(x11)) {
        glViewport(0, 0, 640, 300);
        glDisable(GL_ALPHA_TEST);
        glClearColor(0.05f, 0.15f, 0.35f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        panel(0, 0);
        panel(320, 1);
        mesaGLPortPresent(context);
        nanosleep(&delay, 0);
    }
    glDeleteTextures(1, &texture);
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
