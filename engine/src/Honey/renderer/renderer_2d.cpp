#include "hnpch.h"
#include "renderer_2d.h"

#include "render_command.h"
#include "vertex_array.h"
#include "shader.h"
#include "texture.h"

namespace Honey {


struct QuadInstance {
    glm::vec2  center;        // world‑space centre of the quad
    glm::vec2  half_size;      // size * 0.5f
    float      rotation;      // radians – 0 for axis‑aligned
    glm::vec4  color;
    float      tex_index;      // which of the bound textures to sample
    float      tiling_factor;
    glm::vec2  tex_coord_min;
    glm::vec2  tex_coord_max;
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

    // GL objects
    Ref<VertexArray>  vao;
    Ref<VertexBuffer> static_vbo;   // 4 vertices
    Ref<VertexBuffer> instance_vbo; // maxQuads * QuadInstance
    Ref<IndexBuffer>  ibo;         // 6 indices
    Ref<Shader>       shader;

    // CPU‑side ring buffer for instances
    QuadInstance*     instance_base = nullptr;
    QuadInstance*     instance_ptr  = nullptr;
    uint32_t          instance_count = 0;

    // Texture slots
    uint32_t                       max_texture_slots = 0;
    std::vector<Ref<Texture2D>>    texture_slots;
    uint32_t                       texture_slot_index = 1; // 0 is white tex
    Ref<Texture2D>                 white_texture;

    // Stats
    Renderer2D::Statistics stats;
};

static Renderer2DData s_data;


static float resolve_texture_slot(const Ref<Texture2D>& tex)
{
    if (!tex)
        return 0.0f; // white

    // Already bound this frame?
    for (uint32_t i = 1; i < s_data.texture_slot_index; ++i)
        if (s_data.texture_slots[i] && *s_data.texture_slots[i] == *tex)
            return static_cast<float>(i);

    // Need a new slot
    if (s_data.texture_slot_index >= s_data.max_texture_slots) {
        HN_CORE_WARN("Texture slot limit exceeded – using white texture");
        return 0.0f;
    }
    uint32_t idx = s_data.texture_slot_index++;
    s_data.texture_slots[idx] = tex;
    return static_cast<float>(idx);
}


void Renderer2D::init()
{
    HN_PROFILE_FUNCTION();

    s_data.vao = VertexArray::create();


    s_data.static_vbo = VertexBuffer::create(sizeof(s_static_quad));
    s_data.static_vbo->set_data(s_static_quad, sizeof(s_static_quad));
    {
        BufferLayout layout = {
            { ShaderDataType::Float2, "a_local_pos"  },  // loc 0
            { ShaderDataType::Float2, "a_local_tex"  },  // loc 1
        };
        s_data.static_vbo->set_layout(layout);
        s_data.vao->add_vertex_buffer(s_data.static_vbo); // divisor 0 (default)
    }

    s_data.instance_vbo = VertexBuffer::create(Renderer2DData::max_quads * sizeof(QuadInstance));
    {
        BufferLayout layout = {
            { ShaderDataType::Float2, "i_center", false, true }, // loc 2
            { ShaderDataType::Float2, "i_half_size", false, true }, // loc 3
            { ShaderDataType::Float , "i_rotation", false, true }, // loc 4
            { ShaderDataType::Float4, "i_color", false, true }, // loc 5
            { ShaderDataType::Float , "i_tex_index", false, true }, // loc 6
            { ShaderDataType::Float , "i_tiling", false, true }, // loc 7
            { ShaderDataType::Float2 , "i_tex_coord_min", false, true }, // loc 8
            { ShaderDataType::Float2 , "i_tex_coord_max", false, true }, // loc 9
        };
        s_data.instance_vbo->set_layout(layout);
        s_data.vao->add_vertex_buffer(s_data.instance_vbo);
    }

    uint32_t indices[6] = {0,1,2, 2,3,0};
    s_data.ibo = IndexBuffer::create(indices, 6);
    s_data.vao->set_index_buffer(s_data.ibo);

    s_data.instance_base = new QuadInstance[Renderer2DData::max_quads];

    s_data.max_texture_slots = RenderCommand::get_max_texture_slots();
    s_data.texture_slots.resize(s_data.max_texture_slots);

    s_data.white_texture = Texture2D::create(1,1);
    uint32_t white = 0xffffffff;
    s_data.white_texture->set_data(&white, sizeof(uint32_t));
    s_data.texture_slots[0] = s_data.white_texture;

    s_data.shader = Shader::create("../../application/assets/shaders/texture.glsl");
    s_data.shader->bind();
    {
        std::vector<int> samplers(s_data.max_texture_slots);
        std::iota(samplers.begin(), samplers.end(), 0);
        s_data.shader->set_int_array("u_textures", samplers.data(), samplers.size());
    }
}

void Renderer2D::shutdown()
{
    delete[] s_data.instance_base;
}


void Renderer2D::begin_scene(const OrthographicCamera& cam)
{
    reset_stats();

    s_data.shader->bind();
    s_data.shader->set_mat4("u_view_projection", cam.get_view_projection_matrix());

    s_data.instance_ptr    = s_data.instance_base;
    s_data.instance_count  = 0;
    s_data.texture_slot_index = 1; // keep white bound at 0
}

void Renderer2D::end_scene()
{
    // Upload instance data (sub‑data is fine; persistent‑map even better)
    size_t bytes = s_data.instance_count * sizeof(QuadInstance);
    s_data.instance_vbo->set_data(s_data.instance_base, bytes);

    // Bind textures in the order we populated
    for (uint32_t i = 0; i < s_data.texture_slot_index; ++i)
        s_data.texture_slots[i]->bind(i);

    // Draw all quads in one go
    s_data.vao->bind();
    RenderCommand::draw_indexed_instanced(s_data.vao, 6, s_data.instance_count);
    s_data.stats.draw_calls++;
}


static void flush_and_reset()
{
    Renderer2D::end_scene();
    s_data.instance_ptr   = s_data.instance_base;
    s_data.instance_count = 0;
    //s_data.texture_slot_index = 1;
}

void Renderer2D::draw_quad(const glm::vec3& pos,
                           const glm::vec2& size,
                           const Ref<Texture2D>& tex,
                           const glm::vec4& color,
                           float tiling)
{
    if (s_data.instance_count >= Renderer2DData::max_quads)
        flush_and_reset();

    QuadInstance& inst = *s_data.instance_ptr++;
    inst.center       = {pos.x, pos.y};
    inst.half_size     = size * 0.5f;
    inst.rotation     = 0.0f;
    inst.color        = color;
    inst.tex_index     = resolve_texture_slot(tex);
    inst.tiling_factor = tiling;
    inst.tex_coord_min = {0.0f, 0.0f};
    inst.tex_coord_max = {1.0f, 1.0f};


    ++s_data.instance_count;
    ++s_data.stats.quad_count;
}

void Renderer2D::draw_quad(const glm::vec2& pos, const glm::vec2& size,
                           const Ref<Texture2D>& tex, const glm::vec4& col,
                           float tiling)
{
    draw_quad({pos.x, pos.y, 0.0f}, size, tex, col, tiling);
}

void Renderer2D::draw_quad(const glm::vec3& pos, const glm::vec2& size,
                           const glm::vec4& col)
{
    draw_quad(pos, size, Ref<Texture2D>(nullptr), col, 1.0f);
}

void Renderer2D::draw_quad(const glm::vec2& pos, const glm::vec2& size,
                           const glm::vec4& col)
{
    draw_quad({pos.x, pos.y, 0.0f}, size, Ref<Texture2D>(nullptr), col, 1.0f);
}

void Renderer2D::draw_rotated_quad(const glm::vec3& pos, const glm::vec2& size,
                                   float rot, const Ref<Texture2D>& tex,
                                   const glm::vec4& col, float tiling)
{
    if (s_data.instance_count >= Renderer2DData::max_quads)
        flush_and_reset();

    QuadInstance& inst = *s_data.instance_ptr++;
    inst.center       = {pos.x, pos.y};
    inst.half_size     = size * 0.5f;
    inst.rotation     = rot;
    inst.color        = col;
    inst.tex_index     = resolve_texture_slot(tex);
    inst.tiling_factor = tiling;

    ++s_data.instance_count;
    ++s_data.stats.quad_count;
}

void Renderer2D::draw_rotated_quad(const glm::vec3& pos, const glm::vec2& size,
                                   float rot, const glm::vec4& col)
{
    draw_rotated_quad(pos, size, rot, Ref<Texture2D>(nullptr), col, 1.0f);
}

