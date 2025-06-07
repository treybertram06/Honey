#include <Honey.h>
#include <imgui.h>
#include "Honey/core/statistics.h"
#include "Honey/entry_point.h"
#include "examples.h"
#include "glm/gtc/type_ptr.inl"
#include "platform/opengl/opengl_shader.h"


class ExampleLayer : public Honey::Layer {
public:
    ExampleLayer()
        : Layer("ExampleLayer"),
        m_camera(2.0f, (16.0f / 9.0f), -1.0f, 1.0f),
        m_camera_position(0.0f, 0.0f, 0.0f),
        m_square_position(0.0f, 0.0f, 0.0f) {

        m_vertex_array.reset(Honey::VertexArray::create());

        float vertices[3*3 + 4*3] = {
            -0.5f, -0.5f, 0.0f,     1.0f, 0.0f, 1.0f, 1.0f,
             0.5f, -0.5f, 0.0f,     0.0f, 0.0f, 1.0f, 1.0f,
             0.0f,  0.5f, 0.0f,     1.0f, 1.0f, 0.0f, 1.0f
        };

        Honey::Ref<Honey::VertexBuffer> vertex_buffer;
        vertex_buffer.reset(Honey::VertexBuffer::create(vertices, sizeof(vertices)));


        Honey::BufferLayout layout = {
            { Honey::ShaderDataType::Float3, "a_pos" },
            { Honey::ShaderDataType::Float4, "a_color" }
        };
        vertex_buffer->set_layout(layout);
        m_vertex_array->add_vertex_buffer(vertex_buffer);


        unsigned int indices[3] = { 0, 1, 2 };
        Honey::Ref<Honey::IndexBuffer> index_buffer;
        index_buffer.reset(Honey::IndexBuffer::create(indices, 3));
        m_vertex_array->set_index_buffer(index_buffer);

        m_square_vertex_array.reset(Honey::VertexArray::create());

        float vertices_sq[3*4 + 2*4] = {
            -0.5f, -0.5f, 0.0f,     0.0f, 0.0f,
             0.5f, -0.5f, 0.0f,     1.0f, 0.0f,
             0.5f,  0.5f, 0.0f,     1.0f, 1.0f,
            -0.5f,  0.5f, 0.0f,     0.0f, 1.0f
        };

        Honey::Ref<Honey::VertexBuffer> square_vertex_buffer;
        square_vertex_buffer.reset(Honey::VertexBuffer::create(vertices_sq, sizeof(vertices_sq)));
        Honey::BufferLayout square_layout = {
            { Honey::ShaderDataType::Float3, "a_pos" },
            { Honey::ShaderDataType::Float2, "a_tex_coord" }
        };
        square_vertex_buffer->set_layout(square_layout);
        m_square_vertex_array->add_vertex_buffer(square_vertex_buffer);

        unsigned int square_indices[6] = { 0, 1, 2, 2, 3, 0 };
        Honey::Ref<Honey::IndexBuffer> square_index_buffer;
        square_index_buffer.reset(Honey::IndexBuffer::create(square_indices, sizeof(square_indices)/sizeof(square_indices[0])));
        m_square_vertex_array->set_index_buffer(square_index_buffer);



        //m_shader.reset(Honey::Shader::create(vertex_src, fragment_src));






        m_flat_color_shader.reset(Honey::Shader::create("/Users/treybertram/Desktop/Honey/application/assets/shaders/flat_color.glsl"));
        m_texture_shader.reset(Honey::Shader::create("/Users/treybertram/Desktop/Honey/application/assets/shaders/texture.glsl"));

        m_texture = Honey::Texture2D::create("/Users/treybertram/Desktop/Honey/application/assets/textures/bung.png");
        m_transparent_texture = Honey::Texture2D::create("/Users/treybertram/Desktop/Honey/application/assets/textures/transparent.png");

        std::dynamic_pointer_cast<Honey::OpenGLShader>(m_texture_shader)->bind();
        std::dynamic_pointer_cast<Honey::OpenGLShader>(m_texture_shader)->upload_uniform_int("u_texture", 0);
    }

