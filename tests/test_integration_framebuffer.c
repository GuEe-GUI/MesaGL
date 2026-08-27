#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>

typedef struct Vertex {
    float position[2];
    float uv[2];
} Vertex;

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint compiled = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLchar log[256];

        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "shader compilation failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint create_program(const char *vertex_source, const char *fragment_source)
{
    GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    GLuint program;
    GLint linked = GL_FALSE;

    if (!vertex || !fragment)
        return 0;
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "position");
    glBindAttribLocation(program, 1, "texcoord");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLchar log[256];

        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "program link failed: %s\n", log);
        glDeleteProgram(program);
        program = 0;
    }
    glDeleteShader(fragment);
    glDeleteShader(vertex);
    return program;
}

static int near_byte(unsigned int actual, unsigned int expected)
{
    return actual + 3 >= expected && actual <= expected + 3;
}

static int expect_pixel(int x, int y, unsigned int red, unsigned int green,
                        unsigned int blue, unsigned int alpha)
{
    GLubyte pixel[4] = {0};

    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!near_byte(pixel[0], red) || !near_byte(pixel[1], green) ||
        !near_byte(pixel[2], blue) || !near_byte(pixel[3], alpha)) {
        fprintf(stderr, "pixel (%d,%d): got %u,%u,%u,%u expected %u,%u,%u,%u\n",
                x, y, pixel[0], pixel[1], pixel[2], pixel[3], red, green, blue,
                alpha);
        return 0;
    }
    return 1;
}

int main(void)
{
    static const char vertex_source[] =
        "attribute vec2 position; attribute vec2 texcoord; varying vec2 uv;"
        "uniform float depth;"
        "void main() { uv = texcoord; gl_Position = vec4(position, depth, 1.0); }";
    static const char solid_fragment_source[] =
        "precision mediump float; uniform vec4 color;"
        "vec4 shade(vec4 value) {"
        "  vec4 result = vec4(0.0);"
        "  for (int i = 0; i < 4; ++i) result[i] = value[i];"
        "  return result;"
        "}"
        "void main() { gl_FragColor = shade(color); }";
    static const char texture_fragment_source[] =
        "precision mediump float; varying vec2 uv;"
        "uniform sampler2D scene; uniform sampler2D accent;"
        "void main() {"
        "  vec4 base = texture2D(scene, uv);"
        "  vec4 top = texture2D(accent, uv * 0.0);"
        "  gl_FragColor = mix(base, top, 0.25);"
        "}";
    static const Vertex vertices[] = {
        {{-1.0f, -1.0f}, {0.0f, 0.0f}},
        {{1.0f, -1.0f}, {1.0f, 0.0f}},
        {{1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-1.0f, 1.0f}, {0.0f, 1.0f}},
    };
    static const GLushort indices[] = {0, 1, 2, 0, 2, 3};
    static const GLubyte blue[] = {0, 0, 255, 255};
    uint8_t pixels[64 * 64 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 64, 64, 64 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint solid_program;
    GLuint texture_program;
    GLuint buffers[2];
    GLuint textures[2];
    GLuint target;
    GLuint depth_buffer;
    GLuint stencil_buffer;
    GLint color_location;
    GLint depth_location;
    int result = 1;

    if (!context)
        return 1;
    solid_program = create_program(vertex_source, solid_fragment_source);
    texture_program = create_program(vertex_source, texture_fragment_source);
    if (!solid_program || !texture_program)
        goto cleanup_context;

    glGenBuffers(2, buffers);
    glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (const void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (const void *)(2 * sizeof(float)));

    glGenTextures(2, textures);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textures[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, blue);

    glGenFramebuffers(1, &target);
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           textures[0], 0);
    glGenRenderbuffers(1, &depth_buffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_buffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 32, 32);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                              depth_buffer);
    glGenRenderbuffers(1, &stencil_buffer);
    glBindRenderbuffer(GL_RENDERBUFFER, stencil_buffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, 32, 32);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                              stencil_buffer);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        goto cleanup_objects;

    glViewport(0, 0, 32, 32);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepthf(1.0f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glUseProgram(solid_program);
    color_location = glGetUniformLocation(solid_program, "color");
    depth_location = glGetUniformLocation(solid_program, "depth");
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glUniform1f(depth_location, 0.5f);
    glUniform4f(color_location, 1.0f, 0.0f, 0.0f, 1.0f);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
    glUniform1f(depth_location, -0.5f);
    glUniform4f(color_location, 0.0f, 1.0f, 0.0f, 1.0f);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
    if (!expect_pixel(16, 16, 0, 255, 0, 255))
        goto cleanup_objects;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 64, 64);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(texture_program);
    glUniform1i(glGetUniformLocation(texture_program, "scene"), 0);
    glUniform1i(glGetUniformLocation(texture_program, "accent"), 1);
    glUniform1f(glGetUniformLocation(texture_program, "depth"), 0.0f);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
    if (!expect_pixel(32, 32, 0, 191, 64, 255))
        goto cleanup_objects;

    glUseProgram(solid_program);
    glUniform1f(depth_location, 0.0f);
    glUniform4f(color_location, 1.0f, 0.0f, 0.0f, 0.5f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    glScissor(16, 16, 32, 32);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    if (!expect_pixel(32, 32, 128, 95, 32, 191) ||
        !expect_pixel(4, 4, 0, 191, 64, 255))
        goto cleanup_objects;

    result = 0;

cleanup_objects:
    glDeleteRenderbuffers(1, &stencil_buffer);
    glDeleteRenderbuffers(1, &depth_buffer);
    glDeleteFramebuffers(1, &target);
    glDeleteTextures(2, textures);
    glDeleteBuffers(2, buffers);
    glDeleteProgram(texture_program);
    glDeleteProgram(solid_program);
cleanup_context:
    ntglDestroyContext(context);
    return result;
}
