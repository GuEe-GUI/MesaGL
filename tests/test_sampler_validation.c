#include "GLES2/gl2.h"
#include "mesaGL/config.h"
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
    return compiled ? shader : 0;
}

static int build_sampler_shader(char *output, size_t capacity, GLenum stage,
                                int sampler_count)
{
    int length;

    if (sampler_count > 0)
        length = snprintf(
            output, capacity,
            stage == GL_VERTEX_SHADER
                ? "uniform sampler2D vertex_images[%d];void main(){float sampled="
                  "texture2D(vertex_images[%d],vec2(0.5)).x;"
                  "gl_Position=vec4(sampled*0.0,0.0,0.0,1.0);}"
                : "precision mediump float;uniform sampler2D fragment_images[%d];"
                  "void main(){gl_FragColor=texture2D(fragment_images[%d],vec2(0.5));}",
            sampler_count, sampler_count - 1);
    else
        length = snprintf(
            output, capacity,
            stage == GL_VERTEX_SHADER
                ? "void main(){gl_Position=vec4(0.0,0.0,0.0,1.0);}"
                : "precision mediump float;void main(){gl_FragColor=vec4(1.0);}");
    return length >= 0 && (size_t)length < capacity;
}

static int sampler_link_status(int vertex_samplers, int fragment_samplers,
                               char *log, size_t log_size)
{
    char vertex_source[1024];
    char fragment_source[1024];
    GLuint vertex;
    GLuint fragment;
    GLuint program;
    GLint linked = GL_FALSE;

    if (!build_sampler_shader(vertex_source, sizeof(vertex_source),
                              GL_VERTEX_SHADER, vertex_samplers) ||
        !build_sampler_shader(fragment_source, sizeof(fragment_source),
                              GL_FRAGMENT_SHADER, fragment_samplers))
        return 0;
    vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
    fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (!vertex || !fragment)
        return 0;
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (log)
        glGetProgramInfoLog(program, (GLsizei)log_size, NULL, log);
    glDeleteProgram(program);
    glDeleteShader(fragment);
    glDeleteShader(vertex);
    return linked;
}

int main(void)
{
    uint8_t pixels[8 * 8 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 8, 8, 8 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    const GLfloat vertices[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    const char vertex_source[] =
        "attribute vec2 position;"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); }";
    const char fragment_source[] =
        "precision mediump float;"
        "uniform sampler2D image2d; uniform samplerCube image_cube;"
        "void main() { gl_FragColor = texture2D(image2d, vec2(0.5)) +"
        "                             textureCube(image_cube, vec3(1.0, 0.0, 0.0)); }";
    NTGLcontext *context;
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint empty_program;
    GLuint buffer;
    GLint position;
    GLint image2d;
    GLint image_cube;
    GLint validated = GL_TRUE;
    GLint log_length = 0;
    GLchar log[160];
    GLubyte before[4];
    GLubyte after[4];
    int combined_vertex;
    int combined_fragment;

    context = ntglCreateContext(&framebuffer, NULL);
    if (!context)
        return 1;
    empty_program = glCreateProgram();
    glValidateProgram(empty_program);
    glGetProgramiv(empty_program, GL_VALIDATE_STATUS, &validated);
    glGetProgramInfoLog(empty_program, sizeof(log), NULL, log);
    if (validated || !strstr(log, "not been successfully linked") ||
        glGetError() != GL_NO_ERROR)
        return 19;
    glDeleteProgram(empty_program);
    if (sampler_link_status(MESAGL_MAX_VERTEX_TEXTURE_IMAGE_UNITS + 1, 0,
                            log, sizeof(log)) ||
        !strstr(log, "vertex sampler"))
        return 15;
    if (sampler_link_status(0, MESAGL_MAX_FRAGMENT_TEXTURE_IMAGE_UNITS + 1,
                            log, sizeof(log)) ||
        !strstr(log, "fragment sampler"))
        return 16;
    combined_vertex = MESAGL_MAX_VERTEX_TEXTURE_IMAGE_UNITS <
                              MESAGL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
                          ? MESAGL_MAX_VERTEX_TEXTURE_IMAGE_UNITS
                          : MESAGL_MAX_COMBINED_TEXTURE_IMAGE_UNITS;
    combined_fragment = MESAGL_MAX_COMBINED_TEXTURE_IMAGE_UNITS -
                        combined_vertex;
    if (combined_fragment <= MESAGL_MAX_FRAGMENT_TEXTURE_IMAGE_UNITS &&
        !sampler_link_status(combined_vertex, combined_fragment, log,
                             sizeof(log)))
        return 17;
    if (combined_vertex + combined_fragment + 1 <=
            MESAGL_MAX_VERTEX_TEXTURE_IMAGE_UNITS +
                MESAGL_MAX_FRAGMENT_TEXTURE_IMAGE_UNITS) {
        if (combined_fragment < MESAGL_MAX_FRAGMENT_TEXTURE_IMAGE_UNITS)
            ++combined_fragment;
        else
            ++combined_vertex;
        if (sampler_link_status(combined_vertex, combined_fragment, log,
                                sizeof(log)) ||
            !strstr(log, "combined sampler"))
            return 18;
    }
    vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
    fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (!vertex_shader || !fragment_shader)
        return 2;
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glUseProgram(program);
    position = glGetAttribLocation(program, "position");
    image2d = glGetUniformLocation(program, "image2d");
    image_cube = glGetUniformLocation(program, "image_cube");
    if (position < 0 || image2d < 0 || image_cube < 0)
        return 3;

    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray((GLuint)position);
    glVertexAttribPointer((GLuint)position, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glViewport(0, 0, 8, 8);
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, before);

    glValidateProgram(program);
    glGetProgramiv(program, GL_VALIDATE_STATUS, &validated);
    if (validated)
        return 4;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    if (log_length <= 1 || log_length > (GLint)sizeof(log))
        return 12;
    memset(log, 0, sizeof(log));
    glGetProgramInfoLog(program, sizeof(log), NULL, log);
    if (!strstr(log, "sampler"))
        return 13;
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (glGetError() != GL_INVALID_OPERATION)
        return 5;
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, after);
    if (before[0] != after[0] || before[1] != after[1] ||
        before[2] != after[2] || before[3] != after[3])
        return 6;

    glUniform1i(image_cube, 1);
    glValidateProgram(program);
    glGetProgramiv(program, GL_VALIDATE_STATUS, &validated);
    if (!validated)
        return 7;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    if (log_length != 0)
        return 14;
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (glGetError() != GL_NO_ERROR)
        return 8;

    glUniform1i(image2d, MESAGL_MAX_TEXTURE_UNITS);
    glValidateProgram(program);
    glGetProgramiv(program, GL_VALIDATE_STATUS, &validated);
    if (validated)
        return 9;
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (glGetError() != GL_INVALID_OPERATION)
        return 10;
    glDrawArrays(GL_TRIANGLES, 0, 0);
    if (glGetError() != GL_INVALID_OPERATION)
        return 20;
    glDrawElements(GL_TRIANGLES, 0, GL_UNSIGNED_SHORT, NULL);
    if (glGetError() != GL_INVALID_OPERATION)
        return 21;

    glUniform1i(image2d, -1);
    glValidateProgram(program);
    glGetProgramiv(program, GL_VALIDATE_STATUS, &validated);
    if (validated)
        return 11;

    glDeleteBuffers(1, &buffer);
    glDeleteProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    ntglDestroyContext(context);
    puts("sampler validation tests passed");
    return 0;
}
