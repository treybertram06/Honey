#include "imgui_layer.h"
#include "hnpch.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "../core/engine.h"

// TEMPORARY
#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <ImGuizmo.h>

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

         io.Fonts->AddFontFromFileTTF("../assets/fonts/JetBrains_Mono/static/JetBrainsMono-Bold.ttf", 18.0f);
         io.Fonts->AddFontFromFileTTF("../assets/fonts/JetBrains_Mono/static/JetBrainsMono-ExtraBold.ttf", 18.0f);
         io.FontDefault = io.Fonts->AddFontFromFileTTF("../assets/fonts/JetBrains_Mono/static/JetBrainsMono-Regular.ttf", 18.0f);

         //setup style
         ImGui::StyleColorsDark();
         //ImGui::StyleColorsClassic();


         ImGuiStyle& style = ImGui::GetStyle();
         if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
             style.WindowRounding = 0.0f;
             style.Colors[ImGuiCol_WindowBg].w = 1.0f;
         }

         set_theme(UITheme::HoneyAmber);

         Application& app = Application::get();
         GLFWwindow* window = static_cast<GLFWwindow*>(app.get_window().get_native_window());

         m_renderer = ImGuiRenderer::create(window);

     }

    void ImGuiLayer::on_detach() {
     	HN_PROFILE_FUNCTION();

         m_renderer->shutdown();
         ImGui::DestroyContext();
     }

    void ImGuiLayer::on_event(Event& e) {
         HN_PROFILE_FUNCTION();

         if (!m_block_events)
             return;

         ImGuiIO& io = ImGui::GetIO();

         if (e.is_in_category(event_category_mouse) ||
             e.is_in_category(event_category_mouse_button)) {
             if (io.WantCaptureMouse)
                 e.set_handled(true);
         }

         if (e.is_in_category(event_category_keyboard)) {
             if (io.WantCaptureKeyboard)
                 e.set_handled(true);
         }

    }

    void ImGuiLayer::begin() {
     	HN_PROFILE_FUNCTION();

        ImGuiIO& io = ImGui::GetIO();
        Application& app = Application::get();
        io.DisplaySize = ImVec2(app.get_window().get_width(), app.get_window().get_height());

        m_renderer->new_frame();
        ImGuizmo::BeginFrame();
    }

    void ImGuiLayer::end() {
     	HN_PROFILE_FUNCTION();

         ImGuiIO& io = ImGui::GetIO();


         ImGui::Render();
         m_renderer->render_draw_data(ImGui::GetDrawData());

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

    void ImGuiLayer::set_theme(UITheme theme) {
         m_current_theme = theme;
         ImGuiStyle& style = ImGui::GetStyle();
         ImVec4* colors = style.Colors;

         switch (theme) {
             case UITheme::Monochrome:
                 colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.105f, 0.11f, 1.0f);

                 // Headers
                 colors[ImGuiCol_Header] = ImVec4(0.2f, 0.205f, 0.21f, 1.0f);
                 colors[ImGuiCol_HeaderHovered] = ImVec4(0.3f, 0.305f, 0.31f, 1.0f);
                 colors[ImGuiCol_HeaderActive] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);

                 // Buttons
                 colors[ImGuiCol_Button] = ImVec4(0.2f, 0.205f, 0.21f, 1.0f);
                 colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.305f, 0.31f, 1.0f);
                 colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
                 colors[ImGuiCol_CheckMark] = ImVec4(0.90f, 0.90f, 0.90f, 1.0f);

                 // Frame bg
                 colors[ImGuiCol_FrameBg] = ImVec4(0.2f, 0.205f, 0.21f, 1.0f);
                 colors[ImGuiCol_FrameBgHovered] = ImVec4(0.3f, 0.305f, 0.31f, 1.0f);
                 colors[ImGuiCol_FrameBgActive] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);

                 // Tabs
                 colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
                 colors[ImGuiCol_TabHovered] = ImVec4(0.38f, 0.3805f, 0.381f, 1.0f);
                 colors[ImGuiCol_TabActive] = ImVec4(0.28f, 0.2805f, 0.281f, 1.0f);
                 colors[ImGuiCol_TabUnfocused] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
                 colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.2f, 0.205f, 0.21f, 1.0f);
                 colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);

                 colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
                 colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
                 colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
                 break;

             case UITheme::HoneyAmber:
                 colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.06f, 0.04f, 1.0f);

                 // Headers
                 colors[ImGuiCol_Header] = ImVec4(0.25f, 0.18f, 0.10f, 1.0f);
                 colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.25f, 0.15f, 1.0f);
                 colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.14f, 0.08f, 1.0f);

                 // Buttons
                 colors[ImGuiCol_Button] = ImVec4(0.25f, 0.18f, 0.10f, 1.0f);
                 colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.25f, 0.15f, 1.0f);
                 colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.14f, 0.08f, 1.0f);
                 colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.75f, 0.35f, 1.0f);


                 // Frame bg
                 colors[ImGuiCol_FrameBg] = ImVec4(0.25f, 0.18f, 0.10f, 1.0f);
                 colors[ImGuiCol_FrameBgHovered] = ImVec4(0.35f, 0.25f, 0.15f, 1.0f);
                 colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.14f, 0.08f, 1.0f);

                 // Tabs
                 colors[ImGuiCol_Tab] = ImVec4(0.20f, 0.14f, 0.08f, 1.0f);
                 colors[ImGuiCol_TabHovered] = ImVec4(0.45f, 0.32f, 0.18f, 1.0f);
                 colors[ImGuiCol_TabActive] = ImVec4(0.35f, 0.25f, 0.15f, 1.0f);
                 colors[ImGuiCol_TabUnfocused] = ImVec4(0.20f, 0.14f, 0.08f, 1.0f);
                 colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.25f, 0.18f, 0.10f, 1.0f);
                 colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.95f, 0.75f, 0.35f, 1.0f);

                 colors[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.14f, 0.08f, 1.0f);
                 colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.14f, 0.08f, 1.0f);
                 colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.20f, 0.14f, 0.08f, 1.0f);
                 break;

             case UITheme::ForestGreen:
                 colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.07f, 0.05f, 1.0f);

                 // Headers
                 colors[ImGuiCol_Header] = ImVec4(0.15f, 0.20f, 0.12f, 1.0f);
                 colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.30f, 0.18f, 1.0f);
                 colors[ImGuiCol_HeaderActive] = ImVec4(0.12f, 0.16f, 0.10f, 1.0f);

                 // Buttons
                 colors[ImGuiCol_Button] = ImVec4(0.15f, 0.20f, 0.12f, 1.0f);
                 colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.30f, 0.18f, 1.0f);
                 colors[ImGuiCol_ButtonActive] = ImVec4(0.12f, 0.16f, 0.10f, 1.0f);
                 colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.85f, 0.25f, 1.0f);

                 // Frame bg
                 colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.20f, 0.12f, 1.0f);
                 colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.30f, 0.18f, 1.0f);
                 colors[ImGuiCol_FrameBgActive] = ImVec4(0.12f, 0.16f, 0.10f, 1.0f);

                 // Tabs
                 colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.16f, 0.10f, 1.0f);
                 colors[ImGuiCol_TabHovered] = ImVec4(0.32f, 0.38f, 0.25f, 1.0f);
                 colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.28f, 0.18f, 1.0f);
                 colors[ImGuiCol_TabUnfocused] = ImVec4(0.12f, 0.16f, 0.10f, 1.0f);
                 colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.20f, 0.12f, 1.0f);
                 colors[ImGuiCol_TabSelectedOverline] = ImVec4(1.0f, 0.85f, 0.25f, 1.0f);

                 colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.16f, 0.10f, 1.0f);
                 colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.16f, 0.10f, 1.0f);
                 colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.12f, 0.16f, 0.10f, 1.0f);
                 break;

             case UITheme::CaramelCream:
                 colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.10f, 0.08f, 1.0f);

                 // Headers
                 colors[ImGuiCol_Header] = ImVec4(0.28f, 0.22f, 0.16f, 1.0f);
                 colors[ImGuiCol_HeaderHovered] = ImVec4(0.38f, 0.30f, 0.22f, 1.0f);
                 colors[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.18f, 0.13f, 1.0f);

                 // Buttons
                 colors[ImGuiCol_Button] = ImVec4(0.28f, 0.22f, 0.16f, 1.0f);
                 colors[ImGuiCol_ButtonHovered] = ImVec4(0.38f, 0.30f, 0.22f, 1.0f);
                 colors[ImGuiCol_ButtonActive] = ImVec4(0.24f, 0.18f, 0.13f, 1.0f);
                 colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.88f, 0.70f, 1.0f);

                 // Frame bg
                 colors[ImGuiCol_FrameBg] = ImVec4(0.28f, 0.22f, 0.16f, 1.0f);
                 colors[ImGuiCol_FrameBgHovered] = ImVec4(0.38f, 0.30f, 0.22f, 1.0f);
                 colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.18f, 0.13f, 1.0f);

                 // Tabs
                 colors[ImGuiCol_Tab] = ImVec4(0.24f, 0.18f, 0.13f, 1.0f);
                 colors[ImGuiCol_TabHovered] = ImVec4(0.48f, 0.38f, 0.28f, 1.0f);
                 colors[ImGuiCol_TabActive] = ImVec4(0.38f, 0.30f, 0.22f, 1.0f);
                 colors[ImGuiCol_TabUnfocused] = ImVec4(0.24f, 0.18f, 0.13f, 1.0f);
                 colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.28f, 0.22f, 0.16f, 1.0f);
                 colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.85f, 0.65f, 0.40f, 1.0f);

                 colors[ImGuiCol_TitleBg] = ImVec4(0.24f, 0.18f, 0.13f, 1.0f);
                 colors[ImGuiCol_TitleBgActive] = ImVec4(0.24f, 0.18f, 0.13f, 1.0f);
                 colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.24f, 0.18f, 0.13f, 1.0f);
                 break;

             case UITheme::MaltAndHops:
                 colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.08f, 0.06f, 1.0f);

                 // Headers
                 colors[ImGuiCol_Header] = ImVec4(0.20f, 0.16f, 0.12f, 1.0f);
                 colors[ImGuiCol_HeaderHovered] = ImVec4(0.16f, 0.22f, 0.16f, 1.0f);
                 colors[ImGuiCol_HeaderActive] = ImVec4(0.16f, 0.12f, 0.10f, 1.0f);

                 // Buttons
                 colors[ImGuiCol_Button] = ImVec4(0.20f, 0.16f, 0.12f, 1.0f);
                 colors[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.22f, 0.16f, 1.0f);
                 colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.12f, 0.10f, 1.0f);
                 colors[ImGuiCol_CheckMark] = ImVec4(0.65f, 0.85f, 0.45f, 1.0f);

                 // Frame bg
                 colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.16f, 0.12f, 1.0f);
                 colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.22f, 0.16f, 1.0f);
                 colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.12f, 0.10f, 1.0f);

                 // Tabs
                 colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.12f, 0.10f, 1.0f);
                 colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.32f, 0.26f, 1.0f);
                 colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.26f, 0.20f, 1.0f);
                 colors[ImGuiCol_TabUnfocused] = ImVec4(0.16f, 0.12f, 0.10f, 1.0f);
                 colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.20f, 0.16f, 0.12f, 1.0f);
                 colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.65f, 0.85f, 0.45f, 1.0f);

                 colors[ImGuiCol_TitleBg] = ImVec4(0.16f, 0.12f, 0.10f, 1.0f);
                 colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.12f, 0.10f, 1.0f);
                 colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.16f, 0.12f, 0.10f, 1.0f);
                 break;

             case UITheme::Copper:
                 colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.05f, 0.04f, 1.0f);

                 // Headers
                 colors[ImGuiCol_Header] = ImVec4(0.25f, 0.15f, 0.10f, 1.0f);
                 colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.20f, 0.12f, 1.0f);
                 colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.12f, 0.08f, 1.0f);

                 // Buttons
                 colors[ImGuiCol_Button] = ImVec4(0.25f, 0.15f, 0.10f, 1.0f);
                 colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.20f, 0.12f, 1.0f);
                 colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.12f, 0.08f, 1.0f);
                 colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.60f, 0.30f, 1.0f);

                 // Frame bg
                 colors[ImGuiCol_FrameBg] = ImVec4(0.25f, 0.15f, 0.10f, 1.0f);
                 colors[ImGuiCol_FrameBgHovered] = ImVec4(0.35f, 0.20f, 0.12f, 1.0f);
                 colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.12f, 0.08f, 1.0f);

                 // Tabs
                 colors[ImGuiCol_Tab] = ImVec4(0.20f, 0.12f, 0.08f, 1.0f);
                 colors[ImGuiCol_TabHovered] = ImVec4(0.45f, 0.25f, 0.15f, 1.0f);
                 colors[ImGuiCol_TabActive] = ImVec4(0.35f, 0.20f, 0.12f, 1.0f);
                 colors[ImGuiCol_TabUnfocused] = ImVec4(0.20f, 0.12f, 0.08f, 1.0f);
                 colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.25f, 0.15f, 0.10f, 1.0f);
                 colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.95f, 0.60f, 0.30f, 1.0f);

                 colors[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.12f, 0.08f, 1.0f);
                 colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.12f, 0.08f, 1.0f);
                 colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.20f, 0.12f, 0.08f, 1.0f);
                 break;

             case UITheme::AudreysTheme:
                 colors[ImGuiCol_WindowBg] = ImVec4(0.18f, 0.12f, 0.14f, 1.0f);

                 // Headers
                 colors[ImGuiCol_Header] = ImVec4(0.38f, 0.30f, 0.32f, 1.0f);
                 colors[ImGuiCol_HeaderHovered] = ImVec4(0.48f, 0.38f, 0.40f, 1.0f);
                 colors[ImGuiCol_HeaderActive] = ImVec4(0.32f, 0.24f, 0.26f, 1.0f);

                 // Buttons
                 colors[ImGuiCol_Button] = ImVec4(0.38f, 0.30f, 0.32f, 1.0f);
                 colors[ImGuiCol_ButtonHovered] = ImVec4(0.48f, 0.38f, 0.40f, 1.0f);
                 colors[ImGuiCol_ButtonActive] = ImVec4(0.32f, 0.24f, 0.26f, 1.0f);
                 colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.75f, 0.80f, 1.0f);

                 // Frame bg
                 colors[ImGuiCol_FrameBg] = ImVec4(0.38f, 0.30f, 0.32f, 1.0f);
                 colors[ImGuiCol_FrameBgHovered] = ImVec4(0.48f, 0.38f, 0.40f, 1.0f);
                 colors[ImGuiCol_FrameBgActive] = ImVec4(0.32f, 0.24f, 0.26f, 1.0f);

                 // Tabs
                 colors[ImGuiCol_Tab] = ImVec4(0.32f, 0.24f, 0.26f, 1.0f);
                 colors[ImGuiCol_TabHovered] = ImVec4(0.55f, 0.45f, 0.48f, 1.0f);
                 colors[ImGuiCol_TabActive] = ImVec4(0.48f, 0.38f, 0.40f, 1.0f);
                 colors[ImGuiCol_TabUnfocused] = ImVec4(0.32f, 0.24f, 0.26f, 1.0f);
                 colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.38f, 0.30f, 0.32f, 1.0f);
                 colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.90f, 0.68f, 0.75f, 1.0f);

                 colors[ImGuiCol_TitleBg] = ImVec4(0.32f, 0.24f, 0.26f, 1.0f);
                 colors[ImGuiCol_TitleBgActive] = ImVec4(0.32f, 0.24f, 0.26f, 1.0f);
                 colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.32f, 0.24f, 0.26f, 1.0f);
                 break;



         }

     }

    const char* ImGuiLayer::get_theme_name(UITheme theme) const {
         switch (theme) {
             case UITheme::Monochrome: return "Monochrome";
             case UITheme::HoneyAmber: return "Honey Amber";
             case UITheme::ForestGreen: return "Forest Green";
             case UITheme::CaramelCream: return "Caramel Cream";
             case UITheme::MaltAndHops: return "Malt & Hops";
             case UITheme::Copper: return "Copper";
             case UITheme::AudreysTheme: return "Audrey's Theme";
             default: return "Unknown";
         }
     }










} // Honey