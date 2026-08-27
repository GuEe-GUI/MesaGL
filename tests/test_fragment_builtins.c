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

static int color_matches(const GLubyte color[4], GLubyte red, GLubyte green,
                         GLubyte blue)
{
    return color[0] == red && color[1] == green && color[2] == blue &&
           color[3] == 255;
}

int main(void)
{
    uint8_t pixels[32 * 16 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 32, 16, 32 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    const GLfloat vertices[] = {
        -0.9f, -0.8f,
        -0.1f, -0.8f,
        -0.5f,  0.8f,
         0.1f, -0.8f,
         0.5f,  0.8f,
         0.9f, -0.8f,
    };
    const char vertex_source[] =
        "attribute vec2 position;"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); }";
    const char fragment_source[] =
        "precision mediump float;"
        "void main() {"
        "vec3 normal = faceforward(vec3(0.0, 1.0, 0.0),"
        "                          vec3(0.0, -1.0, 0.0),"
        "                          vec3(0.0, 1.0, 0.0));"
        "vec3 ray = refract(vec3(0.0, -1.0, 0.0),"
        "                   vec3(0.0, 1.0, 0.0), 0.5);"
        "mat2 product = matrixCompMult(mat2(1.0, 2.0, 3.0, 4.0),"
        "                              mat2(0.25, 0.0, 0.0, 0.25));"
        "bool valid = normal.y > 0.99 && -ray.y > 0.99 &&"
        "             product[1][1] > 0.99 &&"
        "             abs(gl_FragCoord.z - 0.5) < 0.01 &&"
        "             abs(gl_FragCoord.w - 1.0) < 0.01 &&"
        "             abs(fract(gl_FragCoord.x) - 0.5) < 0.01 &&"
        "             abs(fract(gl_FragCoord.y) - 0.5) < 0.01;"
        "if (!valid) gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0);"
        "else if (gl_FrontFacing) gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);"
        "else gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);"
        "}";
    const char fragment_without_output_source[] =
        "precision mediump float; void main() { }";
    NTGLcontext *context;
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint buffer;
    GLint linked = GL_FALSE;
    GLint position;
    GLubyte front[4];
    GLubyte back[4];
    GLubyte undefined_output[4];

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
    if (!linked) {
        GLchar log[256];

        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "fragment builtin link failed: %s\n", log);
        return 3;
    }
    position = glGetAttribLocation(program, "position");
    glUseProgram(program);
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray((GLuint)position);
    glVertexAttribPointer((GLuint)position, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glViewport(0, 0, 32, 16);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFrontFace(GL_CCW);
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glReadPixels(8, 7, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, front);
    glReadPixels(24, 7, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, back);
    if (!color_matches(front, 255, 0, 0) ||
        !color_matches(back, 0, 255, 0)) {
        fprintf(stderr, "unexpected builtin pixels: front=%u,%u,%u,%u "
                        "back=%u,%u,%u,%u\n",
                front[0], front[1], front[2], front[3], back[0], back[1],
                back[2], back[3]);
        return 4;
    }

    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
                                     fragment_without_output_source);
    if (!fragment_shader)
        return 5;
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 6;
    glUseProgram(program);
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(8, 7, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, undefined_output);
    if (undefined_output[0] != 0 || undefined_output[1] != 0 ||
        undefined_output[2] != 0 || undefined_output[3] != 0) {
        fprintf(stderr, "undefined fragment output was not deterministic: "
                        "%u,%u,%u,%u\n",
                undefined_output[0], undefined_output[1], undefined_output[2],
                undefined_output[3]);
        return 7;
    }

    glDeleteBuffers(1, &buffer);
    glDeleteProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    ntglDestroyContext(context);
    puts("fragment builtin tests passed");
    return 0;
}
