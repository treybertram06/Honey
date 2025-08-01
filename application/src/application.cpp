#include <Honey.h>
#include <Honey/core/entry_point.h>
#include <imgui.h>

#include "application_2d.h"
#include "application_3d.h"
#include "../../engine/src/Honey/debug/instrumentor.h"
#include "examples.h"
#include "glm/gtc/type_ptr.inl"
#include "platform/opengl/opengl_shader.h"


class ExampleLayer : public Honey::Layer {
public:
    ExampleLayer()
        : Layer("ExampleLayer"),
        m_camera_controller((16.0f / 9.0f), true),
        m_square_position(0.0f, 0.0f, 0.0f) {

        m_vertex_array = Honey::VertexArray::create();

        float vertices[3*3 + 4*3] = {
            -0.5f, -0.5f, 0.0f,     1.0f, 0.0f, 1.0f, 1.0f,
             0.5f, -0.5f, 0.0f,     0.0f, 0.0f, 1.0f, 1.0f,
             0.0f,  0.5f, 0.0f,     1.0f, 1.0f, 0.0f, 1.0f
        };

        Honey::Ref<Honey::VertexBuffer> vertex_buffer = Honey::VertexBuffer::create(vertices, sizeof(vertices));

        Honey::BufferLayout layout = {
            { Honey::ShaderDataType::Float3, "a_pos" },
            { Honey::ShaderDataType::Float4, "a_color" }
        };
        vertex_buffer->set_layout(layout);
        m_vertex_array->add_vertex_buffer(vertex_buffer);


        unsigned int indices[3] = { 0, 1, 2 };
        Honey::Ref<Honey::IndexBuffer> index_buffer = Honey::IndexBuffer::create(indices, 3);
        m_vertex_array->set_index_buffer(index_buffer);

        m_square_vertex_array = Honey::VertexArray::create();

        float vertices_sq[3*4 + 2*4] = {
            -0.5f, -0.5f, 0.0f,     0.0f, 0.0f,
             0.5f, -0.5f, 0.0f,     1.0f, 0.0f,
             0.5f,  0.5f, 0.0f,     1.0f, 1.0f,
            -0.5f,  0.5f, 0.0f,     0.0f, 1.0f
        };

        Honey::Ref<Honey::VertexBuffer> square_vertex_buffer = Honey::VertexBuffer::create(vertices_sq, sizeof(vertices_sq));
        Honey::BufferLayout square_layout = {
            { Honey::ShaderDataType::Float3, "a_pos" },
            { Honey::ShaderDataType::Float2, "a_tex_coord" }
        };
        square_vertex_buffer->set_layout(square_layout);
        m_square_vertex_array->add_vertex_buffer(square_vertex_buffer);

        unsigned int square_indices[6] = { 0, 1, 2, 2, 3, 0 };
        Honey::Ref<Honey::IndexBuffer> square_index_buffer = Honey::IndexBuffer::create(square_indices, sizeof(square_indices)/sizeof(square_indices[0]));
        m_square_vertex_array->set_index_buffer(square_index_buffer);




// this is only here because I want to use the same application file on both of my computers
#ifdef HN_PLATFORM_MACOS
        auto flat_color_shader = m_shader_lib.load("/Users/treybertram/Desktop/Honey/application/assets/shaders/flat_color.glsl");
        auto texture_shader = m_shader_lib.load("/Users/treybertram/Desktop/Honey/application/assets/shaders/texture.glsl");

        m_texture = Honey::Texture2D::create("/Users/treybertram/Desktop/Honey/application/assets/textures/bung.png");
        m_transparent_texture = Honey::Texture2D::create("/Users/treybertram/Desktop/Honey/application/assets/textures/transparent.png");
#endif
#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_LINUX)
        auto flat_color_shader = m_shader_lib.load("C:/Users/treyb/CLionProjects/engine/application/assets/shaders/flat_color.glsl");
        auto texture_shader = m_shader_lib.load("C:/Users/treyb/CLionProjects/engine/application/assets/shaders/texture.glsl");

        m_texture = Honey::Texture2D::create("C:/Users/treyb/CLionProjects/engine/application/assets/textures/bung.png");
        m_transparent_texture = Honey::Texture2D::create("C:/Users/treyb/CLionProjects/engine/application/assets/textures/transparent.png");
#endif

        std::dynamic_pointer_cast<Honey::OpenGLShader>(texture_shader)->bind();
        std::dynamic_pointer_cast<Honey::OpenGLShader>(texture_shader)->upload_uniform_int("u_texture", 0);
    }

