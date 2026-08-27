#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint compiled = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    return compiled ? shader : 0;
}

static int check_attribute(GLint location, GLint expected_location,
                           GLint size, GLenum type, GLboolean normalized,
                           const void *data, const GLfloat expected[4])
{
    GLubyte pixel[4];

    glVertexAttribPointer((GLuint)location, size, type, normalized, 0, data);
    glUniform4fv(expected_location, 1, expected);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_POINTS, 0, 1);
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (glGetError() == GL_NO_ERROR && pixel[0] == 0 && pixel[1] == 255 &&
        pixel[2] == 0 && pixel[3] == 255)
        return 1;
    fprintf(stderr, "vertex conversion pixel: %u %u %u %u\n",
            pixel[0], pixel[1], pixel[2], pixel[3]);
    return 0;
}

int main(void)
{
    static const char vertex_source[] =
        "attribute vec4 signal;"
        "varying vec4 converted;"
        "varying float vertex_valid;"
        "void main(){"
        "converted=signal;"
        "vertex_valid=1.0;"
        "gl_PointSize=3.0;gl_Position=vec4(0.0,0.0,0.0,1.0);}";
    static const char fragment_source[] =
        "precision highp float;varying vec4 converted;varying float vertex_valid;"
        "uniform vec4 expected;void main(){"
        "bool valid=vertex_valid==1.0&&"
        "all(lessThan(abs(converted-expected),vec4(0.00001)));"
        "gl_FragColor=valid?vec4(0.0,1.0,0.0,1.0):"
        "vec4(1.0,0.0,0.0,1.0);}";
    static const GLbyte signed_bytes[4] = {-128, -127, -64, 127};
    static const GLshort signed_shorts[4] = {-32768, -32767, -16384, 32767};
    static const GLubyte unsigned_bytes[4] = {0, 1, 128, 255};
    static const GLushort unsigned_shorts[4] = {0, 1, 32768, 65535};
    static const GLint fixed_values[4] = {-65536, -32768, 32768, 65536};
    static const GLfloat float_values[4] = {-1.0f, -0.5f, 0.5f, 1.0f};
    static const GLfloat expected_signed_byte[4] = {
        -1.0f, -1.0f, -64.0f / 127.0f, 1.0f,
    };
    static const GLfloat expected_signed_short[4] = {
        -1.0f, -1.0f, -16384.0f / 32767.0f, 1.0f,
    };
    static const GLfloat expected_unsigned_byte[4] = {
        0.0f, 1.0f / 255.0f, 128.0f / 255.0f, 1.0f,
    };
    static const GLfloat expected_unsigned_short[4] = {
        0.0f, 1.0f / 65535.0f, 32768.0f / 65535.0f, 1.0f,
    };
    static const GLfloat expected_fixed[4] = {-1.0f, -0.5f, 0.5f, 1.0f};
    static const GLfloat expected_byte_raw[4] = {-128.0f, -127.0f, -64.0f, 127.0f};
    static const GLfloat expected_short_raw[4] = {
        -32768.0f, -32767.0f, -16384.0f, 32767.0f,
    };
    uint8_t pixels[4 * 4 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 4, 4, 4 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT,
    };
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLint linked = GL_FALSE;
    GLint signal_location;
    GLint expected_location;

    if (!context)
        return 1;
    vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
    fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (!vertex_shader || !fragment_shader)
        return 2;
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 3;
    glUseProgram(program);
    signal_location = glGetAttribLocation(program, "signal");
    expected_location = glGetUniformLocation(program, "expected");
    if (signal_location < 0 || expected_location < 0)
        return 4;
    glViewport(0, 0, 4, 4);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnableVertexAttribArray((GLuint)signal_location);

    if (!check_attribute(signal_location, expected_location, 4, GL_BYTE,
                         GL_TRUE, signed_bytes, expected_signed_byte))
        return 5;
    if (!check_attribute(signal_location, expected_location, 4, GL_SHORT,
                         GL_TRUE, signed_shorts, expected_signed_short))
        return 6;
    if (!check_attribute(signal_location, expected_location, 4, GL_UNSIGNED_BYTE,
                         GL_TRUE, unsigned_bytes, expected_unsigned_byte))
        return 7;
    if (!check_attribute(signal_location, expected_location, 4, GL_UNSIGNED_SHORT,
                         GL_TRUE, unsigned_shorts, expected_unsigned_short))
        return 8;
    if (!check_attribute(signal_location, expected_location, 4, GL_FIXED,
                         GL_FALSE, fixed_values, expected_fixed))
        return 9;
    if (!check_attribute(signal_location, expected_location, 4, GL_FLOAT,
                         GL_FALSE, float_values, float_values))
        return 10;
    if (!check_attribute(signal_location, expected_location, 4, GL_BYTE,
                         GL_FALSE, signed_bytes, expected_byte_raw))
        return 11;
    if (!check_attribute(signal_location, expected_location, 4, GL_SHORT,
                         GL_FALSE, signed_shorts, expected_short_raw))
        return 12;

    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return 0;
}
