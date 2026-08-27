#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

int main(void)
{
    uint32_t pixels[4 * 4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 4, 4, 4 * (int)sizeof(*pixels), NTGL_XRGB8888,
        NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLint integer[4] = {0};
    GLboolean boolean[4] = {0};
    GLfloat real[4] = {0.0f};
    GLuint framebuffer_object;
    GLuint renderbuffer;
    GLuint texture;
    static const GLenum capabilities[] = {
        GL_BLEND,
        GL_CULL_FACE,
        GL_DEPTH_TEST,
        GL_DITHER,
        GL_POLYGON_OFFSET_FILL,
        GL_SAMPLE_ALPHA_TO_COVERAGE,
        GL_SAMPLE_COVERAGE,
        GL_SCISSOR_TEST,
        GL_STENCIL_TEST,
    };
    int capability;

    if (!context)
        return 1;
    ntglMakeCurrent(context);

    for (capability = 0;
         capability < (int)(sizeof(capabilities) / sizeof(capabilities[0]));
         ++capability) {
        GLenum cap = capabilities[capability];
        int expected = cap == GL_DITHER;

        integer[0] = -1;
        boolean[0] = 7;
        real[0] = -1.0f;
        glGetIntegerv(cap, integer);
        glGetBooleanv(cap, boolean);
        glGetFloatv(cap, real);
        if (glGetError() != GL_NO_ERROR || integer[0] != expected ||
            boolean[0] != expected || real[0] != (GLfloat)expected)
            return 25;
        glEnable(cap);
        glGetIntegerv(cap, integer);
        glGetBooleanv(cap, boolean);
        glGetFloatv(cap, real);
        if (glGetError() != GL_NO_ERROR || integer[0] != GL_TRUE ||
            boolean[0] != GL_TRUE || real[0] != 1.0f)
            return 26;
        if (!expected)
            glDisable(cap);
    }

    glBlendColor(0.125f, 0.25f, 0.5f, 0.75f);
    glGetFloatv(GL_BLEND_COLOR, real);
    if (glGetError() != GL_NO_ERROR || real[0] != 0.125f || real[1] != 0.25f ||
        real[2] != 0.5f || real[3] != 0.75f)
        return 27;
    glClearColor(0.75f, 0.5f, 0.25f, 0.125f);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, real);
    if (glGetError() != GL_NO_ERROR || real[0] != 0.75f || real[1] != 0.5f ||
        real[2] != 0.25f || real[3] != 0.125f)
        return 28;
    glClearDepthf(0.625f);
    glDepthRangef(0.875f, 0.25f);
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, &real[0]);
    glGetFloatv(GL_DEPTH_RANGE, &real[1]);
    if (glGetError() != GL_NO_ERROR || real[0] != 0.625f || real[1] != 0.875f ||
        real[2] != 0.25f)
        return 29;
    glStencilFuncSeparate(GL_FRONT, GL_LEQUAL, 17, 0x35);
    glStencilFuncSeparate(GL_BACK, GL_GREATER, 29, 0x53);
    glStencilMaskSeparate(GL_FRONT, 0x69);
    glStencilMaskSeparate(GL_BACK, 0x96);
    glStencilOpSeparate(GL_FRONT, GL_REPLACE, GL_INCR, GL_DECR_WRAP);
    glStencilOpSeparate(GL_BACK, GL_INVERT, GL_INCR_WRAP, GL_DECR);
    glGetIntegerv(GL_STENCIL_FUNC, &integer[0]);
    glGetIntegerv(GL_STENCIL_BACK_FUNC, &integer[1]);
    glGetIntegerv(GL_STENCIL_REF, &integer[2]);
    glGetIntegerv(GL_STENCIL_BACK_REF, &integer[3]);
    if (glGetError() != GL_NO_ERROR || integer[0] != GL_LEQUAL ||
        integer[1] != GL_GREATER || integer[2] != 17 || integer[3] != 29)
        return 30;
    glGetIntegerv(GL_STENCIL_FAIL, &integer[0]);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &integer[1]);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &integer[2]);
    if (glGetError() != GL_NO_ERROR || integer[0] != GL_REPLACE ||
        integer[1] != GL_INCR || integer[2] != GL_DECR_WRAP)
        return 31;
    glGetIntegerv(GL_STENCIL_BACK_FAIL, &integer[0]);
    glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_FAIL, &integer[1]);
    glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_PASS, &integer[2]);
    if (glGetError() != GL_NO_ERROR || integer[0] != GL_INVERT ||
        integer[1] != GL_INCR_WRAP || integer[2] != GL_DECR)
        return 32;
    glStencilFunc(GL_ALWAYS, 0, ~0u);
    glStencilMask(~0u);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    boolean[0] = 7;
    glGetBooleanv(0xdead, boolean);
    if (boolean[0] != 7 || glGetError() != GL_INVALID_ENUM)
        return 23;
    real[0] = 19.0f;
    glGetFloatv(0xdead, real);
    if (real[0] != 19.0f || glGetError() != GL_INVALID_ENUM)
        return 24;

    glGetIntegerv(GL_STENCIL_VALUE_MASK, &integer[0]);
    glGetIntegerv(GL_STENCIL_BACK_VALUE_MASK, &integer[1]);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &integer[2]);
    glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &integer[3]);
    if (integer[0] != 0xff || integer[1] != 0xff || integer[2] != 0xff ||
        integer[3] != 0xff)
        return 21;
    glStencilFunc(GL_ALWAYS, 0, ~0u);
    glStencilMask(~0u);
    glGetIntegerv(GL_STENCIL_VALUE_MASK, &integer[0]);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &integer[1]);
    if (integer[0] != 0xff || integer[1] != 0xff)
        return 22;

    glSampleCoverage(0.49f, GL_TRUE);
    glGetIntegerv(GL_SAMPLE_COVERAGE_VALUE, integer);
    glGetBooleanv(GL_SAMPLE_COVERAGE_VALUE, boolean);
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, real);
    if (integer[0] != 0 || boolean[0] != GL_TRUE || real[0] != 0.49f)
        return 2;
    glSampleCoverage(0.5f, GL_FALSE);
    glGetIntegerv(GL_SAMPLE_COVERAGE_VALUE, integer);
    if (integer[0] != 1)
        return 3;

    glPolygonOffset(0.49f, -0.51f);
    glGetIntegerv(GL_POLYGON_OFFSET_FACTOR, &integer[0]);
    glGetIntegerv(GL_POLYGON_OFFSET_UNITS, &integer[1]);
    glGetBooleanv(GL_POLYGON_OFFSET_FACTOR, &boolean[0]);
    glGetBooleanv(GL_POLYGON_OFFSET_UNITS, &boolean[1]);
    if (integer[0] != 0 || integer[1] != -1 || !boolean[0] || !boolean[1])
        return 4;

    glLineWidth(1.5f);
    glGetIntegerv(GL_LINE_WIDTH, integer);
    if (integer[0] != 2)
        return 5;

    glClearColor(1.0e-12f, 0.0f, 0.5f, 1.0f);
    glGetBooleanv(GL_COLOR_CLEAR_VALUE, boolean);
    if (!boolean[0] || boolean[1] || !boolean[2] || !boolean[3])
        return 6;
    glDepthRangef(1.0e-12f, 0.0f);
    glGetBooleanv(GL_DEPTH_RANGE, boolean);
    if (!boolean[0] || boolean[1])
        return 7;

    glVertexAttrib4f(3, 0.49f, 0.5f, -0.51f, 2.2f);
    glGetVertexAttribiv(3, GL_CURRENT_VERTEX_ATTRIB, integer);
    if (integer[0] != 0 || integer[1] != 1 || integer[2] != -1 || integer[3] != 2)
        return 9;

    glGenFramebuffers(1, &framebuffer_object);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_object);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
                                          integer);
    if (integer[0] != GL_NONE || glGetError() != GL_NO_ERROR)
        return 10;
    integer[0] = -1;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                          integer);
    if (integer[0] != -1 || glGetError() != GL_INVALID_ENUM)
        return 11;
    glGenRenderbuffers(1, &renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, 4, 4);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                              renderbuffer);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                          integer);
    if ((GLuint)integer[0] != renderbuffer || glGetError() != GL_NO_ERROR)
        return 12;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL,
                                          integer);
    if (glGetError() != GL_INVALID_ENUM)
        return 13;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteRenderbuffers(1, &renderbuffer);
    glDeleteFramebuffers(1, &framebuffer_object);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    (GLfloat)GL_LINEAR + 0.5f);
    if (glGetError() != GL_INVALID_ENUM)
        return 14;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, integer);
    if (integer[0] != GL_NEAREST || glGetError() != GL_NO_ERROR)
        return 15;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &framebuffer_object);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_object);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           texture, 0);
    if (glGetError() != GL_NO_ERROR ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT)
        return 16;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
                                          integer);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                          &integer[1]);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                          GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL,
                                          &integer[2]);
    if (integer[0] != GL_TEXTURE || (GLuint)integer[1] != texture || integer[2] != 0 ||
        glGetError() != GL_NO_ERROR)
        return 17;
    glClear(GL_DEPTH_BUFFER_BIT);
    if (glGetError() != GL_INVALID_FRAMEBUFFER_OPERATION)
        return 18;
    glDeleteTextures(1, &texture);
    if (glGetError() != GL_NO_ERROR ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT)
        return 19;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
                                          integer);
    if (integer[0] != GL_NONE || glGetError() != GL_NO_ERROR)
        return 20;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer_object);

    if (glGetError() != GL_NO_ERROR)
        return 8;
    ntglDestroyContext(context);
    return 0;
}
