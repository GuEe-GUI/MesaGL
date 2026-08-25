#define _POSIX_C_SOURCE 199309L
#include "GL/gl.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"

#include <time.h>

static void face(float nx, float ny, float nz, float r, float g, float b, const float vertices[12])
{
    int i;

    glNormal3f(nx, ny, nz);
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    for (i = 0; i < 4; ++i)
        glVertex3f(vertices[i * 3], vertices[i * 3 + 1], vertices[i * 3 + 2]);
    glEnd();
}

static void cube(void)
{
    static const float front[] = {-1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1};
    static const float back[] = {1, -1, -1, -1, -1, -1, -1, 1, -1, 1, 1, -1};
    static const float right[] = {1, -1, 1, 1, -1, -1, 1, 1, -1, 1, 1, 1};
    static const float left[] = {-1, -1, -1, -1, -1, 1, -1, 1, 1, -1, 1, -1};
    static const float top[] = {-1, 1, 1, 1, 1, 1, 1, 1, -1, -1, 1, -1};
    static const float bottom[] = {-1, -1, -1, 1, -1, -1, 1, -1, 1, -1, -1, 1};

    face(0, 0, 1, 0.95f, 0.25f, 0.15f, front);
    face(0, 0, -1, 0.2f, 0.75f, 1.0f, back);
    face(1, 0, 0, 0.25f, 0.9f, 0.3f, right);
    face(-1, 0, 0, 0.95f, 0.75f, 0.15f, left);
    face(0, 1, 0, 0.7f, 0.3f, 1.0f, top);
    face(0, -1, 0, 0.2f, 0.85f, 0.85f, bottom);
}

static void draw_cube(int x, float angle, int lighting)
{
    glViewport(x, 0, 450, 420);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -0.93, 0.93, 2.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -6);
    glRotatef(24, 1, 0, 0);
    glRotatef(angle, 0, 1, 0);
    if (lighting)
        glEnable(GL_LIGHTING);
    else
        glDisable(GL_LIGHTING);
    cube();
}

int main(void)
{
    static const GLfloat light_position[] = {-0.4f, 0.7f, 1.0f, 0.0f};
    static const GLfloat light_ambient[] = {0.08f, 0.08f, 0.08f, 1.0f};
    static const GLfloat light_diffuse[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const struct timespec delay = {0, 16666667};
    MesaGLX11 *x11 = mesaGLX11Create(900, 420, "MesaGL cube: UNLIT | AMBIENT + DIFFUSE LIGHT0");
    MesaGLPortContext *context;
    float angle = 0.0f;

    if (!x11 || !(context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11))))
        return 1;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);
    glLightfv(GL_LIGHT0, GL_AMBIENT, light_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);
    while (mesaGLX11PollEvents(x11)) {
        glViewport(0, 0, 900, 420);
        glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        draw_cube(0, angle, 0);
        draw_cube(450, angle, 1);
        mesaGLPortPresent(context);
        angle += 0.7f;
        nanosleep(&delay, NULL);
    }
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
