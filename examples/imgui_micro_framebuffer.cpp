#include "imgui.h"
#include "backends/imgui_impl_opengl2.h"
#include "mesaGL/port.h"

#include <stdint.h>
#include <stdio.h>

int main(void)
{
    static uint16_t pixels[320 * 240];
    MesaGLPortConfig config = {{pixels, 320, 240, 320 * 2, NTGL_RGB565, NTGL_ORIGIN_TOP_LEFT},
                               {NULL, NULL, NULL},
                               NULL,
                               NULL};
    MesaGLPortContext *gl_context = mesaGLPortCreate(&config);
    int changed = 0;
    if (!gl_context)
        return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(320.0f, 240.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = NULL;
    if (!ImGui_ImplOpenGL2_Init())
        return 2;

    ImGui_ImplOpenGL2_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20, 20));
    ImGui::SetNextWindowSize(ImVec2(280, 180));
    ImGui::Begin("mesaGL micro framebuffer");
    ImGui::Text("No EGL, GLVND, DRM or operating system backend");
    ImGui::Text("Direct RGB565 rendering");
    ImGui::Button("Working");
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    if (mesaGLPortPresent(gl_context) != NTGL_OK)
        return 3;

    for (unsigned i = 0; i < 320u * 240u; ++i)
        if (pixels[i] != 0)
            ++changed;
    printf("Dear ImGui micro rendered %d non-black RGB565 pixels\n", changed);

    ImGui_ImplOpenGL2_Shutdown();
    ImGui::DestroyContext();
    mesaGLPortDestroy(gl_context);
    return changed > 5000 ? 0 : 4;
}
