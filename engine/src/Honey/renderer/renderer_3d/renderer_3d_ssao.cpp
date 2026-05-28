#include "hnpch.h"
#include "renderer_3d_ssao.h"

#include <random>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan.h>

#include "renderer_3d_internal.h"
#include "Honey/core/engine.h"
#include "Honey/renderer/frame_graph_registry.h"
#include "Honey/renderer/texture.h"
#include "platform/vulkan/vk_context.h"
#include "platform/vulkan/vk_framebuffer.h"
#include "platform/vulkan/vk_texture.h"
#include "Honey/renderer/pipeline.h"

namespace Honey {

    static const std::filesystem::path asset_root = ASSET_ROOT;

    namespace {

        static uint32_t find_memory_type_ssao(VkPhysicalDevice phys, uint32_t type_filter, VkMemoryPropertyFlags props) {
            VkPhysicalDeviceMemoryProperties mp{};
            vkGetPhysicalDeviceMemoryProperties(phys, &mp);
            for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
                if ((type_filter & (1u << i)) && ((mp.memoryTypes[i].propertyFlags & props) == props))
                    return i;
            }
            HN_CORE_ASSERT(false, "find_memory_type_ssao: no suitable memory type");
            return 0;
        }

        struct SSAOResources {
            VulkanContext* vk_ctx = nullptr;

            // Noise rotation texture (4x4 RGBA8, RG = random rotation vectors)
            Ref<Texture2D> noise_texture;

            // SSAO draw descriptor (set=1: gNormal, gDepth, noise, kernelUBO)
            VkDescriptorSetLayout ssao_set_layout     = VK_NULL_HANDLE;
            VkDescriptorPool      ssao_descriptor_pool = VK_NULL_HANDLE;
            VkDescriptorSet       ssao_sets[VulkanContext::k_max_frames_in_flight]{};

            // Per-frame kernel UBO (persistently mapped)
            VkBuffer       ssao_kernel_ubos    [VulkanContext::k_max_frames_in_flight]{};
            VkDeviceMemory ssao_kernel_memories[VulkanContext::k_max_frames_in_flight]{};
            void*          ssao_kernel_mapped  [VulkanContext::k_max_frames_in_flight]{};

            // Blur descriptor (set=1: ssaoRaw)
            VkDescriptorSetLayout ssao_blur_set_layout      = VK_NULL_HANDLE;
            VkDescriptorPool      ssao_blur_descriptor_pool = VK_NULL_HANDLE;
            VkDescriptorSet       ssao_blur_sets[VulkanContext::k_max_frames_in_flight]{};

            // Blur pipeline cache keyed by render-pass handle
            std::unordered_map<void*, Ref<Pipeline>> blur_pipelines;
        };
        static SSAOResources* s_res = nullptr;

        Ref<Pipeline> get_or_create_ssao_pipeline(void* rp_native) {
            HN_CORE_ASSERT(s_res->ssao_set_layout != VK_NULL_HANDLE, "ssao_set_layout is null");

            Renderer3DInternal::PipelineVariantKey key{rp_native, 0, 0};
            auto it = Renderer3DInternal::g_renderer3d_data->vk_ssao_pipelines.find(key);
            if (it != Renderer3DInternal::g_renderer3d_data->vk_ssao_pipelines.end())
                return it->second;

            auto* base = Application::get().get_window().get_context();
            auto* vk = dynamic_cast<VulkanContext*>(base);
            HN_CORE_ASSERT(vk, "get_or_create_ssao_pipeline: VulkanContext is null");

            auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_SSAO.glsl");
            spec.depthStencil.depthTest  = false;
            spec.depthStencil.depthWrite = false;
            spec.perColorAttachmentBlend.clear();
            spec.perColorAttachmentBlend.resize(1, AttachmentBlendState{});

            auto pipeline = Pipeline::create(spec, rp_native, s_res->ssao_set_layout);
            Renderer3DInternal::g_renderer3d_data->vk_ssao_pipelines.emplace(key, pipeline);
            return pipeline;
        }

