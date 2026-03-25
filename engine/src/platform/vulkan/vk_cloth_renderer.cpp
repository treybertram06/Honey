#include "hnpch.h"
#include "vk_cloth_renderer.h"

#include "Honey/core/engine.h"
#include "Honey/renderer/renderer.h"
#include "Honey/renderer/shader_cache.h"
#include "platform/vulkan/vk_framebuffer.h"

#include <array>
#include <vector>
#include <glm/glm.hpp>

#define GLFW_INCLUDE_VULKAN
#include <vulkan/vulkan.h>

static const std::filesystem::path s_asset_root = ASSET_ROOT;

namespace Honey {

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    namespace {
        static bool load_spirv(VkDevice device,
                               const std::filesystem::path& path,
                               VkShaderModule& out_module)
        {
            out_module = VK_NULL_HANDLE;
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open()) {
                HN_CORE_ERROR("VulkanClothRenderer: failed to open SPIR-V: {0}", path.string());
                return false;
            }
            const std::streamsize sz = file.tellg();
            if (sz <= 0 || (sz % 4) != 0) {
                HN_CORE_ERROR("VulkanClothRenderer: invalid SPIR-V size for '{0}'", path.string());
                return false;
            }
            std::vector<uint32_t> code(static_cast<size_t>(sz / 4));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(code.data()), sz);

            VkShaderModuleCreateInfo ci{};
            ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            ci.codeSize = static_cast<size_t>(sz);
            ci.pCode    = code.data();

