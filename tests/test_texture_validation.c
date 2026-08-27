#include "GLES2/gl2.h"
#include "GLES2/gl2ext.h"
#include "gles2_internal.h"
#include "mesaGL/ntgl.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static int close_color(const float color[4], int red, int green, int blue, int alpha)
{
    return fabsf(color[0] * 255.0f - red) < 1.5f &&
           fabsf(color[1] * 255.0f - green) < 1.5f &&
           fabsf(color[2] * 255.0f - blue) < 1.5f &&
           fabsf(color[3] * 255.0f - alpha) < 1.5f;
}

int main(void)
{
    uint8_t framebuffer_pixels[4 * 4 * 4] = {0};
    NTGLframebuffer framebuffer = {framebuffer_pixels, 4, 4, 4 * 4, NTGL_RGBA8888,
                                   NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    const uint8_t red[4] = {255, 0, 0, 255};
    const uint8_t aligned_rgb[8] = {0, 255, 0, 0, 0, 0, 0, 0};
    const uint8_t cyan[4] = {0, 255, 255, 255};
    const uint8_t red_2x2[16] = {
        255, 0, 0, 255, 255, 0, 0, 255,
        255, 0, 0, 255, 255, 0, 0, 255,
    };
    const uint8_t green_3x1[12] = {
        0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255,
    };
    const GLushort blue_565 = 0x001f;
    const GLushort green_565 = 0x07e0;
    const GLushort magenta_4444 = 0xf0f8;
    const GLushort cyan_5551 = 0x07ff;
    const uint8_t bgra_orange[4] = {0, 128, 255, 64};
    float color[4];
    GLint pixel_store;
    GLuint texture;
    GLuint mip_texture;
    GLuint copy_source_texture;
    GLuint copy_destination_texture;
    GLuint copy_source_framebuffer;
    uint8_t copied_pixel[4];
    uint8_t mip_base[4 * 4 * 4];
    uint8_t mip_level_one[2 * 2 * 4];
    uint8_t invalid_mip[3 * 2 * 4];
    uint8_t mip_level_two[4];
    int pixel;
    int face;

    if (!context)
        return 1;

    for (pixel = 0; pixel < 16; ++pixel) {
        mip_base[pixel * 4] = 255;
        mip_base[pixel * 4 + 1] = 0;
        mip_base[pixel * 4 + 2] = 0;
        mip_base[pixel * 4 + 3] = 255;
    }
    for (pixel = 0; pixel < 4; ++pixel)
        memcpy(mip_level_one + pixel * 4, red, sizeof(red));
    for (pixel = 0; pixel < 6; ++pixel) {
        invalid_mip[pixel * 4] = 0;
        invalid_mip[pixel * 4 + 1] = 255;
        invalid_mip[pixel * 4 + 2] = 0;
        invalid_mip[pixel * 4 + 3] = 255;
    }
    memcpy(mip_level_two, red, sizeof(red));

    /* Name zero is a real per-target default object, but is never a texture name. */
    glBindTexture(GL_TEXTURE_2D, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, red);
    if (glGetError() != GL_NO_ERROR || !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 255, 0, 0, 255) || glIsTexture(0))
        return 2;

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, cyan);
    if (glGetError() != GL_NO_ERROR || !mesaGLSampleTextureCube(0, 1.0f, 0.0f, 0.0f, color) ||
        !close_color(color, 0, 0, 0, 255))
        return 9;
    for (face = 1; face < 6; ++face)
        glTexImage2D((GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), 0, GL_RGBA, 1, 1, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, cyan);
    if (glGetError() != GL_NO_ERROR || !mesaGLSampleTextureCube(0, 1.0f, 0.0f, 0.0f, color) ||
        !close_color(color, 0, 255, 255, 255))
        return 11;

    /* Rejected redefinitions must leave the old image intact. */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, red);
    if (glGetError() != GL_INVALID_OPERATION ||
        !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) || !close_color(color, 255, 0, 0, 255))
        return 3;
    glTexImage2D(0xdead, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, red);
    if (glGetError() != GL_INVALID_ENUM || !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 255, 0, 0, 255))
        return 4;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, red);
    if (glGetError() != GL_INVALID_VALUE || !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 255, 0, 0, 255))
        return 5;
    glTexImage2D(GL_TEXTURE_2D, 0, 0xdead, -1, 1, 0, 0xdead, 0xdead, red);
    if (glGetError() != GL_INVALID_VALUE || !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 255, 0, 0, 255))
        return 38;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, -1, 1, 0xdead, 0xdead, red);
    if (glGetError() != GL_INVALID_VALUE || !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 255, 0, 0, 255))
        return 39;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    /* GLES2 without OES_texture_npot accepts NPOT images only at level zero. */
    glGenTextures(1, &mip_texture);
    glBindTexture(GL_TEXTURE_2D, mip_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, mip_base);
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 2, 2, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, mip_level_one);
    glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, mip_level_two);
    if (glGetError() != GL_NO_ERROR ||
        !mesaGLSampleTexture2DLod(0, 0.5f, 0.5f, 1.0f, color) ||
        !close_color(color, 255, 0, 0, 255))
        return 44;
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 3, 2, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, invalid_mip);
    if (glGetError() != GL_INVALID_VALUE ||
        !mesaGLSampleTexture2DLod(0, 0.5f, 0.5f, 1.0f, color) ||
        !close_color(color, 255, 0, 0, 255))
        return 45;
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 0, 2, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    if (glGetError() != GL_INVALID_VALUE ||
        !mesaGLSampleTexture2DLod(0, 0.5f, 0.5f, 1.0f, color) ||
        !close_color(color, 255, 0, 0, 255))
        return 46;
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glCopyTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 0, 0, 3, 2, 0);
    if (glGetError() != GL_INVALID_VALUE ||
        !mesaGLSampleTexture2DLod(0, 0.5f, 0.5f, 1.0f, color) ||
        !close_color(color, 255, 0, 0, 255))
        return 47;
    glDeleteTextures(1, &mip_texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, red_2x2);
    if (!mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) || !close_color(color, 0, 0, 0, 255))
        return 12;
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, red);
    if (!mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) || !close_color(color, 255, 0, 0, 255))
        return 13;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 3, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, green_3x1);
    if (!mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) || !close_color(color, 0, 0, 0, 255))
        return 14;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) || !close_color(color, 0, 255, 0, 255))
        return 15;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB,
                 GL_UNSIGNED_SHORT_5_6_5, &blue_565);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGB,
                    GL_UNSIGNED_SHORT_5_6_5, &green_565);
    if (glGetError() != GL_NO_ERROR || !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 0, 255, 0, 255))
        return 6;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_SHORT_4_4_4_4, &magenta_4444);
    if (glGetError() != GL_NO_ERROR || !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 255, 0, 255, 136))
        return 34;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_SHORT_5_5_5_1, &cyan_5551);
    if (glGetError() != GL_NO_ERROR || !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 0, 255, 255, 255))
        return 35;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB,
                 GL_UNSIGNED_SHORT_4_4_4_4, &magenta_4444);
    if (glGetError() != GL_INVALID_OPERATION ||
        !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 0, 255, 255, 255))
        return 36;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, 1, 1, 0, GL_BGRA_EXT,
                 GL_UNSIGNED_BYTE, bgra_orange);
    if (glGetError() != GL_NO_ERROR || !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 255, 128, 0, 64))
        return 37;

    /* A three-byte row uses the configured four-byte unpack alignment. */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, aligned_rgb);
    if (glGetError() != GL_NO_ERROR || !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 0, 255, 0, 255))
        return 7;

    /* Rejected pixel-store calls preserve the previous unpack state. */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 3);
    if (glGetError() != GL_INVALID_VALUE)
        return 23;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &pixel_store);
    if (glGetError() != GL_NO_ERROR || pixel_store != 4)
        return 24;
    glPixelStorei(0xdead, 1);
    if (glGetError() != GL_INVALID_ENUM)
        return 25;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &pixel_store);
    if (glGetError() != GL_NO_ERROR || pixel_store != 4)
        return 26;
    glPixelStorei(GL_UNPACK_ROW_LENGTH, -1);
    if (glGetError() != GL_INVALID_VALUE)
        return 27;
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &pixel_store);
    if (glGetError() != GL_NO_ERROR || pixel_store != 0)
        return 28;

    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 1, 2, 0);
    if (glGetError() != GL_NO_ERROR ||
        !mesaGLSampleTexture2D(0, 0.5f, 0.75f, color) ||
        !close_color(color, 255, 0, 0, 255))
        return 40;
    glGetIntegerv(GL_PACK_ALIGNMENT, &pixel_store);
    if (pixel_store != 8)
        return 41;
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, 1, 1, 0);
    if (glGetError() != GL_NO_ERROR || !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 255, 0, 0, 255))
        return 10;

    glClearColor(0.25f, 0.5f, 0.75f, 0.25f);
    glClear(GL_COLOR_BUFFER_BIT);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 0, 0, 1, 1, 0);
    if (glGetError() != GL_NO_ERROR || !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 255, 255, 255, 64))
        return 16;
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 0, 0, 1, 1, 0);
    if (glGetError() != GL_NO_ERROR || !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 64, 64, 64, 255))
        return 17;
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, 0, 0, 1, 1, 0);
    if (glGetError() != GL_NO_ERROR || !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 64, 64, 64, 64))
        return 18;

    /* Source pixels outside the framebuffer are undefined, not an API error. */
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, -1, 0, 2, 1, 0);
    if (glGetError() != GL_NO_ERROR)
        return 19;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    if (!mesaGLSampleTexture2D(0, 0.75f, 0.5f, color) ||
        !close_color(color, 64, 128, 191, 64))
        return 20;

    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, 1, 1, 0, GL_LUMINANCE_ALPHA,
                 GL_UNSIGNED_BYTE, NULL);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 1, 1);
    if (glGetError() != GL_NO_ERROR ||
        !mesaGLSampleTexture2DLod(0, 0.5f, 0.5f, 0.0f, color) ||
        !close_color(color, 64, 64, 64, 64))
        return 21;
    glTexImage2D(GL_TEXTURE_2D, 1, GL_LUMINANCE_ALPHA, 1, 1, 0, GL_LUMINANCE_ALPHA,
                 GL_UNSIGNED_BYTE, NULL);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, 0, 0, 1, 1);
    if (glGetError() != GL_NO_ERROR)
        return 43;
    glCopyTexSubImage2D(0xdead, 0, 0, 0, 0, 0, 1, 1);
    if (glGetError() != GL_INVALID_ENUM)
        return 22;

    glBindTexture(GL_TEXTURE_2D, 0);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 7, 0, 0, 0, 0, 1, 1);
    if (glGetError() != GL_INVALID_OPERATION)
        return 29;
    glTexSubImage2D(GL_TEXTURE_2D, 7, 0, 0, 1, 1, GL_RGBA,
                    GL_UNSIGNED_BYTE, red);
    if (glGetError() != GL_INVALID_OPERATION)
        return 30;
    glBindTexture(GL_TEXTURE_2D, texture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 1, 1, 0, 0, 0, 1, 1);
    if (glGetError() != GL_INVALID_VALUE)
        return 31;

    /* CopyTexImage cannot add an alpha component absent from the color buffer. */
    glGenTextures(1, &copy_source_texture);
    glBindTexture(GL_TEXTURE_2D, copy_source_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &copy_source_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, copy_source_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           copy_source_texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 48;
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glGenTextures(1, &copy_destination_texture);
    glBindTexture(GL_TEXTURE_2D, copy_destination_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, red);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 1, 1);
    if (glGetError() != GL_INVALID_OPERATION ||
        !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 255, 0, 0, 255))
        return 49;
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 1, 1, 0);
    if (glGetError() != GL_INVALID_OPERATION ||
        !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 255, 0, 0, 255))
        return 50;
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, 1, 1, 0);
    if (glGetError() != GL_NO_ERROR ||
        !mesaGLSampleTexture2D(0, 0.5f, 0.5f, color) ||
        !close_color(color, 0, 255, 0, 255))
        return 51;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           copy_destination_texture, 0);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, copied_pixel);
    if (glGetError() != GL_NO_ERROR || copied_pixel[0] != 0 ||
        copied_pixel[1] != 255 || copied_pixel[2] != 0 || copied_pixel[3] != 255)
        return 52;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &copy_source_framebuffer);
    glDeleteTextures(1, &copy_source_texture);
    glDeleteTextures(1, &copy_destination_texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    /* Zero-sized images and updates are legal and must not dereference data. */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    if (glGetError() != GL_NO_ERROR)
        return 8;

    glDeleteTextures(1, &texture);
    ntglDestroyContext(context);
    return 0;
}
