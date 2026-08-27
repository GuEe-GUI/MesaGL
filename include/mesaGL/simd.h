#ifndef MESAGL_SIMD_H
#define MESAGL_SIMD_H

#include "mesaGL/ntgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the selected backend name, or NULL when scalar fallback is used. */
const char *mesaGLInitSIMDPixelOps(NTGLpixelOps *operations);

#ifdef __cplusplus
}
#endif
#endif