        Ref<Pipeline> get_or_create_blur_pipeline(void* rp_native) {
            HN_CORE_ASSERT(s_res->ssao_blur_set_layout != VK_NULL_HANDLE, "ssao_blur_set_layout is null");

            auto it = s_res->blur_pipelines.find(rp_native);
            if (it != s_res->blur_pipelines.end())
                return it->second;

            auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_SSAOBlur.glsl");
            spec.depthStencil.depthTest  = false;
            spec.depthStencil.depthWrite = false;
            spec.perColorAttachmentBlend.clear();
            spec.perColorAttachmentBlend.resize(1, AttachmentBlendState{});

            auto pipeline = Pipeline::create(spec, rp_native, s_res->ssao_blur_set_layout);
            s_res->blur_pipelines.emplace(rp_native, pipeline);
            return pipeline;
        }


        static void generate_ssao_kernel(glm::vec4* out, uint32_t count) {
            std::mt19937 rng{42};
            std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
            std::uniform_real_distribution<float> distN1(-1.0f, 1.0f);

            for (uint32_t i = 0; i < count; ++i) {
                glm::vec3 s{distN1(rng), distN1(rng), dist01(rng)};
                s = glm::normalize(s) * dist01(rng);
                float scale = float(i) / float(count);
                scale = glm::mix(0.1f, 1.0f, scale * scale);
                out[i] = glm::vec4(s * scale, 0.0f);
            }
        }

        static void create_ssao_resources() {
            HN_PROFILE_FUNCTION();
            const VkDevice         device  = s_res->vk_ctx->get_device();
            const VkPhysicalDevice phys    = s_res->vk_ctx->get_physical_device();
            const VulkanBackend*   backend = s_res->vk_ctx->get_backend();

            // SSAO draw descriptor set layout
            //    binding 0,1,2 = COMBINED_IMAGE_SAMPLER
            //    binding 3     = UNIFORM_BUFFER (SSAOKernelUBO)

            static constexpr uint32_t k_ssao_binding_count = 4;

            VkDescriptorSetLayoutBinding bindings[k_ssao_binding_count]{};
            for (uint32_t i = 0; i < 3; ++i) {
                bindings[i].binding        = i;
                bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                bindings[i].descriptorCount = 1;
                bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
            }
            bindings[3].binding        = 3;
            bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            bindings[3].descriptorCount = 1;
            bindings[3].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorBindingFlags binding_flags[k_ssao_binding_count]{};

            VkDescriptorSetLayoutBindingFlagsCreateInfo bf_ci{};
            bf_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            bf_ci.bindingCount  = k_ssao_binding_count;
            bf_ci.pBindingFlags = binding_flags;

            VkDescriptorSetLayoutCreateInfo layout_ci{};
            layout_ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout_ci.pNext        = &bf_ci;
            layout_ci.bindingCount = k_ssao_binding_count;
            layout_ci.pBindings    = bindings;

            {
                VkDescriptorSetLayout sl = VK_NULL_HANDLE;
                VkResult r = vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &sl);
                HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateDescriptorSetLayout (ssao draw) failed");
                s_res->ssao_set_layout = sl;

                VkDescriptorPoolSize pool_sizes[2]{};
                pool_sizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                pool_sizes[0].descriptorCount = VulkanContext::k_max_frames_in_flight * 3;
                pool_sizes[1].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                pool_sizes[1].descriptorCount = VulkanContext::k_max_frames_in_flight;

                VkDescriptorPoolCreateInfo pool_ci{};
                pool_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                pool_ci.maxSets       = VulkanContext::k_max_frames_in_flight;
                pool_ci.poolSizeCount = 2;
                pool_ci.pPoolSizes    = pool_sizes;

                VkDescriptorPool pool = VK_NULL_HANDLE;
                r = vkCreateDescriptorPool(device, &pool_ci, nullptr, &pool);
                HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateDescriptorPool (ssao draw) failed");
                s_res->ssao_descriptor_pool = pool;
            }

            {
                std::vector<VkDescriptorSetLayout> layouts(VulkanContext::k_max_frames_in_flight,
                    s_res->ssao_set_layout);

                VkDescriptorSetAllocateInfo alloc{};
                alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                alloc.descriptorPool     = s_res->ssao_descriptor_pool;
                alloc.descriptorSetCount = VulkanContext::k_max_frames_in_flight;
                alloc.pSetLayouts        = layouts.data();

                std::vector<VkDescriptorSet> sets(VulkanContext::k_max_frames_in_flight);
                VkResult r = vkAllocateDescriptorSets(device, &alloc, sets.data());
                HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateDescriptorSets (ssao draw) failed");
                for (uint32_t f = 0; f < VulkanContext::k_max_frames_in_flight; ++f)
                    s_res->ssao_sets[f] = sets[f];
            }

