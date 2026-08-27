#include "mesaGL_x11.h"
#include "mesaGL/simd.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/keysym.h>
#include <locale.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>

struct MesaGLX11 {
    Display *display;
    Window window;
    GC gc;
    Atom wm_delete;
    XIM input_method;
    XIC input_context;
    XImage *image;
    XShmSegmentInfo shm;
    int shm_attached;
    const char *simd_backend;
    MesaGLPortConfig port;
    MesaGLX11EventCallback event_callback;
    void *event_user;
};

static NTGLformat image_format(const XImage *image)
{
    if (image->bits_per_pixel == 32 && image->red_mask == 0x00ff0000 &&
        image->green_mask == 0x0000ff00 && image->blue_mask == 0x000000ff)
        return NTGL_XRGB8888;
    return NTGL_XRGB8888;
}

MesaGLX11 *mesaGLX11Create(int width, int height, const char *title)
{
    XSetWindowAttributes attributes;
    MesaGLX11 *x11;
    if (width <= 0 || height <= 0)
        return NULL;
    x11 = (MesaGLX11 *)calloc(1, sizeof(*x11));
    if (!x11)
        return NULL;
    x11->shm.shmid = -1;
    x11->display = XOpenDisplay(NULL);
    if (!x11->display || !XShmQueryExtension(x11->display))
        goto fail;

    attributes.event_mask = StructureNotifyMask | ExposureMask | FocusChangeMask |
                            EnterWindowMask | LeaveWindowMask | KeyPressMask |
                            KeyReleaseMask | PointerMotionMask | ButtonPressMask |
                            ButtonReleaseMask;
    x11->window = XCreateWindow(x11->display, DefaultRootWindow(x11->display), 0, 0,
                                (unsigned)width, (unsigned)height, 0, CopyFromParent, InputOutput,
                                CopyFromParent, CWEventMask, &attributes);
    if (!x11->window)
        goto fail;
    XStoreName(x11->display, x11->window, title ? title : "mesaGL X11 framebuffer");
    x11->wm_delete = XInternAtom(x11->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(x11->display, x11->window, &x11->wm_delete, 1);
    setlocale(LC_CTYPE, "");
    XSetLocaleModifiers("");
    x11->input_method = XOpenIM(x11->display, NULL, NULL, NULL);
    if (x11->input_method)
        x11->input_context = XCreateIC(
            x11->input_method, XNInputStyle,
            XIMPreeditNothing | XIMStatusNothing, XNClientWindow,
            x11->window, XNFocusWindow, x11->window, NULL);
    if (x11->input_context) {
        long filter_events = 0;

        XGetICValues(x11->input_context, XNFilterEvents, &filter_events, NULL);
        XSelectInput(x11->display, x11->window,
                     attributes.event_mask | filter_events);
    }
    x11->gc = XCreateGC(x11->display, x11->window, 0, NULL);
    if (!x11->gc)
        goto fail;

    x11->image = XShmCreateImage(x11->display, DefaultVisual(x11->display, 0),
                                 (unsigned)DefaultDepth(x11->display, 0), ZPixmap, NULL, &x11->shm,
                                 (unsigned)width, (unsigned)height);
    if (!x11->image || x11->image->bits_per_pixel != 32)
        goto fail;
    x11->shm.shmid =
        shmget(IPC_PRIVATE, (size_t)x11->image->bytes_per_line * height, IPC_CREAT | 0600);
    if (x11->shm.shmid < 0)
        goto fail;
    x11->shm.shmaddr = (char *)shmat(x11->shm.shmid, NULL, 0);
    if (x11->shm.shmaddr == (char *)-1) {
        x11->shm.shmaddr = NULL;
        goto fail;
    }
    x11->shm.readOnly = False;
    x11->image->data = x11->shm.shmaddr;
    if (!XShmAttach(x11->display, &x11->shm))
        goto fail;
    x11->shm_attached = 1;
    shmctl(x11->shm.shmid, IPC_RMID, NULL);
    x11->shm.shmid = -1;

    memset(x11->image->data, 0, (size_t)x11->image->bytes_per_line * height);
    x11->port.framebuffer.pixels = x11->image->data;
    x11->port.framebuffer.width = width;
    x11->port.framebuffer.height = height;
    x11->port.framebuffer.stride = x11->image->bytes_per_line;
    x11->port.framebuffer.format = image_format(x11->image);
    x11->port.framebuffer.origin = NTGL_ORIGIN_TOP_LEFT;
    x11->port.present = mesaGLX11Present;
    x11->port.user = x11;
    x11->simd_backend = mesaGLInitSIMDPixelOps(&x11->port.pixel_ops);
    XMapWindow(x11->display, x11->window);
    XFlush(x11->display);
    return x11;

fail:
    mesaGLX11Destroy(x11);
    return NULL;
}

const char *mesaGLX11GetSIMDBackend(const MesaGLX11 *x11)
{
    if (!x11 || !x11->simd_backend)
        return "scalar";
    return x11->simd_backend;
}

void mesaGLX11Destroy(MesaGLX11 *x11)
{
    if (!x11)
        return;
    if (x11->display && x11->shm_attached) {
        XShmDetach(x11->display, &x11->shm);
        XSync(x11->display, False);
    }
    if (x11->shm.shmaddr)
        shmdt(x11->shm.shmaddr);
    if (x11->shm.shmid >= 0)
        shmctl(x11->shm.shmid, IPC_RMID, NULL);
    if (x11->image) {
        x11->image->data = NULL;
        XDestroyImage(x11->image);
    }
    if (x11->display && x11->gc)
        XFreeGC(x11->display, x11->gc);
    if (x11->input_context)
        XDestroyIC(x11->input_context);
    if (x11->input_method)
        XCloseIM(x11->input_method);
    if (x11->display && x11->window)
        XDestroyWindow(x11->display, x11->window);
    if (x11->display)
        XCloseDisplay(x11->display);
    free(x11);
}

const MesaGLPortConfig *mesaGLX11GetPortConfig(const MesaGLX11 *x11)
{
    return x11 ? &x11->port : NULL;
}

NTGLresult mesaGLX11Present(void *user, const NTGLframebuffer *framebuffer)
{
    MesaGLX11 *x11 = (MesaGLX11 *)user;
    if (!x11 || !framebuffer || framebuffer->pixels != x11->image->data)
        return NTGL_INVALID_ARGUMENT;
    XShmPutImage(x11->display, x11->window, x11->gc, x11->image, 0, 0, 0, 0,
                 (unsigned)framebuffer->width, (unsigned)framebuffer->height, False);
    /* The renderer and X server share this memory, so do not overwrite it
     * until the server has consumed the submitted image. */
    XSync(x11->display, False);
    return NTGL_OK;
}

void mesaGLX11SetEventCallback(MesaGLX11 *x11,
                               MesaGLX11EventCallback callback, void *user)
{
    if (!x11)
        return;
    x11->event_callback = callback;
    x11->event_user = user;
}

static void send_event(MesaGLX11 *x11, const MesaGLX11Event *event)
{
    if (x11->event_callback)
        x11->event_callback(x11->event_user, event);
}

static int handle_event(MesaGLX11 *x11, XEvent *event)
{
    MesaGLX11Event input;

    memset(&input, 0, sizeof(input));
    if (event->type == KeyRelease && XPending(x11->display)) {
        XEvent next;

        XPeekEvent(x11->display, &next);
        if (next.type == KeyPress && next.xkey.time == event->xkey.time &&
            next.xkey.keycode == event->xkey.keycode)
            return 1;
    }
    if (event->type == ClientMessage && (Atom)event->xclient.data.l[0] == x11->wm_delete)
        return 0;
    if (event->type == DestroyNotify)
        return 0;
    if (event->type == MotionNotify || event->type == EnterNotify) {
        input.type = MESAGL_X11_MOUSE_POSITION;
        input.x = event->type == MotionNotify ? event->xmotion.x : event->xcrossing.x;
        input.y = event->type == MotionNotify ? event->xmotion.y : event->xcrossing.y;
        send_event(x11, &input);
    } else if (event->type == LeaveNotify) {
        input.type = MESAGL_X11_MOUSE_POSITION;
        input.x = -2147483647;
        input.y = -2147483647;
        send_event(x11, &input);
    } else if (event->type == ButtonPress || event->type == ButtonRelease) {
        unsigned int button = event->xbutton.button;

        input.type = MESAGL_X11_MOUSE_POSITION;
        input.x = event->xbutton.x;
        input.y = event->xbutton.y;
        send_event(x11, &input);
        memset(&input, 0, sizeof(input));
        if (event->type == ButtonPress && button >= Button4 && button <= 7) {
            input.type = MESAGL_X11_MOUSE_WHEEL;
            input.wheel_y = button == Button4 ? 1.0f : button == Button5 ? -1.0f : 0.0f;
            input.wheel_x = button == 6 ? 1.0f : button == 7 ? -1.0f : 0.0f;
        } else if (button >= Button1 && button <= Button3) {
            static const int buttons[] = {0, 2, 1};

            input.type = MESAGL_X11_MOUSE_BUTTON;
            input.button = buttons[button - Button1];
            input.down = event->type == ButtonPress;
        } else {
            return 1;
        }
        send_event(x11, &input);
    } else if (event->type == KeyPress || event->type == KeyRelease) {
        char text[sizeof(input.text)];
        KeySym text_key;
        KeySym key = XLookupKeysym(&event->xkey, 0);
        int length = 0;

        if (event->type == KeyPress) {
            if (x11->input_context) {
                Status status;

                length = Xutf8LookupString(x11->input_context, &event->xkey,
                                           text, sizeof(text) - 1, &text_key,
                                           &status);
                if (status == XBufferOverflow)
                    length = 0;
            } else {
                length = XLookupString(&event->xkey, text,
                                       sizeof(text) - 1, &text_key, NULL);
            }
        }
        input.type = MESAGL_X11_KEY;
        input.key = key;
        input.keycode = event->xkey.keycode;
        input.down = event->type == KeyPress;
        send_event(x11, &input);
        if (length > 0 &&
            (length > 1 || (unsigned char)text[0] >= 32)) {
            text[length] = '\0';
            input.type = MESAGL_X11_TEXT;
            memcpy(input.text, text, (size_t)length + 1);
            send_event(x11, &input);
        }
        if (event->type == KeyPress && key == XK_Escape)
            return 0;
    } else if (event->type == FocusIn || event->type == FocusOut) {
        if (x11->input_context) {
            if (event->type == FocusIn)
                XSetICFocus(x11->input_context);
            else
                XUnsetICFocus(x11->input_context);
        }
        input.type = MESAGL_X11_FOCUS;
        input.down = event->type == FocusIn;
        send_event(x11, &input);
    }
    return 1;
}

int mesaGLX11WaitEvent(MesaGLX11 *x11)
{
    XEvent event;
    if (!x11)
        return 0;
    do {
        XNextEvent(x11->display, &event);
    } while (XFilterEvent(&event, x11->window));
    return handle_event(x11, &event);
}

int mesaGLX11PollEvents(MesaGLX11 *x11)
{
    XEvent event;
    if (!x11)
        return 0;
    while (XPending(x11->display)) {
        XNextEvent(x11->display, &event);
        if (XFilterEvent(&event, x11->window))
            continue;
        if (!handle_event(x11, &event))
            return 0;
    }
    return 1;
}
