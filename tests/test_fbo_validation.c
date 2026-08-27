#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

static void draw_triangle(float depth, float red, float green, float blue)
{
    glBegin(GL_TRIANGLES);
    glColor3f(red, green, blue);
    glVertex3f(-1.0f, -1.0f, depth);
    glVertex3f(1.0f, -1.0f, depth);
    glVertex3f(0.0f, 1.0f, depth);
    glEnd();
}

int main(void)
{
    uint8_t default_pixels[16 * 16 * 4] = {0};
    NTGLframebuffer default_framebuffer = {
        default_pixels, 16, 16, 16 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&default_framebuffer, NULL);
    GLuint framebuffer;
    GLuint color;
    GLuint depth;
    GLuint stencil;
    GLuint texture;
    GLint value;
    GLubyte pixel[4];

    if (!context)
        return 1;
    glRenderbufferStorage(GL_RENDERBUFFER, 0xdead, -1, 1);
    if (glGetError() != GL_INVALID_OPERATION)
        return 46;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT)
        return 2;
    glDrawArrays(GL_TRIANGLES, 0, 0);
    if (glGetError() != GL_INVALID_FRAMEBUFFER_OPERATION)
        return 60;
    glDrawElements(GL_TRIANGLES, 0, GL_UNSIGNED_SHORT, NULL);
    if (glGetError() != GL_INVALID_FRAMEBUFFER_OPERATION)
        return 61;
    value = -1;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &value);
    if (value != -1 || glGetError() != GL_INVALID_ENUM)
        return 53;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL, &value);
    if (glGetError() != GL_INVALID_ENUM)
        return 54;
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE, &value);
    if (glGetError() != GL_INVALID_ENUM)
        return 55;

    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, 8, 8);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    if (glGetError() != GL_NO_ERROR ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 3;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_RED_SIZE, &value);
    if (value != 4)
        return 17;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_GREEN_SIZE, &value);
    if (value != 4)
        return 18;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_BLUE_SIZE, &value);
    if (value != 4)
        return 19;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_ALPHA_SIZE, &value);
    if (value != 4)
        return 20;
    glGetIntegerv(GL_RED_BITS, &value);
    if (value != 4)
        return 29;
    glGetIntegerv(GL_DEPTH_BITS, &value);
    if (value != 0)
        return 30;
    glGetIntegerv(GL_STENCIL_BITS, &value);
    if (value != 0)
        return 31;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &value);
    if (value != GL_RENDERBUFFER)
        return 4;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &value);
    if ((GLuint)value != color)
        return 5;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL, &value);
    if (glGetError() != GL_INVALID_ENUM)
        return 56;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 8, 8, 0, GL_ALPHA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 1);
    if (glGetError() != GL_INVALID_VALUE ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 39;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (glGetError() != GL_NO_ERROR ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT)
        return 40;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 41;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0xdead, 0, 7);
    if (glGetError() != GL_NO_ERROR ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT)
        return 44;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (glGetError() != GL_NO_ERROR ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 45;
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 64 || pixel[1] != 128 || pixel[2] != 191 || pixel[3] != 255)
        return 43;
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 42;
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0xdead, 0);
    if (glGetError() != GL_NO_ERROR ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT)
        return 57;
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0xdead, color);
    if (glGetError() != GL_INVALID_ENUM ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT)
        return 58;
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    if (glGetError() != GL_NO_ERROR ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 59;

    glViewport(0, 0, 8, 8);
    glClearColor(0.0f, 0.5f, 0.0f, 0.5f);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] || pixel[1] != 136 || pixel[2] || pixel[3] != 136)
        return 6;

    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB5_A1, 8, 8);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_ALPHA_SIZE, &value);
    if (value != 1)
        return 21;
    glClearColor(0.5f, 0.5f, 0.5f, 0.25f);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 131 || pixel[1] != 131 || pixel[2] != 131 || pixel[3] != 0)
        return 15;
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB565, 8, 8);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_GREEN_SIZE, &value);
    if (value != 6)
        return 22;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_ALPHA_SIZE, &value);
    if (value != 0)
        return 23;
    glGetIntegerv(GL_RED_BITS, &value);
    if (value != 5)
        return 32;
    glGetIntegerv(GL_GREEN_BITS, &value);
    if (value != 6)
        return 33;
    glGetIntegerv(GL_ALPHA_BITS, &value);
    if (value != 0)
        return 34;
    glClearColor(0.5f, 0.5f, 0.5f, 0.25f);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 131 || pixel[1] != 129 || pixel[2] != 131 || pixel[3] != 255)
        return 16;

    /* Depth testing has no effect when the FBO has no depth attachment. */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw_triangle(-0.6f, 1.0f, 0.0f, 0.0f);
    draw_triangle(0.6f, 0.0f, 1.0f, 0.0f);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 0 || pixel[1] != 255 || pixel[2] != 0)
        return 44;

    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 4, 4);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS)
        return 7;
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 8, 8);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE, &value);
    if (value != 16)
        return 24;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_STENCIL_SIZE, &value);
    if (value != 0)
        return 25;
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 8;
    glGetIntegerv(GL_DEPTH_BITS, &value);
    if (value != 16)
        return 35;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw_triangle(-0.6f, 1.0f, 0.0f, 0.0f);
    draw_triangle(0.6f, 0.0f, 1.0f, 0.0f);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 255 || pixel[1] != 0 || pixel[2] != 0)
        return 45;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    draw_triangle(0.0f, 0.0f, 0.0f, 1.0f);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 255 || pixel[1] != 0 || pixel[2] != 0)
        return 46;

    glGenRenderbuffers(1, &stencil);
    glBindRenderbuffer(GL_RENDERBUFFER, stencil);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, 4, 4);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_STENCIL_SIZE, &value);
    if (value != 8)
        return 26;
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencil);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS)
        return 27;
    glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, 8, 8);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 28;
    glGetIntegerv(GL_STENCIL_BITS, &value);
    if (value != 8)
        return 36;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xffu);
    glClearStencil(0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glStencilFunc(GL_ALWAYS, 1, 0xffu);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    draw_triangle(0.0f, 1.0f, 0.0f, 0.0f);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glClear(GL_COLOR_BUFFER_BIT);
    glStencilFunc(GL_EQUAL, 1, 0xffu);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    draw_triangle(0.0f, 0.0f, 1.0f, 0.0f);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 0 || pixel[1] != 255 || pixel[2] != 0)
        return 47;
    glDisable(GL_STENCIL_TEST);

    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, 0xdead, 2, 2);
    if (glGetError() != GL_INVALID_ENUM)
        return 9;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &value);
    if (value != 8)
        return 10;
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, 0xdead, GL_RENDERBUFFER, depth);
    if (glGetError() != GL_INVALID_ENUM ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 11;

    glDeleteRenderbuffers(1, &color);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &value);
    if ((GLuint)value != framebuffer ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 12;
    glClearDepthf(0.25f);
    glClear(GL_DEPTH_BUFFER_BIT);
    if (glGetError() != GL_NO_ERROR)
        return 13;
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (glGetError() != GL_INVALID_OPERATION)
        return 52;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 48;
    glViewport(0, 0, 8, 8);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_triangle(0.0f, 1.0f, 0.0f, 0.0f);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 255)
        return 49;
    glClearDepthf(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
    draw_triangle(0.0f, 1.0f, 0.0f, 0.0f);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 255 || pixel[1] != 0 || pixel[2] != 0)
        return 50;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           0, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 51;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glGetIntegerv(GL_DEPTH_BITS, &value);
    if (value != 16)
        return 37;
    glGetIntegerv(GL_STENCIL_BITS, &value);
    if (value != 8)
        return 38;
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] || pixel[1] || pixel[2])
        return 14;

    glDeleteRenderbuffers(1, &depth);
    glDeleteRenderbuffers(1, &stencil);
    glDeleteTextures(1, &texture);
    glDeleteFramebuffers(1, &framebuffer);
    ntglDestroyContext(context);
    return 0;
}
