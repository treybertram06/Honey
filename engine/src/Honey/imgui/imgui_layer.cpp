#include "imgui_layer.h"
#include "hnpch.h"

#include "GLFW/glfw3.h"
#include "Honey/engine.h"
#include "imgui_impl_opengl3_loader.h"
#include "imgui_internal.h"
#include "platform/opengl/imgui_impl_opengl3.h"

namespace Honey {

     ImGuiLayer::ImGuiLayer() : Layer("ImGuiLayer") {

     }

     ImGuiLayer::~ImGuiLayer() {

     }

    void ImGuiLayer::on_attach() {
         ImGui::CreateContext();
         ImGui::StyleColorsDark();

         ImGuiIO& io = ImGui::GetIO();
         io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
         io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;



        ImGui_ImplOpenGL3_Init("#version 410");

     }

    void ImGuiLayer::on_detach() {

     }

    void ImGuiLayer::on_update() {

         ImGuiIO& io = ImGui::GetIO();
         Application& app = Application::get();
         io.DisplaySize = ImVec2(app.get_window().get_width(), app.get_window().get_height());

         float time = (float)glfwGetTime();
         io.DeltaTime = m_time > 0.0 ? (time - m_time) : (1.0f / 60.0f);
         m_time = time;

         ImGui_ImplOpenGL3_NewFrame();
         ImGui::NewFrame();

         static bool show = true;
         ImGui::ShowDemoWindow(&show);

         ImGui::Render();
         ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

     }

    void ImGuiLayer::on_event(Event &event) {
         EventDispatcher dispatcher(event);
         dispatcher.dispatch<MouseButtonPressedEvent>(HN_BIND_EVENT_FN(ImGuiLayer::on_mouse_button_pressed_event));
         dispatcher.dispatch<MouseButtonReleasedEvent>(HN_BIND_EVENT_FN(ImGuiLayer::on_mouse_button_released_event));
         dispatcher.dispatch<MouseMovedEvent>(HN_BIND_EVENT_FN(ImGuiLayer::on_mouse_moved_event));
         dispatcher.dispatch<MouseScrolledEvent>(HN_BIND_EVENT_FN(ImGuiLayer::on_mouse_scrolled_event));
         dispatcher.dispatch<KeyPressedEvent>(HN_BIND_EVENT_FN(ImGuiLayer::on_key_pressed_event));
         dispatcher.dispatch<KeyTypedEvent>(HN_BIND_EVENT_FN(ImGuiLayer::on_key_typed_event));
         dispatcher.dispatch<KeyReleasedEvent>(HN_BIND_EVENT_FN(ImGuiLayer::on_key_released_event));
         dispatcher.dispatch<WindowResizeEvent>(HN_BIND_EVENT_FN(ImGuiLayer::on_window_resized_event));

     }

    bool ImGuiLayer::on_mouse_button_pressed_event(MouseButtonPressedEvent &e) {
         ImGuiIO& io = ImGui::GetIO();
         io.MouseDown[e.get_mouse_button()] = true;

         return false;
     }

    bool ImGuiLayer::on_mouse_button_released_event(MouseButtonReleasedEvent &e) {
         ImGuiIO& io = ImGui::GetIO();
         io.MouseDown[e.get_mouse_button()] = false;

         return false;
     }

    bool ImGuiLayer::on_mouse_moved_event(MouseMovedEvent &e) {
         ImGuiIO& io = ImGui::GetIO();
         io.MousePos = ImVec2(e.get_x(), e.get_y());

         return false;
     }

    bool ImGuiLayer::on_mouse_scrolled_event(MouseScrolledEvent &e) {
         ImGuiIO& io = ImGui::GetIO();
         io.MouseWheelH += e.get_xoffset();
         io.MouseWheel += e.get_yoffset();

         return false;
     }

    ImGuiKey GLFWKeyToImGuiKey(int keycode);
    bool ImGuiLayer::on_key_pressed_event(KeyPressedEvent& e)
     {
         ImGuiIO& io  = ImGui::GetIO();
         ImGuiKey key = GLFWKeyToImGuiKey(e.get_key_code());

         IM_ASSERT(ImGui::IsNamedKeyOrMod(key));

         io.AddKeyEvent(key, true);

         io.AddKeyEvent(ImGuiKey_ModCtrl,  ImGui::IsKeyDown(ImGuiKey_LeftCtrl)  || ImGui::IsKeyDown(ImGuiKey_RightCtrl));
         io.AddKeyEvent(ImGuiKey_ModShift, ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift));
         io.AddKeyEvent(ImGuiKey_ModAlt,   ImGui::IsKeyDown(ImGuiKey_LeftAlt)   || ImGui::IsKeyDown(ImGuiKey_RightAlt));
         io.AddKeyEvent(ImGuiKey_ModSuper, ImGui::IsKeyDown(ImGuiKey_LeftSuper) || ImGui::IsKeyDown(ImGuiKey_RightSuper));

