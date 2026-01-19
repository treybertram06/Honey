#include "hnpch.h"
#include "platform/opengl/opengl_context.h"

#include <GLFW/glfw3.h>
#include <glad/glad.h>

namespace Honey {

    OpenGLContext::OpenGLContext(GLFWwindow *window_handle)
        : m_window_handle(window_handle) {
        HN_CORE_ASSERT(window_handle, "Window handle is null!");
    }


    void OpenGLContext::init() {
        HN_PROFILE_FUNCTION();

        glfwMakeContextCurrent(m_window_handle);
        int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
        HN_CORE_ASSERT(status, "Failed to init GLAD!");

        HN_CORE_INFO("OpenGL Info:");
        HN_CORE_INFO("  Vendor: {0}",   reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
        HN_CORE_INFO("  Renderer: {0}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        HN_CORE_INFO("  Version: {0}",  reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    }

    void OpenGLContext::swap_buffers() {
        HN_PROFILE_FUNCTION();

        glfwSwapBuffers(m_window_handle);
    }


}
