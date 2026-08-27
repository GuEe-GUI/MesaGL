#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

static int out_of_range_fragment(void *user, const float *varyings,
                                 int varying_count, const float *varying_dfdx,
                                 const float *varying_dfdy,
                                 const float frag_coord[4], int front_facing,
                                 const float point_coord[2], float color[4])
{
    (void)user;
    (void)varyings;
    (void)varying_count;
    (void)varying_dfdx;
    (void)varying_dfdy;
    (void)frag_coord;
    (void)front_facing;
    (void)point_coord;
    color[0] = 0.5f;
    color[1] = 0.0f;
    color[2] = 0.0f;
    color[3] = 2.0f;
    return 1;
}

int main(void)
{
    uint8_t pixels[16 * 16 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 16, 16, 16 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLfloat blend_color[4];
    GLubyte pixel[4];
    GLint value;
    const NTGLprogramVertex triangle[] = {
        {{-1.0f, -1.0f, 0.0f, 1.0f}, {{0}}, 1.0f},
        {{3.0f, -1.0f, 0.0f, 1.0f}, {{0}}, 1.0f},
        {{-1.0f, 3.0f, 0.0f, 1.0f}, {{0}}, 1.0f},
    };

    if (!context)
        return 1;
    glViewport(0, 0, 16, 16);
    glClearColor(0.0f, 0.0f, 1.0f, 0.25f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glColor4f(1.0f, 0.0f, 0.0f, 0.5f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glVertex2f(0.0f, 1.0f);
    glEnd();
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 255 || pixel[1] || pixel[2] || pixel[3] < 127 || pixel[3] > 129)
        return 14;

    glClear(GL_COLOR_BUFFER_BIT);
    glBlendColor(0.25f, 0.5f, 0.75f, 0.5f);
    glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_REVERSE_SUBTRACT);
    glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glVertex2f(0.0f, 1.0f);
    glEnd();
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 63 || pixel[0] > 65 || pixel[1] || pixel[2] || pixel[3] < 63 || pixel[3] > 65)
        return 2;

    glGetIntegerv(GL_BLEND_SRC_RGB, &value);
    if (value != GL_CONSTANT_COLOR)
        return 3;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &value);
    if (value != GL_ZERO)
        return 4;
    glGetIntegerv(GL_BLEND_DST_ALPHA, &value);
    if (value != GL_ONE)
        return 5;
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &value);
    if (value != GL_FUNC_REVERSE_SUBTRACT)
        return 6;
    glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_SRC_COLOR, GL_ZERO);
    if (glGetError() != GL_INVALID_ENUM)
        return 15;
    glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_CONSTANT_COLOR);
    if (glGetError() != GL_INVALID_ENUM)
        return 16;
    glGetIntegerv(GL_BLEND_SRC_RGB, &value);
    if (value != GL_CONSTANT_COLOR)
        return 17;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &value);
    if (value != GL_ZERO)
        return 18;
    glGetFloatv(GL_BLEND_COLOR, blend_color);
    if (blend_color[0] != 0.25f || blend_color[1] != 0.5f || blend_color[2] != 0.75f ||
        blend_color[3] != 0.5f)
        return 7;

    glBlendEquationSeparate(GL_FUNC_SUBTRACT, 0xdead);
    if (glGetError() != GL_INVALID_ENUM)
        return 8;
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &value);
    if (value != GL_FUNC_ADD)
        return 9;
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &value);
    if (value != GL_FUNC_REVERSE_SUBTRACT)
        return 10;

    glClearColor(0.0f, 0.0f, 1.0f, 0.75f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA_SATURATE, GL_ZERO);
    glColor4f(1.0f, 0.0f, 0.0f, 0.5f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glVertex2f(0.0f, 1.0f);
    glEnd();
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 63 || pixel[0] > 65 || pixel[1] || pixel[2] || pixel[3] < 127 ||
        pixel[3] > 129)
        return 11;
    glBlendFunc(GL_ONE, GL_SRC_ALPHA_SATURATE);
    if (glGetError() != GL_INVALID_ENUM)
        return 12;
    glGetIntegerv(GL_BLEND_DST_RGB, &value);
    if (value != GL_ZERO)
        return 13;

    glClearColor(0.0f, 0.0f, 1.0f, 0.25f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ntglDrawProgrammable(NTGL_TRIANGLES, triangle, 3, 0,
                         out_of_range_fragment, NULL);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 127 || pixel[0] > 129 || pixel[1] || pixel[2] ||
        pixel[3] != 255)
        return 19;

    ntglDestroyContext(context);
    return 0;
}
