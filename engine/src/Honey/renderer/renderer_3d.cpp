#include "hnpch.h"
#include "renderer_3d.h"
#include "render_command.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Honey {

    struct Renderer3DStorage {
        // Basic cube
        Ref<VertexArray> cube_vertex_array;
        Ref<Shader> basic_shader;
        Ref<Texture2D> white_texture;

        // Sphere (optional for now)
        Ref<VertexArray> sphere_vertex_array;

        Renderer3D::Statistics stats;
    };

    static Renderer3DStorage* s_data;

    void Renderer3D::init() {
        HN_PROFILE_FUNCTION();

        s_data = new Renderer3DStorage;

        // Create basic 3D shader
        s_data->basic_shader = Shader::create("../../application/assets/shaders/basic3d.glsl");

        // Create white texture for colored rendering
        s_data->white_texture = Texture2D::create(1, 1);
        uint32_t white_data = 0xffffffff;
        s_data->white_texture->set_data(&white_data, sizeof(uint32_t));

        create_cube_geometry();
        // create_sphere_geometry(); // Implement later
    }

    void Renderer3D::shutdown() {
        HN_PROFILE_FUNCTION();

        delete s_data;
    }

    void Renderer3D::begin_scene(const PerspectiveCamera& camera) {
        HN_PROFILE_FUNCTION();

        s_data->basic_shader->bind();
        s_data->basic_shader->set_mat4("u_view_projection", camera.get_view_projection_matrix());
    }

    void Renderer3D::end_scene() {
        HN_PROFILE_FUNCTION();

        // Nothing to do here for now
    }

    void Renderer3D::draw_cube(const glm::vec3& position, const glm::vec3& size, const glm::vec4& color) {
        HN_PROFILE_FUNCTION();

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
                             glm::scale(glm::mat4(1.0f), size);

        s_data->basic_shader->set_mat4("u_transform", transform);
        s_data->basic_shader->set_float4("u_color", color);

        s_data->white_texture->bind();
        s_data->cube_vertex_array->bind();
        RenderCommand::draw_indexed(s_data->cube_vertex_array);

        s_data->stats.draw_calls++;
    }

    void Renderer3D::draw_cube(const glm::vec3& position, const glm::vec3& size, const Ref<Texture2D>& texture, const glm::vec4& color) {
        HN_PROFILE_FUNCTION();

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
                             glm::scale(glm::mat4(1.0f), size);

        s_data->basic_shader->set_mat4("u_transform", transform);
        s_data->basic_shader->set_float4("u_color", color);

        // Use provided texture or fallback to white texture
        const Ref<Texture2D>& actual_texture = texture ? texture : s_data->white_texture;
        actual_texture->bind();

        s_data->cube_vertex_array->bind();
        RenderCommand::draw_indexed(s_data->cube_vertex_array);

        s_data->stats.draw_calls++;
    }

    void Renderer3D::draw_sphere(const glm::vec3& position, float radius, const glm::vec4& color) {
        HN_PROFILE_FUNCTION();

        // TODO: Implement sphere rendering
        // For now, just draw a cube as placeholder
        draw_cube(position, glm::vec3(radius * 2.0f), color);
    }

    void Renderer3D::draw_mesh(const Ref<VertexArray>& vertex_array, const glm::mat4& transform, const Ref<Shader>& shader) {
        HN_PROFILE_FUNCTION();

        shader->bind();
        shader->set_mat4("u_transform", transform);

        vertex_array->bind();
        RenderCommand::draw_indexed(vertex_array);

        s_data->stats.draw_calls++;
    }

    Renderer3D::Statistics Renderer3D::get_stats() {
        return s_data->stats;
    }

    void Renderer3D::reset_stats() {
        memset(&s_data->stats, 0, sizeof(Statistics));
    }

    void Renderer3D::create_cube_geometry() {
        HN_PROFILE_FUNCTION();

        s_data->cube_vertex_array = VertexArray::create();

        // Cube vertices with positions, normals, and texture coordinates
        float cube_vertices[] = {
            // Front face
            -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f, // Bottom-left
             0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 0.0f, // Bottom-right
             0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f, // Top-right
            -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 1.0f, // Top-left

            // Back face
            -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 0.0f, // Bottom-left
             0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f, // Bottom-right
             0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 1.0f, // Top-right
            -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f, // Top-left

            // Left face
            -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.0f, // Top-right
            -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 1.0f, // Top-left
            -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 1.0f, // Bottom-left
            -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 0.0f, // Bottom-right

            // Right face
             0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 0.0f, // Top-left
             0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f, // Top-right
             0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, // Bottom-right
             0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, // Bottom-left

            // Bottom face
            -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f, // Top-right
             0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 1.0f, // Top-left
             0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f, // Bottom-left
            -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 0.0f, // Bottom-right

            // Top face
            -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f, // Top-left
             0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f, // Top-right
             0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 1.0f, // Bottom-right
            -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f  // Bottom-left
        };

        Ref<VertexBuffer> cube_vertex_buffer = VertexBuffer::create(cube_vertices, sizeof(cube_vertices));

        BufferLayout cube_layout = {
            { ShaderDataType::Float3, "a_position" },
            { ShaderDataType::Float3, "a_normal" },
            { ShaderDataType::Float2, "a_tex_coord" }
        };

        cube_vertex_buffer->set_layout(cube_layout);
        s_data->cube_vertex_array->add_vertex_buffer(cube_vertex_buffer);

        // Cube indices
        uint32_t cube_indices[] = {
            0,  1,  2,   2,  3,  0,   // Front face
            4,  5,  6,   6,  7,  4,   // Back face
            8,  9,  10,  10, 11, 8,   // Left face
            12, 13, 14,  14, 15, 12,  // Right face
            16, 17, 18,  18, 19, 16,  // Bottom face
            20, 21, 22,  22, 23, 20   // Top face
        };

        Ref<IndexBuffer> cube_index_buffer = IndexBuffer::create(cube_indices, sizeof(cube_indices) / sizeof(cube_indices[0]));
        s_data->cube_vertex_array->set_index_buffer(cube_index_buffer);
    }

    void Renderer3D::create_sphere_geometry() {
        HN_PROFILE_FUNCTION();

        // TODO: Implement sphere geometry generation
        // This would involve creating vertices and indices for a sphere
        // using parametric equations or subdivision of an icosahedron
    }

}