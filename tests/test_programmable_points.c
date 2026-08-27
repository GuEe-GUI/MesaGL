#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint compiled = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    return compiled ? shader : 0;
}

static int close_byte(unsigned int value, unsigned int expected)
{
    return value + 2 >= expected && value <= expected + 2;
}

int main(void)
{
    static const char vertex_source[] =
        "attribute vec2 position;"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); gl_PointSize = 1.5; }";
    static const char fragment_source[] =
        "precision mediump float;"
        "void main() { gl_FragColor = vec4(gl_PointCoord, 0.0, 1.0); }";
    static const GLfloat position[] = {0.046875f, 0.046875f};
    uint8_t pixels[32 * 32 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 32, 32, 32 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLint linked = GL_FALSE;
    GLubyte covered[4];
    GLubyte outside[4];

    if (!context)
        return 1;
    vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
    fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (!vertex_shader || !fragment_shader)
        return 2;
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glBindAttribLocation(program, 0, "position");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 3;
    glUseProgram(program);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, position);
    glViewport(0, 0, 32, 32);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_POINTS, 0, 1);
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, covered);
    glReadPixels(17, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, outside);
    if (!close_byte(covered[0], 85) || !close_byte(covered[1], 170) ||
        covered[2] || covered[3] != 255)
        return 4;
    if (outside[0] || outside[1] || outside[2] || outside[3] != 255)
        return 5;

    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return 0;
}
