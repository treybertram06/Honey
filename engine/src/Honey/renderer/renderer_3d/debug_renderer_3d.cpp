#include "hnpch.h"
#include "debug_renderer_3d.h"

#include "Honey/renderer/buffer.h"
#include "Honey/renderer/gpu_types.h"
#include "Honey/renderer/pipeline.h"
#include "Honey/renderer/pipeline_spec.h"
#include "Honey/renderer/render_command.h"
#include "Honey/renderer/renderer.h"
#include "Honey/renderer/shader_cache.h"
#include "Honey/renderer/vertex_array.h"
#include "platform/vulkan/vk_framebuffer.h"
#include "platform/vulkan/vk_renderer_api.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>
#include <glm/gtx/norm.hpp>

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey {

// ── Internal data ─────────────────────────────────────────────────────────────

namespace {

struct DebugVertex {
    glm::vec3 position;
    glm::vec4 color;
};
static_assert(sizeof(DebugVertex) == 28, "DebugVertex layout changed");

static constexpr uint32_t k_max_debug_vertices = 65536; // 32768 lines

struct DebugRenderer3DData {
    Ref<VertexArray>  vertex_array;
    Ref<VertexBuffer> vertex_buffer; // dynamic
    Ref<IndexBuffer>  index_buffer;  // static sequential [0,1,2,...,k_max-1]

    std::vector<DebugVertex> cpu_buffer;
    uint32_t vertex_count = 0;

    glm::mat4 view_proj{1.0f};
    bool active = false;

    std::unordered_map<void*, Ref<Pipeline>> pipeline_cache;
    Ref<ShaderCache> shader_cache;

    DebugRenderer3D::Stats stats{};
};

DebugRenderer3DData* s_data = nullptr;

Ref<Pipeline> get_or_create_pipeline(void* rp_native, uint32_t color_attachment_count)
{
    HN_CORE_ASSERT(rp_native, "DebugRenderer3D: render pass handle is null");
    auto it = s_data->pipeline_cache.find(rp_native);
    if (it != s_data->pipeline_cache.end())
        return it->second;

    auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_Debug.glsl");
    spec.topology              = PrimitiveTopology::Lines;
    spec.cullMode              = CullMode::None;
    spec.depthStencil.depthTest  = false;
    spec.depthStencil.depthWrite = false;

    // Match the render pass's color attachment count exactly.
    // Only the first attachment (color) has blending enabled; the rest (e.g.
    // entity-ID, G-buffer layers) are left as opaque pass-through.
    spec.perColorAttachmentBlend.assign(color_attachment_count, AttachmentBlendState{});
    if (color_attachment_count > 0)
        spec.perColorAttachmentBlend[0].enabled = true;

    auto pipeline = Pipeline::create(spec, rp_native);
    s_data->pipeline_cache.emplace(rp_native, pipeline);
    return pipeline;
}

} // anonymous namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void DebugRenderer3D::init()
{
    HN_PROFILE_FUNCTION();
    if (s_data)
        return;

    s_data = new DebugRenderer3DData();
    s_data->shader_cache = Renderer::get_shader_cache();
    s_data->cpu_buffer.reserve(k_max_debug_vertices);

    // Dynamic vertex buffer
    s_data->vertex_buffer = VertexBuffer::create(k_max_debug_vertices * sizeof(DebugVertex));
    {
        BufferLayout layout = {
            { ShaderDataType::Float3, "a_position" }, // loc 0
            { ShaderDataType::Float4, "a_color"    }, // loc 1
        };
        s_data->vertex_buffer->set_layout(layout);
    }

    // Static sequential index buffer [0, 1, 2, ..., k_max_debug_vertices-1]
    std::vector<uint32_t> indices(k_max_debug_vertices);
    std::iota(indices.begin(), indices.end(), 0u);
    s_data->index_buffer = IndexBuffer::create(indices.data(), (uint32_t)indices.size());

    s_data->vertex_array = VertexArray::create();
    s_data->vertex_array->add_vertex_buffer(s_data->vertex_buffer);
    s_data->vertex_array->set_index_buffer(s_data->index_buffer);
}

void DebugRenderer3D::shutdown()
{
    HN_PROFILE_FUNCTION();
    delete s_data;
    s_data = nullptr;
}

// ── Frame API ─────────────────────────────────────────────────────────────────

void DebugRenderer3D::begin_scene(const glm::mat4& view_proj)
{
    HN_CORE_ASSERT(s_data, "DebugRenderer3D::begin_scene called before init()");
    s_data->view_proj    = view_proj;
    s_data->vertex_count = 0;
    s_data->cpu_buffer.clear();
    s_data->stats        = {};
    s_data->active       = true;
}

void DebugRenderer3D::end_scene()
{
    HN_CORE_ASSERT(s_data, "DebugRenderer3D::end_scene called before init()");
    s_data->active = false;

    if (s_data->vertex_count == 0)
        return;

    if (Renderer::get_api() != RendererAPI::API::vulkan)
        return; // OpenGL path not implemented

    // Upload CPU buffer to GPU
    s_data->vertex_buffer->set_data(
        s_data->cpu_buffer.data(),
        s_data->vertex_count * (uint32_t)sizeof(DebugVertex)
    );

    // Get current render pass from the active framebuffer
    auto fb = Renderer::get_render_target();
    auto* vk_fb = dynamic_cast<VulkanFramebuffer*>(fb.get());
    HN_CORE_ASSERT(vk_fb, "DebugRenderer3D: expected active render target to be a VulkanFramebuffer");

    void* rp_native = vk_fb->get_render_pass();
    Ref<Pipeline> pipe = get_or_create_pipeline(rp_native, vk_fb->get_color_attachment_count());

    RenderCommand::bind_pipeline(pipe);

    // Submit camera — debug shader only needs binding 0 (camera UBO).
    // Texture bindings (4) are PARTIALLY_BOUND so they don't need to be valid
    // since the debug shader never samples them.
    CameraUBO cam{};
    cam.view_proj = s_data->view_proj;
    VulkanRendererAPI::submit_camera(cam);
    VulkanRendererAPI::flush_globals();

    s_data->vertex_array->bind();
    RenderCommand::draw_indexed(s_data->vertex_array, s_data->vertex_count);

    s_data->stats.draw_calls++;
}

bool DebugRenderer3D::is_active()
{
    return s_data && s_data->active;
}

// ── Stats ─────────────────────────────────────────────────────────────────────

void DebugRenderer3D::reset_stats()
{
    if (s_data)
        s_data->stats = {};
}

DebugRenderer3D::Stats DebugRenderer3D::get_stats()
{
    return s_data ? s_data->stats : Stats{};
}

// ── Primitives ────────────────────────────────────────────────────────────────

void DebugRenderer3D::draw_line(glm::vec3 from, glm::vec3 to, glm::vec4 color)
{
    if (!s_data || !s_data->active)
        return;

    if (s_data->vertex_count + 2 > k_max_debug_vertices) {
        s_data->stats.dropped_lines++;
        return;
    }

    s_data->cpu_buffer.push_back({ from, color });
    s_data->cpu_buffer.push_back({ to,   color });
    s_data->vertex_count += 2;
    s_data->stats.line_count++;
}

void DebugRenderer3D::draw_wire_aabb(glm::vec3 mn, glm::vec3 mx, glm::vec4 color)
{
    // 8 corners, 12 edges
    glm::vec3 c[8] = {
        { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z },
        { mx.x, mx.y, mn.z }, { mn.x, mx.y, mn.z },
        { mn.x, mn.y, mx.z }, { mx.x, mn.y, mx.z },
        { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z },
    };
    // bottom face
    draw_line(c[0], c[1], color); draw_line(c[1], c[2], color);
    draw_line(c[2], c[3], color); draw_line(c[3], c[0], color);
    // top face
    draw_line(c[4], c[5], color); draw_line(c[5], c[6], color);
    draw_line(c[6], c[7], color); draw_line(c[7], c[4], color);
    // verticals
    draw_line(c[0], c[4], color); draw_line(c[1], c[5], color);
    draw_line(c[2], c[6], color); draw_line(c[3], c[7], color);
}

void DebugRenderer3D::draw_wire_box(glm::vec3 center, glm::vec3 half_extents,
                                    glm::quat rotation, glm::vec4 color)
{
    // Build 8 corners in local space then rotate and translate.
    static const glm::vec3 signs[8] = {
        {-1,-1,-1},{+1,-1,-1},{+1,+1,-1},{-1,+1,-1},
        {-1,-1,+1},{+1,-1,+1},{+1,+1,+1},{-1,+1,+1},
    };
    glm::vec3 c[8];
    glm::mat3 R = glm::mat3_cast(rotation);
    for (int i = 0; i < 8; ++i)
        c[i] = center + R * (signs[i] * half_extents);

    draw_line(c[0], c[1], color); draw_line(c[1], c[2], color);
    draw_line(c[2], c[3], color); draw_line(c[3], c[0], color);
    draw_line(c[4], c[5], color); draw_line(c[5], c[6], color);
    draw_line(c[6], c[7], color); draw_line(c[7], c[4], color);
    draw_line(c[0], c[4], color); draw_line(c[1], c[5], color);
    draw_line(c[2], c[6], color); draw_line(c[3], c[7], color);
}

void DebugRenderer3D::draw_wire_sphere(glm::vec3 center, float radius,
                                       glm::vec4 color, int segments)
{
    // Three great circles: XY, XZ, YZ planes
    const float step = glm::two_pi<float>() / (float)segments;
    for (int i = 0; i < segments; ++i) {
        float a0 = step * (float)i;
        float a1 = step * (float)(i + 1);
        float c0 = glm::cos(a0) * radius, s0 = glm::sin(a0) * radius;
        float c1 = glm::cos(a1) * radius, s1 = glm::sin(a1) * radius;

        draw_line(center + glm::vec3(c0, s0, 0), center + glm::vec3(c1, s1, 0), color); // XY
        draw_line(center + glm::vec3(c0, 0, s0), center + glm::vec3(c1, 0, s1), color); // XZ
        draw_line(center + glm::vec3(0, c0, s0), center + glm::vec3(0, c1, s1), color); // YZ
    }
}

void DebugRenderer3D::draw_wire_capsule(glm::vec3 base, glm::vec3 tip, float radius,
                                        glm::vec4 color, int segments)
{
    glm::vec3 axis = tip - base;
    float height = glm::length(axis);
    if (height < 1e-6f) {
        draw_wire_sphere(base, radius, color, segments);
        return;
    }
    axis /= height;

    // Build two orthogonal vectors perpendicular to the axis
    glm::vec3 perp = glm::abs(axis.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::vec3 u = glm::normalize(glm::cross(axis, perp));
    glm::vec3 v = glm::cross(axis, u);

    const float step = glm::two_pi<float>() / (float)segments;

    // Cylinder edges (4 longitudinal lines at 90° intervals)
    for (int i = 0; i < 4; ++i) {
        float a = step * (float)(segments / 4) * (float)i;
        glm::vec3 r = (glm::cos(a) * u + glm::sin(a) * v) * radius;
        draw_line(base + r, tip + r, color);
    }

    // Circle outlines at base and tip
    for (int i = 0; i < segments; ++i) {
        float a0 = step * (float)i, a1 = step * (float)(i + 1);
        glm::vec3 r0 = (glm::cos(a0) * u + glm::sin(a0) * v) * radius;
        glm::vec3 r1 = (glm::cos(a1) * u + glm::sin(a1) * v) * radius;
        draw_line(base + r0, base + r1, color);
        draw_line(tip  + r0, tip  + r1, color);
    }

    // Hemisphere outlines — two semicircles at each end, in the u and v planes.
    // Use π/half as the step so the arc spans exactly 0..π.
    const int half = segments / 2;
    const float hstep = glm::pi<float>() / (float)half;
    for (int i = 0; i < half; ++i) {
        float a0 = hstep * (float)i, a1 = hstep * (float)(i + 1);
        // base hemisphere (arc goes inward along -axis)
        glm::vec3 p0b = base + (glm::cos(a0) * u - glm::sin(a0) * axis) * radius;
        glm::vec3 p1b = base + (glm::cos(a1) * u - glm::sin(a1) * axis) * radius;
        draw_line(p0b, p1b, color);
        glm::vec3 p0bv = base + (glm::cos(a0) * v - glm::sin(a0) * axis) * radius;
        glm::vec3 p1bv = base + (glm::cos(a1) * v - glm::sin(a1) * axis) * radius;
        draw_line(p0bv, p1bv, color);
        // tip hemisphere (arc goes outward along +axis)
        glm::vec3 p0t = tip + (glm::cos(a0) * u + glm::sin(a0) * axis) * radius;
        glm::vec3 p1t = tip + (glm::cos(a1) * u + glm::sin(a1) * axis) * radius;
        draw_line(p0t, p1t, color);
        glm::vec3 p0tv = tip + (glm::cos(a0) * v + glm::sin(a0) * axis) * radius;
        glm::vec3 p1tv = tip + (glm::cos(a1) * v + glm::sin(a1) * axis) * radius;
        draw_line(p0tv, p1tv, color);
    }
}

void DebugRenderer3D::draw_wire_frustum(const glm::mat4& inv_vp, glm::vec4 color)
{
    // Eight NDC corners unprojected to world space
    static const glm::vec4 ndc[8] = {
        {-1,-1,-1,1},{+1,-1,-1,1},{+1,+1,-1,1},{-1,+1,-1,1},
        {-1,-1,+1,1},{+1,-1,+1,1},{+1,+1,+1,1},{-1,+1,+1,1},
    };
    glm::vec3 w[8];
    for (int i = 0; i < 8; ++i) {
        glm::vec4 p = inv_vp * ndc[i];
        w[i] = glm::vec3(p) / p.w;
    }
    // Near face
    draw_line(w[0], w[1], color); draw_line(w[1], w[2], color);
    draw_line(w[2], w[3], color); draw_line(w[3], w[0], color);
    // Far face
    draw_line(w[4], w[5], color); draw_line(w[5], w[6], color);
    draw_line(w[6], w[7], color); draw_line(w[7], w[4], color);
    // Connecting edges
    draw_line(w[0], w[4], color); draw_line(w[1], w[5], color);
    draw_line(w[2], w[6], color); draw_line(w[3], w[7], color);
}

void DebugRenderer3D::draw_cross(glm::vec3 center, float size, glm::vec4 color)
{
    glm::vec3 h = glm::vec3(size * 0.5f, 0, 0);
    draw_line(center - h, center + h, color);
    h = { 0, size * 0.5f, 0 };
    draw_line(center - h, center + h, color);
    h = { 0, 0, size * 0.5f };
    draw_line(center - h, center + h, color);
}

void DebugRenderer3D::draw_arrow(glm::vec3 from, glm::vec3 to, glm::vec4 color,
                                 float head_fraction)
{
    draw_line(from, to, color);

    glm::vec3 dir = to - from;
    float len = glm::length(dir);
    if (len < 1e-6f)
        return;
    dir /= len;

    glm::vec3 head_base = to - dir * (len * head_fraction);
    // Two perpendicular vectors to build the arrow head
    glm::vec3 perp = glm::abs(dir.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::vec3 side = glm::normalize(glm::cross(dir, perp)) * (len * head_fraction * 0.4f);

    draw_line(head_base + side,  to, color);
    draw_line(head_base - side,  to, color);
    glm::vec3 side2 = glm::normalize(glm::cross(dir, side)) * (len * head_fraction * 0.4f);
    draw_line(head_base + side2, to, color);
    draw_line(head_base - side2, to, color);
}

} // namespace Honey
