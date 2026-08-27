#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stddef.h>
#include <stdint.h>

typedef struct Vertex {
    float position[2];
    uint8_t color[4];
} Vertex;

static GLuint shader(GLenum type, const char *source)
{
    GLuint object = glCreateShader(type);
    GLint status;

    glShaderSource(object, 1, &source, NULL);
    glCompileShader(object);
    glGetShaderiv(object, GL_COMPILE_STATUS, &status);
    return status ? object : 0;
}

int main(void)
{
    static const char vertex_source[] =
        "attribute vec2 Position; varying vec4 Frag_Color; attribute vec4 Color;"
        "void main(){ Frag_Color=Color; gl_Position=vec4(Position,0.0,1.0); }";
    static const char fragment_source[] =
        "precision mediump float; varying vec4 Frag_Color;"
        "void main(){ gl_FragData[0]=Frag_Color; }";
    const Vertex vertices[] = {
        {{-0.8f, -0.8f}, {255, 0, 0, 255}},
        {{0.8f, -0.8f}, {0, 255, 0, 255}},
        {{0.0f, 0.8f}, {0, 0, 255, 255}},
    };
    uint16_t pixels[64 * 64] = {0};
    NTGLframebuffer framebuffer = {pixels, 64, 64, 64 * 2, NTGL_RGB565,
                                   NTGL_ORIGIN_TOP_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader = shader(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment_shader = shader(GL_FRAGMENT_SHADER, fragment_source);
    GLuint program = glCreateProgram();
    GLuint buffer;
    GLint position;
    GLint color;
    int changed = 0;
    int i;

    if (!context || !vertex_shader || !fragment_shader)
        return 1;
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glUseProgram(program);
    position = glGetAttribLocation(program, "Position");
    color = glGetAttribLocation(program, "Color");
    if (position != 0 || color != 2)
        return 2;
    glEnableVertexAttribArray((GLuint)position);
    glEnableVertexAttribArray((GLuint)color);
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, 2 * (GLsizeiptr)sizeof(Vertex), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer((GLuint)position, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (const void *)offsetof(Vertex, position));
    glVertexAttribPointer((GLuint)color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                          (const void *)offsetof(Vertex, color));
    glViewport(0, 0, 64, 64);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (glGetError() != GL_INVALID_OPERATION)
        return 4;
    for (i = 0; i < 64 * 64; ++i)
        if (pixels[i])
            return 5;

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer((GLuint)position, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          &vertices[0].position);
    glVertexAttribPointer((GLuint)color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                          &vertices[0].color);
    glViewport(0, 0, 64, 64);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    for (i = 0; i < 64 * 64; ++i)
        changed += pixels[i] != 0;
    glDeleteBuffers(1, &buffer);
    ntglDestroyContext(context);
    return changed > 1000 ? 0 : 3;
}
