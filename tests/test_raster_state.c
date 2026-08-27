#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <math.h>
#include <limits.h>
#include <stdint.h>

static void read_pixel(int x, int y, GLubyte pixel[4])
{
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
}

int main(void)
{
    uint8_t pixels[16 * 16 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 16, 16, 16 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLint box[4];
    GLfloat value;
    GLfloat depth_range[2];
    GLfloat clear_color[4];
    GLint integer_range[2];
    GLint integer_value;
    GLboolean boolean_range[2];
    GLubyte left[4];
    GLubyte right[4];

    if (!context)
        return 1;
    glGetIntegerv(GL_VIEWPORT, box);
    if (box[0] || box[1] || box[2] != 16 || box[3] != 16)
        return 20;
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, &value);
    if (value != 1.0f)
        return 21;
    glClearColor(-1.0f, 0.25f, 2.0f, 0.5f);
    glClearDepthf(2.0f);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color);
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, &value);
    if (clear_color[0] != 0.0f || clear_color[1] != 0.25f || clear_color[2] != 1.0f ||
        clear_color[3] != 0.5f || value != 1.0f)
        return 22;
    glViewport(0, 0, 16, 16);
    glViewport(2, 3, -1, 8);
    if (glGetError() != GL_INVALID_VALUE)
        return 2;
    glGetIntegerv(GL_VIEWPORT, box);
    if (box[0] != 0 || box[1] != 0 || box[2] != 16 || box[3] != 16)
        return 3;

    glScissor(0, 0, 8, 16);
    glScissor(1, 1, 8, -1);
    if (glGetError() != GL_INVALID_VALUE)
        return 4;
    glGetIntegerv(GL_SCISSOR_BOX, box);
    if (box[0] != 0 || box[1] != 0 || box[2] != 8 || box[3] != 16)
        return 5;
    glEnable(GL_SCISSOR_TEST);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | 0x8u);
    if (glGetError() != GL_INVALID_VALUE)
        return 6;
    read_pixel(4, 8, left);
    if (left[0] || left[1] || left[2])
        return 7;
    glClear(GL_COLOR_BUFFER_BIT);
    read_pixel(4, 8, left);
    read_pixel(12, 8, right);
    if (left[0] != 255 || left[1] || left[2] || right[0] || right[1] || right[2])
        return 8;

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDisable(GL_SCISSOR_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(INT_MAX, INT_MAX, INT_MAX, INT_MAX);
    glEnable(GL_SCISSOR_TEST);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    read_pixel(8, 8, left);
    if (glGetError() != GL_NO_ERROR || left[0] || left[1] || left[2])
        return 25;
    glDisable(GL_SCISSOR_TEST);
    glViewport(INT_MAX, INT_MAX, 16, 16);
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glVertex2f(0.0f, 1.0f);
    glEnd();
    glBegin(GL_LINES);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, 1.0f);
    glEnd();
    glBegin(GL_POINTS);
    glVertex2f(0.0f, 0.0f);
    glEnd();
    read_pixel(8, 8, left);
    if (glGetError() != GL_NO_ERROR || left[0] || left[1] || left[2])
        return 26;
    glViewport(0, 0, 16, 16);
    glScissor(0, 0, 8, 16);
    glEnable(GL_SCISSOR_TEST);

    glLineWidth(5.0f);
    glLineWidth(0.0f);
    if (glGetError() != GL_INVALID_VALUE)
        return 9;
    glLineWidth(NAN);
    if (glGetError() != GL_NO_ERROR)
        return 10;
    glGetFloatv(GL_LINE_WIDTH, &value);
    if (!isnan(value))
        return 11;
    glPointSize(4.0f);
    glPointSize(NAN);
    if (glGetError() != GL_INVALID_VALUE)
        return 12;
    glGetFloatv(GL_POINT_SIZE, &value);
    if (value != 4.0f)
        return 13;
    glDepthRangef(-1.0f, 2.0f);
    glGetFloatv(GL_DEPTH_RANGE, depth_range);
    if (depth_range[0] != 0.0f || depth_range[1] != 1.0f)
        return 15;
    glDepthRangef(0.8f, 0.2f);
    glGetFloatv(GL_DEPTH_RANGE, depth_range);
    if (depth_range[0] != 0.8f || depth_range[1] != 0.2f)
        return 16;
    glGetIntegerv(GL_DEPTH_RANGE, integer_range);
    glGetBooleanv(GL_DEPTH_RANGE, boolean_range);
    if (glGetError() != GL_NO_ERROR || integer_range[0] <= integer_range[1] ||
        !boolean_range[0] || !boolean_range[1])
        return 17;
    glPolygonOffset(2.0f, -3.0f);
    glGetIntegerv(GL_POLYGON_OFFSET_FACTOR, &integer_value);
    if (glGetError() != GL_NO_ERROR || integer_value != 2)
        return 18;
    glGetIntegerv(GL_POLYGON_OFFSET_UNITS, &integer_value);
    if (glGetError() != GL_NO_ERROR || integer_value != -3)
        return 19;
    glSampleCoverage(0.25f, GL_TRUE);
    glGetIntegerv(GL_SAMPLE_COVERAGE_VALUE, &integer_value);
    if (glGetError() != GL_NO_ERROR || integer_value != 0)
        return 23;
    glGetBooleanv(GL_SAMPLE_COVERAGE_INVERT, boolean_range);
    if (glGetError() != GL_NO_ERROR || !boolean_range[0])
        return 24;

    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT_AND_BACK);
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.8f, -0.8f);
    glVertex2f(0.8f, -0.8f);
    glVertex2f(0.0f, 0.8f);
    glEnd();
    read_pixel(8, 8, left);
    if (left[0] || left[1] || left[2])
        return 14;

    ntglDestroyContext(context);
    return 0;
}
