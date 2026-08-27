#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint status = 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    return status ? shader : 0;
}

int main(void)
{
    static const char vertex_source[] =
        "attribute vec2 position;"
        "uniform sampler2D image;"
        "uniform mediump vec4 projected;"
        "varying vec4 explicit_lod_color;"
        "void main() {"
        "explicit_lod_color = texture2DProjLod(image, projected, 1.0);"
        "gl_Position = vec4(position, 0.0, 1.0);"
        "}";
    static const char fragment_source[] =
        "precision mediump float;"
        "uniform sampler2D image;"
        "uniform vec4 projected;"
        "uniform int mode;"
        "varying vec4 explicit_lod_color;"
        "void main() {"
        "if (mode == 0) gl_FragColor = texture2DProj(image, projected.xyz);"
        "else if (mode == 1) gl_FragColor = texture2DProj(image, projected);"
        "else gl_FragColor = explicit_lod_color;"
        "}";
    static const GLfloat triangle[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    static const GLubyte red[16] = {
        255, 0, 0, 255, 255, 0, 0, 255,
        255, 0, 0, 255, 255, 0, 0, 255,
    };
    static const GLubyte blue[4] = {0, 0, 255, 255};
    uint8_t pixels[8 * 8 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 8, 8, 8 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint buffer;
    GLuint texture;
    GLint projected_location;
    GLint mode_location;
    GLint status = 0;
    GLubyte sampled[4];
    int mode;

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
    if (!status)
        return 3;
    glUseProgram(program);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, red);
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, blue);
    glUniform1i(glGetUniformLocation(program, "image"), 0);
    projected_location = glGetUniformLocation(program, "projected");
    mode_location = glGetUniformLocation(program, "mode");
    if (projected_location < 0 || mode_location < 0)
        return 4;
    glUniform4f(projected_location, 1.0f, 1.0f, 2.0f, 2.0f);

    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glViewport(0, 0, 8, 8);
    for (mode = 0; mode < 3; ++mode) {
        glUniform1i(mode_location, mode);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sampled);
        if (mode < 2) {
            if (sampled[0] != 255 || sampled[1] != 0 || sampled[2] != 0 ||
                sampled[3] != 255)
                return 5 + mode;
        } else if (sampled[0] != 0 || sampled[1] != 0 || sampled[2] != 255 ||
                   sampled[3] != 255)
            return 7;
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
