#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint status = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    return status ? shader : 0;
}

static int red_pixel_count(void)
{
    GLubyte pixels[32 * 32 * 4];
    int count = 0;
    int i;

    glReadPixels(0, 0, 32, 32, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (i = 0; i < 32 * 32; ++i)
        if (pixels[i * 4] == 255 && pixels[i * 4 + 1] == 0 &&
            pixels[i * 4 + 2] == 0)
            ++count;
    return count;
}

static int draw_clip_case(GLuint buffer, const GLfloat vertices[3][4],
                          int should_draw)
{
    int count;

    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, 12 * sizeof(GLfloat), vertices,
                 GL_STREAM_DRAW);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    count = red_pixel_count();
    if (glGetError() != GL_NO_ERROR)
        return 0;
    return should_draw ? count > 16 : count == 0;
}

static int draw_primitive_case(GLuint buffer, GLenum mode,
                               const GLfloat *vertices, int vertex_count,
                               int should_draw)
{
    int count;

    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)vertex_count * 4 * (GLsizeiptr)sizeof(GLfloat),
                 vertices, GL_STREAM_DRAW);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(mode, 0, vertex_count);
    count = red_pixel_count();
    if (glGetError() != GL_NO_ERROR)
        return 0;
    return should_draw ? count > 0 : count == 0;
}

