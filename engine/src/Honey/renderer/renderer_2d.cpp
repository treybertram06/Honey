#include "hnpch.h"
#include "renderer_2d.h"

#include "render_command.h"
#include "vertex_array.h"
#include "shader.h"
#include "texture.h"

namespace Honey {

// ──────────────────────────────────────────────────────────────────────────────
// 1.  Per‑instance payload sent to the GPU
// ──────────────────────────────────────────────────────────────────────────────
struct QuadInstance {
    glm::vec2  center;        // world‑space centre of the quad
    glm::vec2  halfSize;      // size * 0.5f
    float      rotation;      // radians – 0 for axis‑aligned
    glm::vec4  color;
    float      texIndex;      // which of the bound textures to sample
    float      tilingFactor;
};

// ──────────────────────────────────────────────────────────────────────────────
// 2.  Static quad mesh (shared by every sprite)
// ──────────────────────────────────────────────────────────────────────────────
struct QuadVertexStatic {
    glm::vec2 localPos;   // corners at (±0.5, ±0.5)
    glm::vec2 localTex;   // UVs 0‑1
};

static const QuadVertexStatic s_staticQuad[4] = {
    {{-0.5f,-0.5f}, {0.0f,0.0f}},
    {{ 0.5f,-0.5f}, {1.0f,0.0f}},
    {{ 0.5f, 0.5f}, {1.0f,1.0f}},
    {{-0.5f, 0.5f}, {0.0f,1.0f}}
};

// ──────────────────────────────────────────────────────────────────────────────
struct Renderer2DData {
    static constexpr uint32_t maxQuads     = 10'000;
    static constexpr uint32_t maxTextures  = 32;   // keep in sync with shader

    // GL objects
    Ref<VertexArray>  vao;
    Ref<VertexBuffer> staticVBO;   // 4 vertices
    Ref<VertexBuffer> instanceVBO; // maxQuads * QuadInstance
    Ref<IndexBuffer>  ibo;         // 6 indices
    Ref<Shader>       shader;

    // CPU‑side ring buffer for instances
    QuadInstance*     instanceBase = nullptr;
    QuadInstance*     instancePtr  = nullptr;
    uint32_t          instanceCount = 0;

    // Texture slots
    uint32_t                       maxTextureSlots = 0;
    std::vector<Ref<Texture2D>>    textureSlots;
    uint32_t                       textureSlotIndex = 1; // 0 is white tex
    Ref<Texture2D>                 whiteTexture;

