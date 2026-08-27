#include "GLES2/gl2.h"
#include "gles2_internal.h"
#include "mesaGL/config.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <string.h>

int main(void)
{
    uint8_t pixels[16 * 16 * 4] = {0};
    NTGLframebuffer framebuffer = {pixels, 16, 16, 16 * 4, NTGL_RGBA8888,
                                   NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    const float constant[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    const uint8_t wrap_pixels[8] = {255, 0, 0, 255, 0, 255, 0, 255};
    float queried[4];
    float sampled[4];
    uint8_t pixel[4];
    GLuint buffer, texture, target, renderbuffer;
    GLuint invalid_shader;
    GLuint invalid_program;
    GLuint namespace_shader;
    GLuint namespace_program;
    GLint value;
    GLint dimensions[2];
    GLint precision_range[2];
    GLint attribute_value;
    void *attribute_pointer;
    GLboolean compiler_available;
    GLchar shader_source[96];
    GLsizei shader_source_length;
    const GLchar *invalid_source = "void main() { gl_Position = vec4(0.0);";
    const GLchar *null_source = NULL;
    const GLuint arbitrary_buffer = 9001;
    const GLuint arbitrary_texture = 9002;
    const GLuint arbitrary_framebuffer = 9003;
    const GLuint arbitrary_renderbuffer = 9004;

    if (!context)
        return 1;
    glDeleteShader(0);
    glDeleteProgram(0);
    if (glGetError() != GL_NO_ERROR)
        return 110;
    namespace_shader = glCreateShader(GL_VERTEX_SHADER);
    namespace_program = glCreateProgram();
    if (!namespace_shader || !namespace_program ||
        namespace_shader == namespace_program ||
        !glIsShader(namespace_shader) || glIsProgram(namespace_shader) ||
        !glIsProgram(namespace_program) || glIsShader(namespace_program))
        return 104;
    glCompileShader(namespace_program);
    if (glGetError() != GL_INVALID_OPERATION)
        return 105;
    glLinkProgram(namespace_shader);
    if (glGetError() != GL_INVALID_OPERATION)
        return 106;
    glAttachShader(namespace_shader, namespace_shader);
    if (glGetError() != GL_INVALID_OPERATION)
        return 107;
    glAttachShader(namespace_program, namespace_program);
    if (glGetError() != GL_INVALID_OPERATION)
        return 108;
    glUseProgram(namespace_shader);
    if (glGetError() != GL_INVALID_OPERATION)
        return 109;
    glDeleteShader(namespace_program);
    if (glGetError() != GL_INVALID_OPERATION)
        return 110;
    glDeleteProgram(namespace_shader);
    if (glGetError() != GL_INVALID_OPERATION)
        return 111;
    glDeleteShader(namespace_shader);
    glDeleteProgram(namespace_program);
    if (glGetError() != GL_NO_ERROR)
        return 112;
    glUniform1f(-1, 1.0f);
    if (glGetError() != GL_INVALID_OPERATION)
        return 96;
    glUniform4fv(0, -1, constant);
    if (glGetError() != GL_INVALID_OPERATION)
        return 97;
    glUniformMatrix4fv(0, 1, GL_TRUE, constant);
    if (glGetError() != GL_INVALID_OPERATION)
        return 98;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &attribute_value);
    if (attribute_value != 4)
        return 72;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_TYPE, &attribute_value);
    if (attribute_value != GL_FLOAT)
        return 73;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &attribute_value);
    if (attribute_value != 0)
        return 74;
    glGetVertexAttribiv(MESAGL_MAX_VERTEX_ATTRIBS, GL_VERTEX_ATTRIB_ARRAY_SIZE,
                        &attribute_value);
    if (glGetError() != GL_INVALID_VALUE)
        return 75;
    glGetVertexAttribiv(0, 0xdead, &attribute_value);
    if (glGetError() != GL_INVALID_ENUM)
        return 76;
    glBindBuffer(GL_ARRAY_BUFFER, arbitrary_buffer);
    if (!glIsBuffer(arbitrary_buffer) || glGetError() != GL_NO_ERROR)
        return 84;
    glBufferData(GL_ARRAY_BUFFER, 16, NULL, GL_STATIC_DRAW);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 0, NULL);
    glGetVertexAttribiv(3, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &attribute_value);
    if ((GLuint)attribute_value != arbitrary_buffer)
        return 85;
    glDeleteBuffers(1, &arbitrary_buffer);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &attribute_value);
    if (attribute_value != 0 || glIsBuffer(arbitrary_buffer))
        return 86;
    glGetVertexAttribiv(3, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &attribute_value);
    if (attribute_value != 0)
        return 87;
    glBindTexture(GL_TEXTURE_2D, arbitrary_texture);
    if (!glIsTexture(arbitrary_texture) || glGetError() != GL_NO_ERROR)
        return 88;
    glDeleteTextures(1, &arbitrary_texture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &attribute_value);
    if (attribute_value != 0 || glIsTexture(arbitrary_texture))
        return 89;
    glBindFramebuffer(GL_FRAMEBUFFER, arbitrary_framebuffer);
    if (!glIsFramebuffer(arbitrary_framebuffer) || glGetError() != GL_NO_ERROR)
        return 90;
    glDeleteFramebuffers(1, &arbitrary_framebuffer);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &attribute_value);
    if (attribute_value != 0 || glIsFramebuffer(arbitrary_framebuffer))
        return 91;
    glBindRenderbuffer(GL_RENDERBUFFER, arbitrary_renderbuffer);
    if (!glIsRenderbuffer(arbitrary_renderbuffer) || glGetError() != GL_NO_ERROR)
        return 92;
    glDeleteRenderbuffers(1, &arbitrary_renderbuffer);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &attribute_value);
    if (attribute_value != 0 || glIsRenderbuffer(arbitrary_renderbuffer))
        return 93;
    glActiveTexture(GL_TEXTURE0 + MESAGL_MAX_TEXTURE_UNITS);
    if (glGetError() != GL_INVALID_ENUM)
        return 77;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &attribute_value);
    if (attribute_value != GL_TEXTURE0)
        return 78;
    if (!glIsEnabled(GL_DITHER))
        return 65;
    glDisable(GL_DITHER);
    if (glIsEnabled(GL_DITHER))
        return 66;
    glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glEnable(GL_SAMPLE_COVERAGE);
    if (!glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE) || !glIsEnabled(GL_SAMPLE_COVERAGE))
        return 67;
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE);
    glGetIntegerv(GL_BLEND_SRC_RGB, &value);
    if (value != GL_ONE)
        return 68;
    glGetIntegerv(GL_BLEND_DST_RGB, &value);
    if (value != GL_ZERO)
        return 69;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &value);
    if (value != GL_ONE)
        return 70;
    glGetIntegerv(GL_BLEND_DST_ALPHA, &value);
    if (value != GL_ZERO)
        return 71;
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, dimensions);
    if (dimensions[0] != 4096 || dimensions[1] != 4096)
        return 40;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &value);
    if (value != MESAGL_MAX_VERTEX_ATTRIBS)
        return 41;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &value);
    if (value != MESAGL_MAX_VARYING_VECTORS)
        return 42;
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &value);
    if (value != MESAGL_MAX_VERTEX_UNIFORM_VECTORS)
        return 94;
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &value);
    if (value != MESAGL_MAX_FRAGMENT_UNIFORM_VECTORS)
        return 95;
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &value);
    if (value != MESAGL_MAX_VERTEX_TEXTURE_IMAGE_UNITS)
        return 112;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &value);
    if (value != MESAGL_MAX_FRAGMENT_TEXTURE_IMAGE_UNITS)
        return 113;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &value);
    if (value != MESAGL_MAX_COMBINED_TEXTURE_IMAGE_UNITS)
        return 114;
    glGetBooleanv(GL_SHADER_COMPILER, &compiler_available);
    if (compiler_available != GL_TRUE)
        return 79;
    glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS, &value);
    if (value != 0)
        return 80;
    value = 1234;
    glGetIntegerv(GL_SHADER_BINARY_FORMATS, &value);
    if (value != 1234 || glGetError() != GL_NO_ERROR)
        return 81;
    glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &value);
    if (value != 0)
        return 82;
    value = 5678;
    glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, &value);
    if (value != 5678 || glGetError() != GL_NO_ERROR)
        return 83;
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &value);
    if (value != GL_RGBA)
        return 43;
    if (!strstr((const char *)glGetString(GL_EXTENSIONS), "GL_OES_standard_derivatives") ||
        !strstr((const char *)glGetString(GL_EXTENSIONS),
                "GL_EXT_texture_format_BGRA8888"))
        return 21;
    if (strstr((const char *)glGetString(GL_EXTENSIONS),
               "GL_EXT_blend_func_separate"))
        return 111;
    glHint(GL_FRAGMENT_SHADER_DERIVATIVE_HINT_OES, GL_NICEST);
    if (glGetError() != GL_NO_ERROR)
        return 22;
    glGetIntegerv(GL_FRAGMENT_SHADER_DERIVATIVE_HINT_OES, &attribute_value);
    if (attribute_value != GL_NICEST)
        return 94;
    glGetIntegerv(0xdead, NULL);
    if (glGetError() != GL_INVALID_ENUM)
        return 102;
    glGetIntegerv(GL_VIEWPORT, NULL);
    if (glGetError() != GL_INVALID_VALUE)
        return 103;
    glHint(GL_GENERATE_MIPMAP_HINT, GL_FASTEST);
    glGetIntegerv(GL_GENERATE_MIPMAP_HINT, &attribute_value);
    if (attribute_value != GL_FASTEST)
        return 95;
    if (glCreateShader(0xdead) != 0 || glGetError() != GL_INVALID_ENUM)
        return 53;
    glCompileShader(0xdead);
    if (glGetError() != GL_INVALID_VALUE)
        return 54;
    invalid_shader = glCreateShader(GL_VERTEX_SHADER);
    glGetShaderiv(invalid_shader, GL_INFO_LOG_LENGTH, &value);
    if (value != 0 || glGetError() != GL_NO_ERROR)
        return 96;
    glShaderSource(invalid_shader, 1, &invalid_source, NULL);
    glShaderSource(invalid_shader, 1, &null_source, NULL);
    if (glGetError() != GL_INVALID_VALUE)
        return 55;
    glGetShaderSource(invalid_shader, sizeof(shader_source), &shader_source_length,
                      shader_source);
    if (strcmp(shader_source, invalid_source) ||
        shader_source_length != (GLsizei)strlen(invalid_source))
        return 56;
    glGetShaderiv(invalid_shader, GL_SHADER_SOURCE_LENGTH, &value);
    if (value != (GLint)strlen(invalid_source) + 1)
        return 64;
    glShaderSource(invalid_shader, -1, &invalid_source, NULL);
    if (glGetError() != GL_INVALID_VALUE)
        return 57;
    glGetShaderSource(invalid_shader, -1, &shader_source_length, shader_source);
    if (glGetError() != GL_INVALID_VALUE)
        return 58;
    glCompileShader(invalid_shader);
    glGetShaderiv(invalid_shader, GL_COMPILE_STATUS, &value);
    if (value)
        return 20;
    shader_source_length = -1;
    glGetShaderInfoLog(invalid_shader, 0, &shader_source_length, NULL);
    if (shader_source_length != 0 || glGetError() != GL_NO_ERROR)
        return 99;
    shader_source_length = -1;
    glGetShaderSource(invalid_shader, 0, &shader_source_length, NULL);
    if (shader_source_length != 0 || glGetError() != GL_NO_ERROR)
        return 100;
    glDeleteShader(invalid_shader);
    glGetShaderSource(invalid_shader, sizeof(shader_source), &shader_source_length,
                      shader_source);
    if (glGetError() != GL_INVALID_VALUE)
        return 59;
    invalid_program = glCreateProgram();
    glGetProgramiv(invalid_program, GL_INFO_LOG_LENGTH, &value);
    if (value != 0 || glGetError() != GL_NO_ERROR)
        return 97;
    glLinkProgram(invalid_program);
    shader_source_length = -1;
    glGetProgramInfoLog(invalid_program, 0, &shader_source_length, NULL);
    if (shader_source_length != 0 || glGetError() != GL_NO_ERROR)
        return 101;
    glUseProgram(invalid_program);
    if (glGetError() != GL_INVALID_OPERATION)
        return 49;
    glUseProgram(invalid_program + 1000);
    if (glGetError() != GL_INVALID_VALUE)
        return 50;
    glBindAttribLocation(invalid_program, MESAGL_MAX_VERTEX_ATTRIBS, "position");
    if (glGetError() != GL_INVALID_VALUE)
        return 51;
    glBindAttribLocation(invalid_program, 0, "gl_position");
    if (glGetError() != GL_INVALID_OPERATION)
        return 52;
    glGetActiveUniform(invalid_program, 0, sizeof(shader_source), NULL, NULL, NULL,
                       shader_source);
    if (glGetError() != GL_INVALID_VALUE)
        return 60;
    glGetActiveAttrib(invalid_program, 0, -1, NULL, NULL, NULL, shader_source);
    if (glGetError() != GL_INVALID_VALUE)
        return 61;
    glDeleteProgram(invalid_program);
    glDeleteProgram(invalid_program);
    if (glGetError() != GL_INVALID_VALUE)
        return 62;
    glGetShaderiv(invalid_shader, GL_COMPILE_STATUS, &value);
    if (glGetError() != GL_INVALID_VALUE)
        return 44;
    invalid_shader = glCreateShader(GL_VERTEX_SHADER);
    glGetShaderiv(invalid_shader, 0xdead, &value);
    if (glGetError() != GL_INVALID_ENUM)
        return 45;
    {
        const GLenum shader_types[] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
        const GLenum precision_types[] = {
            GL_LOW_FLOAT, GL_MEDIUM_FLOAT, GL_HIGH_FLOAT,
            GL_LOW_INT, GL_MEDIUM_INT, GL_HIGH_INT,
        };
        unsigned int shader_index;
        unsigned int precision_index;

        for (shader_index = 0;
             shader_index < sizeof(shader_types) / sizeof(shader_types[0]);
             ++shader_index)
            for (precision_index = 0;
                 precision_index < sizeof(precision_types) /
                                       sizeof(precision_types[0]);
                 ++precision_index) {
                int floating = precision_types[precision_index] <= GL_HIGH_FLOAT;

                precision_range[0] = precision_range[1] = -1;
                value = -1;
                glGetShaderPrecisionFormat(shader_types[shader_index],
                                           precision_types[precision_index],
                                           precision_range, &value);
                if (glGetError() != GL_NO_ERROR ||
                    precision_range[0] != (floating ? 127 : 24) ||
                    precision_range[1] != (floating ? 127 : 24) ||
                    value != (floating ? 23 : 0))
                    return 46;
            }
    }
    glGetShaderPrecisionFormat(0xdead, GL_HIGH_FLOAT, precision_range, &value);
    if (glGetError() != GL_INVALID_ENUM)
        return 47;
    precision_range[0] = precision_range[1] = -1;
    value = -1;
    glGetShaderPrecisionFormat(GL_VERTEX_SHADER, 0xdead,
                               precision_range, &value);
    if (glGetError() != GL_INVALID_ENUM || precision_range[0] != -1 ||
        precision_range[1] != -1 || value != -1)
        return 79;
    glShaderBinary(0, NULL, 0xdead, NULL, 0);
    if (glGetError() != GL_INVALID_ENUM)
        return 48;
    glDeleteShader(invalid_shader);
    glDeleteShader(invalid_shader);
    if (glGetError() != GL_INVALID_VALUE)
        return 63;
    glBufferData(GL_ARRAY_BUFFER, 4, NULL, GL_STATIC_DRAW);
    if (glGetError() != GL_INVALID_OPERATION)
        return 23;
    glBufferData(GL_ARRAY_BUFFER, -1, NULL, 0xffffu);
    if (glGetError() != GL_INVALID_OPERATION)
        return 104;
    glBufferSubData(GL_ARRAY_BUFFER, -1, -1, NULL);
    if (glGetError() != GL_INVALID_OPERATION)
        return 107;
    glGenBuffers(1, &buffer);
    if (glIsBuffer(buffer))
        return 2;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    if (!glIsBuffer(buffer))
        return 3;
    glBindBuffer(0xffffu, buffer);
    if (glGetError() != GL_INVALID_ENUM)
        return 24;
    {
        GLuint implicit_buffer = buffer + 1000;

        glBindBuffer(GL_ARRAY_BUFFER, implicit_buffer);
        if (glGetError() != GL_NO_ERROR || !glIsBuffer(implicit_buffer))
            return 25;
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &value);
        if ((GLuint)value != implicit_buffer)
            return 26;
        glDeleteBuffers(1, &implicit_buffer);
    }
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &value);
    if (value != 0)
        return 25;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, 32, NULL, GL_DYNAMIC_DRAW);
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &value);
    if (value != 32)
        return 4;
    glBufferData(GL_ARRAY_BUFFER, -1, NULL, 0xffffu);
    if (glGetError() != GL_INVALID_VALUE)
        return 105;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &value);
    if (value != 32)
        return 106;
    glBufferSubData(GL_ARRAY_BUFFER, -1, -1, NULL);
    if (glGetError() != GL_INVALID_VALUE)
        return 108;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &value);
    if (value != 32)
        return 109;
    glBufferData(GL_ARRAY_BUFFER, 16, NULL, 0xffffu);
    if (glGetError() != GL_INVALID_ENUM)
        return 27;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &value);
    if (value != 32)
        return 28;
    glBufferSubData(GL_ARRAY_BUFFER, 31, 2, wrap_pixels);
    if (glGetError() != GL_INVALID_VALUE)
        return 29;
    glBufferSubData(GL_ARRAY_BUFFER, 0, 1, NULL);
    if (glGetError() != GL_INVALID_VALUE)
        return 30;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, 0xffffu, &value);
    if (glGetError() != GL_INVALID_ENUM)
        return 31;

    glVertexAttrib4fv(7, constant);
    glGetVertexAttribfv(7, GL_CURRENT_VERTEX_ATTRIB, queried);
    if (queried[0] != constant[0] || queried[1] != constant[1] || queried[2] != constant[2] ||
        queried[3] != constant[3])
        return 5;
    glVertexAttrib1f(6, 0.125f);
    glGetVertexAttribfv(6, GL_CURRENT_VERTEX_ATTRIB, queried);
    if (queried[0] != 0.125f || queried[1] != 0.0f || queried[2] != 0.0f ||
        queried[3] != 1.0f)
        return 102;
    {
        const GLfloat scalar[1] = {0.375f};
        const GLfloat pair[2] = {0.25f, 0.5f};
        const GLfloat triple[3] = {0.125f, 0.375f, 0.625f};

        glVertexAttrib1fv(6, scalar);
        glGetVertexAttribfv(6, GL_CURRENT_VERTEX_ATTRIB, queried);
        if (queried[0] != scalar[0] || queried[1] != 0.0f ||
            queried[2] != 0.0f || queried[3] != 1.0f)
            return 117;
        glVertexAttrib2fv(6, pair);
        glGetVertexAttribfv(6, GL_CURRENT_VERTEX_ATTRIB, queried);
        if (queried[0] != pair[0] || queried[1] != pair[1] ||
            queried[2] != 0.0f || queried[3] != 1.0f)
            return 103;
        glVertexAttrib3fv(6, triple);
        glGetVertexAttribfv(6, GL_CURRENT_VERTEX_ATTRIB, queried);
        if (queried[0] != triple[0] || queried[1] != triple[1] ||
            queried[2] != triple[2] || queried[3] != 1.0f)
            return 104;
        glVertexAttrib3f(6, 0.125f, 0.25f, 0.5f);
        glGetVertexAttribfv(6, GL_CURRENT_VERTEX_ATTRIB, queried);
        if (queried[0] != 0.125f || queried[1] != 0.25f ||
            queried[2] != 0.5f || queried[3] != 1.0f)
            return 118;
    }
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, 16,
                          (const void *)(uintptr_t)4);
    attribute_pointer = NULL;
    glGetVertexAttribPointerv(6, GL_VERTEX_ATTRIB_ARRAY_POINTER,
                              &attribute_pointer);
    if (attribute_pointer != (void *)(uintptr_t)4)
        return 105;
    glEnableVertexAttribArray(6);
    glGetVertexAttribiv(6, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attribute_value);
    if (!attribute_value)
        return 106;
    glDisableVertexAttribArray(6);
    glGetVertexAttribiv(6, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attribute_value);
    if (attribute_value)
        return 107;
    glGetVertexAttribPointerv(6, 0xdead, &attribute_pointer);
    if (glGetError() != GL_INVALID_ENUM)
        return 108;
    glGetVertexAttribPointerv(MESAGL_MAX_VERTEX_ATTRIBS,
                              GL_VERTEX_ATTRIB_ARRAY_POINTER,
                              &attribute_pointer);
    if (glGetError() != GL_INVALID_VALUE)
        return 109;

    glStencilMaskSeparate(GL_FRONT, 0x12u);
    glStencilMaskSeparate(GL_BACK, 0x34u);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &value);
    if (value != 0x12)
        return 110;
    glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &value);
    if (value != 0x34)
        return 111;
    glStencilMaskSeparate(0xdead, 0xffu);
    if (glGetError() != GL_INVALID_ENUM)
        return 112;
    glFlush();
    glFinish();
    glReleaseShaderCompiler();
    if (glGetError() != GL_NO_ERROR)
        return 113;
    glEnable(0xdead);
    glFlush();
    if (glGetError() != GL_INVALID_ENUM)
        return 117;

    glGenTextures(1, &texture);
    if (glIsTexture(texture))
        return 6;
    glBindTexture(GL_TEXTURE_2D, texture);
    if (!glIsTexture(texture))
        return 7;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &value);
    if (value != GL_CLAMP_TO_EDGE)
        return 8;
    {
        const GLint integer_parameter[1] = {GL_REPEAT};
        const GLfloat float_parameter[1] = {(GLfloat)GL_LINEAR};
        GLfloat float_query = 0.0f;

        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                         integer_parameter);
        glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &float_query);
        if (float_query != (GLfloat)GL_REPEAT)
            return 114;
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                         float_parameter);
        glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &float_query);
        if (float_query != (GLfloat)GL_LINEAR)
            return 115;
    }
    glClearColor(0.8f, 0.2f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 8, 8, 0);
    glGenerateMipmap(GL_TEXTURE_2D);
    if (!mesaGLSampleTexture2DLod(0, 0.5f, 0.5f, 3.0f, sampled) || sampled[0] < 0.79f ||
        sampled[0] > 0.81f || sampled[1] < 0.19f || sampled[1] > 0.21f)
        return 9;

    glGenFramebuffers(1, &target);
    if (glIsFramebuffer(target))
        return 10;
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    if (!glIsFramebuffer(target))
        return 11;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 12;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &value);
    if ((GLuint)value != texture)
        return 13;
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 200 || pixel[1] < 45 || pixel[2] < 20)
        return 14;

    glGenRenderbuffers(1, &renderbuffer);
    if (glIsRenderbuffer(renderbuffer))
        return 15;
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 8, 8);
    if (!glIsRenderbuffer(renderbuffer))
        return 16;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &value);
    if (value != 8)
        return 17;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, wrap_pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    if (!mesaGLSampleTexture2D(0, 1.25f, 0.0f, sampled) || sampled[0] > 0.1f ||
        sampled[1] < 0.9f)
        return 18;

    glCompressedTexImage2D(GL_TEXTURE_2D, 0, 0, 4, 4, 0, 0, NULL);
    if (glGetError() != GL_INVALID_ENUM)
        return 19;
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, 0, 0,
                              NULL);
    if (glGetError() != GL_INVALID_ENUM)
        return 116;
    glDeleteRenderbuffers(1, &renderbuffer);
    glDeleteFramebuffers(1, &target);
    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &buffer);
    ntglDestroyContext(context);
    return 0;
}
