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
        float tex_index;
        float tiling_factor;
    };

    struct Renderer2DData {
        static const uint32_t max_quads = 10000;
        static const uint32_t max_vertices = max_quads * 4;
        static const uint32_t max_indices = max_quads * 6;
        uint32_t max_texture_slots;

        Ref<VertexArray> quad_vertex_array;
        Ref<VertexBuffer> quad_vertex_buffer;
        Ref<Shader> texture_shader;
        Ref<Texture2D> blank_texture;

        uint32_t quad_index_count = 0;
        QuadVertex* quad_vertex_buffer_base = nullptr;
        QuadVertex* quad_vertex_buffer_ptr = nullptr;

        std::vector<Ref<Texture2D>> texture_slots;
        uint32_t texture_slot_index = 1; //0 = white texture

        glm::vec4 quad_vertex_positions[4];

        Renderer2D::Statistics statistics;

    };

    static Renderer2DData s_data;

    void Renderer2D::init() {
        HN_PROFILE_FUNCTION();

        s_data.max_texture_slots = RenderCommand::get_max_texture_slots();
        s_data.texture_slots.resize(s_data.max_texture_slots);

        s_data.quad_vertex_array = VertexArray::create();

        s_data.quad_vertex_buffer = VertexBuffer::create(s_data.max_vertices * sizeof(QuadVertex));

        BufferLayout quad_layout = {
            { ShaderDataType::Float3, "a_pos" },
            { ShaderDataType::Float4, "a_color" },
            { ShaderDataType::Float2, "a_tex_coord" },
            { ShaderDataType::Float, "a_tex_index" },
            { ShaderDataType::Float, "a_tiling_factor" }
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

        int32_t samplers[s_data.max_texture_slots];
        for (uint32_t i = 0; i < s_data.max_texture_slots; i++)
            samplers[i] = i;


        s_data.texture_shader = Shader::create("../../application/assets/shaders/texture.glsl", s_data.max_texture_slots);
        s_data.texture_shader->bind();
        s_data.texture_shader->set_int_array("u_textures", samplers, s_data.max_texture_slots);


        s_data.texture_slots[0] = s_data.blank_texture;

        s_data.quad_vertex_positions[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
        s_data.quad_vertex_positions[1] = { 0.5f, -0.5f, 0.0f, 1.0f };
        s_data.quad_vertex_positions[2] = { 0.5f, 0.5f, 0.0f, 1.0f };
        s_data.quad_vertex_positions[3] = { -0.5f, 0.5f, 0.0f, 1.0f };

    }

    void Renderer2D::shutdown() {
        HN_PROFILE_FUNCTION();

    }

    void Renderer2D::begin_scene(const OrthographicCamera &camera) {
        HN_PROFILE_FUNCTION();

        reset_stats();

        s_data.texture_shader->bind();
        s_data.texture_shader->set_mat4("u_view_projection", camera.get_view_projection_matrix());

        s_data.quad_index_count = 0;
        s_data.quad_vertex_buffer_ptr = s_data.quad_vertex_buffer_base;

        s_data.texture_slot_index = 1;

        for (uint32_t i = 1; i < s_data.max_texture_slots; i++) {
            s_data.texture_slots[i].reset();
        }

    }

    void Renderer2D::end_scene() {
        HN_PROFILE_FUNCTION();

        uint32_t data_size = (uint8_t*)s_data.quad_vertex_buffer_ptr - (uint8_t*)s_data.quad_vertex_buffer_base;
        s_data.quad_vertex_buffer->set_data(s_data.quad_vertex_buffer_base, data_size);

        flush();

    }

    void Renderer2D::flush() {
        HN_PROFILE_FUNCTION();

        for (uint32_t i = 0; i < s_data.texture_slot_index; i++)
            s_data.texture_slots[i]->bind(i);

        s_data.quad_vertex_array->bind();
        RenderCommand::draw_indexed(s_data.quad_vertex_array, s_data.quad_index_count);

        s_data.statistics.draw_calls++;
    }

    void Renderer2D::flush_and_reset() {
        end_scene();

        s_data.quad_index_count = 0;
        s_data.quad_vertex_buffer_ptr = s_data.quad_vertex_buffer_base;

        s_data.texture_slot_index = 1;
    }


    void Renderer2D::draw_quad(const glm::vec3& position, const glm::vec2& size,
                          const Ref<Texture2D>& texture, const glm::vec4& color,
                          float tiling_factor) {
        HN_PROFILE_FUNCTION();

        if (s_data.quad_index_count >= Renderer2DData::max_indices)
            flush_and_reset();


        if (!texture) {
            draw_quad(position, size, color);
            return;
        }

        float texture_index = 0.0f;
        for (uint32_t i = 1; i < s_data.texture_slot_index; i++) {
            if (s_data.texture_slots[i] &&
                *s_data.texture_slots[i].get() == *texture.get()) {
                texture_index = (float)i;
                break;
                }
        }

        if (texture_index == 0.0f) {
            if (s_data.texture_slot_index >= s_data.max_texture_slots) {
                HN_CORE_WARN("Texture slot limit exceeded! Using blank texture.");
                texture_index = 0.0f;
            } else {
                texture_index = (float)s_data.texture_slot_index;
                s_data.texture_slots[s_data.texture_slot_index] = texture;
                s_data.texture_slot_index++;
            }
        }

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
           * glm::scale(glm::mat4(1.0f), {size.x, size.y, 1.0f});

        s_data.quad_vertex_buffer_ptr->position = transform * s_data.quad_vertex_positions[0];
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {0.0f, 0.0f};
        s_data.quad_vertex_buffer_ptr->tex_index = texture_index;
        s_data.quad_vertex_buffer_ptr->tiling_factor = tiling_factor;
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_vertex_buffer_ptr->position = transform * s_data.quad_vertex_positions[1];
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {1.0f, 0.0f};
        s_data.quad_vertex_buffer_ptr->tex_index = texture_index;
        s_data.quad_vertex_buffer_ptr->tiling_factor = tiling_factor;
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_vertex_buffer_ptr->position = transform * s_data.quad_vertex_positions[2];
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {1.0f, 1.0f};
        s_data.quad_vertex_buffer_ptr->tex_index = texture_index;
        s_data.quad_vertex_buffer_ptr->tiling_factor = tiling_factor;
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_vertex_buffer_ptr->position = transform * s_data.quad_vertex_positions[3];
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {0.0f, 1.0f};
        s_data.quad_vertex_buffer_ptr->tex_index = texture_index;
        s_data.quad_vertex_buffer_ptr->tiling_factor = tiling_factor;
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_index_count += 6;

        s_data.statistics.quad_count++;

    }

    void Renderer2D::draw_quad(const glm::vec2& position, const glm::vec2& size,
                              const Ref<Texture2D>& texture, const glm::vec4& color,
                              float tiling_multiplier) {
        draw_quad({position.x, position.y, 0.0f}, size, texture, color, tiling_multiplier);
    }


    void Renderer2D::draw_quad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color) {
        draw_quad({position.x, position.y, 0.0f}, size, color);
    }

    void Renderer2D::draw_quad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color) {
        HN_PROFILE_FUNCTION();

        if (s_data.quad_index_count >= Renderer2DData::max_indices)
            flush_and_reset();

        const float texture_index = 0.0f; // white texture
        const float tiling_factor = 1.0f; // no tiliing needed

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
            * glm::scale(glm::mat4(1.0f), {size.x, size.y, 1.0f});

        s_data.quad_vertex_buffer_ptr->position = transform * s_data.quad_vertex_positions[0];
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {0.0f, 0.0f};
        s_data.quad_vertex_buffer_ptr->tex_index = texture_index;
        s_data.quad_vertex_buffer_ptr->tiling_factor = tiling_factor;
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_vertex_buffer_ptr->position = transform * s_data.quad_vertex_positions[1];
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {1.0f, 0.0f};
        s_data.quad_vertex_buffer_ptr->tex_index = texture_index;
        s_data.quad_vertex_buffer_ptr->tiling_factor = tiling_factor;
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_vertex_buffer_ptr->position = transform * s_data.quad_vertex_positions[2];
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {1.0f, 1.0f};
        s_data.quad_vertex_buffer_ptr->tex_index = texture_index;
        s_data.quad_vertex_buffer_ptr->tiling_factor = tiling_factor;
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_vertex_buffer_ptr->position = transform * s_data.quad_vertex_positions[3];
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {0.0f, 1.0f};
        s_data.quad_vertex_buffer_ptr->tex_index = texture_index;
        s_data.quad_vertex_buffer_ptr->tiling_factor = tiling_factor;
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_index_count += 6;

        s_data.statistics.quad_count++;

    }


    void Renderer2D::draw_rotated_quad(const glm::vec3& position, const glm::vec2& size, float rotation,
                          const Ref<Texture2D>& texture, const glm::vec4& color,
                          float tiling_factor) {
        HN_PROFILE_FUNCTION();

        if (s_data.quad_index_count >= Renderer2DData::max_indices)
            flush_and_reset();

        if (!texture) {
            draw_quad(position, size, color);
            return;
        }

        float texture_index = 0.0f;
        for (uint32_t i = 1; i < s_data.texture_slot_index; i++) {
            if (s_data.texture_slots[i] &&
                *s_data.texture_slots[i].get() == *texture.get()) {
                texture_index = (float)i;
                break;
                }
        }

        if (texture_index == 0.0f) {
            if (s_data.texture_slot_index >= s_data.max_texture_slots) {
                HN_CORE_WARN("Texture slot limit exceeded! Using blank texture.");
                texture_index = 0.0f;
            } else {
                texture_index = (float)s_data.texture_slot_index;
                s_data.texture_slots[s_data.texture_slot_index] = texture;
                s_data.texture_slot_index++;
            }
        }


        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
            * glm::rotate(glm::mat4(1.0f), glm::radians(rotation), {0.0f, 0.0f, 1.0f})
            * glm::scale(glm::mat4(1.0f), {size.x, size.y, 1.0f});

        s_data.quad_vertex_buffer_ptr->position = transform * s_data.quad_vertex_positions[0];
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {0.0f, 0.0f};
        s_data.quad_vertex_buffer_ptr->tex_index = texture_index;
        s_data.quad_vertex_buffer_ptr->tiling_factor = tiling_factor;
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_vertex_buffer_ptr->position = transform * s_data.quad_vertex_positions[1];
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {1.0f, 0.0f};
        s_data.quad_vertex_buffer_ptr->tex_index = texture_index;
        s_data.quad_vertex_buffer_ptr->tiling_factor = tiling_factor;
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_vertex_buffer_ptr->position = transform * s_data.quad_vertex_positions[2];
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {1.0f, 1.0f};
        s_data.quad_vertex_buffer_ptr->tex_index = texture_index;
        s_data.quad_vertex_buffer_ptr->tiling_factor = tiling_factor;
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_vertex_buffer_ptr->position = transform * s_data.quad_vertex_positions[3];
        s_data.quad_vertex_buffer_ptr->color = color;
        s_data.quad_vertex_buffer_ptr->tex_coord = {0.0f, 1.0f};
        s_data.quad_vertex_buffer_ptr->tex_index = texture_index;
        s_data.quad_vertex_buffer_ptr->tiling_factor = tiling_factor;
        s_data.quad_vertex_buffer_ptr++;

        s_data.quad_index_count += 6;

        s_data.statistics.quad_count++;

    }

    void Renderer2D::draw_rotated_quad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color) {
        draw_rotated_quad(position, size, rotation, nullptr, color, 1.0f);
    }


    Renderer2D::Statistics Renderer2D::get_stats() {
        return s_data.statistics;
    }

    void Renderer2D::reset_stats() {
        memset(&s_data.statistics, 0, sizeof(Statistics));
    }







}

