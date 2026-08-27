#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <string.h>

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint compiled = 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    return compiled ? shader : 0;
}

int main(void)
{
    uint32_t pixels[32 * 32] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 32, 32, 32 * (int)sizeof(*pixels), NTGL_XRGB8888,
        NTGL_ORIGIN_BOTTOM_LEFT
    };
    static const GLfloat positions[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         0.0f,  1.0f
    };
    static const GLfloat colors[] = {
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f, 0.0f
    };
    static const GLfloat signals[] = {
        0.25f, 0.0f,
        0.25f, 0.0f,
        0.25f, 0.0f
    };
    const char *vertex_source =
        "attribute vec2 position, signal; varying vec2 ignored, encoded;"
        "uniform vec2 offset, scale; uniform vec4 unusedVertex;"
        "void main() { encoded = signal;"
        "gl_Position = vec4(position * scale + offset, 0.0, 1.0); }";
    const char *fragment_source =
        "precision mediump float; varying vec2 ignored, encoded;"
        "uniform vec4 first, second[2]; uniform vec4 unusedFragment;"
        "void main() { vec4 base = first + second[1], signal = vec4(encoded, 0.0, 0.0);"
        "{ vec4 base = vec4(1.0); base *= 0.5; }"
        "gl_FragColor = base + signal; }";
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex;
    GLuint fragment;
    GLuint program;
    GLint linked = 0;
    GLint active_uniforms = 0;
    GLint active_attributes = 0;
    GLint offset_location;
    GLint scale_location;
    GLint first_location;
    GLint second_location;
    GLint second_one_location;
    GLubyte center[4] = {0};

    if (!context)
        return 1;
    ntglMakeCurrent(context);
    vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
    fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (!vertex || !fragment)
        return 2;
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "position");
    glBindAttribLocation(program, 1, "signal");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 3;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &active_uniforms);
    if (active_uniforms != 4)
        return 4;
    glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &active_attributes);
    if (active_attributes != 2 || glGetAttribLocation(program, "position") != 0 ||
        glGetAttribLocation(program, "signal") != 1)
        return 7;
    offset_location = glGetUniformLocation(program, "offset");
    scale_location = glGetUniformLocation(program, "scale");
    first_location = glGetUniformLocation(program, "first");
    second_location = glGetUniformLocation(program, "second");
    second_one_location = glGetUniformLocation(program, "second[1]");
    if (offset_location < 0 || scale_location < 0 || first_location < 0 ||
        second_location < 0 || second_one_location != second_location + 1 ||
        glGetUniformLocation(program, "unusedVertex") != -1 ||
        glGetUniformLocation(program, "unusedFragment") != -1)
        return 5;
    glUseProgram(program);
    glUniform2f(offset_location, 0.0f, 0.0f);
    glUniform2f(scale_location, 1.0f, 1.0f);
    glUniform4f(first_location, 0.25f, 0.0f, 0.0f, 1.0f);
    glUniform4fv(second_location, 2, colors);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, positions);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, signals);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glViewport(0, 0, 32, 32);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
    if (center[0] < 126 || center[0] > 129 || center[1] < 126 || center[1] > 129 ||
        center[2] != 0 || center[3] != 255)
        return 6;
    glDeleteProgram(program);
    glDeleteShader(fragment);
    glDeleteShader(vertex);
    ntglDestroyContext(context);
    return 0;
}
