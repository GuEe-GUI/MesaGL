#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>

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
    uint32_t pixels[16 * 16] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 16, 16, 16 * (int)sizeof(*pixels), NTGL_XRGB8888,
        NTGL_ORIGIN_BOTTOM_LEFT};
    static const GLfloat positions[] = {
        -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
    };
    const char *vertex_source =
        "attribute vec2 position;"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); }";
    const char *fragment_source =
        "precision mediump float;"
        "void adjust(inout vec4 values[2], out float gain) {"
        "values[0] += vec4(0.1, 0.1, 0.0, 0.0);"
        "values[1] *= vec4(1.0, 0.5, 1.0, 1.0); gain = 0.25; }"
        "void main() { vec4 values[2]; values[0] = vec4(0.1, 0.2, 0.0, 0.5);"
        "values[1] = vec4(0.2, 0.6, 0.0, 0.5); float gain;"
        "adjust(((values)), (gain)); gl_FragColor = values[0] + values[1] +"
        "vec4(gain, 0.0, 0.0, 0.0); }";
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex;
    GLuint fragment;
    GLuint program;
    GLint linked = 0;
    GLint position;
    GLubyte pixel[4];

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
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[256];

        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "function-array link failed: %s\n", log);
        return 3;
    }
    position = glGetAttribLocation(program, "position");
    glUseProgram(program);
    glVertexAttribPointer((GLuint)position, 2, GL_FLOAT, GL_FALSE, 0, positions);
    glEnableVertexAttribArray((GLuint)position);
    glViewport(0, 0, 16, 16);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 165 || pixel[0] > 167 || pixel[1] < 152 || pixel[1] > 154 ||
        pixel[2] != 0 || pixel[3] != 255 || glGetError() != GL_NO_ERROR)
        return 4;

    glDeleteProgram(program);
    glDeleteShader(fragment);
    glDeleteShader(vertex);
    ntglDestroyContext(context);
    puts("function array tests passed");
    return 0;
}
