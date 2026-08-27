#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stddef.h>
#include <stdint.h>

typedef struct Vertex {
    float position[2];
    float color[4];
} Vertex;

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint compiled = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    return compiled ? shader : 0;
}

static int pixel_near(const GLubyte pixel[4], int red, int green, int blue,
                      int alpha)
{
    return pixel[0] >= red - 4 && pixel[0] <= red + 4 &&
           pixel[1] >= green - 4 && pixel[1] <= green + 4 &&
           pixel[2] >= blue - 4 && pixel[2] <= blue + 4 &&
           pixel[3] >= alpha - 4 && pixel[3] <= alpha + 4;
}

int main(void)
{
    static const char vertex_source[] =
        "attribute vec2 position; attribute vec4 color; varying vec4 line_color;"
        "void main() { line_color = color; gl_Position = vec4(position, 0.0, 1.0); }";
    static const char fragment_source[] =
        "precision mediump float; varying vec4 line_color;"
        "void main() { gl_FragColor = line_color; }";
    static const Vertex vertices[] = {
        {{-0.75f, -0.75f}, {1.0f, 0.0f, 0.0f, 0.5f}},
        {{0.75f, 0.75f}, {0.0f, 0.0f, 1.0f, 0.5f}},
    };
    static const Vertex rounded_width_vertices[] = {
        {{-0.75f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{0.75f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    };
    uint8_t pixels[32 * 32 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 32, 32, 32 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint buffer;
    GLint linked = GL_FALSE;
    GLubyte center[4];
    GLubyte adjacent[4];
    GLubyte outside[4];
    GLubyte lower[4];
    GLubyte upper[4];

    if (!context)
        return 1;
    vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
    fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (!vertex_shader || !fragment_shader)
        return 2;
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glBindAttribLocation(program, 0, "position");
    glBindAttribLocation(program, 1, "color");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 3;
    glUseProgram(program);
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (const void *)offsetof(Vertex, position));
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (const void *)offsetof(Vertex, color));
    glViewport(0, 0, 32, 32);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(5.0f);
    glDrawArrays(GL_LINES, 0, 2);
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
    glReadPixels(16, 17, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, adjacent);
    glReadPixels(16, 20, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, outside);
    if (!pixel_near(center, 61, 0, 67, 191))
        return 4;
    if (!pixel_near(adjacent, 58, 0, 69, 191))
        return 5;
    if (outside[0] || outside[1] || outside[2] || outside[3] != 255)
        return 6;

    glDisable(GL_BLEND);
    glClear(GL_COLOR_BUFFER_BIT);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rounded_width_vertices),
                 rounded_width_vertices, GL_STATIC_DRAW);
    glLineWidth(1.6f);
    glDrawArrays(GL_LINES, 0, 2);
    glReadPixels(16, 15, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, lower);
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, upper);
    glReadPixels(16, 17, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, outside);
    if (lower[1] < 250 || upper[1] < 250)
        return 7;
    if (outside[0] || outside[1] || outside[2] || outside[3] != 255)
        return 8;

    glDeleteBuffers(1, &buffer);
    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return 0;
}