            // Noise rotation texture (4x4 RGBA8)
            {
                std::mt19937 rng{7};
                std::uniform_int_distribution<int> dist(0, 255);
                struct NoisePixel { uint8_t r, g, b, a; };
                std::array<NoisePixel, 16> noise_pixels{};
                for (auto& p : noise_pixels) {
                    p.r = static_cast<uint8_t>(dist(rng));
                    p.g = static_cast<uint8_t>(dist(rng));
                    p.b = 0;
                    p.a = 255;
                }
                s_res->noise_texture = Texture2D::create(4, 4);
                s_res->noise_texture->set_data(noise_pixels.data(), static_cast<uint32_t>(sizeof(noise_pixels)));
            }

            // Per-frame SSAOKernelUBO (HOST_VISIBLE | HOST_COHERENT, persistently mapped)
            {
                // Generate hemisphere samples once
                SSAOKernelUBOData template_ubo{};
                generate_ssao_kernel(template_ubo.samples, 32);
                template_ubo.radius     = 0.5f;
                template_ubo.bias       = 0.075f;
                template_ubo.noise_scale = {1.0f, 1.0f}; // updated per-frame in execute_draw

                for (uint32_t f = 0; f < VulkanContext::k_max_frames_in_flight; ++f) {
                    VkBufferCreateInfo buf_ci{};
                    buf_ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                    buf_ci.size        = sizeof(SSAOKernelUBOData);
                    buf_ci.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

                    VkResult r = vkCreateBuffer(device, &buf_ci, nullptr, &s_res->ssao_kernel_ubos[f]);
                    HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateBuffer (ssao kernel ubo) failed");

                    VkMemoryRequirements req{};
                    vkGetBufferMemoryRequirements(device, s_res->ssao_kernel_ubos[f], &req);

                    VkMemoryAllocateInfo ai{};
                    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                    ai.allocationSize  = req.size;
                    ai.memoryTypeIndex = find_memory_type_ssao(phys, req.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

                    r = vkAllocateMemory(device, &ai, nullptr, &s_res->ssao_kernel_memories[f]);
                    HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateMemory (ssao kernel ubo) failed");

                    r = vkBindBufferMemory(device, s_res->ssao_kernel_ubos[f], s_res->ssao_kernel_memories[f], 0);
                    HN_CORE_ASSERT(r == VK_SUCCESS, "vkBindBufferMemory (ssao kernel ubo) failed");

                    r = vkMapMemory(device, s_res->ssao_kernel_memories[f], 0, sizeof(SSAOKernelUBOData), 0,
                        &s_res->ssao_kernel_mapped[f]);
                    HN_CORE_ASSERT(r == VK_SUCCESS, "vkMapMemory (ssao kernel ubo) failed");

                    memcpy(s_res->ssao_kernel_mapped[f], &template_ubo, sizeof(SSAOKernelUBOData));
                }
            }

            // Write SSAO draw descriptors
            {
                VkSampler sampler = backend->get_sampler_nearest();
                HN_CORE_ASSERT(sampler, "create_ssao_resources: sampler is null");

                for (uint32_t f = 0; f < VulkanContext::k_max_frames_in_flight; ++f) {
                    // Binding 3: SSAOKernelUBO
                    VkDescriptorBufferInfo buf_info{};
                    buf_info.buffer = s_res->ssao_kernel_ubos[f];
                    buf_info.offset = 0;
                    buf_info.range  = sizeof(SSAOKernelUBOData);

                    VkWriteDescriptorSet ubo_write{};
                    ubo_write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    ubo_write.dstSet          = s_res->ssao_sets[f];
                    ubo_write.dstBinding      = 3;
                    ubo_write.dstArrayElement = 0;
                    ubo_write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    ubo_write.descriptorCount = 1;
                    ubo_write.pBufferInfo     = &buf_info;

                    // Binding 2: noise texture (layout is known at init; bindings 0+1 are updated per-frame)
                    auto* vk_noise = static_cast<VulkanTexture2D*>(s_res->noise_texture.get());
                    VkDescriptorImageInfo noise_info{};
                    noise_info.sampler     = sampler;
                    noise_info.imageView   = static_cast<VkImageView>(vk_noise->get_vk_image_view());
                    noise_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                    VkWriteDescriptorSet noise_write{};
                    noise_write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    noise_write.dstSet          = s_res->ssao_sets[f];
                    noise_write.dstBinding      = 2;
                    noise_write.dstArrayElement = 0;
                    noise_write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    noise_write.descriptorCount = 1;
                    noise_write.pImageInfo      = &noise_info;

                    VkWriteDescriptorSet writes[2] = {noise_write, ubo_write};
                    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
                }
            }

            // Write blur descriptors
            {
                VkDescriptorSetLayoutBinding blur_binding{};
                blur_binding.binding        = 0;
                blur_binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                blur_binding.descriptorCount = 1;
                blur_binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

                VkDescriptorBindingFlags blur_flag{};

                VkDescriptorSetLayoutBindingFlagsCreateInfo blur_bf_ci{};
                blur_bf_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
                blur_bf_ci.bindingCount  = 1;
                blur_bf_ci.pBindingFlags = &blur_flag;

                VkDescriptorSetLayoutCreateInfo blur_layout_ci{};
                blur_layout_ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                blur_layout_ci.pNext        = &blur_bf_ci;
                blur_layout_ci.bindingCount = 1;
                blur_layout_ci.pBindings    = &blur_binding;

                VkDescriptorSetLayout blur_sl = VK_NULL_HANDLE;
                VkResult r = vkCreateDescriptorSetLayout(device, &blur_layout_ci, nullptr, &blur_sl);
                HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateDescriptorSetLayout (ssao blur) failed");
                s_res->ssao_blur_set_layout = blur_sl;

                VkDescriptorPoolSize blur_pool_size{};
                blur_pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                blur_pool_size.descriptorCount = VulkanContext::k_max_frames_in_flight;

                VkDescriptorPoolCreateInfo blur_pool_ci{};
                blur_pool_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                blur_pool_ci.maxSets       = VulkanContext::k_max_frames_in_flight;
                blur_pool_ci.poolSizeCount = 1;
                blur_pool_ci.pPoolSizes    = &blur_pool_size;

                VkDescriptorPool blur_pool = VK_NULL_HANDLE;
                r = vkCreateDescriptorPool(device, &blur_pool_ci, nullptr, &blur_pool);
                HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateDescriptorPool (ssao blur) failed");
                s_res->ssao_blur_descriptor_pool = blur_pool;

                std::vector<VkDescriptorSetLayout> blur_layouts(VulkanContext::k_max_frames_in_flight,
                    s_res->ssao_blur_set_layout);

                VkDescriptorSetAllocateInfo blur_alloc{};
                blur_alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                blur_alloc.descriptorPool     = s_res->ssao_blur_descriptor_pool;
                blur_alloc.descriptorSetCount = VulkanContext::k_max_frames_in_flight;
                blur_alloc.pSetLayouts        = blur_layouts.data();

                std::vector<VkDescriptorSet> blur_sets(VulkanContext::k_max_frames_in_flight);
                r = vkAllocateDescriptorSets(device, &blur_alloc, blur_sets.data());
                HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateDescriptorSets (ssao blur) failed");
                for (uint32_t f = 0; f < VulkanContext::k_max_frames_in_flight; ++f)
                    s_res->ssao_blur_sets[f] = blur_sets[f];
            }
        }

