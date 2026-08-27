#include "mesaGL/ntgl.h"

#include <stdint.h>

static int fragment(void *user, const float *varyings, int varying_count,
                    const float *varying_dfdx, const float *varying_dfdy,
                    const float frag_coord[4], int front_facing,
                    const float point_coord[2], float color[4])
{
    (void)user;
    (void)frag_coord;
    (void)varying_dfdx;
    (void)varying_dfdy;
    (void)front_facing;
    (void)point_coord;
    if (!varyings || varying_count != 1)
        return 0;
    color[0] = varyings[0];
    color[1] = varyings[1];
    color[2] = varyings[2];
    color[3] = varyings[3];
    return 1;
}

static int fragment_reciprocal_w(void *user, const float *varyings, int varying_count,
                                 const float *varying_dfdx, const float *varying_dfdy,
                                 const float frag_coord[4], int front_facing,
                                 const float point_coord[2], float color[4])
{
    (void)user;
    (void)varyings;
    (void)varying_count;
    (void)varying_dfdx;
    (void)varying_dfdy;
    (void)front_facing;
    (void)point_coord;
    color[0] = frag_coord[3];
    color[1] = frag_coord[3];
    color[2] = frag_coord[3];
    color[3] = 1.0f;
    return 1;
}

int main(void)
{
    uint8_t pixels[64 * 64 * 4] = {0};
    NTGLframebuffer framebuffer = {pixels, 64, 64, 64 * 4, NTGL_RGBA8888,
                                   NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLprogramVertex vertices[3] = {
        {{-1.0f, -1.0f, 0.0f, 1.0f}, {{1.0f, 0.0f, 0.0f, 1.0f}}, 1.0f},
        {{1.0f, -1.0f, 0.0f, 1.0f}, {{0.0f, 1.0f, 0.0f, 1.0f}}, 1.0f},
        {{0.0f, 1.0f, 0.0f, 1.0f}, {{0.0f, 0.0f, 1.0f, 1.0f}}, 1.0f},
    };
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    const uint8_t *center = pixels + (32 * 64 + 32) * 4;

    if (!context)
        return 1;
    ntglViewport(0, 0, 64, 64);
    ntglDrawProgrammable(NTGL_TRIANGLES, vertices, 3, 1, fragment, NULL);
    if (center[0] < 55 || center[0] > 75 || center[1] < 55 || center[1] > 75 ||
        center[2] < 115 || center[2] > 140 || center[3] != 255)
        return 2;

    vertices[0].position[0] = -2.0f;
    vertices[0].position[1] = -2.0f;
    vertices[0].position[3] = 2.0f;
    vertices[1].position[0] = 2.0f;
    vertices[1].position[1] = -2.0f;
    vertices[1].position[3] = 2.0f;
    vertices[2].position[0] = 0.0f;
    vertices[2].position[1] = 2.0f;
    vertices[2].position[3] = 2.0f;
    ntglDrawProgrammable(NTGL_TRIANGLES, vertices, 3, 0, fragment_reciprocal_w, NULL);
    if (center[0] < 126 || center[0] > 129 || center[1] < 126 || center[1] > 129 ||
        center[2] < 126 || center[2] > 129 || center[3] != 255)
        return 3;
    ntglDestroyContext(context);
    return 0;
}
