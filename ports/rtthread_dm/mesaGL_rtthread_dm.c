#include "mesaGL_rtthread_dm.h"
#include "mesaGL/simd.h"

#include <drivers/graphic.h>
#include <drivers/input.h>
#include <drivers/lcd.h>

#define MESAGL_RTTHREAD_DM_MAX_DEVICES 32
#define MESAGL_RTTHREAD_DM_MAX_INPUTS 8
#define MESAGL_RTTHREAD_DM_EVENT_COUNT 64

typedef struct InputSlot {
    struct rt_device *device;
    struct rt_input_handler handler;
    rt_bool_t installed;
} InputSlot;

struct MesaGLRTThreadDM {
    struct rt_device *graphic;
    InputSlot inputs[MESAGL_RTTHREAD_DM_MAX_INPUTS];
    int input_count;
    struct rt_device_graphic_info info;
    struct fb_var_screeninfo var;
    MesaGLPortConfig config;
    MesaGLRTThreadDMEventCallback event_callback;
    void *event_user;
    MesaGLRTThreadDMEvent events[MESAGL_RTTHREAD_DM_EVENT_COUNT];
    volatile rt_uint16_t event_head;
    volatile rt_uint16_t event_tail;
    rt_size_t frame_length;
    rt_size_t page_count;
    rt_size_t front_page;
    rt_int32_t pointer_x;
    rt_int32_t pointer_y;
    rt_int32_t mt_x[16];
    rt_int32_t mt_y[16];
    rt_int32_t mt_slot;
    rt_ubase_t mt_active;
    rt_bool_t multitouch;
    rt_bool_t touch_down_reported;
    rt_bool_t powered;
};

static struct rt_device *find_device(const char *name, const char *prefix)
{
    char candidate[16];
    int index;

    if (name && rt_strcmp(name, "auto"))
        return rt_device_find(name);
    for (index = 0; index < MESAGL_RTTHREAD_DM_MAX_DEVICES; ++index) {
        rt_snprintf(candidate, sizeof(candidate), "%s%d", prefix, index);
        if (rt_device_find(candidate))
            return rt_device_find(candidate);
    }
    return RT_NULL;
}

static int framebuffer_format(const struct fb_var_screeninfo *var)
{
    if (var->bits_per_pixel == 16 && var->red.offset == 11 &&
        var->red.length == 5 && var->green.offset == 5 &&
        var->green.length == 6 && var->blue.offset == 0 &&
        var->blue.length == 5)
        return NTGL_RGB565;
    if (var->bits_per_pixel == 16 && var->red.offset == 12 &&
        var->red.length == 4 && var->green.offset == 8 &&
        var->green.length == 4 && var->blue.offset == 4 &&
        var->blue.length == 4 && var->transp.offset == 0 &&
        var->transp.length == 4)
        return NTGL_RGBA4444;
    if (var->bits_per_pixel == 16 && var->red.offset == 11 &&
        var->red.length == 5 && var->green.offset == 6 &&
        var->green.length == 5 && var->blue.offset == 1 &&
        var->blue.length == 5 && var->transp.offset == 0 &&
        var->transp.length == 1)
        return NTGL_RGBA5551;
    if (var->bits_per_pixel == 24 && var->red.offset == 0 &&
        var->green.offset == 8 && var->blue.offset == 16)
        return NTGL_RGB888;
    if (var->bits_per_pixel == 24 && var->red.offset == 16 &&
        var->green.offset == 8 && var->blue.offset == 0)
        return NTGL_BGR888;
    if (var->bits_per_pixel == 32 && var->red.offset == 16 &&
        var->green.offset == 8 && var->blue.offset == 0)
        return var->transp.length ? NTGL_ARGB8888 : NTGL_XRGB8888;
    if (var->bits_per_pixel == 32 && var->red.offset == 0 &&
        var->green.offset == 8 && var->blue.offset == 16)
        return NTGL_RGBA8888;
    return -1;
}

static rt_int32_t scale_axis(struct rt_input_device *input,
                             rt_uint16_t axis, rt_int32_t value,
                             rt_uint32_t size)
{
    struct rt_input_absinfo *info;

    if (!input->absinfo || axis >= ABS_CNT || size <= 1)
        return 0;
    info = &input->absinfo[axis];
    if (info->maximum <= info->minimum)
        return 0;
    value = rt_clamp(value, info->minimum, info->maximum);
    return (rt_uint64_t)(value - info->minimum) * (size - 1) /
           (info->maximum - info->minimum);
}

