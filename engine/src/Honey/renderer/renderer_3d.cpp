#include "hnpch.h"
#include "renderer_3d.h"
#include "render_command.h"
#include <glm/gtc/matrix_transform.hpp>

#include "pipeline.h"
#include "renderer.h"
#include "shader_cache.h"
#include "Honey/core/engine.h"
#include "Honey/core/settings.h"
#include "platform/vulkan/vk_renderer_api.h"

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey {

    struct Renderer3DData {
        static constexpr uint32_t max_textures  = 32;   // keep in sync with shader

        // Texture slots
        uint32_t                       max_texture_slots = 0;
        std::vector<Ref<Texture2D>>    texture_slots;
        uint32_t                       texture_slot_index = 1; // 0 is white tex
        Ref<Texture2D>                 white_texture;

        std::vector<VulkanRendererAPI::GlobalsState> vk_globals_stack;

        Ref<ShaderCache> shader_cache;

        struct VkPipelineCacheEntry {
            void* renderPassNative = nullptr; // VkRenderPass
            Ref<Pipeline> pipeline;
        };
        VkPipelineCacheEntry vk_forward_pipeline{};

        Renderer3D::Statistics stats;
    };

    static Renderer3DData* s_data;

    static PipelineSpec build_vk_forward3d_pipeline_spec()
    {
        auto& rs = Settings::get().renderer;

        PipelineSpec spec{};
        spec.shaderGLSLPath = asset_root / "shaders" / "Renderer3D_Forward.glsl";
        spec.topology = PrimitiveTopology::Triangles;
        spec.cullMode = rs.cull_mode;
        spec.frontFace = FrontFaceWinding::CounterClockwise;
        spec.wireframe = rs.wireframe;

        // Swapchain render pass currently has NO depth attachment, so keep depth off for now.
        spec.depthStencil.depthTest = rs.depth_test;
        spec.depthStencil.depthWrite = rs.depth_write;

        spec.passType = RenderPassType::Swapchain;

        // Vertex layout must match Renderer3D_Forward.glsl:
        // layout(location=0) in vec3 a_position;
        // layout(location=1) in vec3 a_normal;
        // layout(location=2) in vec2 a_uv;
        VertexInputBindingSpec binding{};
        binding.layout = {
            { ShaderDataType::Float3, "a_position" },
            { ShaderDataType::Float3, "a_normal"   },
            { ShaderDataType::Float2, "a_uv"       },
        };

        spec.vertexBindings.clear();
        spec.vertexBindings.push_back(binding);

        // Swapchain pass has a single color attachment; blending off for now.
        spec.perColorAttachmentBlend.clear();
        AttachmentBlendState b0{};
        b0.enabled = rs.blending;
        spec.perColorAttachmentBlend.push_back(b0);

        return spec;
    }

    static Ref<Pipeline> get_or_create_vk_forward3d_pipeline(void* renderPassNative)
    {
        HN_CORE_ASSERT(renderPassNative, "get_or_create_vk_forward3d_pipeline: renderPassNative is null");

        if (s_data->vk_forward_pipeline.renderPassNative != renderPassNative) {
            s_data->vk_forward_pipeline = {};
            s_data->vk_forward_pipeline.renderPassNative = renderPassNative;
        }

        auto& entry = s_data->vk_forward_pipeline;

        if (!entry.pipeline) {
            PipelineSpec spec = build_vk_forward3d_pipeline_spec();
            entry.pipeline = Pipeline::create(spec, renderPassNative);
        }

        return entry.pipeline;
    }

    void Renderer3D::init() {
        HN_PROFILE_FUNCTION();

        if (!s_data)
            s_data = new Renderer3DData;

        s_data->shader_cache = Renderer::get_shader_cache();

    }

    void Renderer3D::shutdown() {
        HN_PROFILE_FUNCTION();

        delete s_data;
    }

    void Renderer3D::begin_scene(const PerspectiveCamera& camera) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(false, "Renderer3D::begin_scene: Not implemented yet!");
    }

    void Renderer3D::begin_scene(const EditorCamera& camera) {
        HN_PROFILE_FUNCTION();
        reset_stats();


        glm::mat4 vp = camera.get_view_projection_matrix(); // EngineClip (GL style)

        s_data->vk_globals_stack.push_back(VulkanRendererAPI::get_globals_state());
        VulkanRendererAPI::submit_camera_view_projection(vp); // Converts to VulkanClip internally

        s_data->texture_slot_index = 1; // keep white bound at 0
    }

    void Renderer3D::end_scene() {
        HN_PROFILE_FUNCTION();

        HN_CORE_ASSERT(!s_data->vk_globals_stack.empty(),
                           "Renderer3D Vulkan globals stack underflow (end_scene without matching begin_scene)");
        VulkanRendererAPI::set_globals_state(s_data->vk_globals_stack.back());
        s_data->vk_globals_stack.pop_back();

    }

    void Renderer3D::draw_mesh(const Ref<VertexArray>& vertex_array, const glm::mat4& transform) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(vertex_array, "Renderer3D::draw_mesh: vertex_array is null");

        if (Renderer::get_api() != RendererAPI::API::vulkan) {
            HN_CORE_ASSERT(false, "Renderer3D::draw_mesh: only Vulkan path is implemented right now");
            return;
        }

        auto* base = Application::get().get_window().get_context();
        auto* vkCtx = dynamic_cast<Honey::VulkanContext*>(base);
        HN_CORE_ASSERT(vkCtx, "Renderer3D Vulkan path expected VulkanContext");

        void* rpNative = vkCtx->get_render_pass();
        Ref<Pipeline> pipe = get_or_create_vk_forward3d_pipeline(rpNative);

        RenderCommand::bind_pipeline(pipe);
        VulkanRendererAPI::submit_push_constants_mat4(transform);
        RenderCommand::draw_indexed(vertex_array);

        s_data->stats.draw_calls++;
    }

    Renderer3D::Statistics Renderer3D::get_stats() {
        return s_data->stats;
    }

    void Renderer3D::reset_stats() {
        memset(&s_data->stats, 0, sizeof(Statistics));
    }

}