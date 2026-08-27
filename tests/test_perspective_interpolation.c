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
    if (!compiled) {
        char log[512];

        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "shader compile failed: %s\n", log);
        return 0;
    }
    return shader;
}

int main(void)
{
    static const char vertex_source[] =
        "attribute vec4 position;attribute vec2 input_uv;varying vec2 uv;"
        "void main(){gl_Position=position;uv=input_uv;}";
    static const char fragment_source[] =
        "precision highp float;varying vec2 uv;"
        "void main(){gl_FragColor=vec4(uv,gl_FragCoord.w*0.25,1.0);}";
    static const GLfloat vertices[] = {
        -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
         2.0f, -2.0f, 0.0f, 2.0f, 1.0f, 0.0f,
         0.0f,  4.0f, 0.0f, 4.0f, 0.5f, 1.0f,
    };
    uint8_t pixels[32 * 32 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 32, 32, 32 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT
    };
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLint linked = GL_FALSE;
    GLint position;
    GLint input_uv;
    GLubyte pixel[4];

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
    glUseProgram(program);
    position = glGetAttribLocation(program, "position");
    input_uv = glGetAttribLocation(program, "input_uv");
    if (position < 0 || input_uv < 0)
        return 4;
    glVertexAttribPointer((GLuint)position, 4, GL_FLOAT, GL_FALSE,
                          6 * sizeof(GLfloat), vertices);
    glVertexAttribPointer((GLuint)input_uv, 2, GL_FLOAT, GL_FALSE,
                          6 * sizeof(GLfloat), vertices + 4);
    glEnableVertexAttribArray((GLuint)position);
    glEnableVertexAttribArray((GLuint)input_uv);
    glViewport(0, 0, 32, 32);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 101 || pixel[0] > 103 ||
        pixel[1] < 67 || pixel[1] > 69 ||
        pixel[2] < 30 || pixel[2] > 32 || pixel[3] != 255) {
        fprintf(stderr, "perspective pixel: %u %u %u %u\n",
                pixel[0], pixel[1], pixel[2], pixel[3]);
        return 5;
    }
    if (glGetError() != GL_NO_ERROR)
        return 6;
    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    puts("perspective interpolation tests passed");
    return 0;
}
