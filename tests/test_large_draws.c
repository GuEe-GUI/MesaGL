#include "GLES2/gl2.h"
#include "mesaGL/config.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <string.h>

enum {
    LARGE_COUNT = MESAGL_MAX_VERTICES + 3,
};

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint status = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    return status ? shader : 0;
}

static void reset_vertices(GLfloat vertices[LARGE_COUNT][2])
{
    int i;

    for (i = 0; i < LARGE_COUNT; ++i) {
        vertices[i][0] = -2.0f;
        vertices[i][1] = -2.0f;
    }
}

static int center_is_red(void)
{
    GLubyte pixel[4];

    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    return pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 &&
           pixel[3] == 255;
}

static int draw_and_check(GLuint buffer, GLenum mode,
                          GLfloat vertices[LARGE_COUNT][2])
{
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * LARGE_COUNT * 2,
                 vertices, GL_STREAM_DRAW);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(mode, 0, LARGE_COUNT);
    return glGetError() == GL_NO_ERROR && center_is_red();
}

int main(void)
{
    static const char vertex_source[] =
        "attribute vec2 position;"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); }";
    static const char fragment_source[] =
        "precision mediump float;"
        "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }";
    uint8_t pixels[32 * 32 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 32, 32, 32 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    GLfloat vertices[LARGE_COUNT][2];
    GLushort indices[LARGE_COUNT];
    NTGLcontext *context;
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint vertex_buffer;
    GLuint index_buffer;
    GLint linked = GL_FALSE;
    int i;

    context = ntglCreateContext(&framebuffer, NULL);
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
    glViewport(0, 0, 32, 32);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glGenBuffers(1, &vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    reset_vertices(vertices);
    vertices[MESAGL_MAX_VERTICES - 1][0] = -0.9f;
    vertices[MESAGL_MAX_VERTICES - 1][1] = 0.0f;
    vertices[MESAGL_MAX_VERTICES][0] = 0.9f;
    vertices[MESAGL_MAX_VERTICES][1] = 0.0f;
    if (!draw_and_check(vertex_buffer, GL_LINE_STRIP, vertices))
        return 4;

    reset_vertices(vertices);
    vertices[MESAGL_MAX_VERTICES - 2][0] = -0.9f;
    vertices[MESAGL_MAX_VERTICES - 2][1] = -0.9f;
    vertices[MESAGL_MAX_VERTICES - 1][0] = 0.9f;
    vertices[MESAGL_MAX_VERTICES - 1][1] = -0.9f;
    vertices[MESAGL_MAX_VERTICES][0] = 0.0f;
    vertices[MESAGL_MAX_VERTICES][1] = 0.9f;
    if (!draw_and_check(vertex_buffer, GL_TRIANGLE_STRIP, vertices))
        return 5;

    reset_vertices(vertices);
    vertices[0][0] = -0.9f;
    vertices[0][1] = -0.9f;
    vertices[MESAGL_MAX_VERTICES - 1][0] = 0.9f;
    vertices[MESAGL_MAX_VERTICES - 1][1] = -0.9f;
    vertices[MESAGL_MAX_VERTICES][0] = 0.0f;
    vertices[MESAGL_MAX_VERTICES][1] = 0.9f;
    if (!draw_and_check(vertex_buffer, GL_TRIANGLE_FAN, vertices))
        return 6;

    reset_vertices(vertices);
    vertices[0][0] = -0.9f;
    vertices[0][1] = 0.0f;
    vertices[LARGE_COUNT - 1][0] = 0.9f;
    vertices[LARGE_COUNT - 1][1] = 0.0f;
    if (!draw_and_check(vertex_buffer, GL_LINE_LOOP, vertices))
        return 7;

    reset_vertices(vertices);
    vertices[LARGE_COUNT - 4][0] = -0.9f;
    vertices[LARGE_COUNT - 4][1] = -0.9f;
    vertices[LARGE_COUNT - 3][0] = 0.9f;
    vertices[LARGE_COUNT - 3][1] = -0.9f;
    vertices[LARGE_COUNT - 2][0] = 0.0f;
    vertices[LARGE_COUNT - 2][1] = 0.9f;
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    for (i = 0; i < LARGE_COUNT; ++i)
        indices[i] = (GLushort)i;
    glGenBuffers(1, &index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES, LARGE_COUNT, GL_UNSIGNED_SHORT, NULL);
    if (glGetError() != GL_NO_ERROR || !center_is_red())
        return 8;

    glDeleteBuffers(1, &index_buffer);
    glDeleteBuffers(1, &vertex_buffer);
    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return 0;
}
