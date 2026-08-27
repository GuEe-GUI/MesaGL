#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_source(const char *path)
{
    FILE *file;
    char *source;
    long length;

    file = fopen(path, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    source = malloc((size_t)length + 1);
    if (!source) {
        fclose(file);
        return NULL;
    }
    if (fread(source, 1, (size_t)length, file) != (size_t)length) {
        free(source);
        fclose(file);
        return NULL;
    }
    source[length] = '\0';
    fclose(file);
    return source;
}

int main(int argc, char **argv)
{
    unsigned char pixel[4] = {0};
    NTGLframebuffer framebuffer = {
        pixel, 1, 1, 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT,
    };
    NTGLcontext *context;
    GLenum stage;
    GLuint shader;
    GLint status;
    GLint log_length;
    char *source;
    char *log;

    if (argc != 3 || (strcmp(argv[1], "vert") && strcmp(argv[1], "frag"))) {
        fprintf(stderr, "usage: %s vert|frag shader\n", argv[0]);
        return 2;
    }
    source = read_source(argv[2]);
    if (!source) {
        fprintf(stderr, "cannot read %s\n", argv[2]);
        return 2;
    }
    context = ntglCreateContext(&framebuffer, NULL);
    if (!context) {
        free(source);
        return 2;
    }
    ntglMakeCurrent(context);
    stage = !strcmp(argv[1], "vert") ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
    shader = glCreateShader(stage);
    glShaderSource(shader, 1, (const GLchar *const *)&source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    log = calloc((size_t)(log_length > 0 ? log_length : 1), 1);
    if (log) {
        glGetShaderInfoLog(shader, log_length, NULL, log);
        if (log[0])
            fprintf(stderr, "%s\n", log);
        free(log);
    }
    glDeleteShader(shader);
    ntglDestroyContext(context);
    free(source);
    return status ? 0 : 1;
}
