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
        char log[256];

        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "long identifier shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

int main(void)
{
    static const char vertex_source[] =
        "attribute vec2 position;"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); }";
    static const char fragment_source[] =
        "precision mediump float;"
        "struct mesaGL_structure_type_identifier_that_is_deliberately_longer_than_sixty_four_characters {"
        "vec4 mesaGL_structure_member_identifier_that_is_deliberately_longer_than_sixty_four_characters;"
        "};"
        "mesaGL_structure_type_identifier_that_is_deliberately_longer_than_sixty_four_characters "
        "pass_value(mesaGL_structure_type_identifier_that_is_deliberately_longer_than_sixty_four_characters value) {"
        "return value;"
        "}"
        "void main() {"
        "mesaGL_structure_type_identifier_that_is_deliberately_longer_than_sixty_four_characters value = "
        "mesaGL_structure_type_identifier_that_is_deliberately_longer_than_sixty_four_characters("
        "vec4(0.25, 0.5, 0.75, 1.0));"
        "value = pass_value(value);"
        "gl_FragColor = value."
        "mesaGL_structure_member_identifier_that_is_deliberately_longer_than_sixty_four_characters;"
        "}";
    static const GLfloat vertices[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    uint8_t pixels[4 * 4 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 4, 4, 4 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context;
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint buffer;
    GLint linked = GL_FALSE;
    GLint position;
    GLubyte pixel[4] = {0};

    context = ntglCreateContext(&framebuffer, NULL);
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
    if (!linked) {
        char log[256];

        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "long identifier program link failed: %s\n", log);
        return 3;
    }
    glUseProgram(program);
    position = glGetAttribLocation(program, "position");
    if (position < 0)
        return 4;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray((GLuint)position);
    glVertexAttribPointer((GLuint)position, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glViewport(0, 0, 4, 4);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 63 || pixel[0] > 65 || pixel[1] < 127 || pixel[1] > 129 ||
        pixel[2] < 190 || pixel[2] > 192 || pixel[3] != 255) {
        fprintf(stderr, "unexpected long identifier pixel: %u %u %u %u\n",
                pixel[0], pixel[1], pixel[2], pixel[3]);
        return 5;
    }
    glDeleteBuffers(1, &buffer);
    glDeleteProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    ntglDestroyContext(context);
    puts("long identifier tests passed");
    return 0;
}
