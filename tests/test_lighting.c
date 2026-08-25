#include "GL/gl.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>

#define WIDTH 32
#define HEIGHT 32

static uint32_t pixels[WIDTH * HEIGHT];

static unsigned draw_scaled_normal(int normalize)
{
    glClear(GL_COLOR_BUFFER_BIT);
    if (normalize)
        glEnable(GL_NORMALIZE);
    else
        glDisable(GL_NORMALIZE);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glScalef(2.0f, 1.0f, 1.0f);
    glNormal3f(1.0f, 0.0f, 0.0f);
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(-0.3f, -0.7f);
    glVertex2f(0.3f, -0.7f);
    glVertex2f(0.3f, 0.7f);
    glVertex2f(-0.3f, 0.7f);
    glEnd();
    return (pixels[(HEIGHT / 2) * WIDTH + WIDTH / 2] >> 16) & 0xffu;
}

int main(void)
{
    static const GLfloat light_position[] = {1, 0, 0, 0};
    static const GLfloat black[] = {0, 0, 0, 1};
    NTGLframebuffer framebuffer = {pixels,    WIDTH,         HEIGHT,
                                   WIDTH * 4, NTGL_ARGB8888, NTGL_ORIGIN_TOP_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    unsigned raw, normalized;

    if (!context)
        return 1;
    glViewport(0, 0, WIDTH, HEIGHT);
    glClearColor(0, 0, 0, 1);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, black);
    glLightfv(GL_LIGHT0, GL_AMBIENT, black);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);
    raw = draw_scaled_normal(0);
    normalized = draw_scaled_normal(1);
    ntglDestroyContext(context);
    if (raw < 126 || raw > 129)
        return 2;
    if (normalized < 254)
        return 3;
    return 0;
}
