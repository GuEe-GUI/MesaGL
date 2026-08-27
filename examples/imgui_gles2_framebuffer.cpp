#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "mesaGL/port.h"

#include <stdint.h>
#include <stdio.h>

int main(void)
{
    static uint16_t pixels[320 * 240];
    MesaGLPortConfig config = {
        {pixels, 320, 240, 320 * 2, NTGL_RGB565, NTGL_ORIGIN_TOP_LEFT},
        {NULL, NULL, NULL},
        NULL,
        NULL,
        {NULL, NULL},
    };
    MesaGLPortContext *gl_context = mesaGLPortCreate(&config);
    int changed = 0;
    uint64_t checksum = UINT64_C(1469598103934665603);
    unsigned i;
    if (!gl_context)
        return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(320.0f, 240.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = NULL;
    if (!ImGui_ImplOpenGL3_Init("#version 100"))
        return 2;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20, 20));
    ImGui::SetNextWindowSize(ImVec2(280, 180));
    ImGui::Begin("mesaGL GLES2 framebuffer");
    ImGui::Text("Programmable UI pipeline");
    ImGui::Text("GLSL ES 1.00 subset, VBO and EBO");
    ImGui::Button("Working");
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    for (i = 0; i < 320u * 240u; ++i) {
        if (pixels[i])
            ++changed;
        checksum ^= pixels[i] & 0xffu;
        checksum *= UINT64_C(1099511628211);
        checksum ^= pixels[i] >> 8;
        checksum *= UINT64_C(1099511628211);
    }
    printf("Dear ImGui GLES2 rendered %d non-black RGB565 pixels, "
           "checksum %016llx\n", changed, (unsigned long long)checksum);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    mesaGLPortDestroy(gl_context);
    return changed > 5000 ? 0 : 3;
}
