#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 64
#define HEIGHT 48

static uint32_t pixels[WIDTH * HEIGHT];

static int colored_pixels(void)
{
    int i, count = 0;
    for (i = 0; i < WIDTH * HEIGHT; ++i)
        if (pixels[i] != 0xff000000u)
            ++count;
    return count;
}

int main(void)
{
    static const unsigned char texel[] = {128, 64, 0, 128};
    static const unsigned char matrix_texels[] = {255, 0, 0, 255, 0, 0, 255, 255};
    NTGLframebuffer fb;
    NTGLtexture texture;
    NTGLcontext *context;
    uint32_t center;
    int count;

    memset(&fb, 0, sizeof(fb));
    fb.pixels = pixels;
    fb.width = WIDTH;
    fb.height = HEIGHT;
    fb.stride = WIDTH * 4;
    fb.format = NTGL_ARGB8888;
    fb.origin = NTGL_ORIGIN_TOP_LEFT;
    context = ntglCreateContext(&fb, NULL);
    if (!context)
        return 1;

    ntglClearColor(0, 0, 0, 1);
    ntglClear(1, 1);
    ntglEnable(NTGL_DEPTH_TEST);
    ntglBegin(NTGL_TRIANGLES);
    ntglColor3f(1, 0, 0);
    ntglVertex3f(-0.8f, -0.8f, 0);
    ntglColor3f(0, 1, 0);
    ntglVertex3f(0.8f, -0.8f, 0);
    ntglColor3f(0, 0, 1);
    ntglVertex3f(0.0f, 0.8f, 0);
    ntglEnd();

    count = colored_pixels();
    if (count < 800 || count > 1400) {
        fprintf(stderr, "unexpected triangle coverage: %d\n", count);
        ntglDestroyContext(context);
        return 2;
    }
    if ((pixels[(HEIGHT / 2) * WIDTH + WIDTH / 2] & 0x00ffffffu) == 0) {
        fprintf(stderr, "center pixel was not rasterized\n");
        ntglDestroyContext(context);
        return 3;
    }
    if (ntglGetError() != NTGL_OK)
        return 4;

    memset(&texture, 0, sizeof(texture));
    texture.pixels = texel;
    texture.width = texture.height = 1;
    texture.stride = 4;
    texture.format = NTGL_RGBA8888;
    texture.filter = NTGL_NEAREST;
    texture.origin = NTGL_ORIGIN_TOP_LEFT;
    texture.wrap_s = texture.wrap_t = NTGL_CLAMP_TO_EDGE;
    texture.environment = NTGL_TEXTURE_ADD;
    ntglDisable(NTGL_DEPTH_TEST);
    ntglEnable(NTGL_TEXTURE_2D);
    ntglBindTexture(&texture);
    ntglClear(1, 0);
    ntglColor4f(0.25f, 0.25f, 0.25f, 0.5f);
    ntglBegin(NTGL_QUADS);
    ntglVertex2f(-1, -1);
    ntglVertex2f(1, -1);
    ntglVertex2f(1, 1);
    ntglVertex2f(-1, 1);
    ntglEnd();
    center = pixels[(HEIGHT / 2) * WIDTH + WIDTH / 2];
    if (((center >> 16) & 0xffu) < 190 || ((center >> 8) & 0xffu) < 126 || (center & 0xffu) < 62 ||
        (center >> 24) < 62 || (center >> 24) > 66) {
        fprintf(stderr, "unexpected texture ADD result: 0x%08x\n", center);
        ntglDestroyContext(context);
        return 5;
    }
    texture.environment = NTGL_TEXTURE_BLEND;
    texture.environment_color[0] = 1.0f;
    texture.environment_color[1] = 0.0f;
    texture.environment_color[2] = 0.5f;
    ntglBindTexture(&texture);
    ntglClear(1, 0);
    ntglBegin(NTGL_QUADS);
    ntglVertex2f(-1, -1);
    ntglVertex2f(1, -1);
    ntglVertex2f(1, 1);
    ntglVertex2f(-1, 1);
    ntglEnd();
    center = pixels[(HEIGHT / 2) * WIDTH + WIDTH / 2];
    if (((center >> 16) & 0xffu) < 158 || ((center >> 16) & 0xffu) > 162 ||
        ((center >> 8) & 0xffu) < 46 || ((center >> 8) & 0xffu) > 50 || (center & 0xffu) < 62 ||
        (center & 0xffu) > 66) {
        fprintf(stderr, "unexpected texture BLEND result: 0x%08x\n", center);
        ntglDestroyContext(context);
        return 6;
    }
    texture.pixels = matrix_texels;
    texture.width = 2;
    texture.stride = 8;
    texture.environment = NTGL_TEXTURE_REPLACE;
    ntglBindTexture(&texture);
    ntglMatrixMode(NTGL_TEXTURE);
    ntglLoadIdentity();
    ntglPushMatrix();
    ntglTranslatef(0.75f, 0.0f, 0.0f);
    ntglTexCoord2f(0.0f, 0.0f);
    ntglClear(1, 0);
    ntglBegin(NTGL_QUADS);
    ntglVertex2f(-1, -1);
    ntglVertex2f(1, -1);
    ntglVertex2f(1, 1);
    ntglVertex2f(-1, 1);
    ntglEnd();
    center = pixels[(HEIGHT / 2) * WIDTH + WIDTH / 2];
    if ((center & 0x00ffffffu) != 0x000000ffu) {
        fprintf(stderr, "texture matrix translation failed: 0x%08x\n", center);
        ntglDestroyContext(context);
        return 7;
    }
    ntglPopMatrix();
    ntglClear(1, 0);
    ntglBegin(NTGL_QUADS);
    ntglVertex2f(-1, -1);
    ntglVertex2f(1, -1);
    ntglVertex2f(1, 1);
    ntglVertex2f(-1, 1);
    ntglEnd();
    center = pixels[(HEIGHT / 2) * WIDTH + WIDTH / 2];
    if ((center & 0x00ffffffu) != 0x00ff0000u) {
        fprintf(stderr, "texture matrix pop failed: 0x%08x\n", center);
        ntglDestroyContext(context);
        return 8;
    }
    ntglDestroyContext(context);
    return 0;
}
