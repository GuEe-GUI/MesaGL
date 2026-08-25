#ifndef MESAGL_NTGL_H
#define MESAGL_NTGL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NTGLcontext NTGLcontext;

typedef enum NTGLresult {
    NTGL_OK = 0,
    NTGL_INVALID_ARGUMENT = -1,
    NTGL_OUT_OF_MEMORY = -2,
    NTGL_INVALID_OPERATION = -3,
    NTGL_STACK_OVERFLOW = -4,
    NTGL_STACK_UNDERFLOW = -5
} NTGLresult;

typedef enum NTGLformat {
    NTGL_RGB565,
    NTGL_RGB888,
    NTGL_BGR888,
    NTGL_XRGB8888,
    NTGL_ARGB8888,
    NTGL_RGBA8888,
    NTGL_BGRA8888
} NTGLformat;

typedef enum NTGLorigin { NTGL_ORIGIN_BOTTOM_LEFT, NTGL_ORIGIN_TOP_LEFT } NTGLorigin;

typedef enum NTGLprimitive {
    NTGL_POINTS,
    NTGL_LINES,
    NTGL_LINE_LOOP,
    NTGL_LINE_STRIP,
    NTGL_TRIANGLES,
    NTGL_TRIANGLE_STRIP,
    NTGL_TRIANGLE_FAN,
    NTGL_QUADS
} NTGLprimitive;

typedef enum NTGLmatrixMode { NTGL_MODELVIEW, NTGL_PROJECTION, NTGL_TEXTURE } NTGLmatrixMode;

typedef enum NTGLpolygonMode {
    NTGL_POLYGON_POINT,
    NTGL_POLYGON_LINE,
    NTGL_POLYGON_FILL
} NTGLpolygonMode;

typedef enum NTGLcapability {
    NTGL_DEPTH_TEST,
    NTGL_BLEND,
    NTGL_CULL_FACE,
    NTGL_TEXTURE_2D,
    NTGL_SCISSOR_TEST,
    NTGL_STENCIL_TEST,
    NTGL_ALPHA_TEST,
    NTGL_LIGHTING,
    NTGL_LIGHT0,
    NTGL_LIGHT1,
    NTGL_LIGHT2,
    NTGL_LIGHT3,
    NTGL_LIGHT4,
    NTGL_LIGHT5,
    NTGL_LIGHT6,
    NTGL_LIGHT7,
    NTGL_FOG,
    NTGL_NORMALIZE
} NTGLcapability;

typedef enum NTGLfogMode { NTGL_FOG_LINEAR, NTGL_FOG_EXP, NTGL_FOG_EXP2 } NTGLfogMode;

typedef enum NTGLstencilOp {
    NTGL_KEEP,
    NTGL_STENCIL_ZERO,
    NTGL_REPLACE,
    NTGL_INCR,
    NTGL_DECR,
    NTGL_INVERT
} NTGLstencilOp;

typedef enum NTGLdepthFunc {
    NTGL_NEVER,
    NTGL_LESS,
    NTGL_LEQUAL,
    NTGL_EQUAL,
    NTGL_NOTEQUAL,
    NTGL_GEQUAL,
    NTGL_GREATER,
    NTGL_ALWAYS
} NTGLdepthFunc;

typedef enum NTGLblendFactor {
    NTGL_ZERO,
    NTGL_ONE,
    NTGL_SRC_COLOR,
    NTGL_ONE_MINUS_SRC_COLOR,
    NTGL_DST_COLOR,
    NTGL_ONE_MINUS_DST_COLOR,
    NTGL_SRC_ALPHA,
    NTGL_ONE_MINUS_SRC_ALPHA,
    NTGL_DST_ALPHA,
    NTGL_ONE_MINUS_DST_ALPHA,
    NTGL_CONSTANT_COLOR,
    NTGL_ONE_MINUS_CONSTANT_COLOR,
    NTGL_CONSTANT_ALPHA,
    NTGL_ONE_MINUS_CONSTANT_ALPHA
} NTGLblendFactor;

typedef enum NTGLblendEquation {
    NTGL_FUNC_ADD,
    NTGL_FUNC_SUBTRACT,
    NTGL_FUNC_REVERSE_SUBTRACT,
    NTGL_MIN,
    NTGL_MAX
} NTGLblendEquation;

typedef enum NTGLfilter { NTGL_NEAREST, NTGL_LINEAR } NTGLfilter;

typedef enum NTGLwrap { NTGL_REPEAT, NTGL_CLAMP_TO_EDGE } NTGLwrap;

typedef enum NTGLtextureEnv {
    NTGL_TEXTURE_MODULATE,
    NTGL_TEXTURE_REPLACE,
    NTGL_TEXTURE_DECAL,
    NTGL_TEXTURE_ADD,
    NTGL_TEXTURE_BLEND
} NTGLtextureEnv;

typedef void *(*NTGLallocFn)(void *user, size_t size);
typedef void (*NTGLfreeFn)(void *user, void *pointer);

typedef struct NTGLallocator {
    NTGLallocFn alloc;
    NTGLfreeFn free;
    void *user;
} NTGLallocator;