        static void cleanup_ssao_resources() {
            HN_PROFILE_FUNCTION();
            const VkDevice device = s_res->vk_ctx->get_device();
            if (!device) return;

            s_res->blur_pipelines.clear();

            // Blur descriptor resources
            for (uint32_t f = 0; f < VulkanContext::k_max_frames_in_flight; ++f)
                s_res->ssao_blur_sets[f] = VK_NULL_HANDLE;

            if (s_res->ssao_blur_descriptor_pool) {
                vkDestroyDescriptorPool(device, s_res->ssao_blur_descriptor_pool, nullptr);
                s_res->ssao_blur_descriptor_pool = VK_NULL_HANDLE;
            }
            if (s_res->ssao_blur_set_layout) {
                vkDestroyDescriptorSetLayout(device, s_res->ssao_blur_set_layout, nullptr);
                s_res->ssao_blur_set_layout = VK_NULL_HANDLE;
            }

            // Kernel UBOs
            for (uint32_t f = 0; f < VulkanContext::k_max_frames_in_flight; ++f) {
                if (s_res->ssao_kernel_mapped[f]) {
                    vkUnmapMemory(device, s_res->ssao_kernel_memories[f]);
                    s_res->ssao_kernel_mapped[f] = nullptr;
                }
                if (s_res->ssao_kernel_ubos[f]) {
                    vkDestroyBuffer(device, s_res->ssao_kernel_ubos[f], nullptr);
                    s_res->ssao_kernel_ubos[f] = VK_NULL_HANDLE;
                }
                if (s_res->ssao_kernel_memories[f]) {
                    vkFreeMemory(device, s_res->ssao_kernel_memories[f], nullptr);
                    s_res->ssao_kernel_memories[f] = VK_NULL_HANDLE;
                }
            }

            // Noise texture (managed by Texture2D)
            s_res->noise_texture.reset();

            // SSAO draw descriptor resources
            for (uint32_t f = 0; f < VulkanContext::k_max_frames_in_flight; ++f)
                s_res->ssao_sets[f] = VK_NULL_HANDLE;

            if (s_res->ssao_descriptor_pool) {
                vkDestroyDescriptorPool(device, s_res->ssao_descriptor_pool, nullptr);
                s_res->ssao_descriptor_pool = VK_NULL_HANDLE;
            }
            if (s_res->ssao_set_layout) {
                vkDestroyDescriptorSetLayout(device, s_res->ssao_set_layout, nullptr);
                s_res->ssao_set_layout = VK_NULL_HANDLE;
            }
        }

