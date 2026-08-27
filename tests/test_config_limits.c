#include "GLES2/gl2.h"
#include "mesaGL/config.h"
#include "mesaGL/ntgl.h"

#include <math.h>
#include <stdint.h>

int main(void)
{
    uint8_t pixels[4 * 4 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 4, 4, 4 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint texture;
    GLuint renderbuffer;
    GLint value;
    GLint dimensions[2];
    GLint viewport[4];
    GLfloat range[2];
    GLfloat scalar;
    GLint maximum_level = 0;

    if (!context)
        return 1;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
    if (value != MESAGL_MAX_TEXTURE_SIZE)
        return 2;
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &value);
    if (value != MESAGL_MAX_RENDERBUFFER_SIZE)
        return 3;
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, dimensions);
    if (dimensions[0] != MESAGL_MAX_VIEWPORT_DIMENSION ||
        dimensions[1] != MESAGL_MAX_VIEWPORT_DIMENSION)
        return 4;
    glViewport(7, 9, MESAGL_MAX_VIEWPORT_DIMENSION + 1,
               MESAGL_MAX_VIEWPORT_DIMENSION + 2);
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[0] != 7 || viewport[1] != 9 ||
        viewport[2] != MESAGL_MAX_VIEWPORT_DIMENSION ||
        viewport[3] != MESAGL_MAX_VIEWPORT_DIMENSION ||
        glGetError() != GL_NO_ERROR)
        return 5;

    glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, range);
    if (range[0] != 1.0f || range[1] != (GLfloat)MESAGL_MAX_LINE_WIDTH)
        return 6;
    glLineWidth((GLfloat)MESAGL_MAX_LINE_WIDTH + 10.0f);
    glGetFloatv(GL_LINE_WIDTH, &scalar);
    if (scalar != (GLfloat)MESAGL_MAX_LINE_WIDTH + 10.0f)
        return 7;
    glLineWidth(NAN);
    if (glGetError() != GL_NO_ERROR)
        return 14;
    glGetFloatv(GL_LINE_WIDTH, &scalar);
    if (!isnan(scalar))
        return 15;
    glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, range);
    if (range[0] != 1.0f || range[1] != (GLfloat)MESAGL_MAX_POINT_SIZE)
        return 8;
    glPointSize((GLfloat)MESAGL_MAX_POINT_SIZE + 10.0f);
    glGetFloatv(GL_POINT_SIZE, &scalar);
    if (scalar != (GLfloat)MESAGL_MAX_POINT_SIZE)
        return 9;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    while ((MESAGL_MAX_TEXTURE_SIZE >> maximum_level) > 1)
        ++maximum_level;
    glTexImage2D(GL_TEXTURE_2D, maximum_level + 1, GL_RGBA, 0, 0, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    if (glGetError() != GL_INVALID_VALUE)
        return 12;
    glCopyTexImage2D(GL_TEXTURE_2D, maximum_level + 1, GL_RGBA, 0, 0, 0, 0, 0);
    if (glGetError() != GL_INVALID_VALUE)
        return 13;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, MESAGL_MAX_TEXTURE_SIZE + 1, 1,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    if (glGetError() != GL_INVALID_VALUE)
        return 10;
    glGenRenderbuffers(1, &renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4,
                          MESAGL_MAX_RENDERBUFFER_SIZE + 1, 1);
    if (glGetError() != GL_INVALID_VALUE)
        return 11;

    glDeleteRenderbuffers(1, &renderbuffer);
    glDeleteTextures(1, &texture);
    ntglDestroyContext(context);
    return 0;
}
