#define _POSIX_C_SOURCE 199309L
#include "GLES2/gl2.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

static bool scene_fog = true;

static void setup_style(void)
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    style.WindowPadding = ImVec2(18, 16);
    style.FramePadding = ImVec2(10, 7);
    style.ItemSpacing = ImVec2(9, 9);
    style.WindowRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    colors[ImGuiCol_WindowBg] = ImVec4(0.045f, 0.055f, 0.085f, 0.98f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.14f, 0.25f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.27f, 0.48f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.14f, 0.22f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.24f, 0.38f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.32f, 0.52f, 1.0f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.35f, 0.75f, 1.0f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.25f, 0.58f, 1.0f, 1.0f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.95f, 0.65f, 0.08f, 1.0f);
}

static void face(float nx, float ny, float nz, float r, float g, float b, const float *vertices)
{
    glNormal3f(nx, ny, nz);
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; ++i)
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

    face(0, 0, 1, 0.95f, 0.2f, 0.12f, front);
    face(0, 0, -1, 0.15f, 0.55f, 1.0f, back);
    face(1, 0, 0, 0.18f, 0.9f, 0.38f, right);
    face(-1, 0, 0, 0.95f, 0.65f, 0.1f, left);
    face(0, 1, 0, 0.65f, 0.25f, 1.0f, top);
    face(0, -1, 0, 0.15f, 0.8f, 0.85f, bottom);
}

static void render_scene_to_fbo(GLuint fbo, float angle)
{
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, 384, 384);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_NORMALIZE);
    if (scene_fog)
        glEnable(GL_FOG);
    else
        glDisable(GL_FOG);
    glFogf(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 3.5f);
    glFogf(GL_FOG_END, 9.0f);
    glClearColor(0.025f, 0.04f, 0.09f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -1, 1, 2, 20);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -5.5f);
    glRotatef(24, 1, 0, 0);
    glRotatef(angle, 0, 1, 0);
    glScalef(1.15f, 1.15f, 1.15f);
    cube();
}

static void compose_scene(GLuint texture, int width, int height)
{
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_LIGHT0);
    glDisable(GL_LIGHT1);
    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_NORMALIZE);
    glDisable(GL_FOG);
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.025f, 0.035f, 0.065f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(-0.94f, -0.82f);
    glTexCoord2f(1, 0);
    glVertex2f(0.12f, -0.82f);
    glTexCoord2f(1, 1);
    glVertex2f(0.12f, 0.82f);
    glTexCoord2f(0, 1);
    glVertex2f(-0.94f, 0.82f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    glColor4f(0.15f, 0.55f, 1.0f, 0.28f);
    glVertex2f(-0.98f, -0.94f);
    glVertex2f(0.17f, -0.94f);
    glColor4f(0.65f, 0.2f, 1.0f, 0.12f);
    glVertex2f(0.17f, -0.86f);
    glVertex2f(-0.98f, -0.86f);
    glEnd();
    glDisable(GL_BLEND);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

static void render_ui(int width, int height, float angle)
{
    static float quality = 0.86f;
    ImGuiIO &io = ImGui::GetIO();

    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = 1.0f / 60.0f;
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(660, 45), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(405, 525), ImGuiCond_Always);
    ImGui::Begin("MesaGL Software Renderer", NULL,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "FRAMEBUFFER PIPELINE ONLINE");
    ImGui::Separator();
    ImGui::Text("Offscreen 3D target");
    ImGui::BulletText("384 x 384 RGBA texture FBO");
    ImGui::BulletText("Depth renderbuffer");
    ImGui::BulletText("Two lights + specular + fog");
    ImGui::BulletText("Texture composited to XRGB8888");
    ImGui::Spacing();
    ImGui::Text("GLES2 UI path");
    ImGui::BulletText("VBO / index buffer / attributes");
    ImGui::BulletText("ES2 ImGui shader subset");
    ImGui::BulletText("Alpha blending + scissor");
    ImGui::Spacing();
    ImGui::Checkbox("Fog enabled in scene", &scene_fog);
    ImGui::SetNextItemWidth(235.0f);
    ImGui::SliderFloat("##quality", &quality, 0.25f, 1.0f, "%.2fx");
    ImGui::SameLine();
    ImGui::Text("Quality %.0f%%", quality * 100.0f);
    ImGui::ProgressBar(quality, ImVec2(-1, 12));
    ImGui::Spacing();
    ImGui::Text("Rotation: %.1f deg", angle);
    ImGui::Text("Backend: framebuffer-only C99 core");
    ImGui::Text("Window port: X11 MIT-SHM reference");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("ESC or close the window to exit");
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

int main(void)
{
    static const GLfloat red_light[] = {1.0f, 0.22f, 0.12f, 1.0f};
    static const GLfloat blue_light[] = {0.12f, 0.35f, 1.0f, 1.0f};
    static const GLfloat red_position[] = {-0.8f, 0.7f, 1.0f, 0.0f};
    static const GLfloat blue_position[] = {0.8f, 0.35f, 1.0f, 0.0f};
    static const GLfloat specular[] = {0.8f, 0.8f, 0.8f, 1.0f};
    static const GLfloat fog_color[] = {0.025f, 0.04f, 0.09f, 1.0f};
    static const struct timespec delay = {0, 16666667};
    const int width = 1100, height = 620;
    MesaGLX11 *x11 = mesaGLX11Create(width, height, "MesaGL integrated framebuffer showcase");
    MesaGLPortContext *context;
    GLuint texture, fbo, depth;
    float angle = 0.0f;

    if (!x11 || !(context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11)))) {
        fprintf(stderr, "Unable to create the X11 framebuffer or MesaGL context\n");
        return 1;
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 384, 384, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 384, 384);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Unable to create the showcase framebuffer object\n");
        return 2;
    }
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_POSITION, red_position);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, red_light);
    glLightfv(GL_LIGHT0, GL_SPECULAR, red_light);
    glLightfv(GL_LIGHT1, GL_POSITION, blue_position);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, blue_light);
    glLightfv(GL_LIGHT1, GL_SPECULAR, blue_light);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specular);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 32.0f);
    glFogfv(GL_FOG_COLOR, fog_color);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    setup_style();
    ImGui::GetIO().IniFilename = NULL;
    if (!ImGui_ImplOpenGL3_Init("#version 100")) {
        fprintf(stderr, "Unable to initialize the Dear ImGui GLES2 backend\n");
        return 3;
    }
    while (mesaGLX11PollEvents(x11)) {
        render_scene_to_fbo(fbo, angle);
        compose_scene(texture, width, height);
        render_ui(width, height, angle);
        mesaGLPortPresent(context);
        angle += 0.65f;
        if (angle >= 360.0f)
            angle -= 360.0f;
        nanosleep(&delay, NULL);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &depth);
    glDeleteTextures(1, &texture);
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
