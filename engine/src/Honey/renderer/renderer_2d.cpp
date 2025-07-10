#include "hnpch.h"
#include "renderer_2d.h"

#include "render_command.h"
#include "vertex_array.h"
#include "shader.h"

namespace Honey {

    struct QuadVertex {
        glm::vec3 position;
        glm::vec4 color;
        glm::vec2 tex_coord;
    };

    struct Renderer2DData {
        const uint32_t max_quads = 10000;
        const uint32_t max_vertices = max_quads * 4;
        const uint32_t max_indices = max_quads * 6;

        Ref<VertexArray> quad_vertex_array;
        Ref<VertexBuffer> quad_vertex_buffer;
        Ref<Shader> texture_shader;
        Ref<Texture2D> blank_texture;

        uint32_t quad_index_count = 0;
        QuadVertex* quad_vertex_buffer_base = nullptr;
        QuadVertex* quad_vertex_buffer_ptr = nullptr;
    };

    static Renderer2DData s_data;

    void Renderer2D::init() {
        HN_PROFILE_FUNCTION();

        s_data.quad_vertex_array = VertexArray::create();

        s_data.quad_vertex_buffer = VertexBuffer::create(s_data.max_vertices * sizeof(QuadVertex));

        BufferLayout quad_layout = {
            { ShaderDataType::Float3, "a_pos" },
            { ShaderDataType::Float4, "a_color" },
            { ShaderDataType::Float2, "a_tex_coord" }
        };

        s_data.quad_vertex_buffer->set_layout(quad_layout);
        s_data.quad_vertex_array->add_vertex_buffer(s_data.quad_vertex_buffer);

        s_data.quad_vertex_buffer_base = new QuadVertex[s_data.max_vertices];

        uint32_t* quad_indices = new uint32_t[s_data.max_indices];

        uint32_t offset = 0;
        for (uint32_t i = 0; i < s_data.max_indices; i += 6) {
            quad_indices[i + 0] = offset + 0;
            quad_indices[i + 1] = offset + 1;
            quad_indices[i + 2] = offset + 2;

            quad_indices[i + 3] = offset + 2;
            quad_indices[i + 4] = offset + 3;
            quad_indices[i + 5] = offset + 0;

            offset += 4;
        }

        Ref<IndexBuffer> quad_index_buffer = IndexBuffer::create(quad_indices, s_data.max_indices);
        s_data.quad_vertex_array->set_index_buffer(quad_index_buffer);
        delete[] quad_indices;

        s_data.blank_texture = Texture2D::create(1, 1);
        uint32_t white_texture_data = 0xffffffff;
        s_data.blank_texture->set_data(&white_texture_data, sizeof(uint32_t));

        s_data.texture_shader = Shader::create("../../application/assets/shaders/texture.glsl");
        s_data.texture_shader->bind();
        //s_data.texture_shader->set_int("u_texture", 0);

    }

    void Renderer2D::shutdown() {
        HN_PROFILE_FUNCTION();

    }

    void Renderer2D::begin_scene(const OrthographicCamera &camera) {
        HN_PROFILE_FUNCTION();

        s_data.texture_shader->bind();
        s_data.texture_shader->set_mat4("u_view_projection", camera.get_view_projection_matrix());

        s_data.quad_index_count = 0;
        s_data.quad_vertex_buffer_ptr = s_data.quad_vertex_buffer_base;
    }

    void Renderer2D::end_scene() {
        HN_PROFILE_FUNCTION();

        uint32_t data_size = (uint8_t*)s_data.quad_vertex_buffer_ptr - (uint8_t*)s_data.quad_vertex_buffer_base;
        s_data.quad_vertex_buffer->set_data(s_data.quad_vertex_buffer_base, data_size);

        flush();

    }

    void Renderer2D::flush() {
        HN_PROFILE_FUNCTION();

        s_data.quad_vertex_array->bind();
        RenderCommand::draw_indexed(s_data.quad_vertex_array, s_data.quad_index_count);
    }

/*
    void Renderer2D::draw_quad(const glm::vec3& position, const glm::vec2& size,
                          const Ref<Texture2D>& texture, const glm::vec4& color,
                          float tiling_multiplier) {
        HN_PROFILE_FUNCTION();

        // Use blank texture if none provided
        const Ref<Texture2D>& actual_texture = texture ? texture : s_data.blank_texture;

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
            glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        s_data.texture_shader->set_mat4("u_transform", transform);
        s_data.texture_shader->set_float4("u_color", color);
        s_data.texture_shader->set_float("u_tiling_multiplier", tiling_multiplier);

        actual_texture->bind();

        s_data.quad_vertex_array->bind();
        RenderCommand::draw_indexed(s_data.quad_vertex_array);
    }

    void Renderer2D::draw_quad(const glm::vec2& position, const glm::vec2& size,
                              const Ref<Texture2D>& texture, const glm::vec4& color,
                              float tiling_multiplier) {
        draw_quad({position.x, position.y, 0.0f}, size, texture, color, tiling_multiplier);
    }
    */

    void Renderer2D::draw_quad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color) {
        draw_quad({position.x, position.y, 0.0f}, size, color);
    }

    void Renderer2D::draw_quad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color) {

        s_data.quad_vertex_buffer_ptr->position = position;
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {0.0f, 0.0f};
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_vertex_buffer_ptr->position = { position.x + size.x, position.y, 0.0f };
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {1.0f, 0.0f};
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_vertex_buffer_ptr->position = { position.x + size.x, position.y + size.y, 0.0f };
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {1.0f, 1.0f};
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_vertex_buffer_ptr->position = { position.x, position.y + size.y, 0.0f };
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {0.0f, 1.0f};
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_index_count += 6;
    }


    void Renderer2D::draw_rotated_quad(const glm::vec3& position, const glm::vec2& size, float rotation,
                          const Ref<Texture2D>& texture, const glm::vec4& color,
                          float tiling_multiplier) {
        HN_PROFILE_FUNCTION();

        // Use blank texture if none provided
        const Ref<Texture2D>& actual_texture = texture ? texture : s_data.blank_texture;

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
            glm::rotate(glm::mat4(1.0f), rotation, { 0.0f, 0.0f, 1.0f }) *
            glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        s_data.texture_shader->set_mat4("u_transform", transform);
        s_data.texture_shader->set_float4("u_color", color);
        s_data.texture_shader->set_float("u_tiling_multiplier", tiling_multiplier);

        actual_texture->bind();

        s_data.quad_vertex_array->bind();
        RenderCommand::draw_indexed(s_data.quad_vertex_array);
    }

    void Renderer2D::draw_rotated_quad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color) {
        draw_rotated_quad(position, size, rotation, nullptr, color, 1.0f);
    }








}

