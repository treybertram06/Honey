#include "imgui_layer.h"
#include "hnpch.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "../core/engine.h"

// TEMPORARY
#include <GLFW/glfw3.h>
#include <glad/glad.h>

namespace Honey {

     ImGuiLayer::ImGuiLayer() : Layer("ImGuiLayer") {

     }

     ImGuiLayer::~ImGuiLayer() {

     }

    void ImGuiLayer::on_attach() {
     	HN_PROFILE_FUNCTION();

         //setup imgui context
         IMGUI_CHECKVERSION();
         ImGui::CreateContext();
         ImGuiIO& io = ImGui::GetIO(); (void)io;
         io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
         //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
         io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
         io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
         //io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoTaskBarIcons;
         //io.ConfigFlags |= ImGuiConfigFlags_ViewPortsNoMerge;

         //setup style
         ImGui::StyleColorsDark();
         //ImGui::StyleColorsClassic();


         ImGuiStyle& style = ImGui::GetStyle();
         if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
             style.WindowRounding = 0.0f;
             style.Colors[ImGuiCol_WindowBg].w = 1.0f;
         }

         Application& app = Application::get();
         GLFWwindow* window = static_cast<GLFWwindow*>(app.get_window().get_native_window());

         ImGui_ImplGlfw_InitForOpenGL(window, true);
         ImGui_ImplOpenGL3_Init("#version 410");

     }

    void ImGuiLayer::on_detach() {
     	HN_PROFILE_FUNCTION();

         ImGui_ImplOpenGL3_Shutdown();
         ImGui_ImplGlfw_Shutdown();
         ImGui::DestroyContext();
     }

    void ImGuiLayer::begin() {
     	HN_PROFILE_FUNCTION();

         ImGuiIO& io = ImGui::GetIO();
         Application& app = Application::get();
         io.DisplaySize = ImVec2(app.get_window().get_width(), app.get_window().get_height());

         ImGui_ImplOpenGL3_NewFrame();
         ImGui_ImplGlfw_NewFrame();
         ImGui::NewFrame();
    }

    void ImGuiLayer::end() {
     	HN_PROFILE_FUNCTION();

         ImGuiIO& io = ImGui::GetIO();


         ImGui::Render();
         ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

         if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
             GLFWwindow* backup_current_context = glfwGetCurrentContext();
             ImGui::UpdatePlatformWindows();
             ImGui::RenderPlatformWindowsDefault();
             glfwMakeContextCurrent(backup_current_context);
         }
    }

    void ImGuiLayer::on_imgui_render() {
     	HN_PROFILE_FUNCTION();

         static bool show = false;
         //ImGui::ShowDemoWindow(&show);
    }







} // Honey