        static void update_ssao_draw_descriptors(uint32_t frame, VulkanFramebuffer* gbuffer_fb) {
            const auto* backend = s_res->vk_ctx->get_backend();
            HN_CORE_ASSERT(backend->get_sampler_linear() || backend->get_sampler_nearest(), "update_ssao_draw_descriptors: sampler is null");
            const VkDevice device = s_res->vk_ctx->get_device();

            VkDescriptorImageInfo image_infos[2]{};

            // binding 0: gNormal (color attachment 1 from gbuffer)
            image_infos[0].sampler     = backend->get_sampler_linear();
            image_infos[0].imageView   = gbuffer_fb->get_color_image_view(1);
            image_infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            // binding 1: gDepth
            image_infos[1].sampler     = backend->get_sampler_nearest();
            image_infos[1].imageView   = gbuffer_fb->get_depth_sampler_image_view();
            image_infos[1].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet writes[2]{};
            for (uint32_t i = 0; i < 2; ++i) {
                writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet          = s_res->ssao_sets[frame];
                writes[i].dstBinding      = i;
                writes[i].dstArrayElement = 0;
                writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[i].descriptorCount = 1;
                writes[i].pImageInfo      = &image_infos[i];
            }

            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        }

        static void update_blur_descriptors(uint32_t frame, VkImageView ssao_raw_view) {
            const auto* backend = s_res->vk_ctx->get_backend();
            VkSampler sampler = backend->get_sampler_linear();
            const VkDevice device = s_res->vk_ctx->get_device();

            VkDescriptorImageInfo img_info{};
            img_info.sampler     = sampler;
            img_info.imageView   = ssao_raw_view;
            img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet write{};
            write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet          = s_res->ssao_blur_sets[frame];
            write.dstBinding      = 0;
            write.dstArrayElement = 0;
            write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo      = &img_info;

            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }

    void Renderer3DSSAO::init(VulkanContext* ctx) {
        if (s_res) return;
        s_res = new SSAOResources{};
        s_res->vk_ctx = ctx;
        create_ssao_resources();
    }

    void Renderer3DSSAO::shutdown() {
        if (!s_res) return;
        cleanup_ssao_resources();
        delete s_res;
        s_res = nullptr;
    }

