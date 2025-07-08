#ifdef HN_PLATFORM_WINDOWS

#include "hnpch.h"
#include "windows_input.h"
#include "../../Honey/core/engine.h"

#include <GLFW/glfw3.h>

namespace Honey {

    Input* Input::s_instance = new WindowsInput;

    bool WindowsInput::is_key_pressed_impl(int keycode) {
        auto window = static_cast<GLFWwindow*>(Application::get().get_window().get_native_window());
        auto state = glfwGetKey(window, keycode);
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool WindowsInput::is_mouse_button_pressed_impl(int button) {
        auto window = static_cast<GLFWwindow*>(Application::get().get_window().get_native_window());
        auto state = glfwGetMouseButton(window, button);
        return state == GLFW_PRESS;
    }

    std::pair<float, float> WindowsInput::get_mouse_position_impl() {
        auto window = static_cast<GLFWwindow*>(Application::get().get_window().get_native_window());
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        return {(float)xpos, (float)ypos };
    }

    void WindowsInput::set_cursor_locked_impl(bool locked) {
        auto window = static_cast<GLFWwindow*>(Application::get().get_window().get_native_window());
        if (locked) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }

    bool WindowsInput::is_cursor_locked_impl() {
        auto window = static_cast<GLFWwindow*>(Application::get().get_window().get_native_window());
        return glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    }


    float WindowsInput:: get_mouse_x_impl() {
        auto[x, y] = get_mouse_position_impl();
        return x;
    }

    float WindowsInput::get_mouse_y_impl() {
        auto[x, y] = get_mouse_position_impl();
        return y;
    }


} // namespace Honey

#endif