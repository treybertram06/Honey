#include <Honey.h>
#include "engine.h"
#include "input.h"
#include <glad/glad.h>

#include <memory>

namespace Honey {

#define BIND_EVENT_FN(x) std::bind(&x, this, std::placeholders::_1)

    Application* Application::s_instance = nullptr;

    Application::Application() {
        HN_CORE_ASSERT(!s_instance, "Application already exists!");
        s_instance = this;

        m_window = std::unique_ptr<Window>(Window::create());
        m_window->set_event_callback([this](auto && PH1) { on_event(std::forward<decltype(PH1)>(PH1)); });

        m_imgui_layer = new ImGuiLayer();
        push_overlay(m_imgui_layer);

        glGenVertexArrays(1, &m_vertex_array);
        glBindVertexArray(m_vertex_array);

        glGenBuffers(1, &m_vertex_buffer);
        glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer);

        float vertices[3 * 3] = {
            -0.5f, -0.5f, 0.0f,
             0.5f, -0.5f, 0.0f,
             0.0f,  0.5f, 0.0f
        };

        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), nullptr);

        glGenBuffers(1, &m_index_buffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_index_buffer);

        unsigned int indices[3] = { 0, 1, 2 };
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

        std::string vertex_src = R"(

            #version 330 core

            layout(location = 0) in vec3 a_pos;

            out vec3 v_pos;

            void main() {
                v_pos = a_pos;
                gl_Position = vec4(a_pos, 1.0);
            }
        )";

        std::string fragment_src = R"(

            #version 330 core

            layout(location = 0) out vec4 color;

            in vec3 v_pos;

            void main() {
                color = vec4(v_pos * 0.5 + 0.5, 1.0);
            }
        )";

        m_shader = std::make_unique<Shader>(vertex_src, fragment_src);
    }

    Application::~Application() {}

    void Application::push_layer(Layer *layer) {
        m_layer_stack.push_layer(layer);
        layer->on_attach();
    }

    void Application::push_overlay(Layer *layer) {
        m_layer_stack.push_overlay(layer);
        layer->on_attach();
    }



    void Application::on_event(Event& e) {
        EventDispatcher dispatcher(e);

        dispatcher.dispatch<WindowCloseEvent>([this](WindowCloseEvent& e) -> bool {
            return on_window_close(e);
        });

        for (auto it = m_layer_stack.end(); it != m_layer_stack.begin(); ) {
            (*--it)->on_event(e);
            if (e.handled()) {
                break;
            }
        }
    }







    void Application::run() {
        while (m_running)
        {
            glClearColor(0.1f, 0.1f, 0.1f, 1);
            glClear(GL_COLOR_BUFFER_BIT);

            m_shader->bind();
            glBindVertexArray(m_vertex_array);
            glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, nullptr);

            for (Layer* layer : m_layer_stack) {
                layer->on_update();
            }

            m_imgui_layer->begin();
            for (Layer* layer : m_layer_stack)
                layer->on_imgui_render();
            m_imgui_layer->end();

            m_window->on_update();
        }
    }

    bool Application::on_window_close(WindowCloseEvent &e) {
        m_running = false;
        return true;
    }

}