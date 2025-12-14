#ifdef HN_PLATFORM_WINDOWS

#include "hnpch.h"
#include "Honey/core/input.h"
#include "Honey/core/engine.h"

#include <GLFW/glfw3.h>

namespace Honey {
    
    bool Input::is_key_pressed(KeyCode keycode) {
        auto window = static_cast<GLFWwindow*>(Application::get().get_window().get_native_window());
        auto state = glfwGetKey(window, (int)keycode);
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Input::is_mouse_button_pressed(MouseButton button) {
        auto window = static_cast<GLFWwindow*>(Application::get().get_window().get_native_window());
        auto state = glfwGetMouseButton(window, (int)button);
        return state == GLFW_PRESS;
    }

    std::pair<float, float> Input::get_mouse_position() {
        auto window = static_cast<GLFWwindow*>(Application::get().get_window().get_native_window());
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        return {(float)xpos, (float)ypos };
    }

    void Input::set_cursor_locked(bool locked) {
        auto window = static_cast<GLFWwindow*>(Application::get().get_window().get_native_window());
        if (locked) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }

    bool Input::is_cursor_locked() {
        auto window = static_cast<GLFWwindow*>(Application::get().get_window().get_native_window());
        return glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    }


    float Input:: get_mouse_x() {
        auto[x, y] = get_mouse_position();
        return x;
    }

    float Input::get_mouse_y() {
        auto[x, y] = get_mouse_position();
        return y;
    }


} // namespace Honey

#endif