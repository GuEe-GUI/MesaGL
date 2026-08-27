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

static int build_shader(char *output, size_t capacity, GLenum stage,
                        int varying_count, const char *builtin_expression)
{
    int index;

    output[0] = '\0';
    if (stage == GL_FRAGMENT_SHADER &&
        !append(output, capacity, "precision mediump float;"))
        return 0;
    for (index = 0; index < varying_count; ++index) {
        char declaration[40];

        snprintf(declaration, sizeof(declaration), "varying vec2 packed_%d;",
                 index);
        if (!append(output, capacity, declaration))
            return 0;
    }
    if (stage == GL_VERTEX_SHADER) {
        if (!append(output, capacity,
                    "attribute vec2 position;void main(){"))
            return 0;
        for (index = 0; index < varying_count; ++index) {
            char assignment[64];

            snprintf(assignment, sizeof(assignment),
                     "packed_%d=vec2(0.03125);", index);
            if (!append(output, capacity, assignment))
                return 0;
        }
        return append(output, capacity,
                      "gl_Position=vec4(position,0.0,1.0);}");
    }
    if (!append(output, capacity, "void main(){float sum=0.0;"))
        return 0;
    for (index = 0; index < varying_count; ++index) {
        char expression[48];

        snprintf(expression, sizeof(expression),
                 "sum+=packed_%d.x+packed_%d.y;", index, index);
        if (!append(output, capacity, expression))
            return 0;
    }
    if (builtin_expression &&
        !append(output, capacity, builtin_expression))
        return 0;
    return append(output, capacity,
                  "gl_FragColor=vec4(sum,0.5,0.25,1.0);}");
}

static int build_mat2_shader(char *output, size_t capacity, GLenum stage,
                             int matrix_count, int overflow_scalar)
{
    int index;

    output[0] = '\0';
    if (stage == GL_FRAGMENT_SHADER &&
        !append(output, capacity, "precision mediump float;"))
        return 0;
    for (index = 0; index < matrix_count; ++index) {
        char declaration[40];

        snprintf(declaration, sizeof(declaration), "varying mat2 matrix_%d;",
                 index);
        if (!append(output, capacity, declaration))
            return 0;
    }
    if (overflow_scalar &&
        !append(output, capacity, "varying float overflow;"))
        return 0;
    if (stage == GL_VERTEX_SHADER) {
        if (!append(output, capacity, "void main(){"))
            return 0;
        for (index = 0; index < matrix_count; ++index) {
            char assignment[48];

            snprintf(assignment, sizeof(assignment),
                     "matrix_%d=mat2(0.03125);", index);
            if (!append(output, capacity, assignment))
                return 0;
        }
        if (overflow_scalar &&
            !append(output, capacity, "overflow=0.0;"))
            return 0;
        return append(output, capacity, "gl_Position=vec4(0.0);}");
    }
    if (!append(output, capacity, "void main(){float sum=0.0;"))
        return 0;
    for (index = 0; index < matrix_count; ++index) {
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

static GLuint link(const char *vertex_source, const char *fragment_source,
                   GLint *linked)
{
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
    char vertex_source[4096];
    char fragment_source[4096];
    GLuint program;
    GLint linked = 0;
    GLint position;
    uint8_t pixel[4] = {0};

    if (!context || !build_shader(vertex_source, sizeof(vertex_source),
                                  GL_VERTEX_SHADER, 16, NULL) ||
        !build_shader(fragment_source, sizeof(fragment_source),
                      GL_FRAGMENT_SHADER, 16, NULL))
        return 1;
    ntglMakeCurrent(context);
    program = link(vertex_source, fragment_source, &linked);
    if (!linked)
        return 2;
    position = glGetAttribLocation(program, "position");
    if (position < 0)
        return 3;
    glUseProgram(program);
    glVertexAttribPointer((GLuint)position, 2, GL_FLOAT, GL_FALSE, 0,
                          positions);
    glEnableVertexAttribArray((GLuint)position);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (glGetError() != GL_NO_ERROR || pixel[0] != 255 ||
        pixel[1] < 127 || pixel[1] > 128 || pixel[2] < 63 || pixel[2] > 64 ||
        pixel[3] != 255) {
        fprintf(stderr, "varying packing pixel: %u %u %u %u\n", pixel[0],
                pixel[1], pixel[2], pixel[3]);
        return 4;
    }
    glDeleteProgram(program);

    if (!build_mat2_shader(vertex_source, sizeof(vertex_source),
                           GL_VERTEX_SHADER, 8, 0) ||
        !build_mat2_shader(fragment_source, sizeof(fragment_source),
                           GL_FRAGMENT_SHADER, 8, 0))
        return 5;
    program = link(vertex_source, fragment_source, &linked);
    if (!linked)
        return 6;
    glDeleteProgram(program);
    if (!build_mat2_shader(vertex_source, sizeof(vertex_source),
                           GL_VERTEX_SHADER, 8, 1) ||
        !build_mat2_shader(fragment_source, sizeof(fragment_source),
                           GL_FRAGMENT_SHADER, 8, 1))
        return 7;
    program = link(vertex_source, fragment_source, &linked);
    if (linked)
        return 8;
    glDeleteProgram(program);

    if (!build_shader(vertex_source, sizeof(vertex_source), GL_VERTEX_SHADER,
                      16, NULL) ||
        !build_shader(fragment_source, sizeof(fragment_source),
                      GL_FRAGMENT_SHADER, 16,
                      "sum+=gl_FragCoord.x*0.0;"))
        return 9;
    program = link(vertex_source, fragment_source, &linked);
    if (!linked)
        return 10;
    glDeleteProgram(program);
    if (!build_shader(vertex_source, sizeof(vertex_source), GL_VERTEX_SHADER,
                      17, NULL) ||
        !build_shader(fragment_source, sizeof(fragment_source),
                      GL_FRAGMENT_SHADER, 17,
                      "sum+=gl_FragCoord.x*0.0;"))
        return 11;
    program = link(vertex_source, fragment_source, &linked);
    if (linked)
        return 12;
    glDeleteProgram(program);

    if (!build_shader(vertex_source, sizeof(vertex_source), GL_VERTEX_SHADER,
                      16, NULL) ||
        !build_shader(fragment_source, sizeof(fragment_source),
                      GL_FRAGMENT_SHADER, 16,
                      "sum+=gl_FragCoord.x*0.0+gl_PointCoord.x*0.0+"
                      "(gl_FrontFacing?0.0:0.0);"))
        return 13;
    program = link(vertex_source, fragment_source, &linked);
    if (!linked)
        return 14;
    glDeleteProgram(program);
    if (!build_shader(vertex_source, sizeof(vertex_source), GL_VERTEX_SHADER,
                      17, NULL) ||
        !build_shader(fragment_source, sizeof(fragment_source),
                      GL_FRAGMENT_SHADER, 17,
                      "sum+=gl_FragCoord.x*0.0+gl_PointCoord.x*0.0+"
                      "(gl_FrontFacing?0.0:0.0);"))
        return 15;
    program = link(vertex_source, fragment_source, &linked);
    if (linked)
        return 16;
    glDeleteProgram(program);

    if (!build_shader(vertex_source, sizeof(vertex_source), GL_VERTEX_SHADER,
                      17, NULL) ||
        !build_shader(fragment_source, sizeof(fragment_source),
                      GL_FRAGMENT_SHADER, 17, NULL))
        return 17;
    program = link(vertex_source, fragment_source, &linked);
    if (linked)
        return 18;
    glDeleteProgram(program);
    ntglDestroyContext(context);
    return 0;
}
