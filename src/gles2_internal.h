#ifndef MESAGL_GLES2_INTERNAL_H
#define MESAGL_GLES2_INTERNAL_H

const void *mesaGLResolveElementPointer(const void *pointer);
void mesaGLPrepareGLES2Draw(void);
void mesaGLGLES2GetIntegerv(unsigned int pname, int *value, int *handled);
void mesaGLGLES2ReleaseCurrentContext(void);
void mesaGLSetError(unsigned int error_code);
void mesaGLSetBlendEquationState(unsigned int rgb, unsigned int alpha);
void mesaGLSetGLES2TextureState(int enabled);
int mesaGLDrawGLES2Arrays(unsigned int mode, int first, int count);
int mesaGLDrawGLES2Elements(unsigned int mode, int count, unsigned int type, const void *indices);
typedef struct MesaGLPreparedTexture2D {
    const void *texture;
    const unsigned char *pixels;
    int width;
    int height;
    int nearest;
    int wrap_s;
    int wrap_t;
    int rgb_white;
} MesaGLPreparedTexture2D;

int mesaGLPrepareTexture2D(int unit, MesaGLPreparedTexture2D *sampler);
int mesaGLSamplePreparedTexture2D(const MesaGLPreparedTexture2D *sampler,
                                  float s, float t, float color[4]);
int mesaGLSampleTexture2D(int unit, float s, float t, float color[4]);
int mesaGLSampleTexture2DLod(int unit, float s, float t, float lod, float color[4]);
int mesaGLSampleTexture2DGrad(int unit, float s, float t, float dsdx, float dtdx, float dsdy,
                              float dtdy, float color[4]);
int mesaGLSampleTexture2DGradBias(int unit, float s, float t, float dsdx, float dtdx, float dsdy,
                                  float dtdy, float bias, float color[4]);
int mesaGLSampleTextureCube(int unit, float x, float y, float z, float color[4]);
int mesaGLSampleTextureCubeLod(int unit, float x, float y, float z, float lod, float color[4]);
int mesaGLSampleTextureCubeGradBias(int unit, float x, float y, float z, float dxdx, float dydx,
                                    float dzdx, float dxdy, float dydy, float dzdy, float bias,
                                    float color[4]);
int mesaGLSetActiveTextureUnit(int unit);

#endif
