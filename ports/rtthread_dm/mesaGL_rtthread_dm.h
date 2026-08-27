#ifndef MESAGL_RTTHREAD_DM_H
#define MESAGL_RTTHREAD_DM_H

#include "mesaGL/port.h"

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MesaGLRTThreadDM MesaGLRTThreadDM;

typedef enum MesaGLRTThreadDMEventType {
    MESAGL_RTTHREAD_DM_POINTER,
    MESAGL_RTTHREAD_DM_BUTTON,
    MESAGL_RTTHREAD_DM_KEY,
    MESAGL_RTTHREAD_DM_SYNC,
} MesaGLRTThreadDMEventType;

typedef struct MesaGLRTThreadDMEvent {
    MesaGLRTThreadDMEventType type;
    rt_uint16_t code;
    rt_int32_t value;
    rt_int32_t x;
    rt_int32_t y;
    rt_bool_t down;
} MesaGLRTThreadDMEvent;

typedef void (*MesaGLRTThreadDMEventCallback)(
    void *user, const MesaGLRTThreadDMEvent *event);

MesaGLRTThreadDM *mesaGLRTThreadDMCreate(const char *graphic_name,
                                     const char *input_name);
void mesaGLRTThreadDMDestroy(MesaGLRTThreadDM *port);
const MesaGLPortConfig *mesaGLRTThreadDMGetPortConfig(
    const MesaGLRTThreadDM *port);
void mesaGLRTThreadDMSetEventCallback(MesaGLRTThreadDM *port,
                                    MesaGLRTThreadDMEventCallback callback,
                                    void *user);
int mesaGLRTThreadDMPollEvents(MesaGLRTThreadDM *port);
NTGLresult mesaGLRTThreadDMPresent(void *user,
                                 const NTGLframebuffer *framebuffer);

#ifdef __cplusplus
}
#endif
#endif
