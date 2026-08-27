#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

int main(void)
{
    uint8_t pixels[8 * 8 * 4] = {0};
    const GLubyte red[4] = {255, 0, 0, 255};
    NTGLframebuffer framebuffer = {
        pixels, 8, 8, 8 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context;
    GLuint texture;
    GLuint renderbuffer;
    GLuint unbound_renderbuffer;
    GLuint framebuffers[2];
    GLint object_type = 0;
    GLint object_name = 0;
    GLubyte pixel[4];

    context = ntglCreateContext(&framebuffer, NULL);
    if (!context)
        return 1;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, red);
    glGenFramebuffers(2, framebuffers);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[0]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 2;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[1]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 3;

    glDeleteTextures(1, &texture);
    if (glIsTexture(texture) ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT)
        return 4;
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &object_type);
    if (object_type != GL_NONE)
        return 5;
    object_name = -1;
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &object_name);
    if (glGetError() != GL_INVALID_ENUM || object_name != -1)
        return 16;

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[0]);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 6;
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &object_type);
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &object_name);
    if (object_type != GL_TEXTURE || object_name != (GLint)texture)
        return 7;
    glViewport(0, 0, 1, 1);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 0 || pixel[1] != 255 || pixel[2] != 0 || pixel[3] != 255)
        return 8;

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, 0, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT)
        return 9;
    glGenRenderbuffers(1, &unbound_renderbuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, unbound_renderbuffer);
    if (glGetError() != GL_INVALID_OPERATION)
        return 10;
    glDeleteRenderbuffers(1, &unbound_renderbuffer);

    glGenRenderbuffers(1, &renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, 1, 1);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[0]);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, renderbuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[1]);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, renderbuffer);
    glDeleteRenderbuffers(1, &renderbuffer);
    if (glIsRenderbuffer(renderbuffer) ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT)
        return 11;
    object_name = -1;
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &object_name);
    if (glGetError() != GL_INVALID_ENUM || object_name != -1)
        return 17;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[0]);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 12;
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &object_type);
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &object_name);
    if (object_type != GL_RENDERBUFFER || object_name != (GLint)renderbuffer)
        return 13;
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 255 || pixel[3] != 255)
        return 14;

    glDeleteFramebuffers(2, framebuffers);
    if (glGetError() != GL_NO_ERROR)
        return 15;
    ntglDestroyContext(context);
    return 0;
}
