#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stddef.h>
#include <stdint.h>

static int compile_status(const char *source)
{
    GLuint shader = glCreateShader(GL_FRAGMENT_SHADER);
    GLint compiled = 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    glDeleteShader(shader);
    return compiled;
}

int main(void)
{
    static const char valid_float_builtins[] =
        "precision mediump float;"
        "void main(){"
        "vec2 value=clamp(abs(vec2(-0.25,2.0)),0.0,1.0);"
        "gl_FragColor=vec4(value,min(0.5,1.0),1.0);"
        "}";
    static const char *const invalid_integer_builtins[] = {
        "precision mediump float;void main(){int x=abs(-2);gl_FragColor=vec4(float(x));}",
        "precision mediump float;void main(){ivec2 x=sign(ivec2(-1));gl_FragColor=vec4(x);}",
        "precision mediump float;void main(){int x=min(2,3);gl_FragColor=vec4(float(x));}",
        "precision mediump float;void main(){ivec2 x=max(ivec2(1),2);gl_FragColor=vec4(x);}",
        "precision mediump float;void main(){int x=clamp(2,0,1);gl_FragColor=vec4(float(x));}"
    };
    uint8_t pixels[8 * 8 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 8, 8, 8 * 4, NTGL_RGBA8888, NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    size_t index;

    if (!context)
        return 1;
    if (!compile_status(valid_float_builtins))
        return 2;
    for (index = 0;
         index < sizeof(invalid_integer_builtins) / sizeof(invalid_integer_builtins[0]);
         ++index) {
        if (compile_status(invalid_integer_builtins[index]))
            return 3 + (int)index;
    }
    ntglDestroyContext(context);
    return 0;
}