    // Stats
    Renderer2D::Statistics stats;
};

static Renderer2DData s_data;

// ──────────────────────────────────────────────────────────────────────────────
// Helper — find / allocate a texture slot (returns index)
// ──────────────────────────────────────────────────────────────────────────────
static float resolve_texture_slot(const Ref<Texture2D>& tex)
{
    if (!tex)
        return 0.0f; // white

    // Already bound this frame?
    for (uint32_t i = 1; i < s_data.textureSlotIndex; ++i)
        if (s_data.textureSlots[i] && *s_data.textureSlots[i] == *tex)
            return static_cast<float>(i);

    // Need a new slot
    if (s_data.textureSlotIndex >= s_data.maxTextureSlots) {
        HN_CORE_WARN("Texture slot limit exceeded – using white texture");
        return 0.0f;
    }
    uint32_t idx = s_data.textureSlotIndex++;
    s_data.textureSlots[idx] = tex;
    return static_cast<float>(idx);
}

// ──────────────────────────────────────────────────────────────────────────────
void Renderer2D::init()
{
    HN_PROFILE_FUNCTION();

    // ── 2.1  Allocate VAO
    s_data.vao = VertexArray::create();

    // ── 2.2  Static quad VBO (divisor 0) -------------------------------------

    s_data.staticVBO = VertexBuffer::create(sizeof(s_staticQuad));
    s_data.staticVBO->set_data(s_staticQuad, sizeof(s_staticQuad));
    {
        BufferLayout layout = {
            { ShaderDataType::Float2, "a_local_pos"  },  // loc 0
            { ShaderDataType::Float2, "a_local_tex"  },  // loc 1
        };
        s_data.staticVBO->set_layout(layout);
        s_data.vao->add_vertex_buffer(s_data.staticVBO); // divisor 0 (default)
    }

    // ── 2.3  Instance VBO (divisor 1) ----------------------------------------
    s_data.instanceVBO = VertexBuffer::create(Renderer2DData::maxQuads * sizeof(QuadInstance));
    {
        BufferLayout layout = {
            { ShaderDataType::Float2, "i_center", false, true }, // loc 2
            { ShaderDataType::Float2, "i_half_size", false, true }, // loc 3
            { ShaderDataType::Float , "i_rotation", false, true }, // loc 4
            { ShaderDataType::Float4, "i_color", false, true }, // loc 5
            { ShaderDataType::Float , "i_tex_index", false, true }, // loc 6
            { ShaderDataType::Float , "i_tiling", false, true }, // loc 7
        };
        s_data.instanceVBO->set_layout(layout);
        s_data.vao->add_vertex_buffer(s_data.instanceVBO);
    }

    // ── 2.4  Indices ----------------------------------------------------------
    uint32_t indices[6] = {0,1,2, 2,3,0};
    s_data.ibo = IndexBuffer::create(indices, 6);
    s_data.vao->set_index_buffer(s_data.ibo);

    // ── 2.5  CPU instance buffer ---------------------------------------------
    s_data.instanceBase = new QuadInstance[Renderer2DData::maxQuads];

    // ── 2.6  White texture + slots -------------------------------------------
    s_data.maxTextureSlots = RenderCommand::get_max_texture_slots();
    s_data.textureSlots.resize(s_data.maxTextureSlots);

    s_data.whiteTexture = Texture2D::create(1,1);
    uint32_t white = 0xffffffff;
    s_data.whiteTexture->set_data(&white, sizeof(uint32_t));
    s_data.textureSlots[0] = s_data.whiteTexture;

    // ── 2.7  Shader -----------------------------------------------------------
    s_data.shader = Shader::create("../../application/assets/shaders/texture.glsl");
    s_data.shader->bind();
    {
        std::vector<int> samplers(s_data.maxTextureSlots);
        std::iota(samplers.begin(), samplers.end(), 0);
        s_data.shader->set_int_array("u_textures", samplers.data(), samplers.size());
    }
}

void Renderer2D::shutdown()
{
    delete[] s_data.instanceBase;
}

// ──────────────────────────────────────────────────────────────────────────────
// Scene lifecycle
// ──────────────────────────────────────────────────────────────────────────────
void Renderer2D::begin_scene(const OrthographicCamera& cam)
{
    reset_stats();

    s_data.shader->bind();
    s_data.shader->set_mat4("u_view_projection", cam.get_view_projection_matrix());

    s_data.instancePtr    = s_data.instanceBase;
    s_data.instanceCount  = 0;
    s_data.textureSlotIndex = 1; // keep white bound at 0
}

void Renderer2D::end_scene()
{
    // Upload instance data (sub‑data is fine; persistent‑map even better)
    size_t bytes = s_data.instanceCount * sizeof(QuadInstance);
    s_data.instanceVBO->set_data(s_data.instanceBase, bytes);

    // Bind textures in the order we populated
    for (uint32_t i = 0; i < s_data.textureSlotIndex; ++i)
        s_data.textureSlots[i]->bind(i);

    // Draw all quads in one go
    s_data.vao->bind();
    RenderCommand::draw_indexed_instanced(s_data.vao, 6, s_data.instanceCount);
    s_data.stats.draw_calls++;
}

// ──────────────────────────────────────────────────────────────────────────────
// Flush helpers
// ──────────────────────────────────────────────────────────────────────────────
static void flush_and_reset()
{
    Renderer2D::end_scene();
    s_data.instancePtr   = s_data.instanceBase;
    s_data.instanceCount = 0;
    s_data.textureSlotIndex = 1;
}

// ──────────────────────────────────────────────────────────────────────────────
// Public draw API (no rotation) ===============================================
// ──────────────────────────────────────────────────────────────────────────────
void Renderer2D::draw_quad(const glm::vec3& pos,
                           const glm::vec2& size,
                           const Ref<Texture2D>& tex,
                           const glm::vec4& color,
                           float tiling)
{
    if (s_data.instanceCount >= Renderer2DData::maxQuads)
        flush_and_reset();

    QuadInstance& inst = *s_data.instancePtr++;
    inst.center       = {pos.x, pos.y};
    inst.halfSize     = size * 0.5f;
    inst.rotation     = 0.0f;
    inst.color        = color;
    inst.texIndex     = resolve_texture_slot(tex);
    inst.tilingFactor = tiling;

    ++s_data.instanceCount;
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
    draw_quad(pos, size, nullptr, col, 1.0f);
}

void Renderer2D::draw_quad(const glm::vec2& pos, const glm::vec2& size,
                           const glm::vec4& col)
{
    draw_quad({pos.x, pos.y, 0.0f}, size, nullptr, col, 1.0f);
}

// ──────────────────────────────────────────────────────────────────────────────
// Rotated version =============================================================
// ──────────────────────────────────────────────────────────────────────────────
void Renderer2D::draw_rotated_quad(const glm::vec3& pos, const glm::vec2& size,
                                   float rot, const Ref<Texture2D>& tex,
                                   const glm::vec4& col, float tiling)
{
    if (s_data.instanceCount >= Renderer2DData::maxQuads)
        flush_and_reset();

    QuadInstance& inst = *s_data.instancePtr++;
    inst.center       = {pos.x, pos.y};
    inst.halfSize     = size * 0.5f;
    inst.rotation     = rot;
    inst.color        = col;
    inst.texIndex     = resolve_texture_slot(tex);
    inst.tilingFactor = tiling;

    ++s_data.instanceCount;
    ++s_data.stats.quad_count;
}

void Renderer2D::draw_rotated_quad(const glm::vec3& pos, const glm::vec2& size,
                                   float rot, const glm::vec4& col)
{
    draw_rotated_quad(pos, size, rot, nullptr, col, 1.0f);
}

// ──────────────────────────────────────────────────────────────────────────────
// Stats helpers
// ──────────────────────────────────────────────────────────────────────────────
Renderer2D::Statistics Renderer2D::get_stats() { return s_data.stats; }

void Renderer2D::reset_stats() { memset(&s_data.stats, 0, sizeof(Statistics)); }



} // namespace Honey