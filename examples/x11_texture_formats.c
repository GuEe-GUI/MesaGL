#define _POSIX_C_SOURCE 199309L
#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"
#include <time.h>

static void panel(int x, GLuint texture)
{
    glViewport(x, 0, 200, 300);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor4f(1, 1, 1, 1);
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
    static const GLubyte alpha[] = {32, 96, 160, 255};
    static const GLubyte luminance[] = {24, 96, 176, 248};
    static const GLubyte luminance_alpha[] = {255, 32, 192, 96, 128, 160, 64, 255};
    static const GLushort rgb565[] = {0xf800, 0x07e0, 0x001f, 0xffff};
    static const GLushort rgba4444[] = {0xf00f, 0x0f08, 0x00f4, 0xffff};
    static const GLushort rgba5551[] = {0xf801, 0x07c1, 0x003e, 0xffff};
    static const struct timespec delay = {0, 16666667};
    MesaGLX11 *x11 =
        mesaGLX11Create(1200, 300, "MesaGL textures: A | L | LA | RGB565 | RGBA4444 | RGBA5551");
    MesaGLPortContext *context;
    GLuint textures[6];
    if (!x11 || !(context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11))))
        return 1;
    glGenTextures(6, textures);
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 2, 2, 0, GL_ALPHA, GL_UNSIGNED_BYTE, alpha);
    glBindTexture(GL_TEXTURE_2D, textures[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 2, 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                 luminance);
    glBindTexture(GL_TEXTURE_2D, textures[2]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, 2, 2, 0, GL_LUMINANCE_ALPHA,
                 GL_UNSIGNED_BYTE, luminance_alpha);
    glBindTexture(GL_TEXTURE_2D, textures[3]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, rgb565);
    glBindTexture(GL_TEXTURE_2D, textures[4]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, rgba4444);
    glBindTexture(GL_TEXTURE_2D, textures[5]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, rgba5551);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    while (mesaGLX11PollEvents(x11)) {
        glViewport(0, 0, 1200, 300);
        glClearColor(0.05f, 0.12f, 0.25f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        panel(0, textures[0]);
        panel(200, textures[1]);
        panel(400, textures[2]);
        panel(600, textures[3]);
        panel(800, textures[4]);
        panel(1000, textures[5]);
        mesaGLPortPresent(context);
        nanosleep(&delay, 0);
    }
    glDeleteTextures(6, textures);
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
