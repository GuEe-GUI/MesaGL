#define _POSIX_C_SOURCE 199309L
#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"
#include <time.h>

static void panel(int x, GLenum mode)
{
    glViewport(x, 0, 240, 300);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode);
    glColor4f(0.15f, 0.65f, 1.0f, 0.8f);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(-0.8f, -0.75f);
    glTexCoord2f(1, 0);
    glVertex2f(0.8f, -0.75f);
    glTexCoord2f(1, 1);
    glVertex2f(0.8f, 0.75f);
    glTexCoord2f(0, 1);
    glVertex2f(-0.8f, 0.75f);
    glEnd();
}

int main(void)
{
    static const GLubyte orange[] = {255, 96, 16, 160, 255, 96, 16, 160,
                                     255, 96, 16, 160, 255, 96, 16, 160};
    static const struct timespec delay = {0, 16666667};
    static const GLfloat environment_color[] = {0.8f, 0.1f, 0.9f, 1.0f};
    MesaGLX11 *x11 =
        mesaGLX11Create(1200, 300, "MesaGL texture env: MODULATE | REPLACE | DECAL | ADD | BLEND");
    MesaGLPortContext *context;
    GLuint texture;
    if (!x11 || !(context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11))))
        return 1;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, orange);
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, environment_color);
    glEnable(GL_TEXTURE_2D);
    while (mesaGLX11PollEvents(x11)) {
        glViewport(0, 0, 1200, 300);
        glClearColor(0.04f, 0.05f, 0.08f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        panel(0, GL_MODULATE);
        panel(240, GL_REPLACE);
        panel(480, GL_DECAL);
        panel(720, GL_ADD);
        panel(960, GL_BLEND);
        mesaGLPortPresent(context);
        nanosleep(&delay, 0);
    }
    glDeleteTextures(1, &texture);
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
