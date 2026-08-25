#define _POSIX_C_SOURCE 199309L
#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"

#include <math.h>
#include <time.h>

#define PI 3.14159265358979323846f

static void sphere(void)
{
    int latitude, longitude;

    for (latitude = 0; latitude < 24; ++latitude) {
        float a0 = -PI * 0.5f + PI * latitude / 24.0f;
        float a1 = -PI * 0.5f + PI * (latitude + 1) / 24.0f;

        glBegin(GL_TRIANGLE_STRIP);
        for (longitude = 0; longitude <= 32; ++longitude) {
            float longitude_angle = 2.0f * PI * longitude / 32.0f;
            float x0 = cosf(a0) * cosf(longitude_angle);
            float y0 = sinf(a0);
            float z0 = cosf(a0) * sinf(longitude_angle);
            float x1 = cosf(a1) * cosf(longitude_angle);
            float y1 = sinf(a1);
            float z1 = cosf(a1) * sinf(longitude_angle);

            glNormal3f(x0, y0, z0);
            glVertex3f(x0, y0, z0);
            glNormal3f(x1, y1, z1);
            glVertex3f(x1, y1, z1);
        }
        glEnd();
    }
}

static void draw_sphere(int x, int shiny)
{
    static const GLfloat no_specular[] = {0, 0, 0, 1};
    static const GLfloat white_specular[] = {1, 1, 1, 1};

    glViewport(x, 0, 400, 400);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -1, 1, 2, 20);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -5);
    glColor3f(0.12f, 0.45f, 0.9f);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, shiny ? white_specular : no_specular);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 48.0f);
    sphere();
}

int main(void)
{
    static const GLfloat position[] = {-0.7f, 0.8f, 1.0f, 0.0f};
    static const GLfloat ambient[] = {0.05f, 0.05f, 0.05f, 1.0f};
    static const struct timespec delay = {0, 16666667};
    MesaGLX11 *x11 = mesaGLX11Create(800, 400, "MesaGL lighting: DIFFUSE | SPECULAR shininess 48");
    MesaGLPortContext *context;

    if (!x11 || !(context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11))))
        return 1;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_POSITION, position);
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    while (mesaGLX11PollEvents(x11)) {
        glViewport(0, 0, 800, 400);
        glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        draw_sphere(0, 0);
        draw_sphere(400, 1);
        mesaGLPortPresent(context);
        nanosleep(&delay, NULL);
    }
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
