#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

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

int main(void)
{
    static const char vertex_source[] =
        "void main(){gl_PointSize=3.0;gl_Position=vec4(0.0,0.0,0.0,1.0);}";
    static const char fragment_source[] =
        "precision mediump float;uniform sampler2D image;"
        "void main(){gl_FragColor=texture2D(image,vec2(0.5));}";
    static const GLubyte texel[4] = {37, 109, 211, 255};
    uint8_t pixels[8 * 8 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 8, 8, 8 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT
    };
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint texture;
    GLint linked = 0;
    GLint value = 19;
    GLubyte pixel[4];

    if (!context)
        return 1;
    glEnable(GL_TEXTURE_2D);
    if (glGetError() != GL_INVALID_ENUM)
        return 2;
    glEnable(GL_LIGHTING);
    if (glGetError() != GL_INVALID_ENUM)
        return 3;
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 4);
    if (glGetError() != GL_INVALID_ENUM)
        return 4;
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &value);
    if (value != 19 || glGetError() != GL_INVALID_ENUM)
        return 5;
    glGetIntegerv(GL_POINT_SIZE, &value);
    if (value != 19 || glGetError() != GL_INVALID_ENUM)
        return 10;
    {
        GLboolean boolean_value = 7;
        GLfloat matrix[16];
        int index;

        glGetBooleanv(GL_POLYGON_MODE, &boolean_value);
        if (boolean_value != 7 || glGetError() != GL_INVALID_ENUM)
            return 11;
        for (index = 0; index < 16; ++index)
            matrix[index] = 23.0f;
        glGetFloatv(GL_MODELVIEW_MATRIX, matrix);
        if (glGetError() != GL_INVALID_ENUM)
            return 12;
        for (index = 0; index < 16; ++index)
            if (matrix[index] != 23.0f)
                return 13;
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    if (glGetError() != GL_INVALID_ENUM)
        return 6;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, texel);

    vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
    fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (!vertex_shader || !fragment_shader)
        return 7;
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked || glGetError() != GL_NO_ERROR)
        return 8;
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "image"), 0);
    glViewport(0, 0, 8, 8);
    glDrawArrays(GL_POINTS, 0, 1);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (glGetError() != GL_NO_ERROR || pixel[0] != texel[0] ||
        pixel[1] != texel[1] || pixel[2] != texel[2] || pixel[3] != texel[3])
        return 9;

    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteTextures(1, &texture);
    ntglDestroyContext(context);
    return 0;
}
