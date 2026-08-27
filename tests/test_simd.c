#include "mesaGL/simd.h"

#include <stdio.h>

static unsigned char scalar_channel(unsigned char p00, unsigned char p10,
                                    unsigned char p01, unsigned char p11,
                                    float column_alpha, float row_alpha)
{
    float value =
        ((p00 * (1.0f - column_alpha) + p10 * column_alpha) *
             (1.0f - row_alpha) +
         (p01 * (1.0f - column_alpha) + p11 * column_alpha) * row_alpha) /
        255.0f;

    return (unsigned char)(value * 255.0f + 0.5f);
}

int main(void)
{
    unsigned char row0[67 * 4];
    unsigned char row1[67 * 4];
    unsigned char destination[131 * 4];
    NTGLlinearColumn columns[131];
    NTGLpixelOps operations;
    const char *backend;
    float row_alpha = 0.37125f;
    int index;

    for (index = 0; index < (int)sizeof(row0); ++index) {
        row0[index] = (unsigned char)(index * 37 + 11);
        row1[index] = (unsigned char)(index * 19 + 73);
    }
    for (index = 0; index < 131; ++index) {
        columns[index].x0 = (index * 17) % 67;
        columns[index].x1 = (columns[index].x0 + 1) % 67;
        columns[index].alpha = (index % 29) / 29.0f;
    }
    backend = mesaGLInitSIMDPixelOps(&operations);
    if (!backend)
        return 0;
    if (!operations.linear_rgba8888_to_xrgb8888(
            operations.user, destination, row0, row1, columns, 131,
            row_alpha))
        return 1;
    for (index = 0; index < 131; ++index) {
        int component;

        for (component = 0; component < 3; ++component) {
            int source_component = 2 - component;
            unsigned char expected = scalar_channel(
                row0[columns[index].x0 * 4 + source_component],
                row0[columns[index].x1 * 4 + source_component],
                row1[columns[index].x0 * 4 + source_component],
                row1[columns[index].x1 * 4 + source_component],
                columns[index].alpha, row_alpha);

            if (destination[index * 4 + component] != expected) {
                fprintf(stderr,
                        "%s SIMD mismatch at pixel %d channel %d: %u != %u\n",
                        backend, index, component,
                        destination[index * 4 + component], expected);
                return 1;
            }
        }
        if (destination[index * 4 + 3] != 255)
            return 1;
    }
    printf("%s SIMD pixel operations match scalar output\n", backend);
    return 0;
}
