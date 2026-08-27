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

int main(void)
{
    static const char vertex_source[] =
        "void main(){gl_PointSize=4.0;gl_Position=vec4(0.0,0.0,0.0,1.0);}";
    static const char fragment_template[] =
        "precision mediump float;void main(){float total=0.0;"
        "for(int i=0,j=3;i<3;i++,j--)total+=float(i+j)*0.025;"
        "for(int k=0;k<1;%s)total+=0.025;"
        "bool ok=abs(total-0.25)<0.001;"
        "gl_FragColor=ok?vec4(0.0,1.0,0.0,1.0):vec4(1.0,0.0,0.0,1.0);}";
    char long_increment[600];
    char fragment_source[1400];
    uint8_t pixels[8 * 8 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 8, 8, 8 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context;
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLint linked = GL_FALSE;
    GLubyte pixel[4];
    int offset = 0;
    int index;

    for (index = 0; index < 260; ++index)
        long_increment[offset++] = '(';
    long_increment[offset++] = 'k';
    for (index = 0; index < 260; ++index)
        long_increment[offset++] = ')';
    long_increment[offset++] = '+';
    long_increment[offset++] = '+';
    long_increment[offset] = '\0';
    if (snprintf(fragment_source, sizeof(fragment_source), fragment_template,
                 long_increment) >= (int)sizeof(fragment_source))
        return 1;
    context = ntglCreateContext(&framebuffer, NULL);
    if (!context)
        return 2;
    vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
    fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (!vertex_shader || !fragment_shader)
        return 3;
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 4;
    glUseProgram(program);
    glViewport(0, 0, 8, 8);
    glDrawArrays(GL_POINTS, 0, 1);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 0 || pixel[1] != 255 || pixel[2] != 0 ||
        pixel[3] != 255) {
        fprintf(stderr, "for declarator pixel: %u %u %u %u\n",
                pixel[0], pixel[1], pixel[2], pixel[3]);
        return 5;
    }
    if (glGetError() != GL_NO_ERROR)
        return 6;

    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    puts("for declarator tests passed");
    return 0;
}
