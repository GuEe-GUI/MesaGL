#include "mesaGL/port.h"

#include <stdlib.h>

struct MesaGLPortContext {
    NTGLcontext *renderer;
    NTGLallocator allocator;
    MesaGLPresentFn present;
    void *user;
};

static void *default_alloc(void *user, size_t size)
{
    (void)user;
    return malloc(size);
}

static void default_free(void *user, void *pointer)
{
    (void)user;
    free(pointer);
}

MesaGLPortContext *mesaGLPortCreate(const MesaGLPortConfig *config)
{
    NTGLallocator allocator;
    MesaGLPortContext *context;
    if (!config)
        return NULL;
    allocator.alloc = config->allocator.alloc ? config->allocator.alloc : default_alloc;
    allocator.free = config->allocator.free ? config->allocator.free : default_free;
    allocator.user = config->allocator.user;
    context = (MesaGLPortContext *)allocator.alloc(allocator.user, sizeof(*context));
    if (!context)
        return NULL;
    context->allocator = allocator;
    context->present = config->present;
    context->user = config->user;
    context->renderer = ntglCreateContext(&config->framebuffer, &allocator);
    if (!context->renderer) {
        allocator.free(allocator.user, context);
        return NULL;
    }
    return context;
}

void mesaGLPortDestroy(MesaGLPortContext *context)
{
    NTGLallocator allocator;
    if (!context)
        return;
    allocator = context->allocator;
    ntglDestroyContext(context->renderer);
    allocator.free(allocator.user, context);
}

NTGLresult mesaGLPortMakeCurrent(MesaGLPortContext *context)
{
    if (!context)
        return NTGL_INVALID_ARGUMENT;
    return ntglMakeCurrent(context->renderer);
}

NTGLresult mesaGLPortAttachFramebuffer(MesaGLPortContext *context,
                                       const NTGLframebuffer *framebuffer)
{
    if (!context)
        return NTGL_INVALID_ARGUMENT;
    return ntglAttachFramebuffer(context->renderer, framebuffer);
}

NTGLresult mesaGLPortPresent(MesaGLPortContext *context)
{
    if (!context)
        return NTGL_INVALID_ARGUMENT;
    if (!context->present)
        return NTGL_OK;
    return context->present(context->user, ntglGetFramebuffer(context->renderer));
}

NTGLcontext *mesaGLPortGetRenderer(MesaGLPortContext *context)
{
    return context ? context->renderer : NULL;
}

const NTGLframebuffer *mesaGLPortGetFramebuffer(const MesaGLPortContext *context)
{
    return context ? ntglGetFramebuffer(context->renderer) : NULL;
}
