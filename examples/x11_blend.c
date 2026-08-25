#define _POSIX_C_SOURCE 199309L

#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"

#include <stdio.h>
#include <time.h>

static void rectangle(float left, float bottom, float right, float top, float red, float green,
                      float blue, float alpha)
{
    glColor4f(red, green, blue, alpha);
    glBegin(GL_QUADS);
    glVertex2f(left, bottom);
    glVertex2f(right, bottom);
    glVertex2f(right, top);
    glVertex2f(left, top);
    glEnd();
}

static void draw_cell(int x, int y, GLenum equation, GLenum source, GLenum destination)
{
    glViewport(x, y, 300, 210);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 0, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_BLEND);
    rectangle(0.08f, 0.08f, 0.92f, 0.92f, 0.10f, 0.25f, 0.80f, 1.0f);

    glEnable(GL_BLEND);
    glBlendEquation(equation);
    glBlendFunc(source, destination);
    rectangle(0.25f, 0.25f, 0.85f, 0.85f, 0.90f, 0.15f, 0.05f, 0.60f);
}

int main(void)
{
    static const struct timespec frame_delay = {0, 16666667};
    MesaGLX11 *x11 = mesaGLX11Create(
        960, 480,
        "MesaGL blend: alpha | add | constant color / subtract | reverse | constant alpha");
    MesaGLPortContext *context;

    if (!x11) {
        fprintf(stderr, "Unable to create X11 framebuffer\n");
        return 1;
    }
    context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11));
    if (!context) {
        mesaGLX11Destroy(x11);
        return 2;
    }

    while (mesaGLX11PollEvents(x11)) {
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.04f, 0.05f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        draw_cell(10, 250, GL_FUNC_ADD, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        draw_cell(330, 250, GL_FUNC_ADD, GL_ONE, GL_ONE);
        glBlendColor(0.25f, 1.0f, 1.0f, 1.0f);
        draw_cell(650, 250, GL_FUNC_ADD, GL_CONSTANT_COLOR, GL_ZERO);
        draw_cell(10, 20, GL_FUNC_SUBTRACT, GL_ONE, GL_ONE);
        draw_cell(330, 20, GL_FUNC_REVERSE_SUBTRACT, GL_ONE, GL_ONE);
        glBlendColor(0.0f, 0.0f, 0.0f, 0.25f);
        draw_cell(650, 20, GL_FUNC_ADD, GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        mesaGLPortPresent(context);
        nanosleep(&frame_delay, NULL);
    }

    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
