#ifndef MESAGL_X11_H
#define MESAGL_X11_H

#include "mesaGL/port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MesaGLX11 MesaGLX11;

MesaGLX11 *mesaGLX11Create(int width, int height, const char *title);
void mesaGLX11Destroy(MesaGLX11 *x11);
const MesaGLPortConfig *mesaGLX11GetPortConfig(const MesaGLX11 *x11);
NTGLresult mesaGLX11Present(void *user, const NTGLframebuffer *framebuffer);
int mesaGLX11WaitEvent(MesaGLX11 *x11);
int mesaGLX11PollEvents(MesaGLX11 *x11);

#ifdef __cplusplus
}
#endif
#endif
