#include "GL/gl.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <string.h>

typedef struct ImVertex {
    float pos[2], uv[2];
    uint32_t color;
} ImVertex;

int main(void)
{
    int i, green_pixels;
    GLint state_values[4];
    GLfloat float_values[16];
    GLboolean boolean_value;
    uint32_t fb_pixels[32 * 32],
        texture_pixels[4] = {0xffffffff, 0x80ffffff, 0x80ffffff, 0xffffffff};
    NTGLframebuffer fb = {fb_pixels, 32, 32, 32 * 4, NTGL_ARGB8888, NTGL_ORIGIN_TOP_LEFT};
    ImVertex vertices[] = {{{4, 4}, {0, 0}, 0xffffffff},
                           {{28, 4}, {1, 0}, 0xffffffff},
                           {{28, 28}, {1, 1}, 0x80ffffff},
                           {{4, 28}, {0, 1}, 0x80ffffff}};
    GLushort indices[] = {0, 1, 2, 0, 2, 3};
    GLuint texture, fbo_texture, framebuffer, depth_renderbuffer;
    GLubyte fbo_pixel[4];
    NTGLcontext *ctx = ntglCreateContext(&fb, NULL);
    if (!ctx)
        return 1;
    glViewport(0, 0, 32, 32);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 32, 32, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 32, 32);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture_pixels);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(ImVertex), &vertices[0].pos);
    glTexCoordPointer(2, GL_FLOAT, sizeof(ImVertex), &vertices[0].uv);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(ImVertex), &vertices[0].color);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
    if (glGetError() != GL_NO_ERROR)
        return 2;
    if ((fb_pixels[16 * 32 + 16] & 0x00ffffffu) == 0)
        return 3;
    memset(texture_pixels, 255, sizeof(texture_pixels));
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 2);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, texture_pixels);
    if (glGetError() != GL_NO_ERROR)
        return 4;
    glDepthFunc(GL_GEQUAL);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
    glFrontFace(GL_CW);
    glCullFace(GL_FRONT);
    glGetIntegerv(GL_DEPTH_FUNC, state_values);
    if (state_values[0] != GL_GEQUAL)
        return 5;
    glGetIntegerv(GL_DEPTH_WRITEMASK, state_values);
    if (state_values[0] != GL_FALSE)
        return 6;
    glGetIntegerv(GL_COLOR_WRITEMASK, state_values);
    if (!state_values[0] || state_values[1] || !state_values[2] || state_values[3])
        return 7;
    glGetIntegerv(GL_FRONT_FACE, state_values);
    if (state_values[0] != GL_CW)
        return 8;
    glGetIntegerv(GL_CULL_FACE_MODE, state_values);
    if (state_values[0] != GL_FRONT)
        return 9;
    glGetFloatv(GL_VIEWPORT, float_values);
    if (float_values[2] != 32.0f || float_values[3] != 32.0f)
        return 15;
    glGetBooleanv(GL_BLEND, &boolean_value);
    if (boolean_value != GL_TRUE)
        return 16;
    glEnable(GL_NORMALIZE);
    if (!glIsEnabled(GL_NORMALIZE))
        return 27;
    glDisable(GL_NORMALIZE);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glClearColor(0, 0, 0, 1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 1, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glBegin(GL_QUADS);
    glVertex2f(8, 8);
    glVertex2f(24, 8);
    glVertex2f(24, 24);
    glVertex2f(8, 24);
    glEnd();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_EQUAL, 1, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glColor3f(1, 0, 0);
    glBegin(GL_QUADS);
    glVertex2f(0, 0);
    glVertex2f(32, 0);
    glVertex2f(32, 32);
    glVertex2f(0, 32);
    glEnd();
    if ((fb_pixels[16 * 32 + 16] & 0x00ff0000u) == 0 || (fb_pixels[2 * 32 + 2] & 0x00ffffffu) != 0)
        return 10;
    glDisable(GL_STENCIL_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(0, 1, 0);
    glBegin(GL_TRIANGLES);
    glVertex2f(4, 4);
    glVertex2f(28, 4);
    glVertex2f(16, 28);
    glEnd();
    green_pixels = 0;
    for (i = 0; i < 32 * 32; ++i)
        if ((fb_pixels[i] & 0x0000ff00u) != 0)
            ++green_pixels;
    if (green_pixels < 40 || green_pixels > 120)
        return 17;
    glGetIntegerv(GL_POLYGON_MODE, state_values);
    if (state_values[0] != GL_LINE || state_values[1] != GL_LINE)
        return 18;
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glLineWidth(5.0f);
    glGetFloatv(GL_LINE_WIDTH, float_values);
    if (float_values[0] != 5.0f)
        return 19;
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_LINES);
    glVertex2f(4, 16);
    glVertex2f(28, 16);
    glEnd();
    green_pixels = 0;
    for (i = 0; i < 32 * 32; ++i)
        if ((fb_pixels[i] & 0x0000ff00u) != 0)
            ++green_pixels;
    if (green_pixels < 100)
        return 20;
    glLineWidth(1.0f);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glTranslatef(0.25f, 0.5f, 0.0f);
    glGetFloatv(GL_TEXTURE_MATRIX, float_values);
    if (float_values[12] != 0.25f || float_values[13] != 0.5f)
        return 21;
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    {
        static const GLfloat light_position[] = {0, 0, 1, 0};

        glEnable(GL_LIGHT0);
        glEnable(GL_LIGHTING);
        glEnable(GL_COLOR_MATERIAL);
        glLightfv(GL_LIGHT0, GL_POSITION, light_position);
        glColor3f(1, 0, 0);
        glNormal3f(0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        glVertex2f(4, 4);
        glVertex2f(28, 4);
        glVertex2f(28, 28);
        glVertex2f(4, 28);
        glEnd();
        if (((fb_pixels[16 * 32 + 16] >> 16) & 0xffu) < 240)
            return 22;
        {
            static const GLfloat white[] = {1, 1, 1, 1};
            static const GLfloat black[] = {0, 0, 0, 1};

            glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, white);
            glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glBegin(GL_QUADS);
            glVertex2f(4, 4);
            glVertex2f(28, 4);
            glVertex2f(28, 28);
            glVertex2f(4, 28);
            glEnd();
            if ((fb_pixels[16 * 32 + 16] & 0x0000ffffu) != 0x0000ffffu)
                return 24;
            glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, black);
        }
        {
            static const GLfloat blue[] = {0, 0, 1, 1};

            glLightfv(GL_LIGHT1, GL_POSITION, light_position);
            glLightfv(GL_LIGHT1, GL_DIFFUSE, blue);
            glDisable(GL_LIGHT0);
            glEnable(GL_LIGHT1);
            glColor3f(1, 1, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glBegin(GL_QUADS);
            glVertex2f(4, 4);
            glVertex2f(28, 4);
            glVertex2f(28, 28);
            glVertex2f(4, 28);
            glEnd();
            if ((fb_pixels[16 * 32 + 16] & 0xffu) < 240 ||
                ((fb_pixels[16 * 32 + 16] >> 16) & 0xffu) > 60)
                return 25;
            glDisable(GL_LIGHT1);
            glEnable(GL_LIGHT0);
            glColor3f(1, 0, 0);
        }
        glNormal3f(0, 0, -1);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        glVertex2f(4, 4);
        glVertex2f(28, 4);
        glVertex2f(28, 28);
        glVertex2f(4, 28);
        glEnd();
        if (((fb_pixels[16 * 32 + 16] >> 16) & 0xffu) > 60)
            return 23;
        glDisable(GL_COLOR_MATERIAL);
        glDisable(GL_LIGHTING);
        glDisable(GL_LIGHT0);
    }
    {
        static const GLfloat blue_fog[] = {0, 0, 1, 1};
        uint32_t pixel;

        glFogfv(GL_FOG_COLOR, blue_fog);
        glFogf(GL_FOG_MODE, GL_LINEAR);
        glFogf(GL_FOG_START, 0.0f);
        glFogf(GL_FOG_END, 1.0f);
        glEnable(GL_FOG);
        glLoadIdentity();
        glTranslatef(0, 0, -0.75f);
        glColor3f(1, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        glVertex2f(4, 4);
        glVertex2f(28, 4);
        glVertex2f(28, 28);
        glVertex2f(4, 28);
        glEnd();
        pixel = fb_pixels[16 * 32 + 16];
        if (((pixel >> 16) & 0xffu) < 62 || ((pixel >> 16) & 0xffu) > 66 || (pixel & 0xffu) < 190 ||
            (pixel & 0xffu) > 194)
            return 26;
        glDisable(GL_FOG);
        glLoadIdentity();
    }
    glGenTextures(1, &fbo_texture);
    glBindTexture(GL_TEXTURE_2D, fbo_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_texture, 0);
    glGenRenderbuffers(1, &depth_renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 8, 8);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                              depth_renderbuffer);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 11;
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, fbo_pixel);
    if (fbo_pixel[0] < 60 || fbo_pixel[0] > 68 || fbo_pixel[1] < 124 || fbo_pixel[1] > 132 ||
        fbo_pixel[2] < 188 || fbo_pixel[2] > 196 || fbo_pixel[3] != 255)
        return 12;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_DEPTH_BUFFER_BIT);
    glBegin(GL_POINTS);
    glVertex3f(0, 0, 0);
    glEnd();
    if (glGetError() != GL_NO_ERROR)
        return 13;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glDeleteTextures(1, &fbo_texture);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, state_values);
    if (state_values[0] != 0)
        return 14;
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteRenderbuffers(1, &depth_renderbuffer);
    glDeleteTextures(1, &texture);
    ntglDestroyContext(ctx);
    return 0;
}
