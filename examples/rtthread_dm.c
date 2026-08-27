#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_rtthread_dm.h"

#include <dt-bindings/input/event-codes.h>
#include <rtthread.h>

typedef struct DemoState {
    volatile rt_bool_t running;
    volatile rt_bool_t pressed;
    volatile rt_int32_t pointer_x;
    volatile rt_int32_t pointer_y;
} DemoState;

static void input_event(void *user, const MesaGLRTThreadDMEvent *event)
{
    DemoState *state = user;

    if (event->type == MESAGL_RTTHREAD_DM_POINTER) {
        state->pointer_x = event->x;
        state->pointer_y = event->y;
    } else if (event->type == MESAGL_RTTHREAD_DM_BUTTON &&
               (event->code == BTN_LEFT || event->code == BTN_TOUCH)) {
        state->pressed = event->down;
    } else if (event->type == MESAGL_RTTHREAD_DM_KEY &&
               event->code == KEY_ESC && event->down) {
        state->running = RT_FALSE;
    }
}

static void draw_triangle(float angle, float x, float y, rt_bool_t pressed)
{
    glClearColor(0.025f, 0.04f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -1.0, 1.0, 2.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(x, y, -5.0f);
    glRotatef(angle, 0.0f, 1.0f, 0.0f);
    glBegin(GL_TRIANGLES);
    glColor3f(pressed ? 1.0f : 0.15f, 0.2f, 0.2f);
    glVertex3f(0.0f, 1.25f, 0.0f);
    glColor3f(0.15f, 1.0f, 0.35f);
    glVertex3f(-1.3f, -1.0f, 0.0f);
    glColor3f(0.2f, 0.45f, 1.0f);
    glVertex3f(1.3f, -1.0f, 0.0f);
    glEnd();
}

static int mesaGL_rtthread_dm(int argc, char **argv)
{
    const char *graphic_name = argc > 1 ? argv[1] : "auto";
    const char *input_name = argc > 2 ? argv[2] : "auto";
    MesaGLRTThreadDM *rt_port;
    MesaGLPortContext *context;
    const MesaGLPortConfig *config;
    DemoState state = {RT_TRUE, RT_FALSE, 0, 0};
    float angle = 0.0f;

    rt_port = mesaGLRTThreadDMCreate(graphic_name, input_name);
    if (!rt_port) {
        rt_kprintf("mesaGL: cannot open graphic/input port\n");
        return -RT_ERROR;
    }
    config = mesaGLRTThreadDMGetPortConfig(rt_port);
    context = mesaGLPortCreate(config);
    if (!context || mesaGLPortMakeCurrent(context) != NTGL_OK) {
        rt_kprintf("mesaGL: cannot create renderer\n");
        mesaGLPortDestroy(context);
        mesaGLRTThreadDMDestroy(rt_port);
        return -RT_ERROR;
    }
    state.pointer_x = config->framebuffer.width / 2;
    state.pointer_y = config->framebuffer.height / 2;
    mesaGLRTThreadDMSetEventCallback(rt_port, input_event, &state);
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, config->framebuffer.width,
               config->framebuffer.height);
    while (state.running) {
        float x = ((float)state.pointer_x / config->framebuffer.width -
                   0.5f) *
                  1.2f;
        float y = (0.5f -
                   (float)state.pointer_y / config->framebuffer.height) *
                  1.2f;

        mesaGLRTThreadDMPollEvents(rt_port);
        draw_triangle(angle, x, y, state.pressed);
        if (mesaGLPortPresent(context) != NTGL_OK)
            break;
        angle += 1.3f;
        if (angle >= 360.0f)
            angle -= 360.0f;
        rt_thread_mdelay(16);
    }
    mesaGLRTThreadDMSetEventCallback(rt_port, RT_NULL, RT_NULL);
    mesaGLPortDestroy(context);
    mesaGLRTThreadDMDestroy(rt_port);
    return RT_EOK;
}

MSH_CMD_EXPORT_ALIAS(mesaGL_rtthread_dm, mesagl_dm,
                     MesaGL RT-Thread framebuffer/input demo);
