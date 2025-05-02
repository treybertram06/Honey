#ifdef HN_PLATFORM_MACOS

#include "hnpch.h"
#include "macos_input.h"
#include "Honey/engine.h"

#include <GLFW/glfw3.h>

namespace Honey {

    Input* Input::s_instance = new MacosInput;

    bool MacosInput::is_key_pressed_impl(int keycode) {
        auto window = static_cast<GLFWwindow*>(Application::get().get_window().get_native_window());
        auto state = glfwGetKey(window, keycode);
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool MacosInput::is_mouse_button_pressed_impl(int button) {
        auto window = static_cast<GLFWwindow*>(Application::get().get_window().get_native_window());
        auto state = glfwGetKey(window, button);
        return state == GLFW_PRESS;
    }

    std::pair<float, float> MacosInput::get_mouse_position_impl() {
        auto window = static_cast<GLFWwindow*>(Application::get().get_window().get_native_window());
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        return {(float)xpos, (float)ypos };
    }


    float MacosInput:: get_mouse_x_impl() {
        auto[x, y] = get_mouse_position_impl();
        return x;
    }

    float MacosInput::get_mouse_y_impl() {
        auto[x, y] = get_mouse_position_impl();
        return y;
    }


} // namespace Honey

#endif