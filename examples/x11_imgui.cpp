#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "GLES2/gl2.h"
#include "mesaGL/port.h"
#include "mesaGL_x11.h"

#include <stdio.h>

static void setup_style(void)
{
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(20, 18);
    style.FramePadding = ImVec2(12, 8);
    style.ItemSpacing = ImVec2(10, 10);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.WindowRounding = 12.0f;
    style.ChildRounding = 9.0f;
    style.FrameRounding = 7.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding = 7.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.91f, 0.93f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.53f, 0.63f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.067f, 0.102f, 0.98f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.090f, 0.135f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.065f, 0.078f, 0.116f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.19f, 0.23f, 0.34f, 0.75f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.105f, 0.126f, 0.184f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.19f, 0.29f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.19f, 0.24f, 0.37f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.055f, 0.067f, 0.102f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.075f, 0.090f, 0.135f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.27f, 0.39f, 0.92f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.34f, 0.47f, 1.00f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.33f, 0.82f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.22f, 0.31f, 0.64f, 0.65f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.29f, 0.40f, 0.82f, 0.75f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.45f, 0.74f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.35f, 0.55f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.33f, 0.68f, 1.00f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.17f, 0.21f, 0.31f, 1.00f);
}

static void render_frame(MesaGLPortContext *context, int width, int height)
{
    static float quality = 0.82f;
    static bool antialiasing = true;
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = 1.0f / 60.0f;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(48, 38), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(544, 400), ImGuiCond_Once);
    ImGui::Begin("mesaGL Control Center");
    ImGui::TextColored(ImVec4(0.43f, 0.72f, 1.0f, 1.0f), "SOFTWARE RENDERER");
    ImGui::SameLine();
    ImGui::TextDisabled(" / FRAMEBUFFER DASHBOARD");
    ImGui::Spacing();
    ImGui::Text("A compact GLES2-style UI rendered entirely on the CPU.");
    ImGui::Spacing();
    ImGui::Separator();

    ImGui::BeginChild("status", ImVec2(0, 92), true);
    ImGui::TextDisabled("DISPLAY STATUS");
    ImGui::TextColored(ImVec4(0.38f, 0.91f, 0.65f, 1.0f), "● ONLINE");
    ImGui::SameLine(150);
    ImGui::Text("%d x %d", width, height);
    ImGui::SameLine(285);
    ImGui::Text("XRGB8888");
    ImGui::TextDisabled("MIT-SHM framebuffer  |  imgui_impl_opengl3 ES2");
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::TextDisabled("RENDER SETTINGS");
    ImGui::Checkbox("Antialiasing", &antialiasing);
    ImGui::SliderFloat("Quality", &quality, 0.25f, 1.0f, "%.0f%%");
    ImGui::ProgressBar(quality, ImVec2(-1, 10), "");
    ImGui::Spacing();
    if (ImGui::Button("Apply settings", ImVec2(170, 38))) {
    }
    ImGui::SameLine();
    ImGui::TextDisabled("CPU rasterizer ready");
    ImGui::Separator();
    ImGui::TextDisabled("ESC to exit  •  mesaGL UI pipeline");
    ImGui::End();
    ImGui::Render();

    glClearColor(0.06f, 0.08f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    mesaGLPortPresent(context);
}

int main(void)
{
    const int width = 640;
    const int height = 480;
    MesaGLX11 *x11 = mesaGLX11Create(width, height, "mesaGL Dear ImGui");
    MesaGLPortContext *context;
    if (!x11) {
        fprintf(stderr, "Unable to create X11 framebuffer; check DISPLAY and MIT-SHM\n");
        return 1;
    }
    context = mesaGLPortCreate(mesaGLX11GetPortConfig(x11));
    if (!context) {
        mesaGLX11Destroy(x11);
        return 2;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    setup_style();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = 1.0f / 60.0f;
    ImFontConfig font_config;
    font_config.SizePixels = 17.0f;
    io.Fonts->AddFontDefault(&font_config);
    if (!ImGui_ImplOpenGL3_Init("#version 100"))
        return 3;

    render_frame(context, width, height);
    while (mesaGLX11WaitEvent(x11))
        render_frame(context, width, height);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    mesaGLPortDestroy(context);
    mesaGLX11Destroy(x11);
    return 0;
}
