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

    for (latitude = 0; latitude < 20; ++latitude) {
        float a0 = -PI * 0.5f + PI * latitude / 20.0f;
        float a1 = -PI * 0.5f + PI * (latitude + 1) / 20.0f;

        glBegin(GL_TRIANGLE_STRIP);
        for (longitude = 0; longitude <= 28; ++longitude) {
            float angle = 2.0f * PI * longitude / 28.0f;
            float x0 = cosf(a0) * cosf(angle), y0 = sinf(a0), z0 = cosf(a0) * sinf(angle);
            float x1 = cosf(a1) * cosf(angle), y1 = sinf(a1), z1 = cosf(a1) * sinf(angle);

            glNormal3f(x0, y0, z0);
            glVertex3f(x0, y0, z0);
            glNormal3f(x1, y1, z1);
            glVertex3f(x1, y1, z1);
        }
        glEnd();
    }
}

static void panel(int x, int red, int blue)
{
    glViewport(x, 0, 300, 300);
    if (red)
        glEnable(GL_LIGHT0);
    else
        glDisable(GL_LIGHT0);
    if (blue)
        glEnable(GL_LIGHT1);
    else
        glDisable(GL_LIGHT1);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -1, 1, 2, 20);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -5);
    glColor3f(0.65f, 0.65f, 0.65f);
    sphere();
}

int main(void)
{
    static const GLfloat red[] = {1.0f, 0.08f, 0.03f, 1.0f};
    static const GLfloat blue[] = {0.03f, 0.15f, 1.0f, 1.0f};
    static const GLfloat red_position[] = {-0.8f, 0.5f, 1.0f, 0.0f};
    static const GLfloat blue_position[] = {0.8f, 0.5f, 1.0f, 0.0f};
    static const GLfloat white[] = {0.7f, 0.7f, 0.7f, 1.0f};
    static const struct timespec delay = {0, 16666667};
    MesaGLX11 *x11 = mesaGLX11Create(900, 300, "MesaGL lights: RED LIGHT0 | BLUE LIGHT1 | BOTH");
    MesaGLPortContext *context;

    if (!x11 || !(context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11))))
        return 1;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, white);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 32.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_POSITION, red_position);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, red);
    glLightfv(GL_LIGHT0, GL_SPECULAR, red);
    glLightfv(GL_LIGHT1, GL_POSITION, blue_position);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, blue);
    glLightfv(GL_LIGHT1, GL_SPECULAR, blue);
    while (mesaGLX11PollEvents(x11)) {
        glViewport(0, 0, 900, 300);
        glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        panel(0, 1, 0);
        panel(300, 0, 1);
        panel(600, 1, 1);
        mesaGLPortPresent(context);
        nanosleep(&delay, NULL);
    }
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
