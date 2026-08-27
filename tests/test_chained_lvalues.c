#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
        "void main(){gl_PointSize=4.0;gl_Position=vec4(0.0,0.0,0.0,1.0);}";
    static const char fragment_source[] =
        "precision highp float;"
        "struct Store{vec2 vectors[2];mat2 matrices[2];};"
        "struct Leaf{mat2 transforms[2];};"
        "struct Root{Leaf leaves[2];};"
        "void addQuarter(inout float value){value+=0.25;}"
        "void setHalf(out float value){value=0.5;}"
        "float bump(inout float value){value+=0.25;return value;}"
        "void main(){bool ok=true;"
        "mat2 matrix=mat2(1.0);matrix[1][1]=2.0;"
        "float assigned=(matrix[0][0]=0.5);"
        "ok=ok&&assigned==0.5&&matrix[0][0]==0.5&&matrix[1][1]==2.0;"
        "vec2 vectors[2];vectors[1][0]=0.25;vectors[1][1]+=0.5;"
        "ok=ok&&all(equal(vectors[1],vec2(0.25,0.5)));"
        "mat2 matrices[2];matrices[1][0][1]=0.75;"
        "float oldElement=matrices[1][0][1]++;"
        "ok=ok&&oldElement==0.75&&matrices[1][0][1]==1.75;"
        "Store store;store.vectors[1][0]=0.375;store.vectors[1][1]+=0.625;"
        "store.matrices[1][0][1]=0.875;"
        "float assignedStore=(store.vectors[0][1]=0.5);"
        "float oldStore=store.matrices[1][0][1]++;"
        "ok=ok&&all(equal(store.vectors[1],vec2(0.375,0.625)));"
        "ok=ok&&assignedStore==0.5&&store.vectors[0][1]==0.5;"
        "ok=ok&&oldStore==0.875&&store.matrices[1][0][1]==1.875;"
        "Root roots[2];roots[1].leaves[0].transforms[1][0][1]=0.625;"
        "roots[1].leaves[0].transforms[1][0][1]+=0.125;"
        "float oldNested=roots[1].leaves[0].transforms[1][0][1]++;"
        "ok=ok&&oldNested==0.75;"
        "ok=ok&&roots[1].leaves[0].transforms[1][0][1]==1.75;"
        "addQuarter(roots[1].leaves[0].transforms[1][0][1]);"
        "setHalf(roots[0].leaves[1].transforms[0][1][0]);"
        "ok=ok&&roots[1].leaves[0].transforms[1][0][1]==2.0;"
        "ok=ok&&roots[0].leaves[1].transforms[0][1][0]==0.5;"
        "float parenthesized=0.0;((parenthesized))=0.125;"
        "(parenthesized)+=0.125;float oldParenthesized=(parenthesized)++;"
        "++((parenthesized));addQuarter(((parenthesized)));"
        "setHalf(((roots[0].leaves[0].transforms[0][0][1])));"
        "ok=ok&&oldParenthesized==0.25&&parenthesized==2.5;"
        "ok=ok&&roots[0].leaves[0].transforms[0][0][1]==0.5;"
        "vec2 parenthesizedVector=vec2(0.0);"
        "(parenthesizedVector).x=0.125;((parenthesizedVector)).y=0.25;"
        "((parenthesizedVector).x)+=0.125;"
        "float oldVector=(parenthesizedVector).y++;"
        "setHalf((parenthesizedVector).x);"
        "ok=ok&&oldVector==0.25;"
        "ok=ok&&all(equal(parenthesizedVector,vec2(0.5,1.25)));"
        "float expressionValue=0.0;expressionValue+1.0;"
        "true?bump(expressionValue):bump(expressionValue);"
        "false&&(bump(expressionValue)>0.0);expressionValue==0.25;"
        "expressionValue,bump(expressionValue);"
        "ok=ok&&expressionValue==0.5;"
        ";if(false);for(;false;);while(false);do;while(false);"
        "gl_FragColor=ok?vec4(0.0,1.0,0.0,1.0):vec4(1.0,0.0,0.0,1.0);}";
    static const char invalid_deep_out_type[] =
        "precision highp float;struct Leaf{mat2 transforms[2];};"
        "struct Root{Leaf leaves[2];};"
        "void consume(inout vec2 value){value=vec2(1.0);}"
        "void main(){Root roots[2];"
        "consume(roots[1].leaves[0].transforms[1][0][1]);"
        "gl_FragColor=vec4(1.0);}";
    static const char invalid_deep_initializer[] =
        "precision highp float;struct Leaf{mat2 transforms[2];};"
        "struct Root{Leaf leaves[2];};void main(){Root roots[2];"
        "vec2 wrong=roots[1].leaves[0].transforms[1][0][1];"
        "gl_FragColor=vec4(wrong,0.0,1.0);}";
    static const char invalid_deep_assignment[] =
        "precision highp float;struct Leaf{mat2 transforms[2];};"
        "struct Root{Leaf leaves[2];};void main(){Root roots[2];"
        "roots[1].leaves[0].transforms[1][0][1]=vec2(1.0);"
        "gl_FragColor=vec4(1.0);}";
    static const char invalid_parenthesized_const_assignment[] =
        "precision mediump float;void main(){const float value=0.0;"
        "(((value)))=1.0;gl_FragColor=vec4(value);}";
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
    if (!strstr(fragment_source, "void main(){") ||
        strlen(strstr(fragment_source, "void main(){")) <= 2048)
        return 10;
    if (!shader_rejected(GL_FRAGMENT_SHADER, invalid_deep_out_type))
        return 6;
    if (!shader_rejected(GL_FRAGMENT_SHADER, invalid_deep_initializer))
        return 7;
    if (!shader_rejected(GL_FRAGMENT_SHADER, invalid_deep_assignment))
        return 8;
    if (!shader_rejected(GL_FRAGMENT_SHADER,
                         invalid_parenthesized_const_assignment))
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
    if (pixel[0] != 0 || pixel[1] != 255 || pixel[2] != 0 || pixel[3] != 255) {
        fprintf(stderr, "chained lvalue pixel: %u %u %u %u\n", pixel[0],
                pixel[1], pixel[2], pixel[3]);
        return 4;
    }
    if (glGetError() != GL_NO_ERROR)
        return 5;

    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    puts("chained lvalue tests passed");
    return 0;
}
