#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

typedef struct Vertex {
    float position[2];
    float uv[2];
    uint8_t color[4];
} Vertex;

static const char vertex_source[] = "attribute vec2 Position;\n"
                                    "attribute vec2 UV;\n"
                                    "attribute vec4 Color;\n"
                                    "varying vec2 Frag_UV;\n"
                                    "varying vec4 Frag_Color;\n"
                                    "void main() {\n"
                                    "  Frag_UV = UV;\n"
                                    "  Frag_Color = Color;\n"
                                    "  gl_Position = vec4(Position, 0.0, 1.0);\n"
                                    "}\n";

static const char fragment_source[] =
    "precision mediump float;\n"
    "uniform sampler2D Texture;\n"
    "varying vec2 Frag_UV;\n"
    "varying vec4 Frag_Color;\n"
    "void main() { gl_FragColor = Frag_Color * texture2D(Texture, Frag_UV); }\n";

int main(void)
{
    uint16_t pixels[64 * 64] = {0};
    NTGLframebuffer framebuffer = {pixels, 64, 64, 64 * 2, NTGL_RGB565, NTGL_ORIGIN_TOP_LEFT};
    const Vertex vertices[] = {
        {{-0.8f, -0.8f}, {0.0f, 0.0f}, {255, 0, 0, 192}},
        {{0.8f, -0.8f}, {1.0f, 0.0f}, {0, 255, 0, 192}},
        {{0.0f, 0.8f}, {0.5f, 1.0f}, {0, 0, 255, 192}},
    };
    const uint16_t indices[] = {0, 1, 2};
    const uint8_t texture_pixels[] = {
        255, 255, 255, 255, 255, 255, 255, 128, 255, 255, 255, 128, 255, 255, 255, 255,
    };
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader, fragment_shader, program, vertex_buffer, index_buffer, texture;
    GLint status;
    const GLchar *source;
    int changed = 0;
    int i;
    if (!context)
        return 1;

    vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    source = vertex_source;
    glShaderSource(vertex_shader, 1, &source, NULL);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &status);
    if (!status)
        return 2;

    fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    source = fragment_source;
    glShaderSource(fragment_shader, 1, &source, NULL);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &status);
    if (!status)
        return 3;

    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status)
        return 4;
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glUseProgram(program);
    glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &status);
    if (status != GL_MODULATE)
        return 6;

    glGenBuffers(1, &vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (const void *)offsetof(Vertex, uv));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                          (const void *)offsetof(Vertex, color));

    glGenBuffers(1, &index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture_pixels);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, 64, 64);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, 0);

    for (i = 0; i < 64 * 64; ++i)
        if (pixels[i])
            ++changed;

    glDeleteBuffers(1, &index_buffer);
    glDeleteBuffers(1, &vertex_buffer);
    glDeleteTextures(1, &texture);
    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return changed > 1000 ? 0 : 5;
}
