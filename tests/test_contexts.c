#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

static void draw_point(float red, float green, float blue)
{
    ntglColor3f(red, green, blue);
    ntglBegin(NTGL_POINTS);
    ntglVertex2f(0, 0);
    ntglEnd();
}

int main(void)
{
    uint32_t pixels_a[16 * 16] = {0};
    uint32_t pixels_b[16 * 16] = {0};
    NTGLframebuffer fb_a = {pixels_a, 16, 16, 16 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLframebuffer fb_b = {pixels_b, 16, 16, 16 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *a = ntglCreateContext(&fb_a, NULL);
    NTGLcontext *b = ntglCreateContext(&fb_b, NULL);
    GLint viewport[4], binding;
    GLuint texture, texture_b, buffer_a, buffer_b;

    if (!a || !b)
        return 1;

    ntglMakeCurrent(a);
    glViewport(1, 2, 13, 14);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGenBuffers(1, &buffer_a);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_a);
    ntglViewport(0, 0, 16, 16);
    ntglClearColor(1, 0, 0, 1);
    ntglClear(1, 0);
    ntglEnable(NTGL_SCISSOR_TEST);
    ntglScissor(0, 0, 8, 16);

    ntglMakeCurrent(b);
    glViewport(3, 4, 11, 12);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    if (binding != 0)
        return 5;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &binding);
    if (binding != 0)
        return 8;
    glGenBuffers(1, &buffer_b);
    if (buffer_b != 1)
        return 9;
    glGenTextures(1, &texture_b);
    if (texture_b != 1)
        return 7;
    glBindTexture(GL_TEXTURE_2D, texture_b);
    ntglViewport(0, 0, 16, 16);
    ntglClearColor(0, 0, 1, 1);
    ntglClear(1, 0);
    draw_point(0, 1, 0);

    ntglMakeCurrent(a);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    if (viewport[0] != 1 || viewport[1] != 2 || viewport[2] != 13 || viewport[3] != 14 ||
        binding != (GLint)texture)
        return 6;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &binding);
    if (binding != (GLint)buffer_a)
        return 10;
    draw_point(1, 1, 1);
    ntglClearColor(0, 1, 0, 1);
    ntglClear(1, 0);

    if ((pixels_a[8 * 16 + 4] & 0x00ffffffu) != 0x0000ff00u)
        return 2;
    if ((pixels_a[8 * 16 + 12] & 0x00ffffffu) != 0x000000ffu)
        return 3;
    if ((pixels_b[8 * 16 + 8] & 0x00ffffffu) != 0x0000ff00u)
        return 4;

    ntglMakeCurrent(a);
    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &buffer_a);
    ntglDestroyContext(a);
    ntglMakeCurrent(b);
    glDeleteTextures(1, &texture_b);
    glDeleteBuffers(1, &buffer_b);
    ntglDestroyContext(b);
    return 0;
}
