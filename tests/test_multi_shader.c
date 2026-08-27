#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader;
    GLint compiled;

    shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[256];

        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "shader compilation failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static int center_is_green(void)
{
    uint8_t pixel[4];

    glReadPixels(31, 31, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    return pixel[0] >= 49 && pixel[0] <= 53 && pixel[1] >= 202 && pixel[1] <= 206 &&
           pixel[2] >= 100 && pixel[2] <= 104 && pixel[3] == 255;
}

static int center_is_blue(void)
{
    uint8_t pixel[4];

    glReadPixels(31, 31, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    return pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 255 && pixel[3] == 255;
}

int main(void)
{
    uint32_t pixels[64 * 64] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 64, 64, 64 * (int)sizeof(pixels[0]), NTGL_XRGB8888,
        NTGL_ORIGIN_BOTTOM_LEFT,
    };
    const GLfloat vertices[] = {
        -0.8f, -0.8f,
        0.8f, -0.8f,
        0.0f, 0.8f,
    };
    const char vertex_helper_source[] =
        "vec2 place(vec2 position) { return position; }";
    const char vertex_main_source[] =
        "attribute vec2 position; vec2 place(vec2 position);"
        "void main() { gl_Position = vec4(place(position), 0.0, 1.0); }";
    const char fragment_helper_source[] =
        "mediump vec4 shade() { return vec4(0.2, 0.8, 0.4, 1.0); }";
    const char fragment_main_source[] =
        "precision mediump float; vec4 shade();"
        "void main() { gl_FragColor = shade(); }";
    const char standalone_vertex_source[] =
        "attribute vec2 position;"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); }";
    const char standalone_fragment_source[] =
        "precision mediump float;"
        "void main() { gl_FragColor = vec4(0.0, 0.0, 1.0, 1.0); }";
    NTGLcontext *context;
    GLuint shaders[4];
    GLuint attached[4] = {0};
    GLuint program;
    GLuint duplicate_program;
    GLuint duplicate_main;
    GLuint relink_failure;
    GLuint namespace_program;
    GLuint namespace_shader[2];
    GLint count;
    GLint linked;
    GLint position;
    GLsizei returned;
    const GLchar *replacement;
    GLchar log[256];
    int i;

    context = ntglCreateContext(&framebuffer, NULL);
    if (!context)
        return 1;
    ntglMakeCurrent(context);

    shaders[0] = compile_shader(GL_VERTEX_SHADER, vertex_helper_source);
    shaders[1] = compile_shader(GL_VERTEX_SHADER, vertex_main_source);
    shaders[2] = compile_shader(GL_FRAGMENT_SHADER, fragment_helper_source);
    shaders[3] = compile_shader(GL_FRAGMENT_SHADER, fragment_main_source);
    for (i = 0; i < 4; ++i) {
        if (!shaders[i])
            return 2;
    }

    program = glCreateProgram();
    for (i = 0; i < 4; ++i)
        glAttachShader(program, shaders[i]);
    glAttachShader(program, shaders[0]);
    if (glGetError() != GL_INVALID_OPERATION)
        return 3;

    glGetProgramiv(program, GL_ATTACHED_SHADERS, &count);
    glGetAttachedShaders(program, 4, &returned, attached);
    if (count != 4 || returned != 4)
        return 4;
    for (i = 0; i < 4; ++i) {
        if (attached[i] != shaders[i])
            return 5;
    }

    glDeleteShader(shaders[0]);
    glDeleteShader(shaders[2]);
    if (!glIsShader(shaders[0]) || !glIsShader(shaders[2]))
        return 6;

    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[256];

        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "multi-object link failed: %s\n", log);
        return 7;
    }
    glUseProgram(program);
    position = glGetAttribLocation(program, "position");
    if (position < 0)
        return 8;
    glViewport(0, 0, 64, 64);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glVertexAttribPointer((GLuint)position, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray((GLuint)position);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!center_is_green())
        return 9;

    glDetachShader(program, shaders[0]);
    glDetachShader(program, shaders[2]);
    if (glIsShader(shaders[0]) || glIsShader(shaders[2]))
        return 10;
    glGetProgramiv(program, GL_ATTACHED_SHADERS, &count);
    if (count != 2)
        return 11;
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!center_is_green())
        return 12;

    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    glGetProgramInfoLog(program, sizeof(log), NULL, log);
    if (linked || !strstr(log, "undefined function") || !strstr(log, "place"))
        return 25;
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!center_is_green())
        return 26;

    relink_failure = compile_shader(GL_VERTEX_SHADER,
                                    "void main() { gl_Position = vec4(0.0); }");
    if (!relink_failure)
        return 13;
    glAttachShader(program, relink_failure);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked)
        return 14;
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!center_is_green())
        return 15;
    glUseProgram(program);
    if (glGetError() != GL_INVALID_OPERATION)
        return 16;
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!center_is_green())
        return 17;

    glDetachShader(program, relink_failure);
    glDeleteShader(relink_failure);
    replacement = standalone_vertex_source;
    glShaderSource(shaders[1], 1, &replacement, NULL);
    glCompileShader(shaders[1]);
    glGetShaderiv(shaders[1], GL_COMPILE_STATUS, &linked);
    if (!linked)
        return 18;
    replacement = standalone_fragment_source;
    glShaderSource(shaders[3], 1, &replacement, NULL);
    glCompileShader(shaders[3]);
    glGetShaderiv(shaders[3], GL_COMPILE_STATUS, &linked);
    if (!linked)
        return 19;
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 20;
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!center_is_blue())
        return 21;

    duplicate_program = glCreateProgram();
    duplicate_main = compile_shader(GL_VERTEX_SHADER,
                                    "void main() { gl_Position = vec4(0.0); }");
    if (!duplicate_main)
        return 22;
    glAttachShader(duplicate_program, shaders[1]);
    glAttachShader(duplicate_program, duplicate_main);
    glAttachShader(duplicate_program, shaders[3]);
    glLinkProgram(duplicate_program);
    glGetProgramiv(duplicate_program, GL_LINK_STATUS, &linked);
    if (linked)
        return 23;

    namespace_shader[0] = compile_shader(GL_VERTEX_SHADER, "float shared_global;");
    namespace_shader[1] = compile_shader(
        GL_VERTEX_SHADER,
        "float shared_global; void main() { gl_Position = vec4(0.0); }");
    if (!namespace_shader[0] || !namespace_shader[1])
        return 27;
    namespace_program = glCreateProgram();
    glAttachShader(namespace_program, namespace_shader[0]);
    glAttachShader(namespace_program, namespace_shader[1]);
    glAttachShader(namespace_program, shaders[3]);
    glLinkProgram(namespace_program);
    glGetProgramiv(namespace_program, GL_LINK_STATUS, &linked);
    glGetProgramInfoLog(namespace_program, sizeof(log), NULL, log);
    if (linked || !strstr(log, "duplicate global"))
        return 28;
    glDeleteProgram(namespace_program);
    glDeleteShader(namespace_shader[0]);
    glDeleteShader(namespace_shader[1]);

    namespace_shader[0] = compile_shader(GL_VERTEX_SHADER,
                                         "uniform vec4 shared_uniform;");
    namespace_shader[1] = compile_shader(
        GL_VERTEX_SHADER,
        "uniform vec4 shared_uniform;"
        "void main() { gl_Position = shared_uniform; }");
    if (!namespace_shader[0] || !namespace_shader[1])
        return 29;
    namespace_program = glCreateProgram();
    glAttachShader(namespace_program, namespace_shader[0]);
    glAttachShader(namespace_program, namespace_shader[1]);
    glAttachShader(namespace_program, shaders[3]);
    glLinkProgram(namespace_program);
    glGetProgramiv(namespace_program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 30;
    glDeleteProgram(namespace_program);
    glDeleteShader(namespace_shader[0]);
    glDeleteShader(namespace_shader[1]);

    namespace_shader[0] = compile_shader(
        GL_VERTEX_SHADER, "struct SharedType { float value; };");
    namespace_shader[1] = compile_shader(
        GL_VERTEX_SHADER,
        "float SharedType; void main() { gl_Position = vec4(SharedType); }");
    if (!namespace_shader[0] || !namespace_shader[1])
        return 31;
    namespace_program = glCreateProgram();
    glAttachShader(namespace_program, namespace_shader[0]);
    glAttachShader(namespace_program, namespace_shader[1]);
    glAttachShader(namespace_program, shaders[3]);
    glLinkProgram(namespace_program);
    glGetProgramiv(namespace_program, GL_LINK_STATUS, &linked);
    glGetProgramInfoLog(namespace_program, sizeof(log), NULL, log);
    if (linked || !strstr(log, "structure type conflicts"))
        return 32;
    glDeleteProgram(namespace_program);
    glDeleteShader(namespace_shader[0]);
    glDeleteShader(namespace_shader[1]);

    glDetachShader(program, shaders[0]);
    if (glGetError() != GL_INVALID_VALUE)
        return 24;

    glDeleteProgram(duplicate_program);
    glDeleteShader(duplicate_main);
    glDeleteProgram(program);
    glDeleteShader(shaders[1]);
    glDeleteShader(shaders[3]);
    ntglDestroyContext(context);
    return 0;
}
