#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint compiled = 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    return compiled ? shader : 0;
}

int main(void)
{
    uint8_t pixels[16 * 16 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 16, 16, 16 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    const GLfloat vertices[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    const char vertex_source[] =
        "attribute vec2 position;"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); }";
    const char fragment_source[] =
        "precision mediump float;"
        "struct Surface { vec4 tint; };"
        "struct Material { Surface surfaces[2]; float weights[2], unused; };"
        "struct Light { Surface surfaces[2]; float weights[2]; };"
        "struct Cluster { Material materials[2]; };"
        "uniform Material material;"
        "uniform Light lights[2]; uniform Cluster cluster; uniform int selected;"
        "vec4 pass_unused(float, vec4 value) { return value; }"
        "void main() { gl_FragColor = pass_unused(123.0, "
        "material.surfaces[selected].tint * "
        "material.weights[selected] + "
        "vec4(lights[selected].surfaces[selected].tint.rgb, 0.0) + "
        "vec4(cluster.materials[selected].surfaces[selected].tint.rgb, 0.0)); }";
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint buffer;
    GLint linked = 0;
    GLint active = 0;
    GLint reported_max_length = 0;
    GLint actual_max_length = 0;
    GLint tint_location;
    GLint weights_location;
    GLint light_location;
    GLint cluster_tint_location;
    GLint selected_location;
    GLint position_location;
    GLchar active_name[48];
    GLenum active_type = 0;
    GLint active_size = 0;
    uint8_t pixel[4] = {0};

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
        GLchar log[256];

        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "uniform structure link failed: %s\n", log);
        return 3;
    }
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &active);
    if (active != 7 || glGetUniformLocation(program, "material.unused") != -1) {
        GLint uniform_index;

        fprintf(stderr, "unexpected active uniform count: %d\n", active);
        for (uniform_index = 0; uniform_index < active; ++uniform_index) {
            glGetActiveUniform(program, (GLuint)uniform_index, sizeof(active_name), NULL,
                               &active_size, &active_type, active_name);
            fprintf(stderr, "  %s size %d type 0x%x\n", active_name, active_size,
                    active_type);
        }
        return 4;
    }
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &reported_max_length);
    {
        GLint uniform_index;

        for (uniform_index = 0; uniform_index < active; ++uniform_index) {
            GLint name_length;

            glGetActiveUniform(program, (GLuint)uniform_index, sizeof(active_name),
                               &name_length, NULL, NULL, active_name);
            ++name_length;
            if (name_length > actual_max_length)
                actual_max_length = name_length;
        }
    }
    if (reported_max_length != actual_max_length)
        return 8;
    glGetActiveUniform(program, 0, sizeof(active_name), NULL, &active_size,
                       &active_type, active_name);
    if (strcmp(active_name, "material.surfaces[0].tint") || active_size != 2 ||
        active_type != GL_FLOAT_VEC4)
        return 5;
    glGetActiveUniform(program, 1, sizeof(active_name), NULL, &active_size,
                       &active_type, active_name);
    if (strcmp(active_name, "material.weights[0]") || active_size != 2 ||
        active_type != GL_FLOAT)
        return 6;
    tint_location = glGetUniformLocation(program, "material.surfaces[0].tint");
    weights_location = glGetUniformLocation(program, "material.weights[0]");
    light_location = glGetUniformLocation(program, "lights[1].surfaces[0].tint");
    cluster_tint_location =
        glGetUniformLocation(program, "cluster.materials[1].surfaces[0].tint");
    selected_location = glGetUniformLocation(program, "selected");
    position_location = glGetAttribLocation(program, "position");
    if (tint_location < 0 || weights_location < 0 || light_location < 0 ||
        cluster_tint_location < 0 ||
        selected_location < 0 || position_location < 0 ||
        glGetUniformLocation(program, "material.weights[1]") != weights_location + 1 ||
        glGetUniformLocation(program, "material.surfaces[1].tint") !=
            tint_location + 1 ||
        glGetUniformLocation(program, "lights[1].surfaces[1].tint") !=
            light_location + 1 ||
        glGetUniformLocation(program, "lights[1].weights[0]") != -1 ||
        glGetUniformLocation(program,
                             "cluster.materials[1].surfaces[1].tint") !=
            cluster_tint_location + 1)
        return 7;

    glUseProgram(program);
    {
        const GLfloat tints[8] = {
            0.0f, 0.0f, 0.0f, 1.0f, 0.2f, 0.4f, 0.8f, 1.0f};
        const GLfloat weights[2] = {0.25f, 0.5f};
        const GLfloat light_colors[8] = {
            0.0f, 0.0f, 0.0f, 1.0f, 0.1f, 0.2f, 0.3f, 1.0f};
        const GLfloat cluster_tints[8] = {
            0.0f, 0.0f, 0.0f, 1.0f, 0.1f, 0.05f, 0.0f, 1.0f};

        glUniform4fv(tint_location, 2, tints);
        glUniform1fv(weights_location, 2, weights);
        glUniform4fv(light_location, 2, light_colors);
        glUniform4fv(cluster_tint_location, 2, cluster_tints);
    }
    glUniform1i(selected_location, 1);
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray((GLuint)position_location);
    glVertexAttribPointer((GLuint)position_location, 2, GL_FLOAT, GL_FALSE,
                          2 * (GLsizei)sizeof(GLfloat), NULL);
    glViewport(0, 0, 16, 16);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 74 || pixel[0] > 79 || pixel[1] < 112 || pixel[1] > 117 ||
        pixel[2] < 176 || pixel[2] > 181 || pixel[3] < 126 || pixel[3] > 129)
        return 8;

    glDeleteBuffers(1, &buffer);
    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return 0;
}
