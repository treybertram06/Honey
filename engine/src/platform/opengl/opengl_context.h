#pragma once

#include "Honey/renderer/graphics_context.h"

struct GLFWwindow;

namespace Honey {
    class OpenGLContext : public GraphicsContext {
    public:
        OpenGLContext(GLFWwindow* window_handle);

        virtual void init() override;
        virtual void swap_buffers() override;
        virtual void wait_idle() override {} // no-op
        virtual void refresh_all_texture_samplers() override {} // no-op
    private:
        GLFWwindow* m_window_handle;
    };
}
