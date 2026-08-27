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

int main(void)
{
    static const char vertex_source[] =
        "void main(){gl_PointSize=1.0;gl_Position=vec4(0.0,0.0,0.0,1.0);}";
    static const char fragment_source[] =
        "precision highp float;"
        "float f20(float x){return x;}"
        "float f19(float x){return f20(x);}"
        "float f18(float x){return f19(x);}"
        "float f17(float x){return f18(x);}"
        "float f16(float x){return f17(x);}"
        "float f15(float x){return f16(x);}"
        "float f14(float x){return f15(x);}"
        "float f13(float x){return f14(x);}"
        "float f12(float x){return f13(x);}"
        "float f11(float x){return f12(x);}"
        "float f10(float x){return f11(x);}"
        "float f9(float x){return f10(x);}"
        "float f8(float x){return f9(x);}"
        "float f7(float x){return f8(x);}"
        "float f6(float x){return f7(x);}"
        "float f5(float x){return f6(x);}"
        "float f4(float x){return f5(x);}"
        "float f3(float x){return f4(x);}"
        "float f2(float x){return f3(x);}"
        "float f1(float x){return f2(x);}"
        "float f0(float x){return f1(x);}"
        "void main(){float value=0.0;for(int i=0;i<300;++i)"
        "value+=1.0/300.0;gl_FragColor=vec4(f0(value),0.0,0.0,1.0);}";
    uint8_t pixels[4 * 4 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 4, 4, 4 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT,
    };
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLint linked = GL_FALSE;
    GLubyte readback[4 * 4 * 4] = {0};
    int found = 0;
    int pixel_index;

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
    glViewport(0, 0, 4, 4);
    glDrawArrays(GL_POINTS, 0, 1);
    if (glGetError() != GL_NO_ERROR)
        return 4;
    glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, readback);
    for (pixel_index = 0; pixel_index < 16; ++pixel_index)
        if (readback[pixel_index * 4] >= 250 &&
            !readback[pixel_index * 4 + 1] &&
            !readback[pixel_index * 4 + 2] &&
            readback[pixel_index * 4 + 3] == 255)
            ++found;
    if (found != 1)
        return 5;
    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return 0;
}