    void on_update(Honey::Timestep ts) override {
        // update
        m_camera_controller.on_update(ts);

        framerate_counter.update(ts);
        framerate = framerate_counter.get_smoothed_fps();

        //HN_TRACE("Deltatime: {0}s ({1}ms)", ts.get_seconds(), ts.get_millis());

        // render
        Honey::RenderCommand::set_clear_color({0.1f, 0.1f, 0.1f, 1.0f});
        Honey::RenderCommand::clear();

        Honey::Renderer::begin_scene(m_camera_controller.get_camera());

        static glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));

        //Honey::MaterialRef material = new Honey::Material(m_flat_color_shader);

        auto flat_color_shader = m_shader_lib.get("flat_color");
        auto texture_shader = m_shader_lib.get("texture");

        std::dynamic_pointer_cast<Honey::OpenGLShader>(flat_color_shader)->bind();
        std::dynamic_pointer_cast<Honey::OpenGLShader>(flat_color_shader)->upload_uniform_float3("u_color", m_square_color);


        for (int i = 0; i < 20; i++) {
            for (int j = 0; j < 20; j++) {

                glm::vec3 pos(i * 0.11f, j * 0.11f, 0.0f);
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_square_position + pos) * scale;
                Honey::Renderer::submit(flat_color_shader, m_square_vertex_array, transform);

            }
        }

        m_texture->bind();
        Honey::Renderer::submit(texture_shader, m_square_vertex_array, glm::scale(glm::mat4(1.0f), glm::vec3(1.5f)));
        m_transparent_texture->bind();
        Honey::Renderer::submit(texture_shader, m_square_vertex_array, glm::scale(glm::mat4(1.0f), glm::vec3(1.5f)));

        Honey::Renderer::end_scene();

    }

    virtual void on_imgui_render() override {
        ImGui::Begin("Framerate");
        ImGui::Text("Framerate: %d", framerate);
        ImGui::End();

        ImGui::Begin("Settings");
        ImGui::ColorEdit3("Square color", glm::value_ptr(m_square_color));
        ImGui::End();
    }

    void on_event(Honey::Event &e) override {

        m_camera_controller.on_event(e);

        Honey::EventDispatcher dispatcher(e);
        dispatcher.dispatch<Honey::KeyPressedEvent>(HN_BIND_EVENT_FN(ExampleLayer::on_key_pressed_event));


        }

    bool on_key_pressed_event(Honey::KeyPressedEvent& e) {

        if (e.get_key_code() == Honey::KeyCode::Escape)
            Honey::Application::quit();

        return false;
    }

private:
    Honey::ShaderLibrary m_shader_lib;
    Honey::Ref<Honey::VertexArray> m_vertex_array;
    Honey::Ref<Honey::VertexArray> m_square_vertex_array;

    Honey::Ref<Honey::Texture2D> m_texture, m_transparent_texture;

    Honey::OrthographicCameraController m_camera_controller;


    glm::vec3 m_square_position;
    glm::vec3 m_square_color = {0.3f, 0.3f, 0.8f};

    Honey::FramerateCounter framerate_counter;
    int framerate = 0;
};

class Sandbox : public Honey::Application {
public:
    Sandbox() {
        //push_layer(new PongLayer());
        //push_layer(new ExampleLayer());
        push_layer(new Application2D());
        //push_layer(new Application3D());

    }

    ~Sandbox() {}



};

Honey::Application* Honey::create_application() {

    return new Sandbox();

}
