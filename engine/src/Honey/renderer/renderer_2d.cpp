#include "hnpch.h"
#include "renderer_2d.h"

#include "render_command.h"
#include "vertex_array.h"
#include "shader.h"

namespace Honey {

    struct Renderer2DStorage {
        Ref<VertexArray> vertex_array;
        Ref<Shader> texture_shader;
        Ref<Texture2D> blank_texture;
    };

    static Renderer2DStorage* s_data;

    void Renderer2D::init() {
        HN_PROFILE_FUNCTION();

        s_data = new Renderer2DStorage;

        s_data->vertex_array = VertexArray::create();

        float vertices_sq[3*4 + 2*4] = {
            -0.5f, -0.5f, 0.0f,     0.0f, 0.0f,
             0.5f, -0.5f, 0.0f,     1.0f, 0.0f,
             0.5f,  0.5f, 0.0f,     1.0f, 1.0f,
            -0.5f,  0.5f, 0.0f,     0.0f, 1.0f
        };

        Ref<VertexBuffer> square_vertex_buffer;
        square_vertex_buffer.reset(VertexBuffer::create(vertices_sq, sizeof(vertices_sq)));

        BufferLayout square_layout = {
            { ShaderDataType::Float3, "a_pos" },
            { ShaderDataType::Float2, "a_tex_coord" }
        };

        square_vertex_buffer->set_layout(square_layout);
        s_data->vertex_array->add_vertex_buffer(square_vertex_buffer);

        unsigned int square_indices[6] = { 0, 1, 2, 2, 3, 0 };
        Ref<IndexBuffer> square_index_buffer;
        square_index_buffer.reset(IndexBuffer::create(square_indices, sizeof(square_indices)/sizeof(square_indices[0])));
        s_data->vertex_array->set_index_buffer(square_index_buffer);

        s_data->blank_texture = Texture2D::create(1, 1);
        uint32_t white_texture_data = 0xffffffff;
        s_data->blank_texture->set_data(&white_texture_data, sizeof(uint32_t));

        s_data->texture_shader = Shader::create("../../application/assets/shaders/texture.glsl");
        s_data->texture_shader->bind();
        s_data->texture_shader->set_int("u_texture", 0);

    }

    void Renderer2D::shutdown() {
        HN_PROFILE_FUNCTION();

        delete s_data;
    }

    void Renderer2D::begin_scene(const OrthographicCamera &camera) {
        HN_PROFILE_FUNCTION();

        s_data->texture_shader->bind();
        s_data->texture_shader->set_mat4("u_view_projection", camera.get_view_projection_matrix());
    }

    void Renderer2D::end_scene() {
        HN_PROFILE_FUNCTION();

    }

    void Renderer2D::draw_quad(const glm::vec3& position, const glm::vec2& size,
                          const Ref<Texture2D>& texture, const glm::vec4& color,
                          float tiling_multiplier) {
        HN_PROFILE_FUNCTION();

        // Use blank texture if none provided
        const Ref<Texture2D>& actual_texture = texture ? texture : s_data->blank_texture;

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
            glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        s_data->texture_shader->set_mat4("u_transform", transform);
        s_data->texture_shader->set_float4("u_color", color);
        s_data->texture_shader->set_float("u_tiling_multiplier", tiling_multiplier);

        actual_texture->bind();

        s_data->vertex_array->bind();
        RenderCommand::draw_indexed(s_data->vertex_array);
    }

    void Renderer2D::draw_quad(const glm::vec2& position, const glm::vec2& size,
                              const Ref<Texture2D>& texture, const glm::vec4& color,
                              float tiling_multiplier) {
        draw_quad({position.x, position.y, 0.0f}, size, texture, color, tiling_multiplier);
    }

    void Renderer2D::draw_quad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color) {
        draw_quad(position, size, nullptr, color, 1.0f);
    }

    void Renderer2D::draw_quad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color) {
        draw_quad(position, size, nullptr, color, 1.0f);
    }







}

