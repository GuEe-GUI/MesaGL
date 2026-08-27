#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>

static GLuint compile_shader(GLenum type, const char *text)
{
    GLuint shader = glCreateShader(type);
    GLint status = 0;

    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    return status ? shader : 0;
}

int main(void)
{
    static const char vertex_source[] =
        "attribute vec2 position;"
        "attribute vec2 texcoord;"
        "varying vec2 uv;"
        "void main() { uv = texcoord; gl_Position = vec4(position, 0.0, 1.0); }";
    static const char fragment_source[] =
        "#extension GL_OES_standard_derivatives : enable\n"
        "precision mediump float;"
        "uniform sampler2D image;"
        "uniform float lod_bias;"
        "varying vec2 uv;"
        "void main() { vec2 transformed = uv * 2.0 + vec2(0.125);"
        "vec4 sampled = texture2DProj(image, vec3(transformed * 2.0, 2.0), lod_bias);"
        "gl_FragColor = vec4(sampled.rgb, clamp(fwidth(transformed).x, 0.0, 1.0)); }";
    static const GLfloat vertices[] = {
        -1.0f, -1.0f, 0.0f,   0.0f,
        3.0f,  -1.0f, 128.0f, 0.0f,
        -1.0f, 3.0f,  0.0f,   128.0f,
    };
    static const GLfloat constant_vertices[] = {
        -1.0f, -1.0f, 0.1875f, 0.1875f,
        3.0f,  -1.0f, 0.1875f, 0.1875f,
        -1.0f, 3.0f,  0.1875f, 0.1875f,
    };
    static const GLfloat unit_lod_vertices[] = {
        -1.0f, -1.0f, 0.0f,  0.0f,
        3.0f,  -1.0f, 8.0f, 0.0f,
        -1.0f, 3.0f,  0.0f,  8.0f,
    };
    static const GLubyte red_level[16] = {
        255, 0, 0, 255, 255, 0, 0, 255,
        255, 0, 0, 255, 255, 0, 0, 255,
    };
    static const GLubyte blue_level[4] = {0, 0, 255, 255};
    static const GLubyte filter_level[16] = {
        255, 0,   0,   255,
        0,   255, 0,   255,
        0,   0,   255, 255,
        255, 255, 255, 255,
    };
    uint8_t texture_pixels[8 * 8 * 4];
    uint8_t pixels[16 * 16 * 4] = {0};
    NTGLframebuffer framebuffer = {pixels, 16, 16, 16 * 4, NTGL_RGBA8888,
                                   NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint buffer;
    GLuint texture;
    GLint status;
    GLint sampler;
    GLint lod_bias;
    GLubyte sampled[4];
    int x;
    int y;

    if (!context)
        return 1;
    for (y = 0; y < 8; ++y)
        for (x = 0; x < 8; ++x) {
            GLubyte *pixel = texture_pixels + (y * 8 + x) * 4;

            pixel[0] = (x + y) & 1 ? 255 : 0;
            pixel[1] = 0;
            pixel[2] = (x + y) & 1 ? 0 : 255;
            pixel[3] = 255;
        }
    vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
    fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (!vertex_shader || !fragment_shader)
        return 2;
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glBindAttribLocation(program, 0, "position");
    glBindAttribLocation(program, 1, "texcoord");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status)
        return 3;
    glUseProgram(program);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 texture_pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenerateMipmap(GL_TEXTURE_2D);
    sampler = glGetUniformLocation(program, "image");
    lod_bias = glGetUniformLocation(program, "lod_bias");
    glUniform1i(sampler, 0);
    glUniform1f(lod_bias, 0.0f);

    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                          (const void *)(2 * sizeof(GLfloat)));
    glViewport(0, 0, 16, 16);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sampled);
    if (sampled[0] < 126 || sampled[0] > 129 || sampled[1] != 0 || sampled[2] < 126 ||
        sampled[2] > 129 || sampled[3] != 255)
        return 4;

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, red_level);
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, blue_level);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glBufferData(GL_ARRAY_BUFFER, sizeof(unit_lod_vertices), unit_lod_vertices, GL_STATIC_DRAW);
    glUniform1f(lod_bias, 0.5f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sampled);
    if (sampled[0] < 126 || sampled[0] > 129 || sampled[1] != 0 || sampled[2] < 126 ||
        sampled[2] > 129 || sampled[3] < 126 || sampled[3] > 129) {
        fprintf(stderr, "trilinear pixel: %u %u %u %u\n", sampled[0], sampled[1], sampled[2],
                sampled[3]);
        return 5;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 filter_level);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBufferData(GL_ARRAY_BUFFER, sizeof(constant_vertices), constant_vertices, GL_STATIC_DRAW);
    glUniform1f(lod_bias, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sampled);
    if (sampled[0] < 126 || sampled[0] > 129 || sampled[1] < 126 || sampled[1] > 129 ||
        sampled[2] < 126 || sampled[2] > 129 || sampled[3] != 0) {
        fprintf(stderr, "magnification pixel: %u %u %u %u\n", sampled[0], sampled[1],
                sampled[2], sampled[3]);
        return 6;
    }

    glDeleteBuffers(1, &buffer);
    glDeleteTextures(1, &texture);
    glUseProgram(0);
    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return 0;
}
