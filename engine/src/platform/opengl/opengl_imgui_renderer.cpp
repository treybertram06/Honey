#include "hnpch.h"
#include "opengl_imgui_renderer.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_glfw.h"
#include <GLFW/glfw3.h>

namespace Honey {
    void OpenGLImGuiRenderer::init(void *window) {
        // Assumes window is a GLFWwindow*
        ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow*>(window), true);
        ImGui_ImplOpenGL3_Init("#version 450");
    }

    void OpenGLImGuiRenderer::shutdown() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
    }

    void OpenGLImGuiRenderer::new_frame() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void OpenGLImGuiRenderer::render_draw_data(ImDrawData *draw_data) {
        ImGui_ImplOpenGL3_RenderDrawData(draw_data);
    }

    void OpenGLImGuiRenderer::update_platform_windows() {
        ImGui::UpdatePlatformWindows();
    }

    void OpenGLImGuiRenderer::render_platform_windows_default() {
        ImGui::RenderPlatformWindowsDefault();
    }
}
