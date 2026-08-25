#define _POSIX_C_SOURCE 199309L
#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"

#include <time.h>

static void panel(int x, int transform)
{
    glViewport(x, 0, 300, 300);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    if (transform == 1)
        glScalef(2.0f, 2.0f, 1.0f);
    else if (transform == 2) {
        glTranslatef(0.5f, 0.5f, 0.0f);
        glRotatef(45.0f, 0.0f, 0.0f, 1.0f);
        glTranslatef(-0.5f, -0.5f, 0.0f);
    }
    glMatrixMode(GL_MODELVIEW);
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(-0.82f, -0.82f);
    glTexCoord2f(1, 0);
    glVertex2f(0.82f, -0.82f);
    glTexCoord2f(1, 1);
    glVertex2f(0.82f, 0.82f);
    glTexCoord2f(0, 1);
    glVertex2f(-0.82f, 0.82f);
    glEnd();
}

int main(void)
{
    static const struct timespec delay = {0, 16666667};
    GLubyte checker[8 * 8 * 4];
    MesaGLX11 *x11 =
        mesaGLX11Create(900, 300, "MesaGL texture matrix: IDENTITY | SCALE 2x | ROTATE 45 deg");
    MesaGLPortContext *context;
    GLuint texture;
    int x, y;

    if (!x11 || !(context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11))))
        return 1;
    for (y = 0; y < 8; ++y)
        for (x = 0; x < 8; ++x) {
            GLubyte *pixel = &checker[(y * 8 + x) * 4];
            int bright = ((x / 2) ^ (y / 2)) & 1;

            pixel[0] = bright ? 245 : 30;
            pixel[1] = bright ? 180 : 70;
            pixel[2] = bright ? 35 : 220;
            pixel[3] = 255;
        }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, checker);
    glEnable(GL_TEXTURE_2D);
    while (mesaGLX11PollEvents(x11)) {
        glViewport(0, 0, 900, 300);
        glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        panel(0, 0);
        panel(300, 1);
        panel(600, 2);
        mesaGLPortPresent(context);
        nanosleep(&delay, NULL);
    }
    glDeleteTextures(1, &texture);
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