static void emit_event(struct MesaGLRTThreadDM *port,
                       MesaGLRTThreadDMEventType type, rt_uint16_t code,
                       rt_int32_t value, rt_bool_t down)
{
    MesaGLRTThreadDMEvent event;
    rt_base_t level;
    rt_uint16_t next;

    if (!port->event_callback)
        return;
    event.type = type;
    event.code = code;
    event.value = value;
    event.x = port->pointer_x;
    event.y = port->pointer_y;
    event.down = down;
    level = rt_hw_interrupt_disable();
    next = (port->event_head + 1) % MESAGL_RTTHREAD_DM_EVENT_COUNT;
    if (next == port->event_tail)
        port->event_tail = (port->event_tail + 1) %
                           MESAGL_RTTHREAD_DM_EVENT_COUNT;
    port->events[port->event_head] = event;
    port->event_head = next;
    rt_hw_interrupt_enable(level);
}

static rt_bool_t input_callback(struct rt_input_handler *handler,
                                struct rt_input_event *event)
{
    struct MesaGLRTThreadDM *port = handler->priv;
    struct rt_input_device *input = handler->idev;

    if (event->type == EV_ABS) {
        if (port->multitouch && event->code == ABS_MT_SLOT) {
            port->mt_slot = event->value >= 0 && event->value < 16
                                ? event->value
                                : -1;
        } else if (port->multitouch && event->code == ABS_MT_TRACKING_ID &&
                   port->mt_slot >= 0) {
            rt_ubase_t mask = RT_BIT(port->mt_slot);

            if (event->value < 0)
                port->mt_active &= ~mask;
            else
                port->mt_active |= mask;
        } else if (event->code == ABS_X) {
            port->pointer_x = scale_axis(input, ABS_X, event->value,
                                         port->info.width);
        } else if (event->code == ABS_Y) {
            port->pointer_y = scale_axis(input, ABS_Y, event->value,
                                         port->info.height);
        } else if (port->multitouch && port->mt_slot >= 0 &&
                   event->code == ABS_MT_POSITION_X) {
            port->mt_x[port->mt_slot] = scale_axis(
                input, ABS_MT_POSITION_X, event->value, port->info.width);
        } else if (port->multitouch && port->mt_slot >= 0 &&
                   event->code == ABS_MT_POSITION_Y) {
            port->mt_y[port->mt_slot] = scale_axis(
                input, ABS_MT_POSITION_Y, event->value, port->info.height);
        }
    } else if (event->type == EV_REL) {
        if (event->code == REL_X)
            port->pointer_x = rt_clamp(port->pointer_x + event->value, 0,
                                       (rt_int32_t)port->info.width - 1);
        else if (event->code == REL_Y)
            port->pointer_y = rt_clamp(port->pointer_y + event->value, 0,
                                       (rt_int32_t)port->info.height - 1);
    } else if (event->type == EV_KEY) {
        if (event->code == BTN_LEFT || event->code == BTN_RIGHT ||
            event->code == BTN_MIDDLE || event->code == BTN_TOUCH)
            emit_event(port, MESAGL_RTTHREAD_DM_BUTTON, event->code,
                       event->value, event->value != 0);
        else
            emit_event(port, MESAGL_RTTHREAD_DM_KEY, event->code,
                       event->value, event->value != 0);
    } else if (event->type == EV_SYN && event->code == SYN_REPORT) {
        int slot;

        if (port->multitouch)
            for (slot = 0; slot < 16; ++slot)
                if (port->mt_active & RT_BIT(slot)) {
                    port->pointer_x = port->mt_x[slot];
                    port->pointer_y = port->mt_y[slot];
                    break;
                }
        emit_event(port, MESAGL_RTTHREAD_DM_POINTER, 0, 0, RT_FALSE);
        if (port->multitouch &&
            port->touch_down_reported != (port->mt_active != 0)) {
            port->touch_down_reported = port->mt_active != 0;
            emit_event(port, MESAGL_RTTHREAD_DM_BUTTON, BTN_TOUCH,
                       port->touch_down_reported,
                       port->touch_down_reported);
        }
        emit_event(port, MESAGL_RTTHREAD_DM_SYNC, SYN_REPORT, 0, RT_FALSE);
    }
    return RT_FALSE;
}

