#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stddef.h>
#include <stdint.h>

typedef struct Vertex {
    float position[2];
    float uv[2];
} Vertex;

static GLuint compile(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint status;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    return status ? shader : 0;
}

int main(void)
{
    static const char vertex_source[] =
        "attribute vec2 p; attribute vec2 tc; varying vec2 uv;"
        "void main(){uv=tc;gl_Position=vec4(p,0.0,1.0);}";
    static const char fragment_source[] =
        "precision mediump float; varying vec2 uv; uniform sampler2D first;"
        "uniform sampler2D second;"
        "void main(){gl_FragColor=mix(texture2D(first,uv),texture2D(second,uv),0.5);}";
    const Vertex vertices[] = {
        {{-1.0f, -1.0f}, {0.0f, 0.0f}},
        {{1.0f, -1.0f}, {1.0f, 0.0f}},
        {{0.0f, 1.0f}, {0.5f, 1.0f}},
    };
    const uint8_t red[4] = {255, 0, 0, 255};
    const uint8_t blue[4] = {0, 0, 255, 255};
    uint8_t pixels[32 * 32 * 4] = {0};
    uint8_t center[4];
    NTGLframebuffer framebuffer = {pixels, 32, 32, 32 * 4, NTGL_RGBA8888,
                                   NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader = compile(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment_shader = compile(GL_FRAGMENT_SHADER, fragment_source);
    GLuint program = glCreateProgram();
    GLuint textures[2];
    GLint active;
    GLint position;
    GLint texcoord;

    if (!context || !vertex_shader || !fragment_shader)
        return 1;
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glUseProgram(program);
    glGenTextures(2, textures);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, red);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    if (active != GL_TEXTURE1)
        return 3;
    glBindTexture(GL_TEXTURE_2D, textures[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, blue);
    glUniform1i(glGetUniformLocation(program, "first"), 0);
    glUniform1i(glGetUniformLocation(program, "second"), 1);
    position = glGetAttribLocation(program, "p");
    texcoord = glGetAttribLocation(program, "tc");
    glEnableVertexAttribArray((GLuint)position);
    glEnableVertexAttribArray((GLuint)texcoord);
    glVertexAttribPointer((GLuint)position, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          &vertices[0].position);
    glVertexAttribPointer((GLuint)texcoord, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), &vertices[0].uv);
    glViewport(0, 0, 32, 32);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
    if (center[0] < 120 || center[0] > 136 || center[1] != 0 || center[2] < 120 ||
        center[2] > 136)
        return 2;
    glDeleteTextures(2, textures);
    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return 0;
}
