
#ifdef HN_PLATFORM_LINUX

#include "hnpch.h"
#include "linux_window.h"

#include "Honey.h"
//#include "glad/glad.h"
#include "Honey/core/settings.h"

#include "Honey/events/application_event.h"
#include "Honey/events/key_event.h"
#include "Honey/events/mouse_event.h"

#include "platform/opengl/opengl_context.h"
#include "platform/vulkan/vk_context.h"

namespace Honey {

    static bool s_glfw_initialized = false;

    static void GLFW_error_callback(int error, const char* desc) {
        HN_CORE_ERROR("GLFW Error ({0}): {1}", error, desc);
    }

    Scope<Window> Window::create(const WindowProps &props) {
        return CreateScope<LinuxWindow>(props);
    }

    LinuxWindow::LinuxWindow(const WindowProps& props) {
        HN_PROFILE_FUNCTION();

        init(props);
    }

    LinuxWindow::~LinuxWindow() {
        HN_PROFILE_FUNCTION();

        shutdown();
    }

    void LinuxWindow::init(const WindowProps& props) {
        HN_PROFILE_FUNCTION();

        m_data.title = props.title;
        m_data.width = props.width;
        m_data.height = props.height;

        HN_CORE_INFO("Creating window {0} ({1}, {2})", props.title, props.width, props.height);

        if (!s_glfw_initialized) {
            //TODO: glfwTerminate on shutdown
            int success = glfwInit();
            HN_CORE_ASSERT(success, "Could not initialize GLFW!");
            glfwSetErrorCallback(GLFW_error_callback);

            s_glfw_initialized = true;
        }

        auto& renderer_settings = get_settings().renderer;
        switch (renderer_settings.api) {

        case RendererAPI::API::opengl:
            {
                glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
                glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
                glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
                glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
                glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

                m_window = glfwCreateWindow((int)props.width, (int)props.height, m_data.title.c_str(), nullptr, nullptr);
                HN_CORE_ASSERT(m_window, "GLFW window creation failed!");

                glfwMakeContextCurrent(m_window);
                m_context = new OpenGLContext(m_window);
                m_context->init();
                m_data.context = m_context;
                set_vsync(true);
            }
            break;
        case RendererAPI::API::vulkan:
            {
                glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

                m_window = glfwCreateWindow((int)props.width, (int)props.height, m_data.title.c_str(), nullptr, nullptr);
                HN_CORE_ASSERT(m_window, "GLFW window creation failed!");

                m_context = new VulkanContext(m_window);
                m_context->init();
                m_data.context = m_context;
                m_data.vsync = true;
            }
            break;

        case RendererAPI::API::none: HN_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
            break;
        }

        glfwSetWindowUserPointer(m_window, &m_data);

        // set glfw callbacks
        glfwSetWindowSizeCallback(m_window, [](GLFWwindow* window, int width, int height) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            data.width = width;
            data.height = height;

            WindowResizeEvent event(width, height);
            data.event_callback(event);
        });

        glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* window, int width, int height) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            // Only Vulkan cares about framebuffer resize for swapchain recreation.
            if (RendererAPI::get_api() == RendererAPI::API::vulkan) {
                auto* ctx = dynamic_cast<VulkanContext*>(data.context);
                if (ctx) {
                    ctx->notify_framebuffer_resized();
                }
            }

            // Keep emitting resize as well (some systems might depend on it)
            WindowResizeEvent event(width, height);
            data.event_callback(event);
        });

        glfwSetWindowCloseCallback(m_window, [](GLFWwindow* window) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            WindowCloseEvent event;
            data.event_callback(event);
        });

        glfwSetKeyCallback(m_window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            switch (action) {
                case GLFW_PRESS: {
                    KeyPressedEvent event((KeyCode)key, 0);
                    data.event_callback(event);
                    break;
                }
                case GLFW_RELEASE: {
                    KeyReleasedEvent event((KeyCode)key);
                    data.event_callback(event);
                    break;
                }
                case GLFW_REPEAT: {
                    KeyPressedEvent event((KeyCode)key, 1);
                    data.event_callback(event);
                    break;
                }
            }
        });

        glfwSetMouseButtonCallback(m_window, [](GLFWwindow* window, int button, int action, int mods) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            switch (action) {
                case GLFW_PRESS: {
                    MouseButtonPressedEvent event((MouseButton)button);
                    data.event_callback(event);
                    break;
                }
                case GLFW_RELEASE: {
                    MouseButtonReleasedEvent event((MouseButton)button);
                    data.event_callback(event);
                    break;
                }
            }
        });

        glfwSetScrollCallback(m_window, [](GLFWwindow* window, double xoffset, double yoffset) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            MouseScrolledEvent event((float)xoffset, (float)yoffset);
            data.event_callback(event);
        });

        glfwSetCursorPosCallback(m_window, [](GLFWwindow* window, double xpos, double ypos) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            MouseMovedEvent event((float)xpos, (float)ypos);
            data.event_callback(event);
        });

        glfwSetCharCallback(m_window, [](GLFWwindow* window, unsigned int keycode) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            KeyTypedEvent event((KeyCode)keycode);
            data.event_callback(event);
        });

    }



    void LinuxWindow::shutdown() {
        HN_PROFILE_FUNCTION();

        if (m_context) {
            delete m_context;
            m_context = nullptr;
            m_data.context = nullptr;
        }

        if (m_window) {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
        }
    }

    void LinuxWindow::on_update() {
        HN_PROFILE_FUNCTION();

        glfwPollEvents();
        m_context->swap_buffers();
    }

    void LinuxWindow::set_vsync(bool enabled) {
        HN_PROFILE_FUNCTION();

        if (enabled) {
            //glfwSwapInterval(1);
            m_data.vsync = enabled;
        } else {
            //glfwSwapInterval(0);
            m_data.vsync = enabled;
        }

        m_data.vsync = enabled;
    }

    bool LinuxWindow::is_vsync() const {
        return m_data.vsync;
    }

}

#endif
