#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <limits.h>
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

static int center_is_black(void)
{
    GLubyte pixel[4];

    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    return pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255;
}

int main(void)
{
    static const char vertex_source[] =
        "attribute vec2 position;"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); }";
    static const char fragment_source[] =
        "precision mediump float;"
        "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }";
    static const GLfloat vertices[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    static const GLint fixed_vertices[] = {
        -65536, -65536, 3 * 65536, -65536, -65536, 3 * 65536,
    };
    static const GLushort valid_indices[] = {0, 1, 2};
    static const GLuint desktop_indices[] = {0, 1, 2};
    static const GLushort invalid_indices[] = {0, 1, 7};
    static const unsigned char zero_update[sizeof(vertices) + 4] = {0};
    uint8_t pixels[16 * 16 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 16, 16, 16 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint vertex_buffer;
    GLuint index_buffer;
    GLint linked = 0;
    GLubyte pixel[4];

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
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 3;
    glUseProgram(program);
    glViewport(0, 0, 16, 16);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDrawArrays(0xffffu, 0, 3);
    if (glGetError() != GL_INVALID_ENUM)
        return 4;
    glDrawArrays(0xffffu, -1, -1);
    if (glGetError() != GL_INVALID_VALUE)
        return 19;
    glDrawArrays(GL_TRIANGLES, -1, 3);
    if (glGetError() != GL_INVALID_VALUE)
        return 5;
    glDrawElements(GL_TRIANGLES, 3, GL_FLOAT, valid_indices);
    if (glGetError() != GL_INVALID_ENUM)
        return 6;
    glDrawElements(0xffffu, -1, GL_FLOAT, valid_indices);
    if (glGetError() != GL_INVALID_VALUE)
        return 20;
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, desktop_indices);
#if !MESAGL_ENABLE_UINT_ELEMENT_INDICES
    if (glGetError() != GL_INVALID_ENUM || !center_is_black())
        return 18;
#else
    if (glGetError() != GL_NO_ERROR || !center_is_black())
        return 18;
#endif
    glEnableVertexAttribArray(99);
    if (glGetError() != GL_INVALID_VALUE)
        return 7;
    glVertexAttribPointer(0, 5, GL_FLOAT, GL_FALSE, 0, NULL);
    if (glGetError() != GL_INVALID_VALUE)
        return 8;
    glVertexAttribPointer(0, 2, GL_INT, GL_FALSE, 0, NULL);
    if (glGetError() != GL_INVALID_ENUM)
        return 9;

    glGenBuffers(1, &vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, 2 * 2 * (GLsizeiptr)sizeof(GLfloat), vertices,
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (glGetError() != GL_INVALID_OPERATION || !center_is_black())
        return 10;

    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glDrawArrays(GL_POINTS, INT_MAX, 2);
    if (glGetError() != GL_INVALID_OPERATION || !center_is_black())
        return 21;
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(zero_update), zero_update);
    if (glGetError() != GL_INVALID_VALUE)
        return 16;
    glGenBuffers(1, &index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 2 * (GLsizeiptr)sizeof(GLushort), valid_indices,
                 GL_STATIC_DRAW);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
    if (glGetError() != GL_INVALID_OPERATION || !center_is_black())
        return 11;

    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(invalid_indices), invalid_indices,
                 GL_STATIC_DRAW);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
    if (glGetError() != GL_INVALID_OPERATION || !center_is_black())
        return 12;

    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(valid_indices), valid_indices, GL_STATIC_DRAW);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
    if (glGetError() != GL_NO_ERROR)
        return 13;
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 255 || pixel[1] != 0 || pixel[2] != 0 || pixel[3] != 255)
        return 14;

    glClear(GL_COLOR_BUFFER_BIT);
    glBufferData(GL_ARRAY_BUFFER, sizeof(fixed_vertices), fixed_vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FIXED, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (glGetError() != GL_NO_ERROR || pixel[0] != 255 || pixel[1] || pixel[2] ||
        pixel[3] != 255)
        return 17;

    glClear(GL_COLOR_BUFFER_BIT);
    glDeleteBuffers(1, &vertex_buffer);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
    if (glGetError() != GL_INVALID_OPERATION || !center_is_black())
        return 15;

    glDeleteBuffers(1, &index_buffer);
    glUseProgram(0);
    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return 0;
}
