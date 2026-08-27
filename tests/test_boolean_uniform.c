#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>

static GLuint compile_shader(GLenum stage, const char *source)
{
    GLuint shader = glCreateShader(stage);
    GLint compiled = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    return compiled ? shader : 0;
}

int main(void)
{
    static const char vertex_source[] =
        "void main(){gl_PointSize=4.0;gl_Position=vec4(0.0,0.0,0.0,1.0);}";
    static const char fragment_source[] =
        "precision mediump float;uniform bool enabled;uniform bvec3 mask;"
        "uniform int number;void main(){gl_FragColor=vec4(enabled,mask.y,"
        "float(number+7),1.0);}";
    uint8_t pixels[8 * 8 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 8, 8, 8 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLint enabled;
    GLint mask;
    GLint number;
    GLint values[4] = {-9, -9, -9, -9};
    GLfloat float_value = -9.0f;
    GLubyte pixel[4];
    GLint linked = GL_FALSE;

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
    enabled = glGetUniformLocation(program, "enabled");
    mask = glGetUniformLocation(program, "mask");
    number = glGetUniformLocation(program, "number");
    if (enabled < 0 || mask < 0 || number < 0)
        return 4;
    glUniform1i(enabled, -7);
    glUniform3i(mask, 0, -2, 9);
    glUniform1i(number, -7);
    glGetUniformiv(program, enabled, values);
    glGetUniformfv(program, enabled, &float_value);
    if (values[0] != 1 || float_value != 1.0f)
        return 5;
    glGetUniformiv(program, mask, values);
    if (values[0] != 0 || values[1] != 1 || values[2] != 1)
        return 6;
    glGetUniformiv(program, number, values);
    if (values[0] != -7 || glGetError() != GL_NO_ERROR)
        return 7;
    glViewport(0, 0, 8, 8);
    glDrawArrays(GL_POINTS, 0, 1);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 255 || pixel[1] != 255 || pixel[2] != 0 || pixel[3] != 255) {
        fprintf(stderr, "boolean uniform pixel: %u %u %u %u\n", pixel[0], pixel[1],
                pixel[2], pixel[3]);
        return 8;
    }
    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return 0;
}