         return false;
     }

    bool ImGuiLayer::on_key_released_event(KeyReleasedEvent &e) {
        ImGuiIO& io  = ImGui::GetIO();
        ImGuiKey key = GLFWKeyToImGuiKey(e.get_key_code());
        IM_ASSERT(ImGui::IsNamedKeyOrMod(key));
        io.AddKeyEvent(key, false);

        // re-publish modifiers again
        io.AddKeyEvent(ImGuiKey_ModCtrl,  ImGui::IsKeyDown(ImGuiKey_LeftCtrl)  || ImGui::IsKeyDown(ImGuiKey_RightCtrl));
        io.AddKeyEvent(ImGuiKey_ModShift, ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift));
        io.AddKeyEvent(ImGuiKey_ModAlt,   ImGui::IsKeyDown(ImGuiKey_LeftAlt)   || ImGui::IsKeyDown(ImGuiKey_RightAlt));
        io.AddKeyEvent(ImGuiKey_ModSuper, ImGui::IsKeyDown(ImGuiKey_LeftSuper) || ImGui::IsKeyDown(ImGuiKey_RightSuper));

        return false;
     }

    bool ImGuiLayer::on_key_typed_event(KeyTypedEvent &e) {
         ImGuiIO& io = ImGui::GetIO();
         int keycode = e.get_key_code();
         if (keycode > 0 && keycode < 0x10000)
             io.AddInputCharacter((unsigned short)keycode);

         return false;
     }


    bool ImGuiLayer::on_window_resized_event(WindowResizeEvent &e) {
         ImGuiIO& io = ImGui::GetIO();
         io.DisplaySize = ImVec2(e.get_width(), e.get_height());
         io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
         glViewport(0, 0, e.get_width(), e.get_height());

         return false;
     }

