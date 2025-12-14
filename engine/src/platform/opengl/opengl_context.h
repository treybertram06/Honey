#pragma once

#include "Honey/renderer/graphics_context.h"

struct GLFWwindow;

namespace Honey {
    class OpenGLContext : public GraphicsContext {
    public:
        OpenGLContext(GLFWwindow* window_handle);

        virtual void init() override;
        virtual void swap_buffers() override;
    private:
        GLFWwindow* m_window_handle;
    };
}
