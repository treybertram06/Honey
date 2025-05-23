#include <Honey.h>
#include "engine.h"
#include "input.h"
#include <glad/glad.h>

#include <memory>

#include "GLFW/glfw3.h"

namespace Honey {

#define BIND_EVENT_FN(x) std::bind(&x, this, std::placeholders::_1)

    Application* Application::s_instance = nullptr;



    Application::Application() {
        HN_CORE_ASSERT(!s_instance, "Application already exists!");
        s_instance = this;

        m_window = std::unique_ptr<Window>(Window::create());

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "Failed to load OpenGL functions\n";
            std::exit(-1);
        }

        m_window->set_event_callback([this](auto && PH1) { on_event(std::forward<decltype(PH1)>(PH1)); });

        m_imgui_layer = new ImGuiLayer();
        push_overlay(m_imgui_layer);

        m_vertex_array.reset(VertexArray::create());

        float vertices[3*3 + 4*3] = {
            -0.5f, -0.5f, 0.0f,     1.0f, 0.0f, 1.0f, 1.0f,
             0.5f, -0.5f, 0.0f,     0.0f, 0.0f, 1.0f, 1.0f,
             0.0f,  0.5f, 0.0f,     1.0f, 1.0f, 0.0f, 1.0f
        };

        std::shared_ptr<VertexBuffer> vertex_buffer;
        vertex_buffer.reset(VertexBuffer::create(vertices, sizeof(vertices)));


        BufferLayout layout = {
            { ShaderDataType::Float3, "a_pos" },
            { ShaderDataType::Float4, "a_color" }
        };
        vertex_buffer->set_layout(layout);
        m_vertex_array->add_vertex_buffer(vertex_buffer);


        unsigned int indices[3] = { 0, 1, 2 };
        std::shared_ptr<IndexBuffer> index_buffer;
        index_buffer.reset(IndexBuffer::create(indices, 3));
        m_vertex_array->set_index_buffer(index_buffer);

        m_square_vertex_array.reset(VertexArray::create());

        float vertices_sq[3*4] = {
            -0.75f, -0.75f, 0.0f,
             0.75f, -0.75f, 0.0f,
             0.75f,  0.75f, 0.0f,
            -0.75f,  0.75f, 0.0f,
        };

        std::shared_ptr<VertexBuffer> square_vertex_buffer;
        square_vertex_buffer.reset(VertexBuffer::create(vertices_sq, sizeof(vertices_sq)));
        BufferLayout square_layout = {
            { ShaderDataType::Float3, "a_pos" },
        };
        square_vertex_buffer->set_layout(square_layout);
        m_square_vertex_array->add_vertex_buffer(square_vertex_buffer);

        unsigned int square_indices[6] = { 0, 1, 2, 2, 3, 0 };
        std::shared_ptr<IndexBuffer> square_index_buffer;
        square_index_buffer.reset(IndexBuffer::create(square_indices, sizeof(square_indices)));
        m_square_vertex_array->set_index_buffer(square_index_buffer);

        std::string vertex_src = R"(

            #version 330 core

            layout(location = 0) in vec3 a_pos;
            layout(location = 1) in vec4 a_color;

            out vec3 v_pos;
            out vec4 v_color;

            void main() {
                v_pos = a_pos;
                v_color = a_color;
                gl_Position = vec4(a_pos, 1.0);
            }
        )";

        std::string fragment_src = R"(

            #version 330 core

            layout(location = 0) out vec4 color;

            in vec3 v_pos;
            in vec4 v_color;

            void main() {
                color = vec4(v_pos * 0.5 + 0.5, 1.0);
                color = v_color;
            }
        )";

        m_shader.reset(new Shader(vertex_src, fragment_src));




        std::string blue_shader_vertex_src = R"(

            #version 330 core

            layout(location = 0) in vec3 a_pos;

            out vec3 v_pos;

            void main() {
                v_pos = a_pos;
                gl_Position = vec4(a_pos, 1.0);
            }
        )";

        std::string blue_shader_fragment_src = R"(

            #version 330 core

            layout(location = 0) out vec4 color;

            in vec3 v_pos;

            void main() {
                color = vec4(0.3, 0.3, 0.8, 1.0);
            }
        )";

        m_blue_shader.reset(new Shader(blue_shader_vertex_src, blue_shader_fragment_src));
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

            m_blue_shader->bind();
            m_square_vertex_array->bind();
            glDrawElements(GL_TRIANGLES, m_square_vertex_array->get_index_buffer()->get_count(), GL_UNSIGNED_INT, nullptr);

            m_shader->bind();
            m_vertex_array->bind();
            glDrawElements(GL_TRIANGLES, m_vertex_array->get_index_buffer()->get_count(), GL_UNSIGNED_INT, nullptr);

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
