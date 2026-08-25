#define _POSIX_C_SOURCE 199309L
#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"
#include <stdio.h>
#include <time.h>

int main(void)
{
    static const struct timespec delay = {0, 16666667};
    MesaGLX11 *x11 = mesaGLX11Create(640, 480, "MesaGL FBO: offscreen texture on screen");
    MesaGLPortContext *context;
    GLuint texture, fbo, depth;
    if (!x11 || !(context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11)))) {
        fprintf(stderr, "Unable to create X11 framebuffer or MesaGL context\n");
        return 1;
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 256, 256);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    {
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        GLenum gl_error = glGetError();
        if (status != GL_FRAMEBUFFER_COMPLETE || gl_error != GL_NO_ERROR) {
            fprintf(stderr, "FBO setup failed: status=0x%04x, GL error=0x%04x\n", (unsigned)status,
                    (unsigned)gl_error);
            return 2;
        }
    }
    glViewport(0, 0, 256, 256);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.1f, 0.1f, 0.35f, 1);
    glClearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glColor3f(0.1f, 0.8f, 1);
    glBegin(GL_TRIANGLES);
    glVertex3f(-0.9f, -0.6f, 0.5f);
    glVertex3f(0.9f, -0.6f, 0.5f);
    glVertex3f(0, 0.95f, 0.5f);
    glEnd();
    glColor3f(1, 0.3f, 0.1f);
    glBegin(GL_TRIANGLES);
    glVertex3f(-0.8f, -0.7f, 0);
    glVertex3f(0.8f, -0.7f, 0);
    glVertex3f(0, 0.8f, 0);
    glEnd();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, 640, 480);
    while (mesaGLX11PollEvents(x11)) {
        glClearColor(0.03f, 0.04f, 0.06f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
        glColor3f(1, 1, 1);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0);
        glVertex2f(-0.75f, -0.75f);
        glTexCoord2f(1, 0);
        glVertex2f(0.75f, -0.75f);
        glTexCoord2f(1, 1);
        glVertex2f(0.75f, 0.75f);
        glTexCoord2f(0, 1);
        glVertex2f(-0.75f, 0.75f);
        glEnd();
        mesaGLPortPresent(context);
        nanosleep(&delay, 0);
    }
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &depth);
    glDeleteTextures(1, &texture);
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
