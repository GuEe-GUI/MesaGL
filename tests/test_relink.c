#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint compiled = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    return compiled ? shader : 0;
}

static int pixel_matches(const uint8_t *pixel, uint8_t red, uint8_t green)
{
    return pixel[0] == red && pixel[1] == green && pixel[2] == 0 &&
           pixel[3] == 255;
}

int main(void)
{
    uint8_t pixels[8 * 8 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 8, 8, 8 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    const GLfloat vertices[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    const char vertex_source[] =
        "attribute vec2 position;"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); }";
    const char fragment_source[] =
        "precision mediump float; uniform vec4 color;"
        "void main() { gl_FragColor = color; }";
    const char invalid_fragment_source[] =
        "precision mediump float; vec4 no_main() { return vec4(1.0); }";
    NTGLcontext *context;
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint buffer;
    GLint position;
    GLint color;
    GLint rebound_position;
    GLint linked;
    uint8_t pixel[4];
    const GLchar *replacement_source = invalid_fragment_source;

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
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 3;

    position = glGetAttribLocation(program, "position");
    color = glGetUniformLocation(program, "color");
    glBindAttribLocation(program, 3, "position");
    if (glGetAttribLocation(program, "position") != position)
        return 11;
    glUseProgram(program);
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray((GLuint)position);
    glVertexAttribPointer((GLuint)position, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glViewport(0, 0, 8, 8);
    glUniform4f(color, 1.0f, 0.0f, 0.0f, 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!pixel_matches(pixel, 255, 0))
        return 4;

    glShaderSource(fragment_shader, 1, &replacement_source, NULL);
    glCompileShader(fragment_shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked)
        return 5;
    if (glGetAttribLocation(program, "position") != -1 ||
        glGetError() != GL_INVALID_OPERATION)
        return 12;

    glUniform4f(color, 0.0f, 1.0f, 0.0f, 1.0f);
    if (glGetError() != GL_NO_ERROR)
        return 6;
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (glGetError() != GL_NO_ERROR)
        return 7;
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!pixel_matches(pixel, 0, 255))
        return 8;

    glUseProgram(program);
    if (glGetError() != GL_INVALID_OPERATION)
        return 9;
    glGetUniformLocation(program, "color");
    if (glGetError() != GL_INVALID_OPERATION)
        return 10;

    replacement_source = fragment_source;
    glShaderSource(fragment_shader, 1, &replacement_source, NULL);
    glCompileShader(fragment_shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    rebound_position = glGetAttribLocation(program, "position");
    if (!linked || rebound_position != 3)
        return 13;
    glUseProgram(program);
    glEnableVertexAttribArray((GLuint)rebound_position);
    glVertexAttribPointer((GLuint)rebound_position, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    color = glGetUniformLocation(program, "color");
    glUniform4f(color, 1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!pixel_matches(pixel, 255, 0))
        return 14;

    glDeleteBuffers(1, &buffer);
    glDeleteProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    ntglDestroyContext(context);
    puts("program relink tests passed");
    return 0;
}
