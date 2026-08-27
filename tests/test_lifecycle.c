#include "GLES2/gl2.h"
#include "mesaGL/port.h"

#include <stdint.h>

int main(void)
{
    static const GLchar shader_source[] = "void main() { gl_Position = vec4(0.0); }";
    int iteration;
    for (iteration = 0; iteration < 20; ++iteration) {
        uint32_t pixels[8 * 8] = {0};
        uint32_t texture_pixels[4] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
        MesaGLPortConfig config = {
            {pixels, 8, 8, 8 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT},
            {0}, 0, 0, {0}};
        MesaGLPortContext *context = mesaGLPortCreate(&config);
        GLuint texture, buffer, shader;
        if (!context || mesaGLPortMakeCurrent(context) != NTGL_OK)
            return 1;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture_pixels);
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(texture_pixels), texture_pixels, GL_STATIC_DRAW);
        shader = glCreateShader(GL_VERTEX_SHADER);
        {
            const GLchar *source = shader_source;
            glShaderSource(shader, 1, &source, 0);
        }
        glCompileShader(shader);
        mesaGLPortDestroy(context);
    }
    return 0;
}
