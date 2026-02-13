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

        Ref<Material> default_material;

        struct BatchKeyHash {
            size_t operator()(const Renderer3D::BatchKey& k) const {
                size_t h1 = std::hash<const void*>{}(k.va);
                size_t h2 = std::hash<const void*>{}(k.mat);
                return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
            }
        };

        std::unordered_map<Renderer3D::BatchKey, Renderer3D::BatchValue, BatchKeyHash> batches;

        Ref<VertexBuffer> instance_vb;
        uint32_t instance_vb_capacity = 0; // in instances

        Renderer3D::Statistics stats;
        std::unordered_set<const void*> unique_meshes_this_frame;
    };

    static Renderer3DData* s_data;

    static PipelineSpec build_vk_forward3d_pipeline_spec() {
        auto& rs = Settings::get().renderer;

        PipelineSpec spec{};
        spec.shaderGLSLPath = asset_root / "shaders" / "Renderer3D_Forward.glsl";
        spec.topology = PrimitiveTopology::Triangles;
        spec.cullMode = rs.cull_mode;
        spec.frontFace = FrontFaceWinding::CounterClockwise;
        spec.wireframe = rs.wireframe;

        spec.depthStencil.depthTest = rs.depth_test;
        spec.depthStencil.depthWrite = rs.depth_write;

        spec.passType = RenderPassType::Swapchain;

        VertexInputBindingSpec vb0{};
        vb0.layout = {
                { ShaderDataType::Float3, "a_position" },
                { ShaderDataType::Float3, "a_normal"   },
                { ShaderDataType::Float2, "a_uv"       },
            };

        VertexInputBindingSpec vb1{};
        vb1.layout = {
                { ShaderDataType::Float4, "a_iModel0", false, true },
                { ShaderDataType::Float4, "a_iModel1", false, true },
                { ShaderDataType::Float4, "a_iModel2", false, true },
                { ShaderDataType::Float4, "a_iModel3", false, true },
            };

        spec.vertexBindings.clear();
        spec.vertexBindings.push_back(vb0);
        spec.vertexBindings.push_back(vb1);

        spec.perColorAttachmentBlend.clear();
        AttachmentBlendState b0{};
        b0.enabled = rs.blending;
        spec.perColorAttachmentBlend.push_back(b0);

        return spec;
    }

    static Ref<Pipeline> get_or_create_vk_forward3d_pipeline(void* renderPassNative) {
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

        s_data->default_material = Material::create();
        s_data->batches.clear();
        s_data->instance_vb.reset();
        s_data->instance_vb_capacity = 0;

        // Texture slot setup (slot 0 = white)
        s_data->max_texture_slots = VulkanRendererAPI::k_max_texture_slots;
        s_data->texture_slots.clear();
        s_data->texture_slots.resize(s_data->max_texture_slots);

        // If you already have a "white texture" helper elsewhere, swap this line to use it.
        // For now: create a tiny white texture.
        s_data->white_texture = Texture2D::create(1, 1);
        {
            const uint32_t white = 0xFFFFFFFFu;
            s_data->white_texture->set_data((void*)&white, sizeof(uint32_t));
        }
        s_data->texture_slots[0] = s_data->white_texture;

        // Default material uses white unless otherwise set.
        s_data->default_material->set_base_color_texture(nullptr);
        s_data->default_material->set_base_color_factor(glm::vec4(1.0f));

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

        // Reset frame texture table (keep white bound at slot 0)
        s_data->texture_slot_index = 1;
        if (!s_data->texture_slots.empty())
            s_data->texture_slots[0] = s_data->white_texture;

        s_data->batches.clear();
        s_data->unique_meshes_this_frame.clear();
    }

    static void ensure_instance_buffer_capacity(uint32_t required_instances) {
        HN_CORE_ASSERT(s_data, "Renderer3D: s_data null");

        if (required_instances == 0)
            return;

        if (!s_data->instance_vb || s_data->instance_vb_capacity < required_instances) {
            // grow (simple doubling strategy)
            uint32_t new_cap = std::max(64u, s_data->instance_vb_capacity);
            while (new_cap < required_instances) new_cap *= 2;

            s_data->instance_vb_capacity = new_cap;

            const uint32_t bytes = s_data->instance_vb_capacity * sizeof(glm::mat4);
            s_data->instance_vb = VertexBuffer::create(bytes);

            s_data->instance_vb->set_layout({
                { ShaderDataType::Float4, "a_iModel0", false, true },
                { ShaderDataType::Float4, "a_iModel1", false, true },
                { ShaderDataType::Float4, "a_iModel2", false, true },
                { ShaderDataType::Float4, "a_iModel3", false, true },
            });
        }
    }

 static void flush_batches_vulkan() {
            HN_PROFILE_FUNCTION();

            auto* base = Application::get().get_window().get_context();
            auto* vkCtx = dynamic_cast<Honey::VulkanContext*>(base);
            HN_CORE_ASSERT(vkCtx, "Renderer3D Vulkan path expected VulkanContext");

            void* rpNative = vkCtx->get_render_pass();
            Ref<Pipeline> pipe = get_or_create_vk_forward3d_pipeline(rpNative);

            // Build per-frame texture bindings used by this flush
            // Slot 0 is white, then unique base color textures.
            auto find_or_add_texture_slot = [&](const Ref<Texture2D>& tex) -> uint32_t {
                Ref<Texture2D> resolved = tex ? tex : s_data->white_texture;

                // Check existing (1..texture_slot_index-1); 0 is white reserved
                for (uint32_t i = 1; i < s_data->texture_slot_index; ++i) {
                    if (s_data->texture_slots[i] == resolved)
                        return i;
                }

                HN_CORE_ASSERT(s_data->texture_slot_index < s_data->max_texture_slots,
                               "Renderer3D: exceeded max texture slots ({0})", s_data->max_texture_slots);

                const uint32_t slot = s_data->texture_slot_index++;
                s_data->texture_slots[slot] = resolved;
                return slot;
            };

            // Pre-scan batches to populate the texture table once
            for (auto& [key, batch] : s_data->batches) {
                if (!batch.material)
                    continue;
                find_or_add_texture_slot(batch.material->get_base_color_texture());
            }

            // Submit global texture bindings (pointer array)
            std::array<void*, VulkanRendererAPI::k_max_texture_slots> bound{};
            const uint32_t count = std::max(1u, s_data->texture_slot_index);

            for (uint32_t i = 0; i < VulkanRendererAPI::k_max_texture_slots; ++i) {
                Ref<Texture2D> t = (i < count) ? s_data->texture_slots[i] : s_data->white_texture;
                bound[i] = t.get();
            }
            VulkanRendererAPI::submit_bound_textures(bound, count);

            // One pipeline bind for all batches (same forward shader for now)
            RenderCommand::bind_pipeline(pipe);
            s_data->stats.pipeline_binds++;

            // --- Pack all instance transforms into one contiguous array ---
            uint32_t total_instances = 0;
            for (auto& [key, batch] : s_data->batches) {
                total_instances += (uint32_t)batch.transforms.size();
            }

            if (total_instances == 0)
                return;

            std::vector<glm::mat4> packed;
            packed.reserve(total_instances);

            // Keep start index per batch, in the SAME iteration order used for packing
            std::vector<std::pair<const Renderer3D::BatchKey*, uint32_t>> starts;
            starts.reserve(s_data->batches.size());

            for (auto& [key, batch] : s_data->batches) {
                if (batch.transforms.empty())
                    continue;

                const uint32_t start_index = (uint32_t)packed.size();
                starts.emplace_back(&key, start_index);

                packed.insert(packed.end(), batch.transforms.begin(), batch.transforms.end());
            }

            HN_CORE_ASSERT(packed.size() == total_instances,
                           "Renderer3D: packed instance count mismatch (packed={}, expected={})",
                           packed.size(), total_instances);

            // Upload ONCE for the whole frame
            ensure_instance_buffer_capacity((uint32_t)packed.size());
            s_data->instance_vb->set_data(packed.data(), (uint32_t)(packed.size() * sizeof(glm::mat4)));

            // --- Emit draws, each referencing a slice of the packed buffer via byte offset ---
            for (auto& [key, batch] : s_data->batches) {
                if (batch.transforms.empty())
                    continue;

                const uint32_t base_color_tex_index =
                    batch.material ? find_or_add_texture_slot(batch.material->get_base_color_texture()) : 0;

                const glm::vec4 base_color_factor =
                    batch.material ? batch.material->get_base_color_factor() : glm::vec4(1.0f);

                struct MaterialPC {
                    glm::vec4 baseColorFactor;
                    int32_t   baseColorTexIndex;
                    int32_t   _pad0;
                    int32_t   _pad1;
                    int32_t   _pad2;
                };

                MaterialPC pc{};
                pc.baseColorFactor   = base_color_factor;
                pc.baseColorTexIndex = (int32_t)base_color_tex_index;
                pc._pad0 = pc._pad1 = pc._pad2 = 0;

                VulkanRendererAPI::submit_push_constants(
                    &pc,
                    (uint32_t)sizeof(MaterialPC),
                    0,
                    VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT
                );
                s_data->stats.push_constant_updates++;

                // Find start index for this batch (linear search; batches are usually not huge).
                // If you want, we can swap this to an unordered_map later.
                uint32_t start_index = 0;
                bool found = false;
                for (const auto& [kptr, start] : starts) {
                    if (kptr->va == key.va && kptr->mat == key.mat) {
                        start_index = start;
                        found = true;
                        break;
                    }
                }
                HN_CORE_ASSERT(found, "Renderer3D: failed to find packed start index for batch");

                const uint32_t instance_count = (uint32_t)batch.transforms.size();
                const uint32_t byte_offset = start_index * (uint32_t)sizeof(glm::mat4);

                HN_CORE_ASSERT((byte_offset % 16u) == 0u, "Renderer3D: instance byte offset must be 16-byte aligned");

                // Safety: ensure range fits in the uploaded packed buffer.
                const uint32_t end_index = start_index + instance_count;
                HN_CORE_ASSERT(end_index <= (uint32_t)packed.size(),
                               "Renderer3D: instance range out of packed bounds (end={}, packed={})",
                               end_index, packed.size());

                VulkanRendererAPI::submit_instanced_draw(
                    batch.va,
                    s_data->instance_vb,
                    0,
                    instance_count,
                    byte_offset
                );

                s_data->stats.draw_calls++;
            }
        }

    void Renderer3D::end_scene() {
        HN_PROFILE_FUNCTION();

        if (Renderer::get_api() == RendererAPI::API::vulkan) {
            flush_batches_vulkan();
        } else {
            HN_CORE_ASSERT(false, "Renderer3D::end_scene: only Vulkan path implemented");
        }

        HN_CORE_ASSERT(!s_data->vk_globals_stack.empty(),
                           "Renderer3D Vulkan globals stack underflow (end_scene without matching begin_scene)");
        VulkanRendererAPI::set_globals_state(s_data->vk_globals_stack.back());
        s_data->vk_globals_stack.pop_back();

    }

    void Renderer3D::draw_mesh(const Ref<VertexArray>& vertex_array, const glm::mat4& transform) {
        draw_mesh(vertex_array, s_data->default_material, transform);
    }

    void Renderer3D::draw_mesh(const Ref<VertexArray>& vertex_array, const Ref<Material>& material, const glm::mat4& transform) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(vertex_array, "Renderer3D::draw_mesh: vertex_array is null");
        HN_CORE_ASSERT(material, "Renderer3D::draw_mesh: material is null");

        s_data->stats.mesh_submissions++;

        // unique mesh stats
        s_data->unique_meshes_this_frame.insert(vertex_array.get());
        s_data->stats.unique_meshes = (uint32_t)s_data->unique_meshes_this_frame.size();

        BatchKey key{};
        key.va = vertex_array.get();
        key.mat = material.get();

        auto it = s_data->batches.find(key);
        if (it == s_data->batches.end()) {
            BatchValue v{};
            v.va = vertex_array;
            v.material = material;
            v.transforms.reserve(128);
            it = s_data->batches.emplace(key, std::move(v)).first;
        }

        it->second.transforms.push_back(transform);
    }

    Renderer3D::Statistics Renderer3D::get_stats() {
        return s_data->stats;
    }

    void Renderer3D::reset_stats() {
        memset(&s_data->stats, 0, sizeof(Statistics));
        if (s_data)
            s_data->unique_meshes_this_frame.clear();
    }

}