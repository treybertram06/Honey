#include "hnpch.h"
#include "macos_window.h"

#include "Honey.h"

namespace Honey {

    static bool s_glfw_initialized = false;

    Window *Window::create(const WindowProps &props) {
        return new MacOSWindow(props);
    }

    MacOSWindow::MacOSWindow(const WindowProps& props) {
        init(props);
    }

    MacOSWindow::~MacOSWindow() {
        shutdown();
    }

    void MacOSWindow::init(const WindowProps& props) {
        m_data.title = props.title;
        m_data.width = props.width;
        m_data.height = props.height;

        HN_CORE_INFO("Creating window {0} ({1}, {2})", props.title, props.width, props.height);

        if (!s_glfw_initialized) {
            //TODO: glfwTerminate on shutdown
            int success = glfwInit();
            HN_CORE_ASSERT(success, "Could not initialize GLFW!");

            s_glfw_initialized = true;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

        m_window = glfwCreateWindow((int)props.width, (int)props.height, m_data.title.c_str(), nullptr, nullptr);
        HN_CORE_ASSERT(m_window, "GLFW window creation failed!");

        glfwMakeContextCurrent(m_window);
        glfwSetWindowUserPointer(m_window, &m_data);
        set_vsync(true);

    }

    void MacOSWindow::shutdown() {
        glfwDestroyWindow(m_window);
    }

    void MacOSWindow::on_update() {
        glfwPollEvents();
        glfwSwapBuffers(m_window);
    }

    void MacOSWindow::set_vsync(bool enabled) {
        if (enabled) {
            glfwSwapInterval(1);
        } else {
            glfwSwapInterval(0);
        }

        m_data.vsync = enabled;
    }

    bool MacOSWindow::is_vsync() const {
        return m_data.vsync;
    }

}
