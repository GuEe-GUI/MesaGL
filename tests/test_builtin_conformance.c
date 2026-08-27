#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>

static GLuint compile_shader(GLenum type, const char *source, GLboolean expected)
{
    GLuint shader = glCreateShader(type);
    GLint compiled = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!!compiled != !!expected) {
        char log[512];

        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "unexpected shader result: %s\n", log);
        return 0;
    }
    return shader;
}

int main(void)
{
    static const char vertex_source[] =
        "void main(){gl_PointSize=4.0;gl_Position=vec4(0.0,0.0,0.0,1.0);}";
    static const char fragment_source[] =
        "precision highp float;"
        "struct Pair{vec2 value;mat2 transform;};"
        "float abs(int value){return float(value)+0.25;}"
        "bool near(float a,float b){return abs(a-b)<0.002;}"
        "void main(){"
        "bool ok=true;float pi=3.14159265;float loopSum=0.0;"
        "for(float loopIndex=0.0;loopIndex<3.0;loopIndex+=1.0)loopSum+=1.0;"
        "ok=ok&&near(loopSum,3.0);"
        "ok=ok&&near(abs(2),2.25);"
        "ok=ok&&near(radians(180.0),pi)&&near(degrees(pi),180.0);"
        "ok=ok&&near(sin(0.0),0.0)&&near(cos(0.0),1.0);"
        "ok=ok&&near(tan(0.0),0.0)&&near(asin(0.0),0.0);"
        "ok=ok&&near(acos(1.0),0.0)&&near(atan(0.0),0.0);"
        "ok=ok&&near(atan(0.0,1.0),0.0);"
        "ok=ok&&near(exp(0.0),1.0)&&near(log(1.0),0.0);"
        "ok=ok&&near(exp2(0.0),1.0)&&near(log2(1.0),0.0);"
        "ok=ok&&near(sqrt(1.0),1.0)&&near(inversesqrt(1.0),1.0);"
        "ok=ok&&near(floor(1.75),1.0)&&near(ceil(0.25),1.0);"
        "ok=ok&&near(mod(1.5,1.0),0.5);"
        "bvec3 a=lessThanEqual(vec3(1.0,2.0,4.0),vec3(1.0,3.0,3.0));"
        "bvec3 b=greaterThan(vec3(2.0,1.0,4.0),vec3(1.0,1.0,3.0));"
        "bvec3 c=greaterThanEqual(vec3(1.0,2.0,3.0),vec3(1.0,1.0,4.0));"
        "bvec3 d=equal(vec3(1.0,2.0,3.0),vec3(1.0,0.0,3.0));"
        "bvec3 e=notEqual(vec3(1.0,2.0,3.0),vec3(1.0,0.0,3.0));"
        "ok=ok&&all(equal(a,bvec3(true,true,false)));"
        "ok=ok&&all(equal(b,bvec3(true,false,true)));"
        "ok=ok&&all(equal(c,bvec3(true,true,false)));"
        "ok=ok&&all(equal(d,bvec3(true,false,true)));"
        "ok=ok&&all(equal(e,bvec3(false,true,false)));"
        "ok=ok&&all(not(bvec3(false,false,false)));"
        "mat2 matrixA=mat2(1.0);mat2 matrixB=mat2(1.0);"
        "ok=ok&&(matrixA==matrixB);matrixB[1][1]=2.0;"
        "ok=ok&&(matrixA!=matrixB);"
        "float assigned=(matrixA[0][0]=0.5);"
        "ok=ok&&near(assigned,0.5)&&near(matrixA[0][0],0.5);"
        "vec2 vectors[2];vectors[1][0]=0.25;vectors[1][1]+=0.5;"
        "ok=ok&&all(equal(vectors[1],vec2(0.25,0.5)));"
        "mat2 matrices[2];matrices[1][0][1]=0.75;"
        "float oldElement=matrices[1][0][1]++;"
        "ok=ok&&near(oldElement,0.75)&&near(matrices[1][0][1],1.75);"
        "Pair pairA=Pair(vec2(0.25),mat2(1.0));Pair pairB=pairA;"
        "ok=ok&&(pairA==pairB);pairB.value.y=0.5;pairB.transform[1][1]=2.0;"
        "ok=ok&&(pairA!=pairB);"
        "gl_FragColor=ok?vec4(0.0,1.0,0.0,1.0):vec4(1.0,0.0,0.0,1.0);}";
    static const char invalid_trig_source[] =
        "precision mediump float;"
        "void main(){gl_FragColor=vec4(sin(ivec2(1)));}";
    static const char invalid_relational_source[] =
        "precision mediump float;"
        "void main(){bvec2 v=lessThan(vec2(1.0),ivec2(1));"
        "gl_FragColor=vec4(v,0.0,1.0);}";
    static const char invalid_vector_result_source[] =
        "precision mediump float;"
        "void main(){float v=normalize(vec3(1.0));gl_FragColor=vec4(v);}";
    static const char invalid_scalar_result_source[] =
        "precision mediump float;"
        "void main(){vec2 v=dot(vec3(1.0),vec3(2.0));"
        "gl_FragColor=vec4(v,0.0,1.0);}";
    static const char invalid_bvec_result_source[] =
        "precision mediump float;"
        "void main(){float v=lessThan(vec2(1.0),vec2(2.0));"
        "gl_FragColor=vec4(v);}";
    static const char invalid_bool_result_source[] =
        "precision mediump float;"
        "void main(){bvec2 v=all(bvec2(true));"
        "gl_FragColor=vec4(v,0.0,1.0);}";
    uint8_t pixels[8 * 8 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 8, 8, 8 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint invalid_shader;
    GLuint program;
    GLint linked = GL_FALSE;
    GLubyte pixel[4];

    if (!context)
        return 1;
    invalid_shader = compile_shader(GL_FRAGMENT_SHADER, invalid_trig_source, GL_FALSE);
    if (!invalid_shader)
        return 2;
    glDeleteShader(invalid_shader);
    invalid_shader = compile_shader(GL_FRAGMENT_SHADER, invalid_relational_source, GL_FALSE);
    if (!invalid_shader)
        return 3;
    glDeleteShader(invalid_shader);
    invalid_shader = compile_shader(GL_FRAGMENT_SHADER,
                                    invalid_vector_result_source, GL_FALSE);
    if (!invalid_shader)
        return 7;
    glDeleteShader(invalid_shader);
    invalid_shader = compile_shader(GL_FRAGMENT_SHADER,
                                    invalid_scalar_result_source, GL_FALSE);
    if (!invalid_shader)
        return 8;
    glDeleteShader(invalid_shader);
    invalid_shader = compile_shader(GL_FRAGMENT_SHADER,
                                    invalid_bvec_result_source, GL_FALSE);
    if (!invalid_shader)
        return 9;
    glDeleteShader(invalid_shader);
    invalid_shader = compile_shader(GL_FRAGMENT_SHADER,
                                    invalid_bool_result_source, GL_FALSE);
    if (!invalid_shader)
        return 10;
    glDeleteShader(invalid_shader);

    vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source, GL_TRUE);
    fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source, GL_TRUE);
    if (!vertex_shader || !fragment_shader)
        return 4;
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 5;
    glUseProgram(program);
    glViewport(0, 0, 8, 8);
    glDrawArrays(GL_POINTS, 0, 1);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 0 || pixel[1] != 255 || pixel[2] != 0 || pixel[3] != 255) {
        fprintf(stderr, "builtin conformance pixel: %u %u %u %u\n", pixel[0],
                pixel[1], pixel[2], pixel[3]);
        return 6;
    }

    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    puts("builtin conformance tests passed");
    return 0;
}