ImGuiKey GLFWKeyToImGuiKey(int keycode)
{
    //IM_UNUSED(scancode);
    switch (keycode)
    {
        case GLFW_KEY_TAB: return ImGuiKey_Tab;
        case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
        case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
        case GLFW_KEY_UP: return ImGuiKey_UpArrow;
        case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
        case GLFW_KEY_PAGE_UP: return ImGuiKey_PageUp;
        case GLFW_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
        case GLFW_KEY_HOME: return ImGuiKey_Home;
        case GLFW_KEY_END: return ImGuiKey_End;
        case GLFW_KEY_INSERT: return ImGuiKey_Insert;
        case GLFW_KEY_DELETE: return ImGuiKey_Delete;
        case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
        case GLFW_KEY_SPACE: return ImGuiKey_Space;
        case GLFW_KEY_ENTER: return ImGuiKey_Enter;
        case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
        case GLFW_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
        case GLFW_KEY_COMMA: return ImGuiKey_Comma;
        case GLFW_KEY_MINUS: return ImGuiKey_Minus;
        case GLFW_KEY_PERIOD: return ImGuiKey_Period;
        case GLFW_KEY_SLASH: return ImGuiKey_Slash;
        case GLFW_KEY_SEMICOLON: return ImGuiKey_Semicolon;
        case GLFW_KEY_EQUAL: return ImGuiKey_Equal;
        case GLFW_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
        case GLFW_KEY_BACKSLASH: return ImGuiKey_Backslash;
        case GLFW_KEY_WORLD_1: return ImGuiKey_Oem102;
        case GLFW_KEY_WORLD_2: return ImGuiKey_Oem102;
        case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
        case GLFW_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
        case GLFW_KEY_CAPS_LOCK: return ImGuiKey_CapsLock;
        case GLFW_KEY_SCROLL_LOCK: return ImGuiKey_ScrollLock;
        case GLFW_KEY_NUM_LOCK: return ImGuiKey_NumLock;
        case GLFW_KEY_PRINT_SCREEN: return ImGuiKey_PrintScreen;
        case GLFW_KEY_PAUSE: return ImGuiKey_Pause;
        case GLFW_KEY_KP_0: return ImGuiKey_Keypad0;
        case GLFW_KEY_KP_1: return ImGuiKey_Keypad1;
        case GLFW_KEY_KP_2: return ImGuiKey_Keypad2;
        case GLFW_KEY_KP_3: return ImGuiKey_Keypad3;
        case GLFW_KEY_KP_4: return ImGuiKey_Keypad4;
        case GLFW_KEY_KP_5: return ImGuiKey_Keypad5;
        case GLFW_KEY_KP_6: return ImGuiKey_Keypad6;
        case GLFW_KEY_KP_7: return ImGuiKey_Keypad7;
        case GLFW_KEY_KP_8: return ImGuiKey_Keypad8;
        case GLFW_KEY_KP_9: return ImGuiKey_Keypad9;
        case GLFW_KEY_KP_DECIMAL: return ImGuiKey_KeypadDecimal;
        case GLFW_KEY_KP_DIVIDE: return ImGuiKey_KeypadDivide;
        case GLFW_KEY_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
        case GLFW_KEY_KP_SUBTRACT: return ImGuiKey_KeypadSubtract;
        case GLFW_KEY_KP_ADD: return ImGuiKey_KeypadAdd;
        case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
        case GLFW_KEY_KP_EQUAL: return ImGuiKey_KeypadEqual;
        case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
        case GLFW_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
        case GLFW_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
        case GLFW_KEY_LEFT_SUPER: return ImGuiKey_LeftSuper;
        case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
        case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
        case GLFW_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
        case GLFW_KEY_RIGHT_SUPER: return ImGuiKey_RightSuper;
        case GLFW_KEY_MENU: return ImGuiKey_Menu;
        case GLFW_KEY_0: return ImGuiKey_0;
        case GLFW_KEY_1: return ImGuiKey_1;
        case GLFW_KEY_2: return ImGuiKey_2;
        case GLFW_KEY_3: return ImGuiKey_3;
        case GLFW_KEY_4: return ImGuiKey_4;
        case GLFW_KEY_5: return ImGuiKey_5;
        case GLFW_KEY_6: return ImGuiKey_6;
        case GLFW_KEY_7: return ImGuiKey_7;
        case GLFW_KEY_8: return ImGuiKey_8;
        case GLFW_KEY_9: return ImGuiKey_9;
        case GLFW_KEY_A: return ImGuiKey_A;
        case GLFW_KEY_B: return ImGuiKey_B;
        case GLFW_KEY_C: return ImGuiKey_C;
        case GLFW_KEY_D: return ImGuiKey_D;
        case GLFW_KEY_E: return ImGuiKey_E;
        case GLFW_KEY_F: return ImGuiKey_F;
        case GLFW_KEY_G: return ImGuiKey_G;
        case GLFW_KEY_H: return ImGuiKey_H;
        case GLFW_KEY_I: return ImGuiKey_I;
        case GLFW_KEY_J: return ImGuiKey_J;
        case GLFW_KEY_K: return ImGuiKey_K;
        case GLFW_KEY_L: return ImGuiKey_L;
        case GLFW_KEY_M: return ImGuiKey_M;
        case GLFW_KEY_N: return ImGuiKey_N;
        case GLFW_KEY_O: return ImGuiKey_O;
        case GLFW_KEY_P: return ImGuiKey_P;
        case GLFW_KEY_Q: return ImGuiKey_Q;
        case GLFW_KEY_R: return ImGuiKey_R;
        case GLFW_KEY_S: return ImGuiKey_S;
        case GLFW_KEY_T: return ImGuiKey_T;
        case GLFW_KEY_U: return ImGuiKey_U;
        case GLFW_KEY_V: return ImGuiKey_V;
        case GLFW_KEY_W: return ImGuiKey_W;
        case GLFW_KEY_X: return ImGuiKey_X;
        case GLFW_KEY_Y: return ImGuiKey_Y;
        case GLFW_KEY_Z: return ImGuiKey_Z;
        case GLFW_KEY_F1: return ImGuiKey_F1;
        case GLFW_KEY_F2: return ImGuiKey_F2;
        case GLFW_KEY_F3: return ImGuiKey_F3;
        case GLFW_KEY_F4: return ImGuiKey_F4;
        case GLFW_KEY_F5: return ImGuiKey_F5;
        case GLFW_KEY_F6: return ImGuiKey_F6;
        case GLFW_KEY_F7: return ImGuiKey_F7;
        case GLFW_KEY_F8: return ImGuiKey_F8;
        case GLFW_KEY_F9: return ImGuiKey_F9;
        case GLFW_KEY_F10: return ImGuiKey_F10;
        case GLFW_KEY_F11: return ImGuiKey_F11;
        case GLFW_KEY_F12: return ImGuiKey_F12;
        case GLFW_KEY_F13: return ImGuiKey_F13;
        case GLFW_KEY_F14: return ImGuiKey_F14;
        case GLFW_KEY_F15: return ImGuiKey_F15;
        case GLFW_KEY_F16: return ImGuiKey_F16;
        case GLFW_KEY_F17: return ImGuiKey_F17;
        case GLFW_KEY_F18: return ImGuiKey_F18;
        case GLFW_KEY_F19: return ImGuiKey_F19;
        case GLFW_KEY_F20: return ImGuiKey_F20;
        case GLFW_KEY_F21: return ImGuiKey_F21;
        case GLFW_KEY_F22: return ImGuiKey_F22;
        case GLFW_KEY_F23: return ImGuiKey_F23;
        case GLFW_KEY_F24: return ImGuiKey_F24;
        default: return ImGuiKey_None;
    }
}




} // Honey