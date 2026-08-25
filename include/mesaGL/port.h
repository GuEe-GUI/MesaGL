#ifndef MESAGL_PORT_H
#define MESAGL_PORT_H

#include "mesaGL/ntgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MesaGLPortContext MesaGLPortContext;

typedef NTGLresult (*MesaGLPresentFn)(void *user, const NTGLframebuffer *framebuffer);

typedef struct MesaGLPortConfig {
    NTGLframebuffer framebuffer;
    NTGLallocator allocator;
    MesaGLPresentFn present;
    void *user;
} MesaGLPortConfig;

MesaGLPortContext *mesaGLPortCreate(const MesaGLPortConfig *config);
void mesaGLPortDestroy(MesaGLPortContext *context);
NTGLresult mesaGLPortMakeCurrent(MesaGLPortContext *context);
NTGLresult mesaGLPortAttachFramebuffer(MesaGLPortContext *context,
                                       const NTGLframebuffer *framebuffer);
NTGLresult mesaGLPortPresent(MesaGLPortContext *context);
NTGLcontext *mesaGLPortGetRenderer(MesaGLPortContext *context);
const NTGLframebuffer *mesaGLPortGetFramebuffer(const MesaGLPortContext *context);

#ifdef __cplusplus
}
#endif
#endif