            VkShaderModule mod = VK_NULL_HANDLE;
            if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS) {
                HN_CORE_ERROR("VulkanClothRenderer: vkCreateShaderModule failed for '{0}'", path.string());
                return false;
            }
            out_module = mod;
            return true;
        }
    } // namespace

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    VulkanClothRenderer::~VulkanClothRenderer() {
        shutdown();
    }

    bool VulkanClothRenderer::init(VulkanContext* context, uint32_t width, uint32_t height) {
        shutdown();

        HN_CORE_ASSERT(context, "VulkanClothRenderer::init: context is null");
        HN_CORE_ASSERT(width > 0 && height > 0, "VulkanClothRenderer::init: zero dimensions");

        m_context         = context;
        m_device          = context->get_device();
        m_physical_device = context->get_physical_device();
        m_width           = width;
        m_height          = height;

        if (!m_device || !m_physical_device) {
            HN_CORE_ERROR("VulkanClothRenderer::init: missing Vulkan device handles");
            return false;
        }

        // ------------------------------------------------------------------
        // Descriptor set layout: set 0, binding 0 = SSBO (vertex stage)
        // ------------------------------------------------------------------
        {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding         = 0;
            binding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.descriptorCount = 1;
            binding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = 1;
            ci.pBindings    = &binding;

            if (vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &m_descriptor_set_layout) != VK_SUCCESS) {
                HN_CORE_ERROR("VulkanClothRenderer: failed to create descriptor set layout");
                shutdown();
                return false;
            }
        }

        // ------------------------------------------------------------------
        // Descriptor pool: k_max_frames_in_flight sets × 1 SSBO each
        // ------------------------------------------------------------------
        {
            VkDescriptorPoolSize pool_size{};
            pool_size.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            pool_size.descriptorCount = VulkanContext::k_max_frames_in_flight;

            VkDescriptorPoolCreateInfo ci{};
            ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            ci.maxSets       = VulkanContext::k_max_frames_in_flight;
            ci.poolSizeCount = 1;
            ci.pPoolSizes    = &pool_size;

            if (vkCreateDescriptorPool(m_device, &ci, nullptr, &m_descriptor_pool) != VK_SUCCESS) {
                HN_CORE_ERROR("VulkanClothRenderer: failed to create descriptor pool");
                shutdown();
                return false;
            }
        }

        // ------------------------------------------------------------------
        // Allocate descriptor sets (one per frame-in-flight)
        // ------------------------------------------------------------------
        {
            std::array<VkDescriptorSetLayout, VulkanContext::k_max_frames_in_flight> layouts;
            layouts.fill(m_descriptor_set_layout);

            VkDescriptorSetAllocateInfo alloc{};
            alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc.descriptorPool     = m_descriptor_pool;
            alloc.descriptorSetCount = VulkanContext::k_max_frames_in_flight;
            alloc.pSetLayouts        = layouts.data();

            if (vkAllocateDescriptorSets(m_device, &alloc, m_descriptor_sets) != VK_SUCCESS) {
                HN_CORE_ERROR("VulkanClothRenderer: failed to allocate descriptor sets");
                shutdown();
                return false;
            }

            for (auto& buf : m_last_bound_buffers)
                buf = VK_NULL_HANDLE;
        }

        // ------------------------------------------------------------------
        // Static index buffer: grid triangle topology
        // w×h particles → (w-1)×(h-1)×2 quads → ×6 indices
        // ------------------------------------------------------------------
        {
            const uint32_t W = m_width;
            const uint32_t H = m_height;
            const uint32_t quad_count  = (W - 1) * (H - 1);
            m_index_count = quad_count * 6;

            std::vector<uint32_t> indices;
            indices.reserve(m_index_count);

            for (uint32_t y = 0; y < H - 1; ++y) {
                for (uint32_t x = 0; x < W - 1; ++x) {
                    const uint32_t a = y * W + x;
                    const uint32_t b = a + 1;
                    const uint32_t c = a + W;
                    const uint32_t d = c + 1;
                    // Triangle 1
                    indices.push_back(a);
                    indices.push_back(b);
                    indices.push_back(c);
                    // Triangle 2
                    indices.push_back(b);
                    indices.push_back(d);
                    indices.push_back(c);
                }
            }

            const VkDeviceSize buf_size = static_cast<VkDeviceSize>(m_index_count) * sizeof(uint32_t);

            VkBufferCreateInfo buf_ci{};
            buf_ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buf_ci.size        = buf_size;
            buf_ci.usage       = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(m_device, &buf_ci, nullptr, &m_index_buffer) != VK_SUCCESS) {
                HN_CORE_ERROR("VulkanClothRenderer: failed to create index buffer");
                shutdown();
                return false;
            }

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(m_device, m_index_buffer, &req);

            VkMemoryAllocateInfo alloc{};
            alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.allocationSize  = req.size;
            alloc.memoryTypeIndex = find_memory_type(
                m_physical_device, req.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            if (vkAllocateMemory(m_device, &alloc, nullptr, &m_index_memory) != VK_SUCCESS) {
                HN_CORE_ERROR("VulkanClothRenderer: failed to allocate index buffer memory");
                shutdown();
                return false;
            }

            vkBindBufferMemory(m_device, m_index_buffer, m_index_memory, 0);

            void* mapped = nullptr;
            vkMapMemory(m_device, m_index_memory, 0, buf_size, 0, &mapped);
            std::memcpy(mapped, indices.data(), static_cast<size_t>(buf_size));
            vkUnmapMemory(m_device, m_index_memory);
        }

        m_initialized = true;
        HN_CORE_INFO("VulkanClothRenderer initialized ({0}x{1}, {2} indices)",
                     m_width, m_height, m_index_count);
        return true;
    }

    void VulkanClothRenderer::shutdown() {
        if (!m_device)
            return;

        vkDeviceWaitIdle(m_device);

        destroy_pipelines();
        destroy_descriptor_resources();
        destroy_index_buffer();

        m_context         = nullptr;
        m_device          = VK_NULL_HANDLE;
        m_physical_device = VK_NULL_HANDLE;
        m_width           = 0;
        m_height          = 0;
        m_index_count     = 0;
        m_initialized     = false;
    }

    void VulkanClothRenderer::destroy_pipelines() {
        for (auto& [rp, entry] : m_pipelines) {
            if (entry.pipeline) vkDestroyPipeline(m_device, entry.pipeline, nullptr);
            if (entry.layout)   vkDestroyPipelineLayout(m_device, entry.layout, nullptr);
        }
        m_pipelines.clear();
    }

    void VulkanClothRenderer::destroy_descriptor_resources() {
        if (m_descriptor_pool) {
            vkDestroyDescriptorPool(m_device, m_descriptor_pool, nullptr);
            m_descriptor_pool = VK_NULL_HANDLE;
            for (auto& s : m_descriptor_sets) s = VK_NULL_HANDLE;
            for (auto& b : m_last_bound_buffers) b = VK_NULL_HANDLE;
        }
        if (m_descriptor_set_layout) {
            vkDestroyDescriptorSetLayout(m_device, m_descriptor_set_layout, nullptr);
            m_descriptor_set_layout = VK_NULL_HANDLE;
        }
    }

    void VulkanClothRenderer::destroy_index_buffer() {
        if (m_index_buffer) {
            vkDestroyBuffer(m_device, m_index_buffer, nullptr);
            m_index_buffer = VK_NULL_HANDLE;
        }
        if (m_index_memory) {
            vkFreeMemory(m_device, m_index_memory, nullptr);
            m_index_memory = VK_NULL_HANDLE;
        }
    }

    // -------------------------------------------------------------------------
    // Pipeline creation (lazy, per render-pass)
    // -------------------------------------------------------------------------

    bool VulkanClothRenderer::ensure_pipeline_for_render_pass(VkRenderPass rp) {
        if (m_pipelines.count(rp))
            return true;

        // ------------------------------------------------------------------
        // Compile / load SPIR-V via ShaderCache
        // ------------------------------------------------------------------
        auto shader_cache = Renderer::get_shader_cache();
        if (!shader_cache) {
            HN_CORE_ERROR("VulkanClothRenderer: shader cache unavailable");
            return false;
        }

        ShaderCache::SpirvPaths spirv{};
        try {
            spirv = shader_cache->get_or_compile_spirv_paths(
                s_asset_root / "shaders" / "ClothRender.glsl");
        } catch (const std::exception& e) {
            HN_CORE_ERROR("VulkanClothRenderer: shader compile failed: {0}", e.what());
            return false;
        }

        if (!spirv.has_graphics()) {
            HN_CORE_ERROR("VulkanClothRenderer: ClothRender.glsl has no graphics SPIR-V");
            return false;
        }

        VkShaderModule vert_mod = VK_NULL_HANDLE;
        VkShaderModule frag_mod = VK_NULL_HANDLE;

        if (!load_spirv(m_device, spirv.vertex, vert_mod)) return false;
        if (!load_spirv(m_device, spirv.fragment, frag_mod)) {
            vkDestroyShaderModule(m_device, vert_mod, nullptr);
            return false;
        }

        // ------------------------------------------------------------------
        // Pipeline layout: descriptor set 0 (cloth SSBO) + push constant (64-byte VP)
        // ------------------------------------------------------------------
        VkPushConstantRange pc_range{};
        pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pc_range.offset     = 0;
        pc_range.size       = sizeof(glm::mat4);

        VkPipelineLayoutCreateInfo layout_ci{};
        layout_ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_ci.setLayoutCount         = 1;
        layout_ci.pSetLayouts            = &m_descriptor_set_layout;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges    = &pc_range;

        VkPipelineLayout layout = VK_NULL_HANDLE;
        if (vkCreatePipelineLayout(m_device, &layout_ci, nullptr, &layout) != VK_SUCCESS) {
            HN_CORE_ERROR("VulkanClothRenderer: failed to create pipeline layout");
            vkDestroyShaderModule(m_device, vert_mod, nullptr);
            vkDestroyShaderModule(m_device, frag_mod, nullptr);
            return false;
        }

        // ------------------------------------------------------------------
        // Shader stages
        // ------------------------------------------------------------------
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert_mod;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag_mod;
        stages[1].pName  = "main";

        // ------------------------------------------------------------------
        // No vertex inputs — pure SSBO pull
        // ------------------------------------------------------------------
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp_state{};
        vp_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp_state.viewportCount = 1;
        vp_state.scissorCount  = 1;

        VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates    = dyn_states;

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;   // cloth has no back-face winding guarantee
        raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{};
        msaa.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable  = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp   = VK_COMPARE_OP_LESS;

        // ------------------------------------------------------------------
        // Blend state — 2 attachments:
        //   [0] RGBA8: write cloth color, no blending
        //   [1] RED_INTEGER entity-ID: disable writes (cloth is not an entity)
        // ------------------------------------------------------------------
        VkPipelineColorBlendAttachmentState blend_atts[2]{};
        blend_atts[0].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend_atts[0].blendEnable = VK_FALSE;

        blend_atts[1].colorWriteMask = 0; // do not write entity IDs for cloth fragments
        blend_atts[1].blendEnable    = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 2;
        blend.pAttachments    = blend_atts;

        // ------------------------------------------------------------------
        // Create pipeline
        // ------------------------------------------------------------------
        VkGraphicsPipelineCreateInfo pipe_ci{};
        pipe_ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipe_ci.stageCount          = 2;
        pipe_ci.pStages             = stages;
        pipe_ci.pVertexInputState   = &vi;
        pipe_ci.pInputAssemblyState = &ia;
        pipe_ci.pViewportState      = &vp_state;
        pipe_ci.pRasterizationState = &raster;
        pipe_ci.pMultisampleState   = &msaa;
        pipe_ci.pDepthStencilState  = &depth;
        pipe_ci.pColorBlendState    = &blend;
        pipe_ci.pDynamicState       = &dyn;
        pipe_ci.layout              = layout;
        pipe_ci.renderPass          = rp;
        pipe_ci.subpass             = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        const VkResult r = vkCreateGraphicsPipelines(
            m_device, VK_NULL_HANDLE, 1, &pipe_ci, nullptr, &pipeline);

        vkDestroyShaderModule(m_device, vert_mod, nullptr);
        vkDestroyShaderModule(m_device, frag_mod, nullptr);

        if (r != VK_SUCCESS) {
            HN_CORE_ERROR("VulkanClothRenderer: vkCreateGraphicsPipelines failed ({0})",
                          static_cast<int>(r));
            vkDestroyPipelineLayout(m_device, layout, nullptr);
            return false;
        }

        m_pipelines[rp] = { pipeline, layout };
        HN_CORE_INFO("VulkanClothRenderer: created cloth render pipeline for render pass {0}",
                     (void*)rp);
        return true;
    }

    // -------------------------------------------------------------------------
    // Record draw — queues a CustomVulkan callback into the FramePacket
    // -------------------------------------------------------------------------

    void VulkanClothRenderer::record_draw(VkBuffer cloth_state_buf, const glm::mat4& vp_engine_clip) {
        if (!m_initialized || !m_context) return;
        if (!cloth_state_buf) return;

        // Get render pass from the currently-active render target
        VkRenderPass rp = VK_NULL_HANDLE;
        {
            auto target = Renderer::get_render_target();
            if (target) {
                auto* vk_fb = dynamic_cast<VulkanFramebuffer*>(target.get());
                HN_CORE_ASSERT(vk_fb, "VulkanClothRenderer::record_draw: render target is not a VulkanFramebuffer");
                rp = vk_fb->get_render_pass();
            } else {
                rp = m_context->get_render_pass();
            }
        }

        if (!rp) {
            HN_CORE_WARN("VulkanClothRenderer::record_draw: no active render pass");
            return;
        }

        if (!ensure_pipeline_for_render_pass(rp)) return;

        const auto& entry = m_pipelines.at(rp);

        // ------------------------------------------------------------------
        // Convert VP from EngineClip to VulkanClip
        // ------------------------------------------------------------------
        glm::mat4 correction(1.0f);
        correction[1][1] = -1.0f;   // flip Y
        correction[2][2] =  0.5f;   // z' = 0.5*z + 0.5*w
        correction[3][2] =  0.5f;
        const glm::mat4 vp_vulkan = correction * vp_engine_clip;

        // ------------------------------------------------------------------
        // Update descriptor set for this frame if the buffer changed
        // ------------------------------------------------------------------
        const uint32_t frame = m_context->get_current_frame();
        if (m_last_bound_buffers[frame] != cloth_state_buf) {
            VkDescriptorBufferInfo buf_info{};
            buf_info.buffer = cloth_state_buf;
            buf_info.offset = 0;
            buf_info.range  = VK_WHOLE_SIZE;

            VkWriteDescriptorSet write{};
            write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet          = m_descriptor_sets[frame];
            write.dstBinding      = 0;
            write.descriptorCount = 1;
            write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo     = &buf_info;

            vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
            m_last_bound_buffers[frame] = cloth_state_buf;
        }

        // ------------------------------------------------------------------
        // Capture everything by value for the deferred callback
        // ------------------------------------------------------------------
        const VkPipeline       pipeline   = entry.pipeline;
        const VkPipelineLayout layout     = entry.layout;
        const VkDescriptorSet  ds         = m_descriptor_sets[frame];
        const VkBuffer         idx_buf    = m_index_buffer;
        const uint32_t         idx_count  = m_index_count;
        const glm::mat4        vp_cap     = vp_vulkan;
        const VkBuffer         ssbo       = cloth_state_buf;

        m_context->queue_custom_vulkan_cmd(
            [pipeline, layout, ds, idx_buf, idx_count, vp_cap, ssbo]
            (VkCommandBuffer cmd, uint32_t pass_w, uint32_t pass_h) {
                // Viewport and scissor (required after switching pipeline)
                VkViewport viewport{};
                viewport.x        = 0.0f;
                viewport.y        = 0.0f;
                viewport.width    = static_cast<float>(pass_w);
                viewport.height   = static_cast<float>(pass_h);
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &viewport);

                VkRect2D scissor{};
                scissor.offset = { 0, 0 };
                scissor.extent = { pass_w, pass_h };
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        layout, 0, 1, &ds, 0, nullptr);
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(glm::mat4), &vp_cap);
                vkCmdBindIndexBuffer(cmd, idx_buf, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, idx_count, 1, 0, 0, 0);
            });
    }

    // -------------------------------------------------------------------------
    // Utility
    // -------------------------------------------------------------------------

    uint32_t VulkanClothRenderer::find_memory_type(VkPhysicalDevice phys,
                                                    uint32_t type_filter,
                                                    VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties mem_props{};
        vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);
        for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
            if ((type_filter & (1u << i)) &&
                (mem_props.memoryTypes[i].propertyFlags & properties) == properties)
                return i;
        }
        HN_CORE_ASSERT(false, "VulkanClothRenderer::find_memory_type: no suitable memory type");
        return 0;
    }

} // namespace Honey
