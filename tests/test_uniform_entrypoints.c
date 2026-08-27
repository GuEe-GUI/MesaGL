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
    if (!compiled) {
        char log[512];

        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "shader compile failed: %s\n", log);
        return 0;
    }
    return shader;
}

static int location(GLuint program, const char *name)
{
    GLint result = glGetUniformLocation(program, name);

    if (result < 0)
        fprintf(stderr, "missing uniform: %s\n", name);
    return result;
}

int main(void)
{
    static const char vertex_source[] =
        "void main(){gl_PointSize=4.0;gl_Position=vec4(0.0,0.0,0.0,1.0);}";
    static const char fragment_source[] =
        "precision mediump float;"
        "uniform vec2 f2;uniform vec3 f3;uniform int ia[2];"
        "uniform ivec2 i2;uniform ivec3 i3;uniform ivec4 i4;"
        "uniform mat2 m2;uniform mat3 m3;"
        "void main(){gl_FragColor=vec4("
        "f2.x+f3.x+float(ia[0]+i2.x)*0.0625,"
        "f2.y+f3.y+float(i3.y+i4.z)*0.0625,"
        "m2[0][0]*0.125+m3[1][1]*0.125+float(ia[1])*0.0625,1.0);}";
    static const GLfloat f2_value[] = {0.125f, 0.25f};
    static const GLfloat f3_value[] = {0.125f, 0.25f, 0.375f};
    static const GLint ia_value[] = {1, 0};
    static const GLint i2_value[] = {1, 0};
    static const GLint i3_value[] = {0, 1, 0};
    static const GLint i4_value[] = {0, 0, 1, 0};
    static const GLfloat m2_value[] = {1.0f, 0.0f, 0.0f, 1.0f};
    static const GLfloat m3_value[] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,
    };
    uint8_t pixels[8 * 8 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 8, 8, 8 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLint linked = GL_FALSE;
    GLint f2;
    GLint f3;
    GLint ia;
    GLint i2;
    GLint i3;
    GLint i4;
    GLint m2;
    GLint m3;
    GLint integer_values[4] = {0};
    GLfloat float_values[9] = {0.0f};
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
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 3;
    glUseProgram(program);

    f2 = location(program, "f2");
    f3 = location(program, "f3");
    ia = location(program, "ia");
    i2 = location(program, "i2");
    i3 = location(program, "i3");
    i4 = location(program, "i4");
    m2 = location(program, "m2");
    m3 = location(program, "m3");
    if (f2 < 0 || f3 < 0 || ia < 0 || i2 < 0 || i3 < 0 || i4 < 0 ||
        m2 < 0 || m3 < 0)
        return 4;

    glUniform2fv(f2, 1, f2_value);
    glUniform3fv(f3, 1, f3_value);
    glUniform1iv(ia, 2, ia_value);
    glUniform2i(i2, 7, 8);
    glGetUniformiv(program, i2, integer_values);
    if (integer_values[0] != 7 || integer_values[1] != 8)
        return 5;
    glUniform2iv(i2, 1, i2_value);
    glUniform3iv(i3, 1, i3_value);
    glUniform4i(i4, 4, 5, 6, 7);
    glGetUniformiv(program, i4, integer_values);
    if (integer_values[0] != 4 || integer_values[1] != 5 ||
        integer_values[2] != 6 || integer_values[3] != 7)
        return 6;
    glUniform4iv(i4, 1, i4_value);
    glUniformMatrix2fv(m2, 1, GL_FALSE, m2_value);
    glUniformMatrix3fv(m3, 1, GL_FALSE, m3_value);

    glGetUniformfv(program, f3, float_values);
    if (float_values[0] != f3_value[0] || float_values[1] != f3_value[1] ||
        float_values[2] != f3_value[2])
        return 7;
    glGetUniformfv(program, m3, float_values);
    if (float_values[0] != 1.0f || float_values[4] != 1.0f ||
        float_values[8] != 1.0f)
        return 8;

    glUniformMatrix2fv(m2, 1, GL_TRUE, m2_value);
    if (glGetError() != GL_INVALID_VALUE)
        return 9;
    glUniformMatrix3fv(m3, -1, GL_FALSE, m3_value);
    if (glGetError() != GL_INVALID_VALUE)
        return 10;
    glUniform3iv(f3, 1, i3_value);
    if (glGetError() != GL_INVALID_OPERATION)
        return 11;
    glUniform4iv(-1, 1, i4_value);
    if (glGetError() != GL_NO_ERROR)
        return 12;

    glViewport(0, 0, 8, 8);
    glDrawArrays(GL_POINTS, 0, 1);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 95 || pixel[0] > 96 || pixel[1] < 159 || pixel[1] > 160 ||
        pixel[2] < 63 || pixel[2] > 64 || pixel[3] != 255) {
        fprintf(stderr, "uniform entrypoint pixel: %u %u %u %u\n", pixel[0],
                pixel[1], pixel[2], pixel[3]);
        return 13;
    }

    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    puts("uniform entrypoint tests passed");
    return 0;
}
