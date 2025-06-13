#include "application_2d.h"

#include "imgui.h"
#include "glm/gtc/type_ptr.hpp"
#include "platform/opengl/opengl_shader.h"

Application2D::Application2D()
    : Layer("Application2D"),
    m_camera_controller((1.6f / 0.9f), true) {}


void Application2D::on_attach() {

    m_square_vertex_array = Honey::VertexArray::create();

    float vertices_sq[3*4] = {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.5f,  0.5f, 0.0f,
        -0.5f,  0.5f, 0.0f
    };

    Honey::Ref<Honey::VertexBuffer> square_vertex_buffer;
    square_vertex_buffer.reset(Honey::VertexBuffer::create(vertices_sq, sizeof(vertices_sq)));
    Honey::BufferLayout square_layout = {
        { Honey::ShaderDataType::Float3, "a_pos" }
    };
    square_vertex_buffer->set_layout(square_layout);
    m_square_vertex_array->add_vertex_buffer(square_vertex_buffer);

    unsigned int square_indices[6] = { 0, 1, 2, 2, 3, 0 };
    Honey::Ref<Honey::IndexBuffer> square_index_buffer;
    square_index_buffer.reset(Honey::IndexBuffer::create(square_indices, sizeof(square_indices)/sizeof(square_indices[0])));
    m_square_vertex_array->set_index_buffer(square_index_buffer);


}

void Application2D::on_detach() {
}

void Application2D::on_update(Honey::Timestep ts) {

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

        m_shader = Honey::Shader::create("C:/Users/treyb/CLionProjects/engine/application/assets/shaders/flat_color.glsl");
        auto flat_color_shader = m_shader;

        std::dynamic_pointer_cast<Honey::OpenGLShader>(flat_color_shader)->bind();
        std::dynamic_pointer_cast<Honey::OpenGLShader>(flat_color_shader)->upload_uniform_float3("u_color", m_square_color);


        for (int i = 0; i < 20; i++) {
            for (int j = 0; j < 20; j++) {

                glm::vec3 pos(i * 0.11f, j * 0.11f, 0.0f);
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_square_position + pos) * scale;
                Honey::Renderer::submit(flat_color_shader, m_square_vertex_array, transform);

            }
        }


        Honey::RenderCommand::draw_indexed(m_square_vertex_array);

        Honey::Renderer::end_scene();
}


void Application2D::on_imgui_render() {

    ImGui::Begin("Settings");
    ImGui::ColorEdit3("Square color", glm::value_ptr(m_square_color));
    ImGui::End();
}

void Application2D::on_event(Honey::Event &event) {
    m_camera_controller.on_event(event);
}
