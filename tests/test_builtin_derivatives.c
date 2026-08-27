#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

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
        "attribute vec2 position; varying vec2 uv;"
        "void main(){uv=position*0.5+0.5;gl_Position=vec4(position,0.0,1.0);}";
    static const char fragment_source[] =
        "#extension GL_OES_standard_derivatives : enable\n"
        "precision mediump float; varying vec2 uv;"
        "void main(){vec2 reflected=reflect(uv,vec2(1.0,0.0));"
        "vec3 crossed=cross(vec3(uv,1.0),vec3(1.0,0.0,0.0));"
        "gl_FragColor=vec4(abs(dFdx(reflected.x))*16.0,"
        "abs(dFdy(reflected.y))*16.0,"
        "min(abs(dFdy(crossed.z)),abs(dFdx(abs(uv.x-0.5))))*16.0,1.0);}";
    const GLfloat vertices[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    uint8_t pixels[16 * 16 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 16, 16, 16 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader = compile(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment_shader = compile(GL_FRAGMENT_SHADER, fragment_source);
    GLuint program = glCreateProgram();
    GLuint buffer;
    GLint position;
    GLint status;
    GLubyte negative[4];
    GLubyte positive[4];

    if (!context || !vertex_shader || !fragment_shader)
        return 1;
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status)
        return 2;
    glUseProgram(program);
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    position = glGetAttribLocation(program, "position");
    glEnableVertexAttribArray((GLuint)position);
    glVertexAttribPointer((GLuint)position, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glViewport(0, 0, 16, 16);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(4, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, negative);
    glReadPixels(12, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, positive);
    if (negative[0] < 248 || negative[1] < 248 || negative[2] < 248 ||
        negative[3] != 255 || positive[0] < 248 || positive[1] < 248 ||
        positive[2] < 248 || positive[3] != 255)
        return 3;

    glDeleteBuffers(1, &buffer);
    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return 0;
}