static int add_input(struct MesaGLRTThreadDM *port, struct rt_device *device)
{
    struct rt_input_device *input;
    InputSlot *slot;

    if (!device || port->input_count >= MESAGL_RTTHREAD_DM_MAX_INPUTS)
        return -RT_EINVAL;
    input = rt_container_of(device, struct rt_input_device, parent);
    if (!rt_bitmap_test_bit(input->cap, EV_KEY) &&
        !rt_bitmap_test_bit(input->cap, EV_REL) &&
        !rt_bitmap_test_bit(input->cap, EV_ABS))
        return -RT_EINVAL;
    if (rt_device_open(device, 0))
        return -RT_ERROR;
    slot = &port->inputs[port->input_count];
    slot->device = device;
    slot->handler.idev = input;
    slot->handler.callback = input_callback;
    slot->handler.priv = port;
    if (rt_input_add_handler(&slot->handler)) {
        rt_device_close(device);
        slot->device = RT_NULL;
        return -RT_ERROR;
    }
    slot->installed = RT_TRUE;
    ++port->input_count;
    if (rt_bitmap_test_bit(input->abs_map, ABS_MT_SLOT) &&
        rt_bitmap_test_bit(input->abs_map, ABS_MT_TRACKING_ID))
        port->multitouch = RT_TRUE;
    return RT_EOK;
}

static int setup_inputs(struct MesaGLRTThreadDM *port, const char *name)
{
    if (name && rt_strcmp(name, "auto"))
        return add_input(port, rt_device_find(name));
    {
        char candidate[16];
        int index;

        for (index = 0; index < MESAGL_RTTHREAD_DM_MAX_DEVICES &&
                        port->input_count < MESAGL_RTTHREAD_DM_MAX_INPUTS;
             ++index) {
            struct rt_device *device;

            rt_snprintf(candidate, sizeof(candidate), "input%d", index);
            device = rt_device_find(candidate);
            if (device)
                add_input(port, device);
        }
    }
    return RT_EOK;
}

MesaGLRTThreadDM *mesaGLRTThreadDMCreate(const char *graphic_name,
                                     const char *input_name)
{
    MesaGLRTThreadDM *port;
    int format;

    port = rt_calloc(1, sizeof(*port));
    if (!port)
        return RT_NULL;
    port->mt_slot = -1;
    port->graphic = find_device(graphic_name, "fb");
    if (!port->graphic || rt_device_open(port->graphic, 0))
        goto fail;
    if (rt_device_control(port->graphic, RTGRAPHIC_CTRL_POWERON, RT_NULL))
        goto fail;
    port->powered = RT_TRUE;
    if (rt_device_control(port->graphic, RTGRAPHIC_CTRL_GET_INFO,
                          &port->info) ||
        rt_device_control(port->graphic, FBIOGET_VSCREENINFO, &port->var))
        goto fail;
    format = framebuffer_format(&port->var);
    if (format < 0 || !port->info.width ||
        !port->info.height || !port->info.pitch)
        goto fail;
    port->frame_length = (rt_size_t)port->info.pitch * port->info.height;
    port->page_count = port->info.smem_len / port->frame_length;
    if (!port->info.framebuffer || !port->page_count)
        goto fail;
    if (port->page_count > 1) {
        struct fb_fix_screeninfo fix;

        rt_memset(&fix, 0, sizeof(fix));
        if (rt_device_control(port->graphic, FBIOGET_FSCREENINFO, &fix) ||
            !fix.ypanstep) {
            port->page_count = 1;
        } else {
            port->var.pixclock = 0;
            port->var.activate = FB_ACTIVATE_NOW;
            if (rt_device_control(port->graphic, FBIOPUT_VSCREENINFO,
                                  &port->var))
                port->page_count = 1;
        }
    }
    port->front_page = port->var.yoffset / port->info.height;
    if (port->front_page >= port->page_count)
        port->front_page = 0;
    port->config.framebuffer.pixels = rt_malloc(port->frame_length);
    if (!port->config.framebuffer.pixels)
        goto fail;
    rt_memset(port->config.framebuffer.pixels, 0, port->frame_length);
    port->config.framebuffer.width = port->info.width;
    port->config.framebuffer.height = port->info.height;
    port->config.framebuffer.stride = port->info.pitch;
    port->config.framebuffer.format = (NTGLformat)format;
    port->config.framebuffer.origin = NTGL_ORIGIN_TOP_LEFT;
    port->config.present = mesaGLRTThreadDMPresent;
    port->config.user = port;
    mesaGLInitSIMDPixelOps(&port->config.pixel_ops);
    port->pointer_x = port->info.width / 2;
    port->pointer_y = port->info.height / 2;
    if (setup_inputs(port, input_name))
        goto fail;
    return port;

fail:
    mesaGLRTThreadDMDestroy(port);
    return RT_NULL;
}

