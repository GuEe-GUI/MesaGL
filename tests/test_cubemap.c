#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <string.h>

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
        "uniform samplerCube cube_texture;"
        "uniform mediump vec3 direction;"
        "varying vec4 explicit_lod_color;"
        "void main() {"
        "explicit_lod_color = textureCubeLod(cube_texture, direction, 1.0);"
        "gl_Position = vec4(position, 0.0, 1.0);"
        "}";
    static const char fragment_source[] =
        "precision mediump float;"
        "uniform samplerCube cube_texture;"
        "uniform vec3 direction;"
        "uniform int sample_mode;"
        "varying vec4 explicit_lod_color;"
        "void main() {"
        "if (sample_mode == 0) gl_FragColor = textureCube(cube_texture, direction);"
        "else if (sample_mode == 1) gl_FragColor = textureCube(cube_texture, direction, 1.0);"
        "else gl_FragColor = explicit_lod_color;"
        "}";
    static const GLfloat triangle[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    static const GLenum targets[6] = {
        GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
    };
    static const GLubyte colors[6][4] = {
        {255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255},
        {255, 255, 0, 255}, {255, 0, 255, 255}, {0, 255, 255, 255},
    };
    static const GLfloat directions[6][3] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    };
    static const GLubyte mip_color[4] = {255, 255, 255, 255};
    uint8_t pixels[48 * 8 * 4] = {0};
    NTGLframebuffer framebuffer = {pixels, 48, 8, 48 * 4, NTGL_RGBA8888,
                                   NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint buffer;
    GLuint cube;
    GLuint target;
    GLint direction_location;
    GLint sampler_location;
    GLint mode_location;
    GLint value;
    GLubyte fbo_pixel[4];
    int face;

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
    glGetProgramiv(program, GL_LINK_STATUS, &value);
    if (!value)
        return 3;
    glUseProgram(program);
    direction_location = glGetUniformLocation(program, "direction");
    sampler_location = glGetUniformLocation(program, "cube_texture");
    mode_location = glGetUniformLocation(program, "sample_mode");
    if (direction_location < 0 || sampler_location < 0 || mode_location < 0)
        return 4;

    glGenTextures(1, &cube);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    for (face = 0; face < 6; ++face) {
        GLubyte face_pixels[16];
        int pixel;

        for (pixel = 0; pixel < 4; ++pixel)
            memcpy(face_pixels + pixel * 4, colors[face], 4);
        glTexImage2D(targets[face], 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     face_pixels);
        glTexImage2D(targets[face], 1, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     mip_color);
    }
    if (glGetError() != GL_NO_ERROR)
        return 5;
    glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &value);
    if ((GLuint)value != cube)
        return 6;

    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glUniform1i(sampler_location, 0);
    glUniform1i(mode_location, 0);
    for (face = 0; face < 6; ++face) {
        glViewport(face * 8, 0, 8, 8);
        glUniform3f(direction_location, directions[face][0], directions[face][1],
                    directions[face][2]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    for (face = 0; face < 6; ++face) {
        GLubyte sampled[4];

        glReadPixels(face * 8 + 4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sampled);
        if (sampled[0] != colors[face][0] || sampled[1] != colors[face][1] ||
            sampled[2] != colors[face][2] || sampled[3] != 255)
            return 7 + face;
    }
    for (face = 1; face <= 2; ++face) {
        GLubyte sampled[4];

        glUniform1i(mode_location, face);
        glViewport(0, 0, 8, 8);
        glUniform3f(direction_location, 1.0f, 0.0f, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sampled);
        if (sampled[0] != 255 || sampled[1] != 255 || sampled[2] != 255 ||
            sampled[3] != 255)
            return 16 + face;
    }

    glGenFramebuffers(1, &target);
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_X, cube, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 13;
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE,
        &value);
    if (value != GL_TEXTURE_CUBE_MAP_POSITIVE_X)
        return 14;
    for (face = 0; face < 6; ++face) {
        const GLubyte *expected = colors[(face + 1) % 6];

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               targets[face], cube, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            return 19 + face;
        glViewport(0, 0, 2, 2);
        glClearColor(expected[0] / 255.0f, expected[1] / 255.0f,
                     expected[2] / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, fbo_pixel);
        if (glGetError() != GL_NO_ERROR || memcmp(fbo_pixel, expected, 4))
            return 25 + face;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 48, 8);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUniform1i(mode_location, 0);
    for (face = 0; face < 6; ++face) {
        glViewport(face * 8, 0, 8, 8);
        glUniform3f(direction_location, directions[face][0],
                    directions[face][1], directions[face][2]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    for (face = 0; face < 6; ++face) {
        const GLubyte *expected = colors[(face + 1) % 6];

        glReadPixels(face * 8 + 4, 4, 1, 1, GL_RGBA,
                     GL_UNSIGNED_BYTE, fbo_pixel);
        if (glGetError() != GL_NO_ERROR || memcmp(fbo_pixel, expected, 4))
            return 31 + face;
    }

    glBindTexture(GL_TEXTURE_2D, cube);
    if (glGetError() != GL_INVALID_OPERATION)
        return 15;
    glDeleteFramebuffers(1, &target);
    glDeleteBuffers(1, &buffer);
    glDeleteTextures(1, &cube);
    glUseProgram(0);
    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return 0;
}
