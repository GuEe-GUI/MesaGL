#include "GLES2/gl2.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"
#include "mesaGL/port.h"
#include "mesaGL_imgui_rtthread_dm.h"
#include "mesaGL_rtthread_dm.h"

#include <rtthread.h>

static int mesaGL_rtthread_dm_imgui(int argc, char **argv)
{
    const char *graphic_name = argc > 1 ? argv[1] : "auto";
    const char *input_name = argc > 2 ? argv[2] : "auto";
    MesaGLRTThreadDM *dm_port;
    MesaGLPortContext *context;
    const MesaGLPortConfig *config;
    rt_tick_t previous_tick;
    bool running = true;
    bool show_demo = true;
    float value = 0.72f;
    char text[64] = "RT-Thread DM input";

    dm_port = mesaGLRTThreadDMCreate(graphic_name, input_name);
    if (!dm_port) {
        rt_kprintf("mesaGL: cannot open RT-Thread DM port\n");
        return -RT_ERROR;
    }
    config = mesaGLRTThreadDMGetPortConfig(dm_port);
    context = mesaGLPortCreate(config);
    if (!context || mesaGLPortMakeCurrent(context) != NTGL_OK) {
        mesaGLPortDestroy(context);
        mesaGLRTThreadDMDestroy(dm_port);
        return -RT_ERROR;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().DisplaySize = ImVec2((float)config->framebuffer.width,
                                        (float)config->framebuffer.height);
    mesaGLImGuiRTThreadDMInit(dm_port);
    if (!ImGui_ImplOpenGL3_Init("#version 100")) {
        mesaGLImGuiRTThreadDMShutdown(dm_port);
        ImGui::DestroyContext();
        mesaGLPortDestroy(context);
        mesaGLRTThreadDMDestroy(dm_port);
        return -RT_ERROR;
    }
    previous_tick = rt_tick_get();
    while (running) {
        rt_tick_t tick = rt_tick_get();
        rt_tick_t elapsed = tick - previous_tick;
        ImGuiIO &io = ImGui::GetIO();

        previous_tick = tick;
        mesaGLRTThreadDMPollEvents(dm_port);
        io.DeltaTime = elapsed ? (float)elapsed / RT_TICK_PER_SECOND
                               : 1.0f / 60.0f;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(390, 300), ImGuiCond_FirstUseEver);
        ImGui::Begin("MesaGL RT-Thread DM");
        ImGui::Text("Framebuffer: %d x %d", config->framebuffer.width,
                    config->framebuffer.height);
        ImGui::Text("Backend: RT-Thread graphic + input DM");
        ImGui::Checkbox("Show Dear ImGui demo", &show_demo);
        ImGui::SliderFloat("Value", &value, 0.0f, 1.0f);
        ImGui::ProgressBar(value);
        ImGui::InputText("Text", text, sizeof(text));
        if (ImGui::Button("Exit") ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            running = false;
        ImGui::End();
        if (show_demo)
            ImGui::ShowDemoWindow(&show_demo);
        ImGui::Render();
        glViewport(0, 0, config->framebuffer.width,
                   config->framebuffer.height);
        glClearColor(0.02f, 0.03f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (mesaGLPortPresent(context) != NTGL_OK)
            break;
        rt_thread_mdelay(1);
    }
    ImGui_ImplOpenGL3_Shutdown();
    mesaGLImGuiRTThreadDMShutdown(dm_port);
    ImGui::DestroyContext();
    mesaGLPortDestroy(context);
    mesaGLRTThreadDMDestroy(dm_port);
    return RT_EOK;
}

MSH_CMD_EXPORT_ALIAS(mesaGL_rtthread_dm_imgui, mesagl_dm_imgui,
                     MesaGL Dear ImGui RT-Thread DM demo);