void mesaGLRTThreadDMDestroy(MesaGLRTThreadDM *port)
{
    if (!port)
        return;
    while (port->input_count > 0) {
        InputSlot *slot = &port->inputs[--port->input_count];

        if (slot->installed)
            rt_input_del_handler(&slot->handler);
        if (slot->device)
            rt_device_close(slot->device);
    }
    if (port->powered)
        rt_device_control(port->graphic, RTGRAPHIC_CTRL_POWEROFF, RT_NULL);
    if (port->graphic)
        rt_device_close(port->graphic);
    rt_free(port->config.framebuffer.pixels);
    rt_free(port);
}

const MesaGLPortConfig *mesaGLRTThreadDMGetPortConfig(
    const MesaGLRTThreadDM *port)
{
    return port ? &port->config : RT_NULL;
}

void mesaGLRTThreadDMSetEventCallback(MesaGLRTThreadDM *port,
                                    MesaGLRTThreadDMEventCallback callback,
                                    void *user)
{
    if (!port)
        return;
    port->event_callback = callback;
    port->event_user = user;
}

int mesaGLRTThreadDMPollEvents(MesaGLRTThreadDM *port)
{
    int count = 0;

    if (!port || !port->event_callback)
        return 0;
    for (;;) {
        MesaGLRTThreadDMEvent event;
        rt_base_t level = rt_hw_interrupt_disable();

        if (port->event_tail == port->event_head) {
            rt_hw_interrupt_enable(level);
            break;
        }
        event = port->events[port->event_tail];
        port->event_tail = (port->event_tail + 1) %
                           MESAGL_RTTHREAD_DM_EVENT_COUNT;
        rt_hw_interrupt_enable(level);
        port->event_callback(port->event_user, &event);
        ++count;
    }
    return count;
}

NTGLresult mesaGLRTThreadDMPresent(void *user,
                                 const NTGLframebuffer *framebuffer)
{
    MesaGLRTThreadDM *port = user;
    struct rt_device_rect_info rectangle;

    if (!port || !framebuffer ||
        framebuffer->pixels != port->config.framebuffer.pixels)
        return NTGL_INVALID_ARGUMENT;
    if (port->page_count > 1) {
        rt_size_t back_page = (port->front_page + 1) % port->page_count;
        struct fb_var_screeninfo var = port->var;

        rt_memcpy((rt_uint8_t *)port->info.framebuffer +
                      back_page * port->frame_length,
                  framebuffer->pixels, port->frame_length);
        var.xoffset = 0;
        var.yoffset = back_page * port->info.height;
        var.activate = FB_ACTIVATE_VBL;
        if (rt_device_control(port->graphic, FBIOPAN_DISPLAY, &var))
            return NTGL_INVALID_OPERATION;
        port->var = var;
        port->front_page = back_page;
        return NTGL_OK;
    }
    rt_memcpy(port->info.framebuffer, framebuffer->pixels,
              port->frame_length);
    rectangle.x = 0;
    rectangle.y = 0;
    rectangle.width = port->info.width;
    rectangle.height = port->info.height;
    if (rt_device_control(port->graphic, RTGRAPHIC_CTRL_RECT_UPDATE,
                          &rectangle) ||
        rt_device_control(port->graphic, RTGRAPHIC_CTRL_WAIT_VSYNC,
                          RT_NULL))
        return NTGL_INVALID_OPERATION;
    return NTGL_OK;
}