    void Renderer3DSSAO::execute_draw(FrameGraphPassContext& ctx) {
        HN_PROFILE_FUNCTION();
        if (!s_res || !s_res->vk_ctx || !Renderer3DInternal::g_renderer3d_data) return;

        auto* vk_ctx = s_res->vk_ctx;
        auto* data   = Renderer3DInternal::g_renderer3d_data;
        const uint32_t frame = vk_ctx->get_current_frame();

        // Get this pass's render target to find the render pass handle
        auto target   = ctx.get_pass_target_framebuffer();
        auto* vk_fb   = dynamic_cast<VulkanFramebuffer*>(target.get());
        HN_CORE_ASSERT(vk_fb, "Renderer3DSSAO::execute_draw: target is not a VulkanFramebuffer");
        void* rp_native = vk_fb->get_render_pass();
        HN_CORE_ASSERT(rp_native, "Renderer3DSSAO::execute_draw: rp_native is null");

        Ref<Pipeline> pipe = get_or_create_ssao_pipeline(rp_native);
        RenderCommand::bind_pipeline(pipe);

        // Bind global set=0 (camera matrices needed for view-space reconstruction)
        CameraUBO cam_ubo{};
        cam_ubo.view_proj  = data->scene_view_proj;
        cam_ubo.view       = data->scene_view;
        cam_ubo.projection = data->scene_projection;
        cam_ubo.position   = data->scene_camera_pos;
        cam_ubo.exposure   = data->scene_camera_exposure;
        VulkanRendererAPI::submit_camera(cam_ubo);
        VulkanRendererAPI::flush_globals();

        // Update noise_scale based on current render target size
        const auto& spec = target->get_specification();
        auto* mapped = reinterpret_cast<SSAOKernelUBOData*>(s_res->ssao_kernel_mapped[frame]);
        mapped->noise_scale = glm::vec2(float(spec.width) / 4.0f, float(spec.height) / 4.0f);

        // Update SSAO draw descriptor set (bindings 0+1 change per-frame due to potential resize)
        VulkanFramebuffer* gbuffer_vk = dynamic_cast<VulkanFramebuffer*>(
            ctx.get_input_framebuffer("gBuffer").get());
        HN_CORE_ASSERT(gbuffer_vk, "Renderer3DSSAO::execute_draw: could not get gBuffer");
        update_ssao_draw_descriptors(frame, gbuffer_vk);

        VkPipelineLayout pipe_layout = static_cast<VkPipelineLayout>(pipe->get_native_pipeline_layout());

        ctx.submit_vulkan_graphics_raw([pipe_layout, frame](VkCommandBuffer cmd) {
            HN_GPU_SCOPE(cmd, "SSAO Draw");
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_layout,
                1, 1, &s_res->ssao_sets[frame], 0, nullptr);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        });
    }

    void Renderer3DSSAO::execute_blur(FrameGraphPassContext& ctx) {
        HN_PROFILE_FUNCTION();
        if (!s_res || !s_res->vk_ctx || !Renderer3DInternal::g_renderer3d_data) return;

        const uint32_t frame = s_res->vk_ctx->get_current_frame();

        auto target   = ctx.get_pass_target_framebuffer();
        auto* vk_fb   = dynamic_cast<VulkanFramebuffer*>(target.get());
        HN_CORE_ASSERT(vk_fb, "Renderer3DSSAO::execute_blur: target is not a VulkanFramebuffer");
        void* rp_native = vk_fb->get_render_pass();
        HN_CORE_ASSERT(rp_native, "Renderer3DSSAO::execute_blur: rp_native is null");

        Ref<Pipeline> pipe = get_or_create_blur_pipeline(rp_native);
        RenderCommand::bind_pipeline(pipe);

        // Get the raw SSAO result from the draw pass
        auto ssao_raw_fb = ctx.get_input_framebuffer("ssaoRaw");
        auto* ssao_raw_vk = dynamic_cast<VulkanFramebuffer*>(ssao_raw_fb.get());
        HN_CORE_ASSERT(ssao_raw_vk, "Renderer3DSSAO::execute_blur: could not get ssaoRaw framebuffer");
        VkImageView ssao_raw_view = ssao_raw_vk->get_color_image_view(0);

        update_blur_descriptors(frame, ssao_raw_view);

        VkPipelineLayout pipe_layout = static_cast<VkPipelineLayout>(pipe->get_native_pipeline_layout());

        ctx.submit_vulkan_graphics_raw([pipe_layout, frame](VkCommandBuffer cmd) {
            HN_GPU_SCOPE(cmd, "SSAO Blur");
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_layout,
                1, 1, &s_res->ssao_blur_sets[frame], 0, nullptr);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        });
    }

    void Renderer3DSSAO::register_frame_graph_executors() {
        auto& registry = FrameGraphRegistry::get();
        registry.register_executor("ssao.draw", [](FrameGraphPassContext& ctx) {
            execute_draw(ctx);
        });
        registry.register_executor("ssao.blur", [](FrameGraphPassContext& ctx) {
            execute_blur(ctx);
        });
    }

    bool Renderer3DSSAO::is_initialized() {
        return s_res != nullptr;
    }
}