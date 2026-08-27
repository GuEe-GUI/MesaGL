#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int append(char *output, size_t capacity, const char *text)
{
    size_t used = strlen(output);
    size_t length = strlen(text);

    if (used + length >= capacity)
        return 0;
    memcpy(output + used, text, length + 1);
    return 1;
}

static int build_fragment_shader(char *output, size_t capacity,
                                 int uniform_count)
{
    int index;

    strcpy(output, "precision mediump float;");
    for (index = 0; index < uniform_count; ++index) {
        char declaration[40];

        snprintf(declaration, sizeof(declaration), "uniform float packed_%d;",
                 index);
        if (!append(output, capacity, declaration))
            return 0;
    }
    if (!append(output, capacity, "void main(){float sum=0.0;"))
        return 0;
    for (index = 0; index < uniform_count; ++index) {
        char expression[32];

        snprintf(expression, sizeof(expression), "sum+=packed_%d;", index);
        if (!append(output, capacity, expression))
            return 0;
    }
    return append(output, capacity,
                  "gl_FragColor=vec4(sum,0.5,0.25,1.0);}");
}

static int build_mat2_fragment_shader(char *output, size_t capacity,
                                      int overflow_scalar)
{
    int index;

    strcpy(output, "precision mediump float;");
    for (index = 0; index < 8; ++index) {
        char declaration[40];

        snprintf(declaration, sizeof(declaration), "uniform mat2 matrix_%d;",
                 index);
        if (!append(output, capacity, declaration))
            return 0;
    }
    if (overflow_scalar &&
        !append(output, capacity, "uniform float overflow;"))
        return 0;
    if (!append(output, capacity, "void main(){float sum=0.0;"))
        return 0;
    for (index = 0; index < 8; ++index) {
        char expression[64];

        snprintf(expression, sizeof(expression),
                 "sum+=matrix_%d[0][0]+matrix_%d[1][1];", index, index);
        if (!append(output, capacity, expression))
            return 0;
    }
    if (overflow_scalar &&
        !append(output, capacity, "sum+=overflow;"))
        return 0;
    return append(output, capacity, "gl_FragColor=vec4(sum);}");
}

static GLuint compile(GLenum stage, const char *source)
{
    GLuint shader = glCreateShader(stage);
    GLint compiled = 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    return compiled ? shader : 0;
}

static GLuint link(const char *fragment_source, GLint *linked)
{
    static const char vertex_source[] =
        "attribute vec2 position;"
        "void main(){gl_Position=vec4(position,0.0,1.0);}";
    GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
    GLuint program = glCreateProgram();

    if (!vertex || !fragment) {
        *linked = 0;
        return program;
    }
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, linked);
    glDeleteShader(fragment);
    glDeleteShader(vertex);
    return program;
}

int main(void)
{
    static const GLfloat positions[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    uint8_t framebuffer_pixels[4 * 4 * 4] = {0};
    NTGLframebuffer framebuffer = {
        framebuffer_pixels, 4, 4, 4 * 4, NTGL_XRGB8888,
        NTGL_ORIGIN_TOP_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    char fragment_source[8192];
    GLuint program;
    GLint linked = 0;
    GLint active_uniforms = 0;
    GLint position;
    uint8_t pixel[4] = {0};
    int index;

    if (!context || !build_fragment_shader(fragment_source,
                                           sizeof(fragment_source), 64))
        return 1;
    ntglMakeCurrent(context);
    program = link(fragment_source, &linked);
    if (!linked)
        return 2;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &active_uniforms);
    position = glGetAttribLocation(program, "position");
    if (active_uniforms != 64 || position < 0)
        return 3;
    glUseProgram(program);
    for (index = 0; index < 64; ++index) {
        char name[32];
        GLint location;

        snprintf(name, sizeof(name), "packed_%d", index);
        location = glGetUniformLocation(program, name);
        if (location < 0)
            return 4;
        glUniform1f(location, 1.0f / 64.0f);
    }
    glVertexAttribPointer((GLuint)position, 2, GL_FLOAT, GL_FALSE, 0,
                          positions);
    glEnableVertexAttribArray((GLuint)position);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (glGetError() != GL_NO_ERROR || pixel[0] != 255 ||
        pixel[1] < 127 || pixel[1] > 128 || pixel[2] < 63 || pixel[2] > 64 ||
        pixel[3] != 255) {
        fprintf(stderr, "uniform packing pixel: %u %u %u %u\n", pixel[0],
                pixel[1], pixel[2], pixel[3]);
        return 5;
    }
    glDeleteProgram(program);

    if (!build_mat2_fragment_shader(fragment_source, sizeof(fragment_source),
                                    0))
        return 6;
    program = link(fragment_source, &linked);
    if (!linked)
        return 7;
    glDeleteProgram(program);
    if (!build_mat2_fragment_shader(fragment_source, sizeof(fragment_source),
                                    1))
        return 8;
    program = link(fragment_source, &linked);
    if (linked)
        return 9;
    glDeleteProgram(program);

    if (!build_fragment_shader(fragment_source, sizeof(fragment_source), 65))
        return 10;
    program = link(fragment_source, &linked);
    if (linked)
        return 11;
    glDeleteProgram(program);
    ntglDestroyContext(context);
    return 0;
}
