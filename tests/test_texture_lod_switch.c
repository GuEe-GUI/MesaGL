#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint status = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    return status == GL_TRUE ? shader : 0;
}

static int expected_color(const GLubyte pixel[4], int red, int green, int blue)
{
    return pixel[0] == red && pixel[1] == green && pixel[2] == blue &&
           pixel[3] == 255;
}

int main(void)
{
    static const char vertex_source[] =
        "attribute vec2 position;"
        "uniform sampler2D image;"
        "uniform float lod;"
        "varying vec4 color;"
        "void main() {"
        "color = texture2DLod(image, vec2(0.5), lod);"
        "gl_Position = vec4(position, 0.0, 1.0);"
        "}";
    static const char fragment_source[] =
        "precision mediump float;"
        "varying vec4 color;"
        "void main() { gl_FragColor = color; }";
    static const GLfloat triangle[] = {
        -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
    };
    static const GLfloat lods[] = {0.25f, 0.5f, 0.5001f, 1.5f, 1.5001f};
    static const GLubyte level1[] = {
        255, 0, 0, 255, 255, 0, 0, 255,
        255, 0, 0, 255, 255, 0, 0, 255,
    };
    static const GLubyte level2[] = {0, 255, 0, 255};
    static const GLushort packed_red[] = {0xf00f, 0xf00f, 0xf00f, 0xf00f};
    GLubyte level0[4 * 4 * 4] = {0};
    GLubyte framebuffer_pixels[5 * 4] = {0};
    NTGLframebuffer framebuffer = {
        framebuffer_pixels, 5, 1, 5 * 4, NTGL_RGBA8888,
        NTGL_ORIGIN_BOTTOM_LEFT,
    };
    NTGLcontext *context;
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint buffer;
    GLuint texture;
    GLint lod_location;
    GLint status = GL_FALSE;
    GLubyte pixels[5][4];
    int index;

    level0[(1 * 4 + 1) * 4 + 0] = 255;
    level0[(1 * 4 + 1) * 4 + 1] = 255;
    level0[(1 * 4 + 1) * 4 + 2] = 255;
    for (index = 0; index < 16; ++index)
        level0[index * 4 + 3] = 255;

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
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE)
        return 3;
    glUseProgram(program);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, level0);
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 2, 2, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, level1);
    glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, level2);
    glUniform1i(glGetUniformLocation(program, "image"), 0);
    lod_location = glGetUniformLocation(program, "lod");
    if (lod_location < 0)
        return 4;

    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    for (index = 0; index < 5; ++index) {
        glViewport(index, 0, 1, 1);
        glUniform1f(lod_location, lods[index]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glReadPixels(0, 0, 5, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    if (!expected_color(pixels[0], 64, 64, 64) ||
        !expected_color(pixels[1], 64, 64, 64) ||
        !expected_color(pixels[2], 255, 0, 0) ||
        !expected_color(pixels[3], 255, 0, 0) ||
        !expected_color(pixels[4], 0, 255, 0)) {
        for (index = 0; index < 5; ++index) {
            fprintf(stderr, "lod %.4f: %u %u %u %u\n", lods[index],
                    pixels[index][0], pixels[index][1], pixels[index][2],
                    pixels[index][3]);
        }
        return 5;
    }

    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 2, 2, 0, GL_RGBA,
                 GL_UNSIGNED_SHORT_4_4_4_4, packed_red);
    glViewport(0, 0, 1, 1);
    glUniform1f(lod_location, 0.5001f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels[0]);
    if (!expected_color(pixels[0], 0, 0, 0)) {
        fprintf(stderr, "mixed mip types: %u %u %u %u\n",
                pixels[0][0], pixels[0][1], pixels[0][2], pixels[0][3]);
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
