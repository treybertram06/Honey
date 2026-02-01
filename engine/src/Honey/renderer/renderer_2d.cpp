#include "hnpch.h"
#include "renderer_2d.h"

#include "renderer.h"
#include "render_command.h"
#include "vertex_array.h"
#include "shader.h"
#include "shader_cache.h"
#include "texture.h"
#include "shader_cache.h"
#include "glm/gtx/string_cast.hpp"
#include "platform/vulkan/vk_renderer_api.h"

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey {
    struct QuadInstance {
        glm::vec3  center;        // world‑space centre of the quad
        glm::vec2  half_size;      // size * 0.5f
        float      rotation;      // radians – 0 for axis‑aligned
        glm::vec4  color;
        int        tex_index;      // which of the bound textures to sample
        float      tiling_factor;
        glm::vec2  tex_coord_min;
        glm::vec2  tex_coord_max;
        int        entity_id;
    };

    struct CircleInstance {
        glm::vec3   center;
        //glm::vec3   local_position;
        glm::vec2   half_size;
        float       thickness;
        glm::vec4   color;
        float       fade;
        int         tex_index;
        glm::vec2   tex_coord_min;
        glm::vec2   tex_coord_max;
        int         entity_id;
    };

    struct LineInstance {
        glm::vec3   center;
        glm::vec2   half_size;
        float       rotation;
        glm::vec4   color;
        float       fade;
        int         tex_index;
        glm::vec2   tex_coord_min;
        glm::vec2   tex_coord_max;
        int         entity_id;
    };


    struct QuadVertexStatic {
        glm::vec2 local_pos;   // corners at (±0.5, ±0.5)
        glm::vec2 local_tex;   // UVs 0‑1
    };

    static const QuadVertexStatic s_static_quad[4] = {
        {{-0.5f,-0.5f}, {0.0f,0.0f}},
        {{ 0.5f,-0.5f}, {1.0f,0.0f}},
        {{ 0.5f, 0.5f}, {1.0f,1.0f}},
        {{-0.5f, 0.5f}, {0.0f,1.0f}}
    };

    struct Renderer2DData {
        static constexpr uint32_t max_quads     = 100'000;
        static constexpr uint32_t max_textures  = 32;   // keep in sync with shader

        // Quad GL objects
        Ref<VertexArray>  quad_vertex_array;
        Ref<VertexBuffer> s_quad_vertex_buffer;   // 4 vertices
        Ref<VertexBuffer> i_quad_vertex_buffer; // maxQuads * QuadInstance
        Ref<IndexBuffer>  quad_ibo;         // 6 indices
        Ref<Shader>       quad_shader;

        std::vector<QuadInstance> quad_instances;
        std::vector<QuadInstance> quad_sorted_instances;

        // Circle GL objects
        Ref<VertexArray>  circle_vertex_array;
        Ref<VertexBuffer> s_circle_vertex_buffer;   // 4 vertices
        Ref<VertexBuffer> i_circle_vertex_buffer; // maxQuads * QuadInstance
        Ref<IndexBuffer>  circle_ibo;         // 6 indices
        Ref<Shader>       circle_shader;

        std::vector<CircleInstance> circle_instances;
        std::vector<CircleInstance> circle_sorted_instances;

        // Line GL objects
        Ref<VertexArray>  line_vertex_array;
        Ref<VertexBuffer> s_line_vertex_buffer;   // 4 vertices
        Ref<VertexBuffer> i_line_vertex_buffer; // maxQuads * QuadInstance
        Ref<IndexBuffer>  line_ibo;         // 6 indices
        Ref<Shader>       line_shader;

        std::vector<LineInstance> line_instances;
        std::vector<LineInstance> line_sorted_instances;

        // Texture slots
        uint32_t                       max_texture_slots = 0;
        std::vector<Ref<Texture2D>>    texture_slots;
        uint32_t                       texture_slot_index = 1; // 0 is white tex
        Ref<Texture2D>                 white_texture;

        // Stats
        Renderer2D::Statistics stats;

        struct CameraData {
            glm::mat4 view_projection;
        };
        CameraData camera_buffer;
        Ref<UniformBuffer> camera_uniform_buffer;

        std::unique_ptr<ShaderCache> shader_cache;
    };

    static Renderer2DData* s_data = nullptr;


    static int resolve_texture_slot(const Ref<Texture2D>& tex) {
        if (!s_data)
            return 0;
        if (!tex)
            return 0; // white

        // Already bound this frame?
        for (uint32_t i = 1; i < s_data->texture_slot_index; ++i)
            if (s_data->texture_slots[i] && *s_data->texture_slots[i] == *tex)
                return (int)i;

        // Need a new slot
        if (s_data->texture_slot_index >= s_data->max_texture_slots) {
            HN_CORE_WARN("Texture slot limit exceeded – using white texture");
            return 0;
        }
        uint32_t idx = s_data->texture_slot_index++;
        s_data->texture_slots[idx] = tex;
        return (int)idx;
    }


    void Renderer2D::init(std::unique_ptr<ShaderCache> shader_cache) {
        HN_PROFILE_FUNCTION();

        if (!s_data)
            s_data = new Renderer2DData();

        s_data->shader_cache = std::move(shader_cache);

        ////////////////// QUADS //////////////////////////
        s_data->quad_vertex_array = VertexArray::create();

        s_data->s_quad_vertex_buffer = VertexBuffer::create(sizeof(s_static_quad));
        s_data->s_quad_vertex_buffer->set_data(s_static_quad, sizeof(s_static_quad));
        {
            BufferLayout layout = {
                { ShaderDataType::Float2, "a_local_pos"  },  // loc 0
                { ShaderDataType::Float2, "a_local_tex"  },  // loc 1
            };
            s_data->s_quad_vertex_buffer->set_layout(layout);
            s_data->quad_vertex_array->add_vertex_buffer(s_data->s_quad_vertex_buffer);
        }

        s_data->i_quad_vertex_buffer = VertexBuffer::create(Renderer2DData::max_quads * sizeof(QuadInstance));
        {
            BufferLayout layout = {
                { ShaderDataType::Float3, "i_center", false, true }, // loc 2
                { ShaderDataType::Float2, "i_half_size", false, true }, // loc 3
                { ShaderDataType::Float , "i_rotation", false, true }, // loc 4
                { ShaderDataType::Float4, "i_color", false, true }, // loc 5
                { ShaderDataType::Int , "i_tex_index", false, true }, // loc 6
                { ShaderDataType::Float , "i_tiling", false, true }, // loc 7
                { ShaderDataType::Float2 , "i_tex_coord_min", false, true }, // loc 8
                { ShaderDataType::Float2 , "i_tex_coord_max", false, true }, // loc 9
                { ShaderDataType::Int, "i_entity_id", false, true} // loc 10
            };
            s_data->i_quad_vertex_buffer->set_layout(layout);
            s_data->quad_vertex_array->add_vertex_buffer(s_data->i_quad_vertex_buffer);
        }

        uint32_t indices[6] = {0,1,2, 2,3,0};
        s_data->quad_ibo = IndexBuffer::create(indices, 6);
        s_data->quad_vertex_array->set_index_buffer(s_data->quad_ibo);

        s_data->quad_instances.reserve(Renderer2DData::max_quads);
        s_data->quad_sorted_instances.reserve(Renderer2DData::max_quads);

        s_data->max_texture_slots = RenderCommand::get_max_texture_slots();
        s_data->texture_slots.resize(s_data->max_texture_slots);

        if (!s_data->white_texture) {
            s_data->white_texture = Texture2D::create(1,1);
            uint32_t white = 0xffffffff;
            s_data->white_texture->set_data(&white, sizeof(uint32_t));
        }
        s_data->texture_slots[0] = s_data->white_texture;

        auto shader_path = asset_root / "shaders" / "Renderer2D_Quad.glsl";
        s_data->quad_shader = s_data->shader_cache->get_or_compile_shader(shader_path);

        if (RendererAPI::get_api() == RendererAPI::API::vulkan) {
            HN_CORE_INFO("Renderer2D::init() early return avoiding lines and circles");
            return;
        }



        ////////////////// CIRCLES //////////////////////////
        s_data->circle_vertex_array = VertexArray::create();


        s_data->s_circle_vertex_buffer = VertexBuffer::create(sizeof(s_static_quad));
        s_data->s_circle_vertex_buffer->set_data(s_static_quad, sizeof(s_static_quad));
        {
            BufferLayout layout = {
                { ShaderDataType::Float2, "a_local_pos"  },  // loc 0
                { ShaderDataType::Float2, "a_local_tex"  },  // loc 1
            };
            s_data->s_circle_vertex_buffer->set_layout(layout);
            s_data->circle_vertex_array->add_vertex_buffer(s_data->s_circle_vertex_buffer); // divisor 0 (default)
        }

        s_data->i_circle_vertex_buffer = VertexBuffer::create(Renderer2DData::max_quads * sizeof(CircleInstance));
        {
            BufferLayout layout = {
                { ShaderDataType::Float3, "i_center", false, true }, // loc 2
                //{ ShaderDataType::Float3, "i_local_position", false, true }, // loc
                { ShaderDataType::Float2, "i_half_size", false, true }, // loc 3
                { ShaderDataType::Float , "i_thickness", false, true }, // loc 4
                { ShaderDataType::Float4, "i_color", false, true }, // loc 5
                { ShaderDataType::Float, "i_fade", false, true }, // loc 6
                { ShaderDataType::Int , "i_tex_index", false, true }, // loc 7
                { ShaderDataType::Float2 , "i_tex_coord_min", false, true }, // loc 8
                { ShaderDataType::Float2 , "i_tex_coord_max", false, true }, // loc 9
                { ShaderDataType::Int, "i_entity_id", false, true} // loc 10
            };
            s_data->i_circle_vertex_buffer->set_layout(layout);
            s_data->circle_vertex_array->add_vertex_buffer(s_data->i_circle_vertex_buffer);
        }

        //uint32_t indices[6] = {0,1,2, 2,3,0};
        s_data->circle_ibo = IndexBuffer::create(indices, 6);
        s_data->circle_vertex_array->set_index_buffer(s_data->circle_ibo);

        s_data->circle_instances.reserve(Renderer2DData::max_quads);
        s_data->circle_sorted_instances.reserve(Renderer2DData::max_quads);

        s_data->max_texture_slots = RenderCommand::get_max_texture_slots();
        s_data->texture_slots.resize(s_data->max_texture_slots);

        if (!s_data->white_texture) {
            s_data->white_texture = Texture2D::create(1,1);
            uint32_t white = 0xffffffff;
            s_data->white_texture->set_data(&white, sizeof(uint32_t));
        }

        auto circle_shader_path = asset_root / "shaders" / "Renderer2D_Circle.glsl";
        s_data->circle_shader = s_data->shader_cache->get_or_compile_shader(circle_shader_path);

        ////////////////// LINES //////////////////////////
        s_data->line_vertex_array = VertexArray::create();


        s_data->s_line_vertex_buffer = VertexBuffer::create(sizeof(s_static_quad));
        s_data->s_line_vertex_buffer->set_data(s_static_quad, sizeof(s_static_quad));
        {
            BufferLayout layout = {
                { ShaderDataType::Float2, "a_local_pos"  },  // loc 0
                { ShaderDataType::Float2, "a_local_tex"  },  // loc 1
            };
            s_data->s_line_vertex_buffer->set_layout(layout);
            s_data->line_vertex_array->add_vertex_buffer(s_data->s_line_vertex_buffer); // divisor 0 (default)
        }

        s_data->i_line_vertex_buffer = VertexBuffer::create(Renderer2DData::max_quads * sizeof(LineInstance));
        {
            BufferLayout layout = {
                { ShaderDataType::Float3, "i_center", false, true }, // loc 2
                //{ ShaderDataType::Float3, "i_local_position", false, true }, // loc
                { ShaderDataType::Float2, "i_half_size", false, true }, // loc 3
                { ShaderDataType::Float, "i_rotation", false, true }, // loc 4
                { ShaderDataType::Float4, "i_color", false, true }, // loc 5
                { ShaderDataType::Float, "i_fade", false, true }, // loc 6
                { ShaderDataType::Int , "i_tex_index", false, true }, // loc 7
                { ShaderDataType::Float2 , "i_tex_coord_min", false, true }, // loc 8
                { ShaderDataType::Float2 , "i_tex_coord_max", false, true }, // loc 9
                { ShaderDataType::Int, "i_entity_id", false, true} // loc 10
            };
            s_data->i_line_vertex_buffer->set_layout(layout);
            s_data->line_vertex_array->add_vertex_buffer(s_data->i_line_vertex_buffer);
        }

        //uint32_t indices[6] = {0,1,2, 2,3,0};
        s_data->line_ibo = IndexBuffer::create(indices, 6);
        s_data->line_vertex_array->set_index_buffer(s_data->line_ibo);

        s_data->line_instances.reserve(Renderer2DData::max_quads);
        s_data->line_sorted_instances.reserve(Renderer2DData::max_quads);

        s_data->max_texture_slots = RenderCommand::get_max_texture_slots();
        s_data->texture_slots.resize(s_data->max_texture_slots);

        if (!s_data->white_texture) {
            s_data->white_texture = Texture2D::create(1,1);
            uint32_t white = 0xffffffff;
            s_data->white_texture->set_data(&white, sizeof(uint32_t));
        }

        auto line_shader_path = asset_root / "shaders" / "Renderer2D_Line.glsl";
        s_data->line_shader = s_data->shader_cache->get_or_compile_shader(line_shader_path);



        s_data->camera_uniform_buffer = UniformBuffer::create(sizeof(Renderer2DData::CameraData), 0);


        s_data->quad_shader->bind();
        s_data->circle_shader->bind();
        s_data->line_shader->bind(); // Are these doing anything?
        {
            std::vector<int> samplers(s_data->max_texture_slots);
            std::iota(samplers.begin(), samplers.end(), 0);
            s_data->quad_shader->    set_int_array("u_textures", samplers.data(), samplers.size());
            s_data->circle_shader->  set_int_array("u_textures", samplers.data(), samplers.size());
            s_data->line_shader->    set_int_array("u_textures", samplers.data(), samplers.size());
        }
    }

    void Renderer2D::shutdown() {
        if (!s_data)
            return;

        s_data->quad_vertex_array.reset();
        s_data->s_quad_vertex_buffer.reset();
        s_data->i_quad_vertex_buffer.reset();
        s_data->quad_ibo.reset();

        s_data->circle_vertex_array.reset();
        s_data->s_circle_vertex_buffer.reset();
        s_data->i_circle_vertex_buffer.reset();
        s_data->circle_ibo.reset();

        s_data->line_vertex_array.reset();
        s_data->s_line_vertex_buffer.reset();
        s_data->i_line_vertex_buffer.reset();
        s_data->line_ibo.reset();

        s_data->quad_shader.reset();
        s_data->circle_shader.reset();
        s_data->line_shader.reset();

        s_data->camera_uniform_buffer.reset();

        for (auto& t : s_data->texture_slots)
            t.reset();
        s_data->texture_slots.clear();
        s_data->texture_slots.shrink_to_fit();
        s_data->texture_slot_index = 1;

        s_data->white_texture.reset();

        s_data->shader_cache.reset();

        delete s_data;
        s_data = nullptr;
    }


    void Renderer2D::begin_scene(const OrthographicCamera& cam) {
        reset_stats();

        HN_CORE_ASSERT(s_data, "Renderer2D not initialized before calling begin_scene");

        if (Renderer::get_api() != RendererAPI::API::vulkan) {
            RenderCommand::set_blend_for_attachment(1, false);

            s_data->camera_buffer.view_projection = cam.get_view_projection_matrix();
            s_data->camera_uniform_buffer->set_data(sizeof(Renderer2DData::CameraData), &s_data->camera_buffer);
        } else {
            VulkanRendererAPI::submit_camera_view_projection(cam.get_view_projection_matrix());
        }

        s_data->quad_instances.clear();
        s_data->circle_instances.clear();
        s_data->line_instances.clear();

        s_data->texture_slot_index = 1; // keep white bound at 0
    }

    void Renderer2D::begin_scene(const Camera &camera, const glm::mat4& transform) {
        reset_stats();

        glm::mat4 view_proj = camera.get_projection_matrix() * glm::inverse(transform);

        if (Renderer::get_api() != RendererAPI::API::vulkan) {
            s_data->camera_buffer.view_projection = view_proj;
            s_data->camera_uniform_buffer->set_data(sizeof(Renderer2DData::CameraData), &s_data->camera_buffer);
        } else {
            VulkanRendererAPI::submit_camera_view_projection(view_proj);
        }

        s_data->quad_instances.clear();
        s_data->circle_instances.clear();
        s_data->line_instances.clear();

        s_data->texture_slot_index = 1; // keep white bound at 0
    }

    void Renderer2D::begin_scene(const EditorCamera& camera) {
        reset_stats();

        glm::mat4 vp = camera.get_view_projection_matrix(); // EngineClip
        //HN_CORE_INFO("Engine VP:\n{}", glm::to_string(vp));

        if (Renderer::get_api() != RendererAPI::API::vulkan) {
            // OpenGL (or any future GL-style API) consumes EngineClip directly.
            s_data->camera_buffer.view_projection = vp;
            s_data->camera_uniform_buffer->set_data(
                sizeof(Renderer2DData::CameraData),
                &s_data->camera_buffer
            );
        } else {
            // Vulkan backend will convert EngineClip to VulkanClip.
            VulkanRendererAPI::submit_camera_view_projection(vp);
        }

        s_data->quad_instances.clear();
        s_data->circle_instances.clear();
        s_data->line_instances.clear();

        s_data->texture_slot_index = 1; // keep white bound at 0
    }

    void Renderer2D::end_scene() {
        quad_end_scene();
        circle_end_scene();
        line_end_scene();
    }

    void Renderer2D::line_end_scene() {
        if (s_data->line_instances.empty())
            return;

        s_data->line_shader->bind();
        // Sort instances by Z coordinate (back to front for correct alpha blending)
        s_data->line_sorted_instances = s_data->line_instances;
        std::sort(s_data->line_sorted_instances.begin(), s_data->line_sorted_instances.end(),
            [](const LineInstance& a, const LineInstance& b) {
                return a.center.z < b.center.z; // Higher Z values drawn first (back to front)
            });

        // Upload sorted instance data
        size_t bytes = s_data->line_sorted_instances.size() * sizeof(LineInstance);
        s_data->i_line_vertex_buffer->set_data(s_data->line_sorted_instances.data(), bytes);

        // Bind textures in the order we populated
        for (uint32_t i = 0; i < s_data->texture_slot_index; ++i)
            s_data->texture_slots[i]->bind(i);

        // Draw all lines in one go
        s_data->line_vertex_array->bind();
        RenderCommand::draw_indexed_instanced(s_data->line_vertex_array, 6, s_data->line_sorted_instances.size());
        s_data->stats.draw_calls++;
    }

    void Renderer2D::circle_end_scene() {
        if (s_data->circle_instances.empty())
            return;

        s_data->circle_shader->bind();
        // Sort instances by Z coordinate (back to front for correct alpha blending)
        s_data->circle_sorted_instances = s_data->circle_instances;
        std::sort(s_data->circle_sorted_instances.begin(), s_data->circle_sorted_instances.end(),
            [](const CircleInstance& a, const CircleInstance& b) {
                return a.center.z < b.center.z; // Higher Z values drawn first (back to front)
            });

        // Upload sorted instance data
        size_t bytes = s_data->circle_sorted_instances.size() * sizeof(CircleInstance);
        s_data->i_circle_vertex_buffer->set_data(s_data->circle_sorted_instances.data(), bytes);

        // Bind textures in the order we populated
        for (uint32_t i = 0; i < s_data->texture_slot_index; ++i)
            s_data->texture_slots[i]->bind(i);

        // Draw all circles in one go
        s_data->circle_vertex_array->bind();
        RenderCommand::draw_indexed_instanced(s_data->circle_vertex_array, 6, s_data->circle_sorted_instances.size());
        s_data->stats.draw_calls++;
    }

    struct VulkanQuadInstancePacked {
        glm::vec3 center;
        glm::vec2 half_size;
        float rotation;
        glm::vec4 color;
    }; // TEMP

    void Renderer2D::quad_end_scene() {
        if (s_data->quad_instances.empty())
            return;

        if (Renderer::get_api() == RendererAPI::API::vulkan) {
            // Sort instances by Z coordinate (back to front for correct alpha blending)
            s_data->quad_sorted_instances = s_data->quad_instances;
            std::sort(s_data->quad_sorted_instances.begin(), s_data->quad_sorted_instances.end(),
                [](const QuadInstance& a, const QuadInstance& b) {
                    return a.center.z < b.center.z;
                });

            // Upload instance data
            const size_t bytes = s_data->quad_sorted_instances.size() * sizeof(QuadInstance);
            s_data->i_quad_vertex_buffer->set_data(s_data->quad_sorted_instances.data(), static_cast<uint32_t>(bytes));

            // Submit bound textures list for Vulkan (slot 0..texture_slot_index-1)
            std::array<void*, VulkanRendererAPI::k_max_texture_slots> vk_textures{};
            const uint32_t count = std::min<uint32_t>(s_data->texture_slot_index, VulkanRendererAPI::k_max_texture_slots);
            for (uint32_t i = 0; i < count; ++i) {
                vk_textures[i] = s_data->texture_slots[i].get();
            }
            VulkanRendererAPI::submit_bound_textures(vk_textures, count);

            s_data->quad_vertex_array->bind();
            RenderCommand::draw_indexed_instanced(s_data->quad_vertex_array, 6, static_cast<uint32_t>(s_data->quad_sorted_instances.size()));
            s_data->stats.draw_calls++;
            return;
        }



        s_data->quad_shader->bind();
        // Sort instances by Z coordinate (back to front for correct alpha blending)
        s_data->quad_sorted_instances = s_data->quad_instances;
        std::sort(s_data->quad_sorted_instances.begin(), s_data->quad_sorted_instances.end(),
            [](const QuadInstance& a, const QuadInstance& b) {
                return a.center.z < b.center.z; // Higher Z values drawn first (back to front)
            });

        // Upload sorted instance data
        size_t bytes = s_data->quad_sorted_instances.size() * sizeof(QuadInstance);
        s_data->i_quad_vertex_buffer->set_data(s_data->quad_sorted_instances.data(), bytes);

        // Bind textures in the order we populated
        for (uint32_t i = 0; i < s_data->texture_slot_index; ++i)
            s_data->texture_slots[i]->bind(i);

        // Draw all quads in one go
        s_data->quad_vertex_array->bind();
        RenderCommand::draw_indexed_instanced(s_data->quad_vertex_array, 6, s_data->quad_sorted_instances.size());
        s_data->stats.draw_calls++;
    }



    static void quad_flush_and_reset() {
        Renderer2D::quad_end_scene();
        s_data->quad_instances.clear();
    }

    static void circle_flush_and_reset() {
        Renderer2D::circle_end_scene();
        s_data->circle_instances.clear();
    }

    static void line_flush_and_reset() {
        Renderer2D::line_end_scene();
        s_data->line_instances.clear();
    }


    void Renderer2D::submit_quad(const glm::vec3& position, const glm::vec2& size, float rotation,
                                const Ref<Texture2D>& texture, const Ref<SubTexture2D>& sub_texture,
                                const glm::vec4& color, float tiling_factor, int entity_id) {
        if (s_data->quad_instances.size() >= Renderer2DData::max_quads)
            quad_flush_and_reset();


        QuadInstance inst;
        inst.center = position;
        inst.half_size = size * 0.5f;
        inst.rotation = rotation;
        inst.color = color;
        inst.tiling_factor = tiling_factor;
        inst.entity_id = entity_id;


        // Handle texture/subtexture
        if (sub_texture) {
            const glm::vec2* tex_coords = sub_texture->get_tex_coords();
            inst.tex_index = resolve_texture_slot(sub_texture->get_texture());
            inst.tex_coord_min = tex_coords[0];
            inst.tex_coord_max = tex_coords[2];
        } else {
            inst.tex_index = resolve_texture_slot(texture);
            inst.tex_coord_min = {0.0f, 0.0f};
            inst.tex_coord_max = {1.0f, 1.0f};
        }

        s_data->quad_instances.push_back(inst);
        ++s_data->stats.quad_count;
    }

    void Renderer2D::submit_circle(const glm::vec3& position, const glm::vec2& size, float thickness,
                                const Ref<Texture2D>& texture, const Ref<SubTexture2D>& sub_texture,
                                const glm::vec4& color, float fade, int entity_id) {
        if (s_data->circle_instances.size() >= Renderer2DData::max_quads)
            circle_flush_and_reset();


        CircleInstance inst;
        inst.center = position;
        //inst.local_position = local_position;
        inst.half_size = size * 0.5f;
        inst.thickness = thickness;
        inst.color = color;
        inst.fade = fade;
        inst.entity_id = entity_id;


        // Handle texture/subtexture
        if (sub_texture) {
            const glm::vec2* tex_coords = sub_texture->get_tex_coords();
            inst.tex_index = resolve_texture_slot(sub_texture->get_texture());
            inst.tex_coord_min = tex_coords[0];
            inst.tex_coord_max = tex_coords[2];
        } else {
            inst.tex_index = resolve_texture_slot(texture);
            inst.tex_coord_min = {0.0f, 0.0f};
            inst.tex_coord_max = {1.0f, 1.0f};
        }

        s_data->circle_instances.push_back(inst);
        ++s_data->stats.quad_count;
    }

    void Renderer2D::submit_line(const glm::vec3& position, const glm::vec2& size, float rotation,
                                const Ref<Texture2D>& texture, const Ref<SubTexture2D>& sub_texture,
                                const glm::vec4& color, float fade, int entity_id) {
        if (s_data->line_instances.size() >= Renderer2DData::max_quads)
            line_flush_and_reset();


        LineInstance inst;
        inst.center = position;
        //inst.local_position = local_position;
        inst.half_size = size * 0.5f;
        inst.rotation = rotation;
        inst.color = color;
        inst.fade = fade;
        inst.entity_id = entity_id;


        // Handle texture/subtexture
        if (sub_texture) {
            const glm::vec2* tex_coords = sub_texture->get_tex_coords();
            inst.tex_index = resolve_texture_slot(sub_texture->get_texture());
            inst.tex_coord_min = tex_coords[0];
            inst.tex_coord_max = tex_coords[2];
        } else {
            inst.tex_index = resolve_texture_slot(texture);
            inst.tex_coord_min = {0.0f, 0.0f};
            inst.tex_coord_max = {1.0f, 1.0f};
        }

        s_data->line_instances.push_back(inst);
        ++s_data->stats.quad_count;
    }

    void Renderer2D::decompose_transform(const glm::mat4& transform, glm::vec3& position,
                                        glm::vec2& scale, float& rotation) {
        // Extract position
        position = glm::vec3(transform[3]);

        // Extract scale
        glm::vec3 scale3d = glm::vec3(
            glm::length(glm::vec3(transform[0])),
            glm::length(glm::vec3(transform[1])),
            glm::length(glm::vec3(transform[2]))
        );
        scale = glm::vec2(scale3d.x, scale3d.y);

        // Extract rotation (simplified - assumes rotation around Z axis)
        rotation = atan2(transform[0][1], transform[0][0]);
    }


    // Public API implementations - all delegate to submit_quad:

    void Renderer2D::draw_quad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color) {
        submit_quad({position.x, position.y, 0.0f}, size, 0.0f, nullptr, nullptr, color, 1.0f);
    }

    void Renderer2D::draw_quad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color) {
        submit_quad(position, size, 0.0f, nullptr, nullptr, color, 1.0f);
    }

    void Renderer2D::draw_quad(const glm::vec2& position, const glm::vec2& size,
                              const Ref<Texture2D>& texture, const glm::vec4& color, float tiling_factor) {
        submit_quad({position.x, position.y, 0.0f}, size, 0.0f, texture, nullptr, color, tiling_factor);
    }

    void Renderer2D::draw_quad(const glm::vec3& position, const glm::vec2& size,
                              const Ref<Texture2D>& texture, const glm::vec4& color, float tiling_factor) {
        submit_quad(position, size, 0.0f, texture, nullptr, color, tiling_factor);
    }

    void Renderer2D::draw_rotated_quad(const glm::vec2& position, const glm::vec2& size,
                                      float rotation, const glm::vec4& color) {
        submit_quad({position.x, position.y, 0.0f}, size, rotation, nullptr, nullptr, color, 1.0f);
    }

    void Renderer2D::draw_rotated_quad(const glm::vec3& position, const glm::vec2& size,
                                      float rotation, const glm::vec4& color) {
        submit_quad(position, size, rotation, nullptr, nullptr, color, 1.0f);
    }

    void Renderer2D::draw_rotated_quad(const glm::vec3& position, const glm::vec2& size, float rotation,
                                      const Ref<Texture2D>& texture, const glm::vec4& color, float tiling_factor) {
        submit_quad(position, size, rotation, texture, nullptr, color, tiling_factor);
    }

    void Renderer2D::draw_quad(const glm::vec3& position, const glm::vec2& size,
                              const Ref<SubTexture2D>& sub_texture, const glm::vec4& color, float tiling_factor) {
        submit_quad(position, size, 0.0f, nullptr, sub_texture, color, tiling_factor);
    }

    void Renderer2D::draw_rotated_quad(const glm::vec3& position, const glm::vec2& size, float rotation,
                                      const Ref<SubTexture2D>& sub_texture, const glm::vec4& color, float tiling_factor) {
        submit_quad(position, size, rotation, nullptr, sub_texture, color, tiling_factor);
    }

    void Renderer2D::draw_quad(const glm::mat4& transform, const glm::vec4& color) {
        glm::vec3 position;
        glm::vec2 scale;
        float rotation;
        decompose_transform(transform, position, scale, rotation);
        submit_quad(position, scale, rotation, nullptr, nullptr, color, 1.0f);
        //HN_CORE_INFO("Entity ID: {0}", entity_id);
    }

    void Renderer2D::draw_quad(const glm::mat4& transform, const Ref<Texture2D>& texture,
                              const glm::vec4& color, float tiling_factor) {
        glm::vec3 position;
        glm::vec2 scale;
        float rotation;
        decompose_transform(transform, position, scale, rotation);
        submit_quad(position, scale, rotation, texture, nullptr, color, tiling_factor);
    }

    void Renderer2D::draw_sprite(const glm::mat4& transform, SpriteRendererComponent& src, int entity_id) {
        glm::vec3 position;
        glm::vec2 scale;
        float rotation;
        decompose_transform(transform, position, scale, rotation);

        if (src.sprite && src.sprite->get_texture()) {

            glm::vec2 pivot_offset = src.sprite->get_pivot_offset();
            glm::vec2 rotated_offset = {
                pivot_offset.x * std::cos(rotation) - pivot_offset.y * std::sin(rotation),
                pivot_offset.x * std::sin(rotation) + pivot_offset.y * std::cos(rotation)
            };
            position += glm::vec3(rotated_offset, 0.0f);

            glm::vec2 size = src.sprite->get_world_size();

            glm::vec2 uv_min, uv_max;
            src.sprite->get_uvs(uv_min, uv_max);

            submit_quad(
                position,
                size,
                rotation,
                src.sprite->get_texture(),
                nullptr,
                src.color,
                1.0f,
                entity_id
            );
        }
        else {
            submit_quad(
                position,
                scale,
                rotation,
                nullptr,      // uses white texture in slot 0
                nullptr,
                src.color,
                1.0f,
                entity_id
            );
        }
    }

    // Circles!

    void Renderer2D::draw_circle(const glm::vec2& position, const glm::vec2& size,
                                 const glm::vec4& color, float thickness, float fade) {
        submit_circle({position.x, position.y, 0.0f}, size, thickness,
                      nullptr, nullptr, color, fade, -1);
    }

    void Renderer2D::draw_circle(const glm::vec3& position, const glm::vec2& size,
                                 const glm::vec4& color, float thickness, float fade) {
        submit_circle(position, size, thickness,
                      nullptr, nullptr, color, fade, -1);
    }

    void Renderer2D::draw_circle(const glm::vec2& position, const glm::vec2& size,
                                 const Ref<Texture2D>& texture, const glm::vec4& color,
                                 float thickness, float fade) {
        submit_circle({position.x, position.y, 0.0f}, size, thickness,
                      texture, nullptr, color, fade, -1);
    }

    void Renderer2D::draw_circle(const glm::vec3& position, const glm::vec2& size,
                                 const Ref<Texture2D>& texture, const glm::vec4& color,
                                 float thickness, float fade) {
        submit_circle(position, size, thickness,
                      texture, nullptr, color, fade, -1);
    }

    void Renderer2D::draw_circle(const glm::vec3& position, const glm::vec2& size,
                                 const Ref<SubTexture2D>& sub_texture, const glm::vec4& color,
                                 float thickness, float fade) {
        submit_circle(position, size, thickness,
                      nullptr, sub_texture, color, fade, -1);
    }

    void Renderer2D::draw_circle(const glm::mat4& transform, const glm::vec4& color,
                                 float thickness, float fade) {
        glm::vec3 position;
        glm::vec2 scale;
        float rotation;
        decompose_transform(transform, position, scale, rotation);
        submit_circle(position, scale, thickness,
                      nullptr, nullptr, color, fade, -1);
    }

    void Renderer2D::draw_circle(const glm::mat4& transform, const Ref<Texture2D>& texture,
                                 const glm::vec4& color, float thickness, float fade) {
        glm::vec3 position;
        glm::vec2 scale;
        float rotation;
        decompose_transform(transform, position, scale, rotation);
        submit_circle(position, scale, thickness,
                      texture, nullptr, color, fade, -1);
    }

    void Renderer2D::draw_circle_sprite(const glm::mat4& transform, CircleRendererComponent& src, int entity_id) {
        glm::vec3 position;
        glm::vec2 scale;
        float rotation;
        decompose_transform(transform, position, scale, rotation);

        if (src.texture)
            submit_circle(position, scale, src.thickness,
                          src.texture, nullptr, src.color, src.fade, entity_id);
        else
            submit_circle(position, scale, src.thickness,
                          nullptr, nullptr, src.color, src.fade, entity_id);
    }

    // lines!!
    void Renderer2D::draw_line_sprite(const glm::mat4& transform, LineRendererComponent& src, int entity_id) { // Note to future self: do some math here to make the line renderers inputs (fade thickness etc) work for quads
        glm::vec3 position;                                                                                     // Note to two seconds ago self: you cannot do a fade effect without a different shader so this might as well follow the circles method (third shader)
        glm::vec2 scale;
        float rotation;
        decompose_transform(transform, position, scale, rotation);

        if (src.texture)
            submit_line(position, scale, rotation, src.texture, nullptr, src.color, src.fade, entity_id);
        else
            submit_line(position, scale, rotation, nullptr, nullptr, src.color, src.fade, entity_id);
    }

    void Renderer2D::draw_rect(const glm::mat4& transform, const glm::vec4& color) {
        glm::vec3 position;
        glm::vec2 scale;
        float rotation;
        decompose_transform(transform, position, scale, rotation);

        constexpr float thickness = 0.025f;
        const float half_t = thickness * 0.5f;

        const float half_w = scale.x * 0.5f;
        const float half_h = scale.y * 0.5f;

        auto rotate = [](const glm::vec2& v, float r) {
            float c = std::cos(r);
            float s = std::sin(r);
            return glm::vec2(
                c * v.x - s * v.y,
                s * v.x + c * v.y
            );
        };

        // Local edge centers
        const glm::vec2 top_local    = { 0.0f,  half_h - half_t };
        const glm::vec2 bottom_local = { 0.0f, -half_h + half_t };
        const glm::vec2 left_local   = { -half_w + half_t, 0.0f };
        const glm::vec2 right_local  = {  half_w - half_t, 0.0f };

        // Rotate into world space
        const glm::vec3 top    = position + glm::vec3(rotate(top_local, rotation), 0.0f);
        const glm::vec3 bottom = position + glm::vec3(rotate(bottom_local, rotation), 0.0f);
        const glm::vec3 left   = position + glm::vec3(rotate(left_local, rotation), 0.0f);
        const glm::vec3 right  = position + glm::vec3(rotate(right_local, rotation), 0.0f);

        // Horizontal edges
        submit_line(top,    { scale.x, thickness }, rotation, nullptr, nullptr, color, 0.0f, -1);
        submit_line(bottom, { scale.x, thickness }, rotation, nullptr, nullptr, color, 0.0f, -1);

        // Vertical edges
        submit_line(left,  { scale.y, thickness }, rotation + glm::half_pi<float>(), nullptr, nullptr, color, 0.0f, -1);
        submit_line(right, { scale.y, thickness }, rotation + glm::half_pi<float>(), nullptr, nullptr, color, 0.0f, -1);
    }


    Renderer2D::Statistics Renderer2D::get_stats() { return s_data->stats; }

    void Renderer2D::reset_stats() { memset(&s_data->stats, 0, sizeof(Statistics)); }
} // namespace Honey
