#ifndef MESAGL_X11_H
#define MESAGL_X11_H

#include "mesaGL/port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MesaGLX11 MesaGLX11;

typedef enum MesaGLX11EventType {
    MESAGL_X11_MOUSE_POSITION,
    MESAGL_X11_MOUSE_BUTTON,
    MESAGL_X11_MOUSE_WHEEL,
    MESAGL_X11_KEY,
    MESAGL_X11_TEXT,
    MESAGL_X11_FOCUS,
} MesaGLX11EventType;

typedef struct MesaGLX11Event {
    MesaGLX11EventType type;
    int x;
    int y;
    int button;
    int down;
    float wheel_x;
    float wheel_y;
    unsigned long key;
    unsigned int keycode;
    char text[64];
} MesaGLX11Event;

typedef void (*MesaGLX11EventCallback)(void *user,
                                       const MesaGLX11Event *event);

MesaGLX11 *mesaGLX11Create(int width, int height, const char *title);
void mesaGLX11Destroy(MesaGLX11 *x11);
const MesaGLPortConfig *mesaGLX11GetPortConfig(const MesaGLX11 *x11);
const char *mesaGLX11GetSIMDBackend(const MesaGLX11 *x11);
NTGLresult mesaGLX11Present(void *user, const NTGLframebuffer *framebuffer);
void mesaGLX11SetEventCallback(MesaGLX11 *x11,
                               MesaGLX11EventCallback callback, void *user);
int mesaGLX11WaitEvent(MesaGLX11 *x11);
int mesaGLX11PollEvents(MesaGLX11 *x11);

#ifdef __cplusplus
}
#endif
#endif