typedef struct NTGLframebuffer {
    void *pixels;
    int width;
    int height;
    int stride; /* bytes; negative values are supported */
    NTGLformat format;
    NTGLorigin origin; /* memory row represented by y=0 in GL coordinates */
} NTGLframebuffer;

typedef struct NTGLtexture {
    const void *pixels;
    int width;
    int height;
    int stride; /* bytes; zero means width * 4 */
    NTGLformat format;
    NTGLfilter filter;
    NTGLorigin origin;
    NTGLwrap wrap_s;
    NTGLwrap wrap_t;
    NTGLtextureEnv environment;
    float environment_color[4];
} NTGLtexture;

/* pixels may be NULL: the context then owns an internal color buffer. */
NTGLcontext *ntglCreateContext(const NTGLframebuffer *framebuffer, const NTGLallocator *allocator);
void ntglDestroyContext(NTGLcontext *context);
NTGLresult ntglMakeCurrent(NTGLcontext *context);
NTGLcontext *ntglGetCurrent(void);
void *ntglAlloc(size_t size);
void ntglFree(void *pointer);
NTGLresult ntglAttachFramebuffer(NTGLcontext *context, const NTGLframebuffer *framebuffer);
const NTGLframebuffer *ntglGetFramebuffer(const NTGLcontext *context);
NTGLresult ntglResize(NTGLcontext *context, int width, int height);

void ntglViewport(int x, int y, int width, int height);
void ntglScissor(int x, int y, int width, int height);
void ntglClearColor(float red, float green, float blue, float alpha);
void ntglClearDepth(float depth);
void ntglClear(int color, int depth);
void ntglEnable(NTGLcapability capability);
void ntglDisable(NTGLcapability capability);
void ntglDepthFunc(NTGLdepthFunc function);
void ntglDepthMask(int enabled);
void ntglColorMask(int red, int green, int blue, int alpha);
void ntglClearStencil(unsigned value);
void ntglClearStencilBuffer(void);
void ntglStencilFunc(NTGLdepthFunc function, unsigned reference, unsigned mask);
void ntglStencilMask(unsigned mask);
void ntglStencilOp(NTGLstencilOp fail, NTGLstencilOp depth_fail, NTGLstencilOp pass);
void ntglAlphaFunc(NTGLdepthFunc function, float reference);
void ntglShadeModel(int smooth);
void ntglBlendFunc(NTGLblendFactor source, NTGLblendFactor destination);
void ntglBlendFuncSeparate(NTGLblendFactor source_rgb, NTGLblendFactor destination_rgb,
                           NTGLblendFactor source_alpha, NTGLblendFactor destination_alpha);
void ntglBlendEquationSeparate(NTGLblendEquation rgb, NTGLblendEquation alpha);
void ntglBlendColor(float red, float green, float blue, float alpha);
void ntglFrontFace(int counter_clockwise);
void ntglCullFace(int cull_front);
void ntglPolygonMode(int front, NTGLpolygonMode mode);
void ntglPointSize(float size);
void ntglLineWidth(float width);

void ntglMatrixMode(NTGLmatrixMode mode);
void ntglLoadIdentity(void);
void ntglLoadMatrixf(const float *matrix);
void ntglMultMatrixf(const float *matrix);
void ntglPushMatrix(void);
void ntglPopMatrix(void);
void ntglTranslatef(float x, float y, float z);
void ntglScalef(float x, float y, float z);
void ntglRotatef(float degrees, float x, float y, float z);
void ntglGetMatrix(NTGLmatrixMode mode, float *matrix);
void ntglFrustum(float left, float right, float bottom, float top, float near_value,
                 float far_value);
void ntglOrtho(float left, float right, float bottom, float top, float near_value, float far_value);

void ntglBegin(NTGLprimitive primitive);
void ntglEnd(void);
void ntglColor4f(float red, float green, float blue, float alpha);
void ntglColor3f(float red, float green, float blue);
void ntglNormal3f(float x, float y, float z);
void ntglMaterial(const float *ambient, const float *diffuse);
void ntglMaterialSpecular(const float *color);
void ntglMaterialShininess(float shininess);
void ntglLightAmbient(int light, const float *color);
void ntglLightDiffuse(int light, const float *color);
void ntglLightSpecular(int light, const float *color);
void ntglLightPosition(int light, const float *position);
void ntglLightModelAmbient(const float *color);
void ntglFogMode(NTGLfogMode mode);
void ntglFogColor(const float *color);
void ntglFogDensity(float density);
void ntglFogRange(float start, float end);
void ntglTexCoord2f(float s, float t);
void ntglVertex2f(float x, float y);
void ntglVertex3f(float x, float y, float z);
void ntglVertex4f(float x, float y, float z, float w);

/* Texture data is referenced, not copied; it must remain valid while bound. */
NTGLresult ntglBindTexture(const NTGLtexture *texture);
NTGLresult ntglGetError(void); /* returns and clears the first pending error */
const char *ntglGetString(void);

#ifdef __cplusplus
}
#endif
#endif