    void on_update(Honey::Timestep ts) override {

        framerate_counter.update(ts);
        framerate = framerate_counter.get_smoothed_fps();

        //HN_TRACE("Deltatime: {0}s ({1}ms)", ts.get_seconds(), ts.get_millis());

        if (Honey::Input::is_key_pressed(HN_KEY_A))
            m_camera_position.x -= m_camera_speed * ts;
        if (Honey::Input::is_key_pressed(HN_KEY_D))
            m_camera_position.x += m_camera_speed * ts;
        if (Honey::Input::is_key_pressed(HN_KEY_W))
            m_camera_position.y += m_camera_speed * ts;
        if (Honey::Input::is_key_pressed(HN_KEY_S))
            m_camera_position.y -= m_camera_speed * ts;

        if (Honey::Input::is_key_pressed(HN_KEY_Q))
            m_camera_rotation += m_camera_rotation_speed * ts;
        if (Honey::Input::is_key_pressed(HN_KEY_E))
            m_camera_rotation -= m_camera_rotation_speed * ts;



        /*
        if (Honey::Input::is_key_pressed(HN_KEY_J))
            m_square_position.x -= m_camera_speed * ts;
        if (Honey::Input::is_key_pressed(HN_KEY_L))
            m_square_position.x += m_camera_speed * ts;
        if (Honey::Input::is_key_pressed(HN_KEY_I))
            m_square_position.y += m_camera_speed * ts;
        if (Honey::Input::is_key_pressed(HN_KEY_K))
            m_square_position.y -= m_camera_speed * ts;
            */


        Honey::RenderCommand::set_clear_color({0.1f, 0.1f, 0.1f, 1.0f});
        Honey::RenderCommand::clear();

        m_camera.set_position(m_camera_position);
        m_camera.set_rotation(m_camera_rotation);

        Honey::Renderer::begin_scene(m_camera);

        static glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));

        //Honey::MaterialRef material = new Honey::Material(m_flat_color_shader);

        std::dynamic_pointer_cast<Honey::OpenGLShader>(m_flat_color_shader)->bind();
        std::dynamic_pointer_cast<Honey::OpenGLShader>(m_flat_color_shader)->upload_uniform_float3("u_color", m_square_color);


        for (int i = 0; i < 20; i++) {
            for (int j = 0; j < 20; j++) {

                glm::vec3 pos(i * 0.11f, j * 0.11f, 0.0f);
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_square_position + pos) * scale;
                Honey::Renderer::submit(m_flat_color_shader, m_square_vertex_array, transform);

            }
        }

        m_texture->bind();
        Honey::Renderer::submit(m_texture_shader, m_square_vertex_array, glm::scale(glm::mat4(1.0f), glm::vec3(1.5f)));
        m_transparent_texture->bind();
        Honey::Renderer::submit(m_texture_shader, m_square_vertex_array, glm::scale(glm::mat4(1.0f), glm::vec3(1.5f)));






        //triangle
        //Honey::Renderer::submit(m_shader, m_vertex_array);

        Honey::RenderCommand::draw_indexed(m_square_vertex_array);
        Honey::RenderCommand::draw_indexed(m_vertex_array);

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

    void on_event(Honey::Event &event) override {

        Honey::EventDispatcher dispatcher(event);
        dispatcher.dispatch<Honey::KeyPressedEvent>(HN_BIND_EVENT_FN(ExampleLayer::on_key_pressed_event));

    }

    bool on_key_pressed_event(Honey::KeyPressedEvent& event) {

        if (event.get_key_code() == HN_KEY_ESCAPE)
            Honey::Application::quit();

        return false;
    }

private:
    Honey::Ref<Honey::Shader> m_shader;
    Honey::Ref<Honey::VertexArray> m_vertex_array;

    Honey::Ref<Honey::Shader> m_flat_color_shader, m_texture_shader;
    Honey::Ref<Honey::VertexArray> m_square_vertex_array;

    Honey::Ref<Honey::Texture2D> m_texture, m_transparent_texture;

    Honey::OrthographicCamera m_camera;
    glm::vec3 m_camera_position;
    float m_camera_rotation;

    glm::vec3 m_square_position;
    glm::vec3 m_square_color = {0.3f, 0.3f, 0.8f};

    float m_camera_speed = 1.0f;
    float m_camera_rotation_speed = 60.0f; //      degrees / second

    Honey::FramerateCounter framerate_counter;
    int framerate = 0;
};

class Sandbox : public Honey::Application {
public:
    Sandbox() {
        //push_layer(new PongLayer());
        push_layer(new ExampleLayer());


    }

    ~Sandbox() {}



};

Honey::Application* Honey::create_application() {

    return new Sandbox();

}
