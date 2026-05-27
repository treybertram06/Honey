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

    static float s_delta_x = 0.0f;
    static float s_delta_y = 0.0f;
    static float s_prev_x  = 0.0f;
    static float s_prev_y  = 0.0f;
    static bool  s_first_delta = true;

    void Input::update_mouse_delta() {
        auto [x, y] = get_mouse_position();
        if (s_first_delta) {
            s_prev_x = x;
            s_prev_y = y;
            s_first_delta = false;
        }
        s_delta_x = x - s_prev_x;
        s_delta_y = y - s_prev_y;
        s_prev_x  = x;
        s_prev_y  = y;
    }

    float Input::get_mouse_delta_x() { return s_delta_x; }
    float Input::get_mouse_delta_y() { return s_delta_y; }

} // namespace Honey

#endif