    void Renderer2D::draw_quad(const glm::vec3& pos, const glm::vec2& size,
                          const Ref<SubTexture2D>& sub_texture,
                          const glm::vec4& color, float tiling) {
    if (s_data.instance_count >= Renderer2DData::max_quads)
        flush_and_reset();

    const glm::vec2* tex_coords = sub_texture->get_tex_coords();

    QuadInstance& inst = *s_data.instance_ptr++;
    inst.center = {pos.x, pos.y};
    inst.half_size = size * 0.5f;
    inst.rotation = 0.0f;
    inst.color = color;
    inst.tex_index = resolve_texture_slot(sub_texture->get_texture());
    inst.tiling_factor = tiling;
    inst.tex_coord_min = tex_coords[0];  // bottom-left
    inst.tex_coord_max = tex_coords[2];  // top-right

    ++s_data.instance_count;
    ++s_data.stats.quad_count;
}

    void Renderer2D::draw_rotated_quad(const glm::vec3& pos, const glm::vec2& size,
                                      float rotation, const Ref<SubTexture2D>& sub_texture,
                                      const glm::vec4& color, float tiling) {
    if (s_data.instance_count >= Renderer2DData::max_quads)
        flush_and_reset();

    const glm::vec2* tex_coords = sub_texture->get_tex_coords();

    QuadInstance& inst = *s_data.instance_ptr++;
    inst.center = {pos.x, pos.y};
    inst.half_size = size * 0.5f;
    inst.rotation = rotation;
    inst.color = color;
    inst.tex_index = resolve_texture_slot(sub_texture->get_texture());
    inst.tiling_factor = tiling;
    inst.tex_coord_min = tex_coords[0];
    inst.tex_coord_max = tex_coords[2];

    ++s_data.instance_count;
    ++s_data.stats.quad_count;
}


Renderer2D::Statistics Renderer2D::get_stats() { return s_data.stats; }

void Renderer2D::reset_stats() { memset(&s_data.stats, 0, sizeof(Statistics)); }



} // namespace Honey