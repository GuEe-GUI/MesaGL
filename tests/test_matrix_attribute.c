#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static GLuint compile_shader(GLenum type, const char *source, int expected)
{
    GLuint shader = glCreateShader(type);
    GLint compiled = 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!!compiled != expected) {
        char log[256];

        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "unexpected shader result: %s\n", log);
        return 0;
    }
    return shader;
}

int main(void)
{
    uint32_t pixels[32 * 32] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 32, 32, 32 * (int)sizeof(*pixels), NTGL_XRGB8888,
        NTGL_ORIGIN_BOTTOM_LEFT};
    static const GLfloat positions[] = {
        -0.8f, -0.8f, 0.8f, -0.8f, 0.0f, 0.8f,
    };
    const char *vertex_source =
        "attribute mat2 transform; attribute vec2 position;"
        "void main() { gl_Position = vec4(transform * position, 0.0, 1.0); }";
    const char *fragment_source =
        "precision mediump float;"
        "void main() { gl_FragColor = vec4(0.2, 0.8, 0.0, 1.0); }";
    const char *illegal_array_source =
        "attribute vec2 positions[2];"
        "void main() { gl_Position = vec4(positions[0], 0.0, 1.0); }";
    const char *illegal_integer_source =
        "attribute int position;"
        "void main() { gl_Position = vec4(float(position)); }";
    const char *illegal_struct_source =
        "struct Input { vec2 position; }; attribute Input inputValue;"
        "void main() { gl_Position = vec4(inputValue.position, 0.0, 1.0); }";
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex;
    GLuint fragment;
    GLuint illegal_array;
    GLuint illegal_integer;
    GLuint illegal_struct;
    GLuint program;
    GLuint conflict_program;
    GLint linked = 0;
    GLint active = 0;
    GLint array_size = 0;
    GLenum type = 0;
    GLchar name[32];
    GLubyte pixel[4];

    if (!context)
        return 1;
    ntglMakeCurrent(context);
    illegal_array = compile_shader(GL_VERTEX_SHADER, illegal_array_source, 0);
    if (!illegal_array)
        return 2;
    glDeleteShader(illegal_array);
    illegal_integer = compile_shader(GL_VERTEX_SHADER, illegal_integer_source, 0);
    illegal_struct = compile_shader(GL_VERTEX_SHADER, illegal_struct_source, 0);
    if (!illegal_integer || !illegal_struct)
        return 8;
    glDeleteShader(illegal_integer);
    glDeleteShader(illegal_struct);

    vertex = compile_shader(GL_VERTEX_SHADER, vertex_source, 1);
    fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source, 1);
    if (!vertex || !fragment)
        return 3;
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glBindAttribLocation(program, 2, "transform");
    glBindAttribLocation(program, 4, "position");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked || glGetAttribLocation(program, "transform") != 2 ||
        glGetAttribLocation(program, "position") != 4)
        return 4;
    glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &active);
    glGetActiveAttrib(program, 0, sizeof(name), NULL, &array_size, &type, name);
    if (active != 2 || strcmp(name, "transform") || array_size != 1 ||
        type != GL_FLOAT_MAT2)
        return 5;

    glUseProgram(program);
    glVertexAttrib2f(2, 1.0f, 0.0f);
    glVertexAttrib2f(3, 0.0f, 1.0f);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, 0, positions);
    glEnableVertexAttribArray(4);
    glViewport(0, 0, 32, 32);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 50 || pixel[0] > 52 || pixel[1] < 203 || pixel[1] > 205 ||
        pixel[2] != 0 || pixel[3] != 255 || glGetError() != GL_NO_ERROR)
        return 6;

    conflict_program = glCreateProgram();
    glAttachShader(conflict_program, vertex);
    glAttachShader(conflict_program, fragment);
    glBindAttribLocation(conflict_program, 2, "transform");
    glBindAttribLocation(conflict_program, 3, "position");
    glLinkProgram(conflict_program);
    glGetProgramiv(conflict_program, GL_LINK_STATUS, &linked);
    if (linked)
        return 7;

    glDeleteProgram(conflict_program);
    glDeleteProgram(program);
    glDeleteShader(fragment);
    glDeleteShader(vertex);
    ntglDestroyContext(context);
    puts("matrix attribute tests passed");
    return 0;
}
