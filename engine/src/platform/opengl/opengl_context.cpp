#include "hnpch.h"
#include "opengl_context.h"

#include <GLFW/glfw3.h>
#include <glad/glad.h>


namespace Honey {

    OpenGLContext::OpenGLContext(GLFWwindow *window_handle)
        : m_window_handle(window_handle) {
        HN_CORE_ASSERT(window_handle, "Window handle is null!");
    }


    void OpenGLContext::init() {
        glfwMakeContextCurrent(m_window_handle);
        int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
        HN_CORE_ASSERT(status, "Failed to init GLAD!");
    }

    void OpenGLContext::swap_buffers() {
        glfwSwapBuffers(m_window_handle);
    }


}