int main(void)
{
    static const char vertex_source[] =
        "attribute vec4 position;"
        "void main() { gl_Position = position; }";
    static const char fragment_source[] =
        "precision mediump float;"
        "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }";
    static const GLfloat crossing[6][3][4] = {
        {{-2.0f, 0.0f, 0.0f, 1.0f}, {0.8f, -0.8f, 0.0f, 1.0f},
         {0.8f, 0.8f, 0.0f, 1.0f}},
        {{2.0f, 0.0f, 0.0f, 1.0f}, {-0.8f, 0.8f, 0.0f, 1.0f},
         {-0.8f, -0.8f, 0.0f, 1.0f}},
        {{0.0f, -2.0f, 0.0f, 1.0f}, {-0.8f, 0.8f, 0.0f, 1.0f},
         {0.8f, 0.8f, 0.0f, 1.0f}},
        {{0.0f, 2.0f, 0.0f, 1.0f}, {0.8f, -0.8f, 0.0f, 1.0f},
         {-0.8f, -0.8f, 0.0f, 1.0f}},
        {{0.0f, 0.8f, -2.0f, 1.0f}, {-0.8f, -0.8f, 0.0f, 1.0f},
         {0.8f, -0.8f, 0.0f, 1.0f}},
        {{0.0f, 0.8f, 2.0f, 1.0f}, {0.8f, -0.8f, 0.0f, 1.0f},
         {-0.8f, -0.8f, 0.0f, 1.0f}},
    };
    static const GLfloat outside[6][3][4] = {
        {{-2.0f, -0.8f, 0.0f, 1.0f}, {-2.0f, 0.8f, 0.0f, 1.0f},
         {-2.0f, 0.0f, 0.8f, 1.0f}},
        {{2.0f, -0.8f, 0.0f, 1.0f}, {2.0f, 0.8f, 0.0f, 1.0f},
         {2.0f, 0.0f, 0.8f, 1.0f}},
        {{-0.8f, -2.0f, 0.0f, 1.0f}, {0.8f, -2.0f, 0.0f, 1.0f},
         {0.0f, -2.0f, 0.8f, 1.0f}},
        {{-0.8f, 2.0f, 0.0f, 1.0f}, {0.8f, 2.0f, 0.0f, 1.0f},
         {0.0f, 2.0f, 0.8f, 1.0f}},
        {{-0.8f, 0.0f, -2.0f, 1.0f}, {0.8f, 0.0f, -2.0f, 1.0f},
         {0.0f, 0.8f, -2.0f, 1.0f}},
        {{-0.8f, 0.0f, 2.0f, 1.0f}, {0.8f, 0.0f, 2.0f, 1.0f},
         {0.0f, 0.8f, 2.0f, 1.0f}},
    };
    static const GLfloat negative_w[3][4] = {
        {-0.5f, -0.5f, 0.0f, -1.0f},
        {0.5f, -0.5f, 0.0f, -1.0f},
        {0.0f, 0.5f, 0.0f, -1.0f},
    };
    static const GLfloat crossing_lines[6][2][4] = {
        {{-2.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.0f, 0.0f, 1.0f}},
        {{2.0f, 0.0f, 0.0f, 1.0f}, {-0.5f, 0.0f, 0.0f, 1.0f}},
        {{0.0f, -2.0f, 0.0f, 1.0f}, {0.0f, 0.5f, 0.0f, 1.0f}},
        {{0.0f, 2.0f, 0.0f, 1.0f}, {0.0f, -0.5f, 0.0f, 1.0f}},
        {{-0.8f, 0.0f, -2.0f, 1.0f}, {0.5f, 0.0f, 0.5f, 1.0f}},
        {{-0.8f, 0.0f, 2.0f, 1.0f}, {0.5f, 0.0f, -0.5f, 1.0f}},
    };
    static const GLfloat outside_lines[6][2][4] = {
        {{-2.0f, -0.5f, 0.0f, 1.0f}, {-2.0f, 0.5f, 0.0f, 1.0f}},
        {{2.0f, -0.5f, 0.0f, 1.0f}, {2.0f, 0.5f, 0.0f, 1.0f}},
        {{-0.5f, -2.0f, 0.0f, 1.0f}, {0.5f, -2.0f, 0.0f, 1.0f}},
        {{-0.5f, 2.0f, 0.0f, 1.0f}, {0.5f, 2.0f, 0.0f, 1.0f}},
        {{-0.5f, 0.0f, -2.0f, 1.0f}, {0.5f, 0.0f, -2.0f, 1.0f}},
        {{-0.5f, 0.0f, 2.0f, 1.0f}, {0.5f, 0.0f, 2.0f, 1.0f}},
    };
    static const GLfloat inside_points[6][4] = {
        {-0.9f, 0.0f, 0.0f, 1.0f}, {0.9f, 0.0f, 0.0f, 1.0f},
        {0.0f, -0.9f, 0.0f, 1.0f}, {0.0f, 0.9f, 0.0f, 1.0f},
        {0.0f, 0.0f, -0.9f, 1.0f}, {0.0f, 0.0f, 0.9f, 1.0f},
    };
    static const GLfloat outside_points[6][4] = {
        {-1.1f, 0.0f, 0.0f, 1.0f}, {1.1f, 0.0f, 0.0f, 1.0f},
        {0.0f, -1.1f, 0.0f, 1.0f}, {0.0f, 1.1f, 0.0f, 1.0f},
        {0.0f, 0.0f, -1.1f, 1.0f}, {0.0f, 0.0f, 1.1f, 1.0f},
    };
    uint8_t pixels[32 * 32 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 32, 32, 32 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context;
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint buffer;
    GLint linked = GL_FALSE;
    int plane;

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
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, NULL);
    for (plane = 0; plane < 6; ++plane) {
        if (!draw_clip_case(buffer, crossing[plane], 1))
            return 4 + plane;
        if (!draw_clip_case(buffer, outside[plane], 0))
            return 10 + plane;
    }
    if (!draw_clip_case(buffer, negative_w, 0))
        return 16;
    for (plane = 0; plane < 6; ++plane) {
        if (!draw_primitive_case(buffer, GL_LINES, &crossing_lines[plane][0][0],
                                 2, 1))
            return 17 + plane;
        if (!draw_primitive_case(buffer, GL_LINES, &outside_lines[plane][0][0],
                                 2, 0))
            return 23 + plane;
        if (!draw_primitive_case(buffer, GL_POINTS, inside_points[plane], 1, 1))
            return 29 + plane;
        if (!draw_primitive_case(buffer, GL_POINTS, outside_points[plane], 1, 0))
            return 35 + plane;
    }

    glDeleteBuffers(1, &buffer);
    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return 0;
}
