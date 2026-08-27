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

static int shader_rejected(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint compiled = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    glDeleteShader(shader);
    return !compiled;
}

int main(void)
{
    static const char vertex_source[] =
        "varying mat2 transform;varying vec2 samples[2];varying mat2 banks[2];"
        "void addQuarter(inout float value){value+=0.25;}"
        "void main(){transform=mat2(0.0);"
        "transform[0][0]=0.25;transform[0][1]+=0.5;"
        "float assigned=(transform[1][1]=0.75);"
        "addQuarter(transform[1][0]);"
        "samples[0]=vec2(0.0);samples[1]=vec2(0.0);"
        "samples[1][0]=0.125;addQuarter(samples[1][1]);"
        "banks[0]=mat2(0.0);banks[1]=mat2(0.0);"
        "banks[1][0][0]=0.125;banks[1][1][1]+=0.125;"
        "gl_Position=vec4(0.0,0.0,0.0,1.0);gl_PointSize=4.0+assigned*0.0;}";
    static const char fragment_source[] =
        "precision highp float;varying mat2 transform;varying vec2 samples[2];"
        "varying mat2 banks[2];"
        "void main(){gl_FragColor=vec4(transform[0]+vec2(banks[1][0][0]),"
        "transform[1]+samples[1]+vec2(0.0,banks[1][1][1]));}";
    static const char invalid_out_type[] =
        "varying mat2 transform;void consume(inout vec2 value){value=vec2(1.0);}"
        "void main(){consume(transform[0][0]);gl_Position=vec4(0.0);}";
    static const char invalid_attribute_write[] =
        "attribute mat2 transform;"
        "void main(){transform[0][0]=1.0;gl_Position=vec4(0.0);}";
    static const char invalid_scalar_initializer[] =
        "varying mat2 transform;"
        "void main(){vec2 wrong=transform[0][0];gl_Position=vec4(wrong,0.0,1.0);}";
    static const char invalid_scalar_assignment[] =
        "varying mat2 transform;"
        "void main(){transform[0][0]=vec2(1.0);gl_Position=vec4(0.0);}";
    uint8_t pixels[8 * 8 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 8, 8, 8 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLint linked = GL_FALSE;
    GLubyte pixel[4];

    if (!context)
        return 1;
    if (!shader_rejected(GL_VERTEX_SHADER, invalid_out_type))
        return 6;
    if (!shader_rejected(GL_VERTEX_SHADER, invalid_attribute_write))
        return 7;
    if (!shader_rejected(GL_VERTEX_SHADER, invalid_scalar_initializer))
        return 8;
    if (!shader_rejected(GL_VERTEX_SHADER, invalid_scalar_assignment))
        return 9;
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
    glViewport(0, 0, 8, 8);
    glDrawArrays(GL_POINTS, 0, 1);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 95 || pixel[0] > 96 || pixel[1] < 159 || pixel[1] > 160 ||
        pixel[2] < 95 || pixel[2] > 96 || pixel[3] != 255) {
        fprintf(stderr, "varying matrix lvalue pixel: %u %u %u %u\n", pixel[0],
                pixel[1], pixel[2], pixel[3]);
        return 4;
    }
    if (glGetError() != GL_NO_ERROR)
        return 5;

    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    puts("varying matrix lvalue tests passed");
    return 0;
}
