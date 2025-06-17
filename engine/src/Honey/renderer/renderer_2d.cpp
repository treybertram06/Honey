#include "hnpch.h"
#include "renderer_2d.h"

#include "render_command.h"
#include "vertex_array.h"
#include "shader.h"
#include "platform/opengl/opengl_shader.h"

namespace Honey {

    struct Renderer2DStorage {
        Ref<VertexArray> vertex_array;
        Ref<Shader> shader;
    };

    static Renderer2DStorage* s_data;

    void Renderer2D::init() {
        s_data = new Renderer2DStorage;

        s_data->vertex_array = VertexArray::create();

        float vertices_sq[3*4] = {
            -0.5f, -0.5f, 0.0f,
             0.5f, -0.5f, 0.0f,
             0.5f,  0.5f, 0.0f,
            -0.5f,  0.5f, 0.0f
        };

        Ref<VertexBuffer> square_vertex_buffer;
        square_vertex_buffer.reset(VertexBuffer::create(vertices_sq, sizeof(vertices_sq)));
        BufferLayout square_layout = {
            { ShaderDataType::Float3, "a_pos" }
        };
        square_vertex_buffer->set_layout(square_layout);
        s_data->vertex_array->add_vertex_buffer(square_vertex_buffer);

        unsigned int square_indices[6] = { 0, 1, 2, 2, 3, 0 };
        Ref<IndexBuffer> square_index_buffer;
        square_index_buffer.reset(IndexBuffer::create(square_indices, sizeof(square_indices)/sizeof(square_indices[0])));
        s_data->vertex_array->set_index_buffer(square_index_buffer);


        s_data->shader = Shader::create("C:/Users/treyb/CLionProjects/engine/application/assets/shaders/flat_color.glsl");

    }

    void Renderer2D::shutdown() {
        delete s_data;
    }

    void Renderer2D::begin_scene(const OrthographicCamera &camera) {
        std::dynamic_pointer_cast<OpenGLShader>(s_data->shader)->bind();
        std::dynamic_pointer_cast<OpenGLShader>(s_data->shader)->upload_uniform_mat4("u_view_projection", camera.get_view_projection_matrix());
        std::dynamic_pointer_cast<OpenGLShader>(s_data->shader)->upload_uniform_mat4("u_transform", glm::mat4(1.0f));
    }

    void Renderer2D::end_scene() {
    }

    void Renderer2D::draw_quad(const glm::vec2 &position, const glm::vec2 &size, const glm::vec4 &color) {
       draw_quad({position.x, position.y, 0.0f}, size, color);
    }

    void Renderer2D::draw_quad(const glm::vec3 &position, const glm::vec2 &size, const glm::vec4 &color) {
        std::dynamic_pointer_cast<OpenGLShader>(s_data->shader)->bind();
        std::dynamic_pointer_cast<OpenGLShader>(s_data->shader)->upload_uniform_float3("u_color", color);

        s_data->vertex_array->bind();
        RenderCommand::draw_indexed(s_data->vertex_array);
    }

}

