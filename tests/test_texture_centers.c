#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint status = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    return status ? shader : 0;
}

static int close_byte(unsigned int actual, unsigned int expected)
{
    return actual + 2 >= expected && actual <= expected + 2;
}

static int sample_matches(GLint coordinate_location, GLfloat s,
                          GLint filter, GLint wrap, int red, int green,
                          int blue)
{
    GLubyte pixel[4];

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glUniform2f(coordinate_location, s, 0.5f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    return glGetError() == GL_NO_ERROR && close_byte(pixel[0], red) &&
           close_byte(pixel[1], green) && close_byte(pixel[2], blue) &&
           pixel[3] == 255;
}

int main(void)
{
    static const char vertex_source[] =
        "attribute vec2 position;"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); }";
    static const char fragment_source[] =
        "precision mediump float;"
        "uniform sampler2D image; uniform vec2 coordinate;"
        "void main() { gl_FragColor = texture2D(image, coordinate); }";
    static const GLfloat vertices[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    static const GLubyte texture_pixels[] = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 255, 255,
    };
    static const GLfloat fixed_vertices[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    GLfloat fixed_coordinates[] = {
        0.25f, 0.5f,
        0.25f, 0.5f,
        0.25f, 0.5f,
    };
    uint8_t pixels[16 * 16 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 16, 16, 16 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context;
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint buffer;
    GLuint texture;
    GLint linked = GL_FALSE;
    GLint coordinate_location;
    GLubyte fixed_pixel[4];

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
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 3;
    glUseProgram(program);
    coordinate_location = glGetUniformLocation(program, "coordinate");
    glUniform1i(glGetUniformLocation(program, "image"), 0);
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, texture_pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glViewport(0, 0, 16, 16);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    if (!sample_matches(coordinate_location, 0.125f, GL_NEAREST,
                        GL_CLAMP_TO_EDGE, 255, 0, 0))
        return 4;
    if (!sample_matches(coordinate_location, 0.249f, GL_NEAREST,
                        GL_CLAMP_TO_EDGE, 255, 0, 0))
        return 5;
    if (!sample_matches(coordinate_location, 0.251f, GL_NEAREST,
                        GL_CLAMP_TO_EDGE, 0, 255, 0))
        return 6;
    if (!sample_matches(coordinate_location, 0.25f, GL_LINEAR,
                        GL_CLAMP_TO_EDGE, 128, 128, 0))
        return 7;
    if (!sample_matches(coordinate_location, 0.0f, GL_LINEAR,
                        GL_REPEAT, 255, 128, 128))
        return 8;
    if (!sample_matches(coordinate_location, 0.0f, GL_LINEAR,
                        GL_CLAMP_TO_EDGE, 255, 0, 0))
        return 9;
    if (!sample_matches(coordinate_location, 1.125f, GL_NEAREST,
                        GL_REPEAT, 255, 0, 0))
        return 10;
    if (!sample_matches(coordinate_location, 1.125f, GL_NEAREST,
                        GL_MIRRORED_REPEAT, 255, 255, 255))
        return 11;
    if (!sample_matches(coordinate_location, 1.25f, GL_NEAREST,
                        GL_MIRRORED_REPEAT, 255, 255, 255))
        return 13;
    if (!sample_matches(coordinate_location, 1.0e30f, GL_NEAREST,
                        GL_REPEAT, 255, 0, 0))
        return 14;
    if (!sample_matches(coordinate_location, -1.0e30f, GL_NEAREST,
                        GL_CLAMP_TO_EDGE, 255, 0, 0))
        return 15;

    glUseProgram(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, fixed_vertices);
    glTexCoordPointer(2, GL_FLOAT, 0, fixed_coordinates);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, fixed_pixel);
    if (!close_byte(fixed_pixel[0], 128) ||
        !close_byte(fixed_pixel[1], 128) || fixed_pixel[2] != 0 ||
        fixed_pixel[3] != 255)
        return 12;

    fixed_coordinates[0] = fixed_coordinates[2] = fixed_coordinates[4] = 1.0e30f;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, fixed_pixel);
    if (fixed_pixel[0] != 255 || fixed_pixel[1] || fixed_pixel[2] ||
        fixed_pixel[3] != 255)
        return 16;

    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &buffer);
    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return 0;
}
