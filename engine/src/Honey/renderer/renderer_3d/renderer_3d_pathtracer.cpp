#include "hnpch.h"
#include "renderer_3d_pathtracer.h"

#include "glm/gtc/type_ptr.hpp"
#include "Honey/renderer/frame_graph_registry.h"
#include "Honey/renderer/gpu_types.h"
#include "Honey/renderer/mesh.h"
#include "Honey/renderer/shader_compiler.h"
#include "Honey/scene/components.h"
#include "Honey/scene/scene.h"
#include "platform/vulkan/vk_context.h"
#include "platform/vulkan/vk_framebuffer.h"
#include "platform/vulkan/vk_texture.h"

namespace Honey {

    namespace {

        struct PathTracerResources {
            VulkanContext* vk_ctx = nullptr;

            // Lazy-loaded function pointers (same pattern as shadow module).
            PFN_vkCreateAccelerationStructureKHR      fn_create_as     = nullptr;
            PFN_vkDestroyAccelerationStructureKHR     fn_destroy_as    = nullptr;
            PFN_vkCmdBuildAccelerationStructuresKHR   fn_cmd_build_as  = nullptr;
            PFN_vkGetAccelerationStructureBuildSizesKHR fn_get_as_sizes = nullptr;
            PFN_vkGetAccelerationStructureDeviceAddressKHR fn_get_as_addr = nullptr;
            PFN_vkCreateRayTracingPipelinesKHR        fn_create_rt_pipeline = nullptr;
            PFN_vkCmdTraceRaysKHR                     fn_cmd_trace_rays = nullptr;
            PFN_vkGetRayTracingShaderGroupHandlesKHR  fn_get_sbt_handles = nullptr;
            PFN_vkGetBufferDeviceAddressKHR           fn_get_buf_addr  = nullptr;

            // Cached once at init — physical device memory properties never change.
            VkPhysicalDeviceMemoryProperties cached_mem_props{};

            // Per-instance geometry lookup (one entry per TLAS instance, rebuilt each frame)
            struct GeometryInfo {
                VkDeviceAddress vertex_buffer_addr;
                VkDeviceAddress index_buffer_addr;
            };
            VkBuffer geometry_lookup_buffer = VK_NULL_HANDLE;
            VkDeviceMemory geometry_lookup_memory = VK_NULL_HANDLE;
            void* geometry_lookup_mapped = nullptr;
            uint32_t geometry_lookup_capacity = 0;

            // Material / texture data
            struct MaterialInfo {
                glm::vec4 base_color_factor{1.0f};    // 16 bytes
                glm::vec4 emissive_factor{0.0f};      // 16 bytes
                int  base_color_tex  = -1;            //  4 bytes
                int  metal_rough_tex = -1;            //  4 bytes  (B=metalness, G=roughness per glTF)
                int  normal_tex      = -1;            //  4 bytes
                int  emissive_tex    = -1;            //  4 bytes
                float metalness      = 1.0f;          //  4 bytes
                float roughness      = 1.0f;          //  4 bytes
                float normal_scale   = 1.0f;          //  4 bytes
                float _pad           = 0.0f;          //  4 bytes
            }; // 64 bytes. GLSL std430 must match exactly.

            static constexpr uint32_t k_max_rt_textures = 512;

            VkBuffer material_buffer = VK_NULL_HANDLE;
            VkDeviceMemory material_memory = VK_NULL_HANDLE;
            void* material_mapped = nullptr;
            uint32_t material_capacity = 0;

            // Texture descriptors — partially bound array, updated once per frame.
            // Keyed by VkImageView so we can detect changes without re-uploading.
            std::vector<std::pair<VkImageView, VkSampler>> bound_textures;
            // O(1) dedup index alongside bound_textures.
            std::unordered_map<VkImageView, int> texture_index_map;

            VkBuffer lights_buffer = VK_NULL_HANDLE;
            VkDeviceMemory lights_memory = VK_NULL_HANDLE;
            void* lights_mapped = nullptr;

            // One BLAS per unique Submesh* (keyed by pointer so it invalidates when mesh is replaced).
            struct BlasEntry {
                VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
                VkBuffer buffer = VK_NULL_HANDLE;
                VkDeviceMemory memory = VK_NULL_HANDLE;
                VkDeviceAddress device_address = 0;
            };
            std::unordered_map<const Submesh*, BlasEntry> blas_cache;

            // Persistent BLAS scratch — sized to the max scratch needed across all builds seen so
            // far, reused sequentially (barriers between builds allow offset-0 reuse).
            VkBuffer        blas_scratch_buffer = VK_NULL_HANDLE;
            VkDeviceMemory  blas_scratch_memory = VK_NULL_HANDLE;
            VkDeviceAddress blas_scratch_addr   = 0;
            VkDeviceSize    blas_scratch_size   = 0;

            // Pending BLAS builds accumulated by prepare_tlas_cpu(), consumed by record_pending_blas_builds().
            struct PendingBlas {
                const Submesh* submesh_key; // look up handle in blas_cache during recording
                VkDeviceAddress vbuf_addr;
                VkDeviceAddress ibuf_addr;
                uint32_t tri_count;
                uint32_t max_vertex;
            };
            std::vector<PendingBlas> pending_blas;

            // TLAS — persistent handle and backing buffer; refitted when instance count is stable.
            VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
            VkBuffer tlas_buffer = VK_NULL_HANDLE;
            VkDeviceMemory tlas_memory = VK_NULL_HANDLE;
            VkDeviceSize   tlas_backing_size = 0;       // size of tlas_buffer allocation
            uint32_t       tlas_last_instance_count = UINT32_MAX; // triggers rebuild on first frame

            // Persistent TLAS scratch — sized to buildScratchSize (always >= updateScratchSize).
            VkBuffer        tlas_scratch_buffer = VK_NULL_HANDLE;
            VkDeviceMemory  tlas_scratch_memory = VK_NULL_HANDLE;
            VkDeviceAddress tlas_scratch_addr   = 0;
            VkDeviceSize    tlas_scratch_size   = 0;

            // Per-frame TLAS state set by prepare_tlas_cpu(), consumed by record_tlas_build().
            uint32_t tlas_prim_count  = 0;
            bool     tlas_mode_update = false; // false = full build, true = refit

            // Per-frame instance upload buffer (host visible)
            VkBuffer instance_buffer = VK_NULL_HANDLE;
            VkDeviceMemory instance_memory = VK_NULL_HANDLE;
            void* instance_mapped = nullptr;
            uint32_t instance_buffer_capacity = 0;

            // Per-frame reusable work vectors (avoid heap churn every frame).
            std::vector<VkAccelerationStructureInstanceKHR> frame_instances;
            std::vector<GeometryInfo>                       frame_geo_infos;
            std::vector<MaterialInfo>                       frame_materials;

            // Output accumulation image (RGBA32F storage image).
            VkImage accum_image = VK_NULL_HANDLE;
            VkDeviceMemory accum_memory = VK_NULL_HANDLE;
            VkImageView accum_view = VK_NULL_HANDLE;
            uint32_t accum_width = 0;
            uint32_t accum_height = 0;
            uint32_t accum_frame_count = 0;
            bool accum_needs_layout_init = true; // transition UNDEFINED → GENERAL on first trace

            // SVGF G-buffer: normal.xyz + linear depth.w (RGBA32F, written by raygen).
            VkImage gbuffer_image = VK_NULL_HANDLE;
            VkDeviceMemory gbuffer_memory = VK_NULL_HANDLE;
            VkImageView gbuffer_view = VK_NULL_HANDLE;
            bool gbuffer_needs_layout_init = true;

            // SVGF À-Trous ping-pong scratch image.
            VkImage ping_image = VK_NULL_HANDLE;
            VkDeviceMemory ping_memory = VK_NULL_HANDLE;
            VkImageView ping_view = VK_NULL_HANDLE;
            bool ping_needs_layout_init = true;

            // SVGF filtered output (final result after À-Trous, blitted to display).
            VkImage filtered_image = VK_NULL_HANDLE;
            VkDeviceMemory filtered_memory = VK_NULL_HANDLE;
            VkImageView filtered_view = VK_NULL_HANDLE;
            bool filtered_needs_layout_init = true;

            // SVGF compute pipeline.
            VkPipeline svgf_pipeline = VK_NULL_HANDLE;
            VkPipelineLayout svgf_pipeline_layout = VK_NULL_HANDLE;
            VkDescriptorSetLayout svgf_desc_layout = VK_NULL_HANDLE;
            VkDescriptorPool svgf_desc_pool = VK_NULL_HANDLE;
            VkDescriptorSet svgf_desc_set = VK_NULL_HANDLE;
            bool svgf_pipeline_built = false;

            // Camera matrices for ray generation push constants.
            glm::mat4 inv_view{1.0f};
            glm::mat4 inv_proj{1.0f};

            // RT pipeline + SBT.
            VkPipeline rt_pipeline = VK_NULL_HANDLE;
            VkPipelineLayout rt_pipeline_layout = VK_NULL_HANDLE;
            VkDescriptorSetLayout rt_desc_layout = VK_NULL_HANDLE;
            VkDescriptorPool rt_desc_pool = VK_NULL_HANDLE;
            VkDescriptorSet rt_desc_set = VK_NULL_HANDLE;

            // SBT buffer holds [raygen | miss | hit] regions contiguously.
            VkBuffer sbt_buffer = VK_NULL_HANDLE;
            VkDeviceMemory sbt_memory = VK_NULL_HANDLE;
            VkStridedDeviceAddressRegionKHR sbt_raygen{};
            VkStridedDeviceAddressRegionKHR sbt_miss{};
            VkStridedDeviceAddressRegionKHR sbt_hit{};
            VkStridedDeviceAddressRegionKHR sbt_callable{}; // unused, must be zeroed

            bool pipeline_built = false;
        };

        static PathTracerResources* s_res = nullptr;

        // Takes cached props instead of querying vkGetPhysicalDeviceMemoryProperties every call.
        static uint32_t find_memory_type(const VkPhysicalDeviceMemoryProperties& mem_props,
                                         uint32_t type_filter,
                                         VkMemoryPropertyFlags props) {
            for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
                if ((type_filter & (1u << i)) && ((mem_props.memoryTypes[i].propertyFlags & props) == props))
                    return i;
            }
            HN_CORE_ASSERT(false, "Failed to find suitable Vulkan memory type");
            return 0;
        }

        static VkTransformMatrixKHR to_vk_transform(const glm::mat4& mat) {
            VkTransformMatrixKHR out = {};
            glm::mat4 t = glm::transpose(mat);
            memcpy(out.matrix, glm::value_ptr(t), sizeof(out.matrix));
            return out;
        }

        static void destroy_image(VkDevice device, VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
            if (view  != VK_NULL_HANDLE) { vkDestroyImageView(device, view,  nullptr); view  = VK_NULL_HANDLE; }
            if (img   != VK_NULL_HANDLE) { vkDestroyImage(device, img,   nullptr); img   = VK_NULL_HANDLE; }
            if (mem   != VK_NULL_HANDLE) { vkFreeMemory(device, mem,   nullptr); mem   = VK_NULL_HANDLE; }
        }

        static void destroy_accum_image() {
            VkDevice device = s_res->vk_ctx->get_device();
            destroy_image(device, s_res->accum_image,    s_res->accum_memory,    s_res->accum_view);
            destroy_image(device, s_res->gbuffer_image,  s_res->gbuffer_memory,  s_res->gbuffer_view);
            destroy_image(device, s_res->ping_image,     s_res->ping_memory,     s_res->ping_view);
            destroy_image(device, s_res->filtered_image, s_res->filtered_memory, s_res->filtered_view);
        }

        static void alloc_storage_image(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags extra_usage,
                                        VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
            VkDevice device = s_res->vk_ctx->get_device();

            VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            ici.imageType     = VK_IMAGE_TYPE_2D;
            ici.format        = fmt;
            ici.extent        = {w, h, 1};
            ici.mipLevels     = 1;
            ici.arrayLayers   = 1;
            ici.samples       = VK_SAMPLE_COUNT_1_BIT;
            ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
            ici.usage         = VK_IMAGE_USAGE_STORAGE_BIT | extra_usage;
            ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkResult r = vkCreateImage(device, &ici, nullptr, &img);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateImage failed");

            VkMemoryRequirements req{};
            vkGetImageMemoryRequirements(device, img, &req);

            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            r = vkAllocateMemory(device, &ai, nullptr, &mem);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateMemory failed");
            vkBindImageMemory(device, img, mem, 0);

            VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vci.image            = img;
            vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
            vci.format           = fmt;
            vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            r = vkCreateImageView(device, &vci, nullptr, &view);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateImageView failed");
        }

        static void create_accum_image(uint32_t w, uint32_t h) {
            destroy_accum_image();

            alloc_storage_image(w, h, VK_FORMAT_R32G32B32A32_SFLOAT,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                s_res->accum_image,    s_res->accum_memory,    s_res->accum_view);
            alloc_storage_image(w, h, VK_FORMAT_R32G32B32A32_SFLOAT, 0,
                s_res->gbuffer_image,  s_res->gbuffer_memory,  s_res->gbuffer_view);
            alloc_storage_image(w, h, VK_FORMAT_R32G32B32A32_SFLOAT, 0,
                s_res->ping_image,     s_res->ping_memory,     s_res->ping_view);
            alloc_storage_image(w, h, VK_FORMAT_R32G32B32A32_SFLOAT,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                s_res->filtered_image, s_res->filtered_memory, s_res->filtered_view);

            s_res->accum_width    = w;
            s_res->accum_height   = h;
            s_res->accum_frame_count      = 0;
            s_res->accum_needs_layout_init    = true;
            s_res->gbuffer_needs_layout_init  = true;
            s_res->ping_needs_layout_init     = true;
            s_res->filtered_needs_layout_init = true;
        }

        static void update_desc_set() {
            HN_CORE_ASSERT(s_res->geometry_lookup_buffer != VK_NULL_HANDLE, "Geometry lookup buffer not created");
            HN_CORE_ASSERT(s_res->rt_desc_set != VK_NULL_HANDLE, "RT descriptor set not created");
            HN_CORE_ASSERT(s_res->accum_view != VK_NULL_HANDLE, "Accumulation image not created");
            HN_CORE_ASSERT(s_res->gbuffer_view != VK_NULL_HANDLE, "G-buffer image not created");

            VkDevice device = s_res->vk_ctx->get_device();

            VkWriteDescriptorSet writes[7] = {};

            VkWriteDescriptorSetAccelerationStructureKHR as_info{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
            as_info.accelerationStructureCount = 1;
            as_info.pAccelerationStructures    = &s_res->tlas;

            writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].pNext           = &as_info;
            writes[0].dstSet          = s_res->rt_desc_set;
            writes[0].dstBinding      = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

            VkDescriptorImageInfo image_info{};
            image_info.imageView   = s_res->accum_view;
            image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet          = s_res->rt_desc_set;
            writes[1].dstBinding      = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].pImageInfo      = &image_info;

            // binding 2 - geometry info SSBO
            VkDescriptorBufferInfo geo_buf_info{};
            geo_buf_info.buffer = s_res->geometry_lookup_buffer;
            geo_buf_info.offset = 0;
            geo_buf_info.range  = VK_WHOLE_SIZE;

            writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet          = s_res->rt_desc_set;
            writes[2].dstBinding      = 2;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[2].pBufferInfo     = &geo_buf_info;

            // binding 3 - material SSBO
            VkDescriptorBufferInfo mat_buf_info{};
            mat_buf_info.buffer = s_res->material_buffer;
            mat_buf_info.offset = 0;
            mat_buf_info.range  = VK_WHOLE_SIZE;

            writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet          = s_res->rt_desc_set;
            writes[3].dstBinding      = 3;
            writes[3].descriptorCount = 1;
            writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[3].pBufferInfo     = &mat_buf_info;

            // binding 4 — texture array (write only the occupied slots)
            std::vector<VkDescriptorImageInfo> img_infos(s_res->bound_textures.size());
            for (size_t i = 0; i < s_res->bound_textures.size(); i++) {
                img_infos[i].imageView   = s_res->bound_textures[i].first;
                img_infos[i].sampler     = s_res->bound_textures[i].second;
                img_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            if (!img_infos.empty()) {
                writes[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[4].dstSet          = s_res->rt_desc_set;
                writes[4].dstBinding      = 4;
                writes[4].dstArrayElement = 0;
                writes[4].descriptorCount = (uint32_t)img_infos.size();
                writes[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[4].pImageInfo      = img_infos.data();
            }

            // binding 5 - lights UBO
            VkDescriptorBufferInfo lights_buf_info{};
            lights_buf_info.buffer = s_res->lights_buffer;
            lights_buf_info.offset = 0;
            lights_buf_info.range  = VK_WHOLE_SIZE;

            writes[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[5].dstSet          = s_res->rt_desc_set;
            writes[5].dstBinding      = 5;
            writes[5].descriptorCount = 1;
            writes[5].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[5].pBufferInfo     = &lights_buf_info;

            // binding 6 - G-buffer image
            VkDescriptorImageInfo gbuffer_info{};
            gbuffer_info.imageView   = s_res->gbuffer_view;
            gbuffer_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            writes[6].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[6].dstSet          = s_res->rt_desc_set;
            writes[6].dstBinding      = 6;
            writes[6].descriptorCount = 1;
            writes[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[6].pImageInfo      = &gbuffer_info;

            // writes[4] is only valid when img_infos is non-empty; slide slots when empty.
            uint32_t write_count;
            if (img_infos.empty()) {
                writes[4] = writes[5];
                writes[5] = writes[6];
                write_count = 6;
            } else {
                write_count = 7;
            }
            vkUpdateDescriptorSets(device, write_count, writes, 0, nullptr);
        }

        static void ensure_instance_buffer(uint32_t count) {
            HN_PROFILE_FUNCTION();

            if (count <= s_res->instance_buffer_capacity)
                return;

            VkDevice device = s_res->vk_ctx->get_device();

            if (s_res->instance_buffer != VK_NULL_HANDLE) {
                vkUnmapMemory(device, s_res->instance_memory);
                vkDestroyBuffer(device, s_res->instance_buffer, nullptr);
                vkFreeMemory(device, s_res->instance_memory, nullptr);
            }

            VkDeviceSize size = count * sizeof(VkAccelerationStructureInstanceKHR);

            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.size      = size;
            bi.usage     = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                         | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateBuffer(device, &bi, nullptr, &s_res->instance_buffer);

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, s_res->instance_buffer, &req);

            VkMemoryAllocateFlagsInfo flags_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
            flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.pNext           = &flags_info;
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            vkAllocateMemory(device, &ai, nullptr, &s_res->instance_memory);
            vkBindBufferMemory(device, s_res->instance_buffer, s_res->instance_memory, 0);
            vkMapMemory(device, s_res->instance_memory, 0, size, 0, &s_res->instance_mapped);

            s_res->instance_buffer_capacity = count;
        }

        static void ensure_geometry_lookup_buffer(uint32_t count) {
            if (count <= s_res->geometry_lookup_capacity)
                return;

            VkDevice device = s_res->vk_ctx->get_device();
            if (s_res->geometry_lookup_buffer != VK_NULL_HANDLE) {
                vkUnmapMemory(device, s_res->geometry_lookup_memory);
                vkDestroyBuffer(device, s_res->geometry_lookup_buffer, nullptr);
                vkFreeMemory(device, s_res->geometry_lookup_memory, nullptr);
            }

            VkDeviceSize size = count * sizeof(PathTracerResources::GeometryInfo);
            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.size      = size;
            bi.usage     = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateBuffer(device, &bi, nullptr, &s_res->geometry_lookup_buffer);

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, s_res->geometry_lookup_buffer, &req);

            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            vkAllocateMemory(device, &ai, nullptr, &s_res->geometry_lookup_memory);
            vkBindBufferMemory(device, s_res->geometry_lookup_buffer, s_res->geometry_lookup_memory, 0);
            vkMapMemory(device, s_res->geometry_lookup_memory, 0, size, 0, &s_res->geometry_lookup_mapped);
            s_res->geometry_lookup_capacity = count;
        }

        static void ensure_material_buffer(uint32_t count) {
            if (count <= s_res->material_capacity)
                return;

            VkDevice device = s_res->vk_ctx->get_device();
            if (s_res->material_buffer != VK_NULL_HANDLE) {
                vkUnmapMemory(device, s_res->material_memory);
                vkDestroyBuffer(device, s_res->material_buffer, nullptr);
                vkFreeMemory(device, s_res->material_memory, nullptr);
            }

            VkDeviceSize size = count * sizeof(PathTracerResources::MaterialInfo);
            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.size      = size;
            bi.usage     = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateBuffer(device, &bi, nullptr, &s_res->material_buffer);

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, s_res->material_buffer, &req);

            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            vkAllocateMemory(device, &ai, nullptr, &s_res->material_memory);
            vkBindBufferMemory(device, s_res->material_buffer, s_res->material_memory, 0);
            vkMapMemory(device, s_res->material_memory, 0, size, 0, &s_res->material_mapped);
            s_res->material_capacity = count;
        }

        // Grows a generic device-local scratch buffer to at least `needed` bytes.
        // Returns the new device address (or the existing one if no growth was needed).
        static VkDeviceAddress ensure_scratch_buffer(VkDeviceSize needed,
                                                     VkBuffer& buf, VkDeviceMemory& mem,
                                                     VkDeviceAddress& addr, VkDeviceSize& current_size) {
            if (needed <= current_size)
                return addr;

            VkDevice device = s_res->vk_ctx->get_device();
            if (buf != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, buf, nullptr);
                vkFreeMemory(device, mem, nullptr);
                buf  = VK_NULL_HANDLE;
                mem  = VK_NULL_HANDLE;
                addr = 0;
            }

            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.size        = needed;
            bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VkResult r = vkCreateBuffer(device, &bi, nullptr, &buf);
            HN_CORE_ASSERT(r == VK_SUCCESS, "ensure_scratch_buffer: vkCreateBuffer failed");

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, buf, &req);

            VkMemoryAllocateFlagsInfo flags_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
            flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.pNext           = &flags_info;
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            r = vkAllocateMemory(device, &ai, nullptr, &mem);
            HN_CORE_ASSERT(r == VK_SUCCESS, "ensure_scratch_buffer: vkAllocateMemory failed");
            vkBindBufferMemory(device, buf, mem, 0);

            VkBufferDeviceAddressInfo addr_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            addr_info.buffer = buf;
            addr = s_res->fn_get_buf_addr(device, &addr_info);
            current_size = needed;
            return addr;
        }

        // Allocates a BLAS backing buffer and creates its AS handle on the CPU.
        // Does NOT submit any GPU work — recording is deferred to record_pending_blas_builds().
        // Appends to s_res->pending_blas and grows s_res->blas_scratch_size as needed.
        static PathTracerResources::BlasEntry alloc_blas_entry(
                const Submesh* submesh_key,
                VkDeviceAddress vbuf_addr,
                VkDeviceAddress ibuf_addr,
                uint32_t tri_count,
                uint32_t max_vertex) {
            HN_PROFILE_FUNCTION();

            VkDevice device = s_res->vk_ctx->get_device();

            VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
            triangles.sType          = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            triangles.vertexFormat   = VK_FORMAT_R32G32B32_SFLOAT;
            triangles.vertexData.deviceAddress = vbuf_addr;
            triangles.vertexStride   = 56; // sizeof(VertexPBR) from gltf_loader.cpp
            triangles.maxVertex      = max_vertex;
            triangles.indexType      = VK_INDEX_TYPE_UINT32;
            triangles.indexData.deviceAddress  = ibuf_addr;

            VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
            geometry.geometryType        = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.geometry.triangles  = triangles;
            geometry.flags               = VK_GEOMETRY_OPAQUE_BIT_KHR;

            VkAccelerationStructureBuildGeometryInfoKHR build_info{};
            build_info.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            build_info.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            build_info.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            build_info.geometryCount = 1;
            build_info.pGeometries   = &geometry;

            VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
            s_res->fn_get_as_sizes(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                   &build_info, &tri_count, &sizes);

            PathTracerResources::BlasEntry entry{};

            // Alloc backing buffer and create handle.
            {
                VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                bi.size        = sizes.accelerationStructureSize;
                bi.usage       = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                               | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                VkResult r = vkCreateBuffer(device, &bi, nullptr, &entry.buffer);
                HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateBuffer failed (BLAS)");

                VkMemoryRequirements req{};
                vkGetBufferMemoryRequirements(device, entry.buffer, &req);

                VkMemoryAllocateFlagsInfo flags_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
                flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

                VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                ai.pNext           = &flags_info;
                ai.allocationSize  = req.size;
                ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                r = vkAllocateMemory(device, &ai, nullptr, &entry.memory);
                HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateMemory failed (BLAS)");
                r = vkBindBufferMemory(device, entry.buffer, entry.memory, 0);
                HN_CORE_ASSERT(r == VK_SUCCESS, "vkBindBufferMemory failed (BLAS)");
            }

            VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
            ci.buffer = entry.buffer;
            ci.size   = sizes.accelerationStructureSize;
            ci.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            VkResult r = s_res->fn_create_as(device, &ci, nullptr, &entry.handle);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateAccelerationStructureKHR failed");

            VkAccelerationStructureDeviceAddressInfoKHR as_addr{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
            as_addr.accelerationStructure = entry.handle;
            entry.device_address = s_res->fn_get_as_addr(device, &as_addr);

            // Grow persistent BLAS scratch to accommodate this build (scratch is reused sequentially).
            ensure_scratch_buffer(sizes.buildScratchSize,
                                  s_res->blas_scratch_buffer, s_res->blas_scratch_memory,
                                  s_res->blas_scratch_addr,   s_res->blas_scratch_size);

            s_res->pending_blas.push_back({ submesh_key, vbuf_addr, ibuf_addr, tri_count, max_vertex });
            return entry;
        }

        // Records all pending BLAS builds into cmd, then clears pending_blas.
        // Emits an inter-build barrier when reusing scratch sequentially, and a final
        // AS_WRITE → AS_READ barrier so the TLAS build can consume the results.
        static void record_pending_blas_builds(VkCommandBuffer cmd) {
            if (s_res->pending_blas.empty())
                return;

            const auto& pending = s_res->pending_blas;
            for (size_t i = 0; i < pending.size(); ++i) {
                const auto& pb = pending[i];
                const PathTracerResources::BlasEntry& entry = s_res->blas_cache.at(pb.submesh_key);

                VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
                triangles.sType          = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                triangles.vertexFormat   = VK_FORMAT_R32G32B32_SFLOAT;
                triangles.vertexData.deviceAddress = pb.vbuf_addr;
                triangles.vertexStride   = 56;
                triangles.maxVertex      = pb.max_vertex;
                triangles.indexType      = VK_INDEX_TYPE_UINT32;
                triangles.indexData.deviceAddress  = pb.ibuf_addr;

                VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
                geometry.geometryType       = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                geometry.geometry.triangles = triangles;
                geometry.flags              = VK_GEOMETRY_OPAQUE_BIT_KHR;

                VkAccelerationStructureBuildGeometryInfoKHR build_info{};
                build_info.sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
                build_info.type                      = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                build_info.flags                     = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
                build_info.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
                build_info.dstAccelerationStructure  = entry.handle;
                build_info.geometryCount             = 1;
                build_info.pGeometries               = &geometry;
                build_info.scratchData.deviceAddress = s_res->blas_scratch_addr; // offset 0, reused sequentially

                VkAccelerationStructureBuildRangeInfoKHR range{};
                range.primitiveCount = pb.tri_count;
                const VkAccelerationStructureBuildRangeInfoKHR* range_ptr = &range;
                s_res->fn_cmd_build_as(cmd, 1, &build_info, &range_ptr);

                // Between sequential builds: ensure previous build's scratch writes are visible
                // before the next build reads/writes scratch at offset 0.
                if (i + 1 < pending.size()) {
                    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                    mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                    mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
                                     | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        0, 1, &mb, 0, nullptr, 0, nullptr);
                }
            }

            // Final barrier: all BLAS writes must be visible to the TLAS build.
            VkMemoryBarrier final_mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            final_mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            final_mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                0, 1, &final_mb, 0, nullptr, 0, nullptr);

            s_res->pending_blas.clear();
        }

        // CPU-only: iterates the scene, uploads instance/material/geo data, prepares TLAS state.
        // New BLASes are allocated but not built — they are appended to pending_blas.
        // Returns false if the scene has no renderable instances.
        static bool prepare_tlas_cpu(Scene* scene) {
            VkDevice device = s_res->vk_ctx->get_device();

            s_res->frame_instances.clear();
            s_res->frame_geo_infos.clear();
            s_res->frame_materials.clear();
            s_res->bound_textures.clear();
            s_res->texture_index_map.clear();
            s_res->pending_blas.clear();

            auto view = scene->get_registry().view<TransformComponent, MeshRendererComponent>();
            for (auto entity : view) {
                auto& mrc = view.get<MeshRendererComponent>(entity);
                auto& tc  = view.get<TransformComponent>(entity);

                if (!mrc.mesh || !mrc.mesh->meshlet_buffers.has_value())
                    continue;
                auto& bufs = *mrc.mesh->meshlet_buffers;
                if (!bufs.flat_index_buffer || !bufs.vertex_buffer)
                    continue;

                VkBufferDeviceAddressInfo addr_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
                addr_info.buffer = reinterpret_cast<VkBuffer>(bufs.vertex_buffer->get_native_buffer());
                const VkDeviceAddress vbuf_addr = s_res->fn_get_buf_addr(device, &addr_info);
                addr_info.buffer = reinterpret_cast<VkBuffer>(bufs.flat_index_buffer->get_native_buffer());
                const VkDeviceAddress ibuf_base_addr = s_res->fn_get_buf_addr(device, &addr_info);

                const uint32_t max_vertex = bufs.vertex_buffer->get_size() / 56u - 1u;

                const auto& submeshes = mrc.mesh->get_submeshes();
                for (size_t si = 0; si < submeshes.size(); ++si) {
                    const Submesh& sm = submeshes[si];
                    if (!sm.meshlets.has_value()) continue;
                    if (sm.meshlets->flat_index_tri_count == 0) continue;

                    const uint32_t tri_count = sm.meshlets->flat_index_tri_count;
                    const VkDeviceAddress ibuf_addr = ibuf_base_addr
                        + (VkDeviceAddress)sm.meshlets->flat_index_first * 3u * sizeof(uint32_t);

                    // Allocate BLAS if not cached — GPU build is deferred to record_pending_blas_builds().
                    if (!s_res->blas_cache.count(&sm))
                        s_res->blas_cache[&sm] = alloc_blas_entry(&sm, vbuf_addr, ibuf_addr, tri_count, max_vertex);

                    auto& blas = s_res->blas_cache.at(&sm);

                    // Pick material: override at submesh index, else the submesh's own material.
                    PathTracerResources::MaterialInfo mat_info{};
                    Ref<Material> mat;
                    if (si < mrc.material_overrides.size() && mrc.material_overrides[si])
                        mat = mrc.material_overrides[si];
                    else if (sm.material)
                        mat = sm.material;

                    if (mat) {
                        mat_info.base_color_factor = mat->get_base_color_factor();
                        mat_info.emissive_factor   = glm::vec4(mat->get_emissive_factor(), 0.0f);
                        mat_info.metalness         = mat->get_metallic_factor();
                        mat_info.roughness         = mat->get_roughness_factor();
                        mat_info.normal_scale      = mat->get_normal_scale();

                        // O(1) texture deduplication via unordered_map.
                        auto register_tex = [&](const Ref<Texture2D>& tex) -> int {
                            if (!tex) return -1;
                            auto* vk = dynamic_cast<VulkanTexture2D*>(tex.get());
                            VkImageView img_view = vk ? static_cast<VkImageView>(vk->get_vk_image_view()) : VK_NULL_HANDLE;
                            VkSampler   sampler  = vk ? static_cast<VkSampler>(vk->get_vk_sampler())      : VK_NULL_HANDLE;
                            if (img_view == VK_NULL_HANDLE) return -1;
                            auto [it, inserted] = s_res->texture_index_map.try_emplace(
                                img_view, (int)s_res->bound_textures.size());
                            if (inserted)
                                s_res->bound_textures.push_back({ img_view, sampler });
                            return it->second;
                        };

                        mat_info.base_color_tex  = register_tex(mat->get_base_color_texture());
                        mat_info.metal_rough_tex = register_tex(mat->get_metallic_roughness_texture());
                        mat_info.normal_tex      = register_tex(mat->get_normal_texture());
                        mat_info.emissive_tex    = register_tex(mat->get_emissive_texture());
                    }
                    s_res->frame_materials.push_back(mat_info);
                    s_res->frame_geo_infos.push_back({ vbuf_addr, ibuf_addr });

                    VkAccelerationStructureInstanceKHR inst{};
                    inst.transform                              = to_vk_transform(tc.world * sm.transform);
                    inst.instanceCustomIndex                    = (uint32_t)(s_res->frame_geo_infos.size() - 1);
                    inst.mask                                   = 0xFF;
                    inst.instanceShaderBindingTableRecordOffset = 0;
                    inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                    inst.accelerationStructureReference         = blas.device_address;
                    s_res->frame_instances.push_back(inst);
                }
            }

            // Upload host data to persistently-mapped GPU-visible buffers.
            ensure_geometry_lookup_buffer((uint32_t)s_res->frame_geo_infos.size());
            if (!s_res->frame_geo_infos.empty())
                memcpy(s_res->geometry_lookup_mapped, s_res->frame_geo_infos.data(),
                       s_res->frame_geo_infos.size() * sizeof(PathTracerResources::GeometryInfo));

            ensure_material_buffer((uint32_t)s_res->frame_materials.size());
            if (!s_res->frame_materials.empty())
                memcpy(s_res->material_mapped, s_res->frame_materials.data(),
                       s_res->frame_materials.size() * sizeof(PathTracerResources::MaterialInfo));

            if (s_res->frame_instances.empty()) {
                static bool warned = false;
                if (!warned) {
                    HN_CORE_WARN("[PathTracer] prepare_tlas_cpu: no renderable instances — scene has no meshes with meshlet+flat_index buffers");
                    warned = true;
                }
                return false;
            }

            ensure_instance_buffer((uint32_t)s_res->frame_instances.size());
            memcpy(s_res->instance_mapped, s_res->frame_instances.data(),
                   s_res->frame_instances.size() * sizeof(VkAccelerationStructureInstanceKHR));

            // Determine if a full rebuild or an in-place refit is needed.
            const uint32_t prim_count   = (uint32_t)s_res->frame_instances.size();
            const bool count_changed    = (prim_count != s_res->tlas_last_instance_count);
            const bool first_build      = (s_res->tlas == VK_NULL_HANDLE);
            const bool need_full_build  = first_build || count_changed;

            if (need_full_build) {
                // Query AS and scratch sizes for a fresh build.
                VkAccelerationStructureGeometryInstancesDataKHR instances_data{};
                instances_data.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;

                VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
                geometry.geometryType       = VK_GEOMETRY_TYPE_INSTANCES_KHR;
                geometry.geometry.instances = instances_data;

                VkAccelerationStructureBuildGeometryInfoKHR size_query{};
                size_query.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
                size_query.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
                // ALLOW_UPDATE so future same-count frames can refit instead of rebuilding.
                size_query.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                                         | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
                size_query.geometryCount = 1;
                size_query.pGeometries   = &geometry;

                VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
                s_res->fn_get_as_sizes(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                       &size_query, &prim_count, &sizes);

                // Reallocate backing buffer only if it has grown beyond current capacity.
                if (sizes.accelerationStructureSize > s_res->tlas_backing_size) {
                    if (s_res->tlas != VK_NULL_HANDLE) {
                        // The TLAS may still be in use — wait for all GPU work to complete
                        // before destroying and reallocating.  This path only triggers when
                        // the instance count grows past the previous high-water mark.
                        vkDeviceWaitIdle(device);
                        s_res->fn_destroy_as(device, s_res->tlas, nullptr);
                        vkDestroyBuffer(device, s_res->tlas_buffer, nullptr);
                        vkFreeMemory(device, s_res->tlas_memory, nullptr);
                        s_res->tlas = VK_NULL_HANDLE;
                    }

                    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                    bi.size        = sizes.accelerationStructureSize;
                    bi.usage       = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                                   | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                    vkCreateBuffer(device, &bi, nullptr, &s_res->tlas_buffer);

                    VkMemoryRequirements req{};
                    vkGetBufferMemoryRequirements(device, s_res->tlas_buffer, &req);

                    VkMemoryAllocateFlagsInfo flags_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
                    flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

                    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                    ai.pNext           = &flags_info;
                    ai.allocationSize  = req.size;
                    ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                    vkAllocateMemory(device, &ai, nullptr, &s_res->tlas_memory);
                    vkBindBufferMemory(device, s_res->tlas_buffer, s_res->tlas_memory, 0);
                    s_res->tlas_backing_size = sizes.accelerationStructureSize;
                }

                // Create the TLAS handle if it was destroyed (or never existed).
                if (s_res->tlas == VK_NULL_HANDLE) {
                    VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
                    ci.buffer = s_res->tlas_buffer;
                    ci.size   = s_res->tlas_backing_size;
                    ci.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
                    s_res->fn_create_as(device, &ci, nullptr, &s_res->tlas);
                }

                // Scratch: buildScratchSize is always >= updateScratchSize so one persistent
                // buffer covers both modes.
                ensure_scratch_buffer(sizes.buildScratchSize,
                                      s_res->tlas_scratch_buffer, s_res->tlas_scratch_memory,
                                      s_res->tlas_scratch_addr,   s_res->tlas_scratch_size);

                s_res->tlas_mode_update        = false;
                s_res->tlas_last_instance_count = prim_count;
            } else {
                // Same instance count — refit in place; scratch already sized from the initial build.
                s_res->tlas_mode_update = true;
            }

            s_res->tlas_prim_count = prim_count;
            return true;
        }

        // Records the TLAS build or refit into cmd, then emits an AS_WRITE → RT_READ barrier
        // so the immediately-following vkCmdTraceRaysKHR can consume the result.
        static void record_tlas_build(VkCommandBuffer cmd) {
            VkBufferDeviceAddressInfo addr_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            addr_info.buffer = s_res->instance_buffer;
            VkDeviceAddress instances_addr = s_res->fn_get_buf_addr(s_res->vk_ctx->get_device(), &addr_info);

            VkAccelerationStructureGeometryInstancesDataKHR instances_data{};
            instances_data.sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
            instances_data.arrayOfPointers = VK_FALSE;
            instances_data.data.deviceAddress = instances_addr;

            VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
            geometry.geometryType       = VK_GEOMETRY_TYPE_INSTANCES_KHR;
            geometry.geometry.instances = instances_data;

            VkAccelerationStructureBuildGeometryInfoKHR build_info{};
            build_info.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            build_info.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            build_info.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                                     | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
            build_info.mode                      = s_res->tlas_mode_update
                                                       ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                                                       : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            build_info.srcAccelerationStructure  = s_res->tlas_mode_update ? s_res->tlas : VK_NULL_HANDLE;
            build_info.dstAccelerationStructure  = s_res->tlas;
            build_info.geometryCount             = 1;
            build_info.pGeometries               = &geometry;
            build_info.scratchData.deviceAddress = s_res->tlas_scratch_addr;

            VkAccelerationStructureBuildRangeInfoKHR range{};
            range.primitiveCount = s_res->tlas_prim_count;
            const VkAccelerationStructureBuildRangeInfoKHR* range_ptr = &range;
            s_res->fn_cmd_build_as(cmd, 1, &build_info, &range_ptr);

            // TLAS write must complete before ray tracing reads the structure.
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                0, 1, &barrier, 0, nullptr, 0, nullptr);
        }

        static void build_rt_pipeline() {
            VkDevice device = s_res->vk_ctx->get_device();
            VkPhysicalDevice physical_device = s_res->vk_ctx->get_physical_device();

            // Query RT pipeline properties
            VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props{};
            rt_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
            VkPhysicalDeviceProperties2 props2{};
            props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            props2.pNext = &rt_props;
            vkGetPhysicalDeviceProperties2(physical_device, &props2);

            uint32_t handle_size = rt_props.shaderGroupHandleSize;
            uint32_t base_align  = rt_props.shaderGroupBaseAlignment;

            auto align_up = [](uint32_t value, uint32_t align) -> uint32_t {
                return (value + align - 1) & ~(align - 1);
            };

            uint32_t region_size = align_up(handle_size, base_align);
            uint32_t sbt_total   = region_size * 4; // raygen + miss_primary + miss_shadow + hit

            VkDescriptorSetLayoutBinding bindings[7] = {};
            bindings[0].binding         = 0;
            bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            // binding 1 - Accumulation image
            bindings[1].binding         = 1;
            bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            // binding 2 - Geometry Info SSBO (closest hit)
            bindings[2].binding         = 2;
            bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[2].descriptorCount = 1;
            bindings[2].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            // binding 3 - material info SSBO (closest hit)
            bindings[3].binding         = 3;
            bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[3].descriptorCount = 1;
            bindings[3].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            // binding 4 - texture array (closest hit) - partially bound
            bindings[4].binding         = 4;
            bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[4].descriptorCount = PathTracerResources::k_max_rt_textures;
            bindings[4].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            bindings[4].pImmutableSamplers = nullptr;
            // binding 5 - lights UBO
            bindings[5].binding         = 5;
            bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            bindings[5].descriptorCount = 1;
            bindings[5].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
            // binding 6 - G-buffer image (normal.xyz + depth.w), written by raygen for SVGF
            bindings[6].binding         = 6;
            bindings[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bindings[6].descriptorCount = 1;
            bindings[6].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

            VkDescriptorBindingFlags binding_flags[7] = {
                0, // 0: TLAS
                0, // 1: accum image
                0, // 2: geometry SSBO
                0, // 3: material SSBO
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, // 4: texture array
                0, // 5: lights UBO
                0, // 6: gbuffer image
            };
            VkDescriptorSetLayoutBindingFlagsCreateInfo flags_ci{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
            flags_ci.bindingCount  = 7;
            flags_ci.pBindingFlags = binding_flags;

            VkDescriptorSetLayoutCreateInfo layout_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            layout_ci.pNext        = &flags_ci;
            layout_ci.bindingCount = std::size(bindings);
            layout_ci.pBindings    = bindings;
            vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &s_res->rt_desc_layout);


            // Create descriptor pool and allocate the set
            VkDescriptorPoolSize pool_sizes[5] = {
                { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
                { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 }, // accum + gbuffer
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 }, // geometry + material
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, PathTracerResources::k_max_rt_textures },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
            };
            VkDescriptorPoolCreateInfo pool_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            pool_ci.maxSets       = 1;
            pool_ci.poolSizeCount = std::size(pool_sizes);
            pool_ci.pPoolSizes    = pool_sizes;
            vkCreateDescriptorPool(device, &pool_ci, nullptr, &s_res->rt_desc_pool);

            VkDescriptorSetAllocateInfo alloc_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            alloc_info.descriptorPool     = s_res->rt_desc_pool;
            alloc_info.descriptorSetCount = 1;
            alloc_info.pSetLayouts        = &s_res->rt_desc_layout;
            vkAllocateDescriptorSets(device, &alloc_info, &s_res->rt_desc_set);


            // Create pipeline layout
            VkPushConstantRange pc_range{};
            pc_range.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            pc_range.offset     = 0;
            pc_range.size       = sizeof(glm::mat4) * 2 + sizeof(uint32_t);

            VkPipelineLayoutCreateInfo pl_ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            pl_ci.setLayoutCount         = 1;
            pl_ci.pSetLayouts            = &s_res->rt_desc_layout;
            pl_ci.pushConstantRangeCount = 1;
            pl_ci.pPushConstantRanges    = &pc_range;
            vkCreatePipelineLayout(device, &pl_ci, nullptr, &s_res->rt_pipeline_layout);


            // Compile shaders and create shader modules TODO: use proper shader compilation flow here rather than handling it here
            bool compile_ok = true;
            auto make_module = [&](const std::string& src, ShaderCompiler::ShaderStage stage, const char* name) -> VkShaderModule {
                if (src.empty()) {
                    HN_CORE_ERROR("[PathTracer] Failed to read shader source for '{}'", name);
                    compile_ok = false;
                    return VK_NULL_HANDLE;
                }
                auto spirv = ShaderCompiler::compile_single_stage(src, stage);
                if (spirv.empty()) {
                    HN_CORE_ERROR("[PathTracer] SPIRV compilation failed for '{}'", name);
                    compile_ok = false;
                    return VK_NULL_HANDLE;
                }
                VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                ci.codeSize = spirv.size() * 4;
                ci.pCode    = spirv.data();
                VkShaderModule mod = VK_NULL_HANDLE;
                VkResult r = vkCreateShaderModule(device, &ci, nullptr, &mod);
                if (r != VK_SUCCESS) {
                    HN_CORE_ERROR("[PathTracer] vkCreateShaderModule failed for '{}' ({})", name, (int)r);
                    compile_ok = false;
                    return VK_NULL_HANDLE;
                }
                return mod;
            };

            std::filesystem::path assets_dir(ASSET_ROOT);
            VkShaderModule raygen_module      = make_module(ShaderCompiler::read_file(
                (assets_dir / "shaders" / "PathTrace_RayGen.rgen")),       ShaderCompiler::ShaderStage::RayGen,    "PathTrace_RayGen.rgen");
            VkShaderModule miss_module        = make_module(ShaderCompiler::read_file(
                (assets_dir / "shaders" / "PathTrace_Miss.rmiss")),        ShaderCompiler::ShaderStage::Miss,      "PathTrace_Miss.rmiss");
            VkShaderModule shadow_miss_module = make_module(ShaderCompiler::read_file(
                (assets_dir / "shaders" / "PathTrace_ShadowMiss.rmiss")),  ShaderCompiler::ShaderStage::Miss,      "PathTrace_ShadowMiss.rmiss");
            VkShaderModule hit_module         = make_module(ShaderCompiler::read_file(
                (assets_dir / "shaders" / "PathTrace_ClosestHit.rchit")),  ShaderCompiler::ShaderStage::ClosestHit,"PathTrace_ClosestHit.rchit");

            if (!compile_ok) {
                HN_CORE_ERROR("[PathTracer] Shader compilation failed — RT pipeline will not be built");
                if (raygen_module)      vkDestroyShaderModule(device, raygen_module,      nullptr);
                if (miss_module)        vkDestroyShaderModule(device, miss_module,        nullptr);
                if (shadow_miss_module) vkDestroyShaderModule(device, shadow_miss_module, nullptr);
                if (hit_module)         vkDestroyShaderModule(device, hit_module,         nullptr);
                return;
            }


            // Stages: 0=raygen, 1=primary_miss, 2=shadow_miss, 3=closest_hit
            VkPipelineShaderStageCreateInfo stages[4] = {};
            stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            stages[0].module = raygen_module;
            stages[0].pName  = "main";
            stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
            stages[1].module = miss_module;
            stages[1].pName  = "main";
            stages[2].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[2].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
            stages[2].module = shadow_miss_module;
            stages[2].pName  = "main";
            stages[3].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[3].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            stages[3].module = hit_module;
            stages[3].pName  = "main";

            // Groups: 0=raygen, 1=primary_miss, 2=shadow_miss, 3=closest_hit
            VkRayTracingShaderGroupCreateInfoKHR groups[4] = {};
            groups[0] = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
            groups[0].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            groups[0].generalShader    = 0;
            groups[0].closestHitShader = groups[0].anyHitShader = groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

            groups[1] = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
            groups[1].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            groups[1].generalShader    = 1;
            groups[1].closestHitShader = groups[1].anyHitShader = groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

            groups[2] = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
            groups[2].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            groups[2].generalShader    = 2;
            groups[2].closestHitShader = groups[2].anyHitShader = groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

            groups[3] = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
            groups[3].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            groups[3].generalShader    = VK_SHADER_UNUSED_KHR;
            groups[3].closestHitShader = 3;
            groups[3].anyHitShader     = groups[3].intersectionShader = VK_SHADER_UNUSED_KHR;

            VkRayTracingPipelineCreateInfoKHR rt_ci{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
            rt_ci.stageCount                   = 4;
            rt_ci.pStages                      = stages;
            rt_ci.groupCount                   = 4;
            rt_ci.pGroups                      = groups;
            rt_ci.maxPipelineRayRecursionDepth = 2; // allows shadow ray from within closest hit
            rt_ci.layout                       = s_res->rt_pipeline_layout;

            VkResult r = s_res->fn_create_rt_pipeline(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rt_ci, nullptr, &s_res->rt_pipeline);

            vkDestroyShaderModule(device, raygen_module,      nullptr);
            vkDestroyShaderModule(device, miss_module,        nullptr);
            vkDestroyShaderModule(device, shadow_miss_module, nullptr);
            vkDestroyShaderModule(device, hit_module,         nullptr);

            if (r != VK_SUCCESS) {
                HN_CORE_ERROR("[PathTracer] vkCreateRayTracingPipelinesKHR failed ({})", (int)r);
                return;
            }

            // Retrieve shader handles from pipeline (4 groups: raygen, miss, shadow_miss, hit)
            std::vector<uint8_t> handles(handle_size * 4);
            r = s_res->fn_get_sbt_handles(device, s_res->rt_pipeline, 0, 4, handle_size * 4, handles.data());
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkGetRayTracingShaderGroupHandlesKHR failed");


            // Allocate host-visible sbt buffer
            {
                VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                bi.size        = sbt_total;
                bi.usage       = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR
                               | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                vkCreateBuffer(device, &bi, nullptr, &s_res->sbt_buffer);

                VkMemoryRequirements req{};
                vkGetBufferMemoryRequirements(device, s_res->sbt_buffer, &req);

                VkMemoryAllocateFlagsInfo flags_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
                flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

                VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                ai.pNext           = &flags_info;
                ai.allocationSize  = req.size;
                ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                vkAllocateMemory(device, &ai, nullptr, &s_res->sbt_memory);
                vkBindBufferMemory(device, s_res->sbt_buffer, s_res->sbt_memory, 0);
            }


            // Copy handles: [raygen | miss_primary | miss_shadow | hit]
            void* mapped = nullptr;
            vkMapMemory(device, s_res->sbt_memory, 0, sbt_total, 0, &mapped);
            uint8_t* dst = static_cast<uint8_t*>(mapped);
            memcpy(dst + 0 * region_size, handles.data() + 0 * handle_size, handle_size);
            memcpy(dst + 1 * region_size, handles.data() + 1 * handle_size, handle_size);
            memcpy(dst + 2 * region_size, handles.data() + 2 * handle_size, handle_size);
            memcpy(dst + 3 * region_size, handles.data() + 3 * handle_size, handle_size);
            vkUnmapMemory(device, s_res->sbt_memory);

            VkBufferDeviceAddressInfo sbt_addr_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            sbt_addr_info.buffer = s_res->sbt_buffer;
            VkDeviceAddress sbt_base = s_res->fn_get_buf_addr(device, &sbt_addr_info);

            // miss region covers 2 entries (primary + shadow) with stride=region_size
            // so traceRayEXT(..., missIndex=1) selects the shadow miss shader
            s_res->sbt_raygen = { sbt_base + 0 * region_size, region_size, region_size };
            s_res->sbt_miss   = { sbt_base + 1 * region_size, region_size, 2 * region_size };
            s_res->sbt_hit    = { sbt_base + 3 * region_size, region_size, region_size };

            s_res->pipeline_built = true;
            HN_CORE_INFO("[PathTracer] RT pipeline built successfully");
        }

        static void build_svgf_pipeline() {
            VkDevice device = s_res->vk_ctx->get_device();

            // Descriptor layout: 4 storage images (accum, ping, filtered, gbuffer)
            VkDescriptorSetLayoutBinding svgf_bindings[4] = {};
            for (uint32_t i = 0; i < 4; i++) {
                svgf_bindings[i].binding        = i;
                svgf_bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                svgf_bindings[i].descriptorCount = 1;
                svgf_bindings[i].stageFlags     = VK_SHADER_STAGE_COMPUTE_BIT;
            }
            VkDescriptorSetLayoutCreateInfo slci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            slci.bindingCount = 4;
            slci.pBindings    = svgf_bindings;
            vkCreateDescriptorSetLayout(device, &slci, nullptr, &s_res->svgf_desc_layout);

            VkDescriptorPoolSize pool_size{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 };
            VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            dpci.maxSets       = 1;
            dpci.poolSizeCount = 1;
            dpci.pPoolSizes    = &pool_size;
            vkCreateDescriptorPool(device, &dpci, nullptr, &s_res->svgf_desc_pool);

            VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            dsai.descriptorPool     = s_res->svgf_desc_pool;
            dsai.descriptorSetCount = 1;
            dsai.pSetLayouts        = &s_res->svgf_desc_layout;
            vkAllocateDescriptorSets(device, &dsai, &s_res->svgf_desc_set);

            // Push constants: step_size, pass_idx, width, height (ints) + blend_factor (float) = 20 bytes
            VkPushConstantRange pc_range{};
            pc_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pc_range.offset     = 0;
            pc_range.size       = sizeof(int32_t) * 4 + sizeof(float);

            VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            plci.setLayoutCount         = 1;
            plci.pSetLayouts            = &s_res->svgf_desc_layout;
            plci.pushConstantRangeCount = 1;
            plci.pPushConstantRanges    = &pc_range;
            vkCreatePipelineLayout(device, &plci, nullptr, &s_res->svgf_pipeline_layout);

            std::filesystem::path assets_dir(ASSET_ROOT);
            std::string src = ShaderCompiler::read_file(assets_dir / "shaders" / "PathTrace_SVGF.comp");
            if (src.empty()) {
                HN_CORE_ERROR("[PathTracer] Failed to read PathTrace_SVGF.comp");
                return;
            }
            auto spirv = ShaderCompiler::compile_single_stage(src, ShaderCompiler::ShaderStage::Compute);
            if (spirv.empty()) {
                HN_CORE_ERROR("[PathTracer] SPIRV compilation failed for PathTrace_SVGF.comp");
                return;
            }
            VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            smci.codeSize = spirv.size() * 4;
            smci.pCode    = spirv.data();
            VkShaderModule comp_module = VK_NULL_HANDLE;
            vkCreateShaderModule(device, &smci, nullptr, &comp_module);

            VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            cpci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
            cpci.stage.module = comp_module;
            cpci.stage.pName  = "main";
            cpci.layout       = s_res->svgf_pipeline_layout;
            VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &s_res->svgf_pipeline);
            vkDestroyShaderModule(device, comp_module, nullptr);

            if (r != VK_SUCCESS) {
                HN_CORE_ERROR("[PathTracer] vkCreateComputePipelines failed for SVGF ({})", (int)r);
                return;
            }

            s_res->svgf_pipeline_built = true;
            HN_CORE_INFO("[PathTracer] SVGF compute pipeline built successfully");
        }

        static void update_svgf_desc_set() {
            VkDevice device = s_res->vk_ctx->get_device();

            VkDescriptorImageInfo img_infos[4] = {};
            img_infos[0] = { VK_NULL_HANDLE, s_res->accum_view,    VK_IMAGE_LAYOUT_GENERAL };
            img_infos[1] = { VK_NULL_HANDLE, s_res->ping_view,     VK_IMAGE_LAYOUT_GENERAL };
            img_infos[2] = { VK_NULL_HANDLE, s_res->filtered_view, VK_IMAGE_LAYOUT_GENERAL };
            img_infos[3] = { VK_NULL_HANDLE, s_res->gbuffer_view,  VK_IMAGE_LAYOUT_GENERAL };

            VkWriteDescriptorSet writes[4] = {};
            for (uint32_t i = 0; i < 4; i++) {
                writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet          = s_res->svgf_desc_set;
                writes[i].dstBinding      = i;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                writes[i].pImageInfo      = &img_infos[i];
            }
            vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
        }

    } // anonymous namespace

    void Renderer3DPathTracer::init(VulkanContext* ctx) {
        if (!s_res)
            s_res = new PathTracerResources{};

        s_res->vk_ctx = ctx;

        // Cache memory properties once; queried in find_memory_type throughout.
        vkGetPhysicalDeviceMemoryProperties(ctx->get_physical_device(), &s_res->cached_mem_props);

        VkDevice device = ctx->get_device();
#define LOAD(field, fn_name) s_res->field = reinterpret_cast<PFN_##fn_name>(vkGetDeviceProcAddr(device, #fn_name))
        LOAD(fn_create_as,          vkCreateAccelerationStructureKHR);
        LOAD(fn_destroy_as,         vkDestroyAccelerationStructureKHR);
        LOAD(fn_cmd_build_as,       vkCmdBuildAccelerationStructuresKHR);
        LOAD(fn_get_as_sizes,       vkGetAccelerationStructureBuildSizesKHR);
        LOAD(fn_get_as_addr,        vkGetAccelerationStructureDeviceAddressKHR);
        LOAD(fn_get_buf_addr,       vkGetBufferDeviceAddressKHR);
        LOAD(fn_create_rt_pipeline, vkCreateRayTracingPipelinesKHR);
        LOAD(fn_cmd_trace_rays,     vkCmdTraceRaysKHR);
        LOAD(fn_get_sbt_handles,    vkGetRayTracingShaderGroupHandlesKHR);
#undef LOAD

        // Allocate host-visible lights UBO (fixed size, mapped for lifetime of s_res)
        {
            VkDeviceSize lights_size = sizeof(LightsUBO);
            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.size        = lights_size;
            bi.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateBuffer(device, &bi, nullptr, &s_res->lights_buffer);

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, s_res->lights_buffer, &req);

            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            vkAllocateMemory(device, &ai, nullptr, &s_res->lights_memory);
            vkBindBufferMemory(device, s_res->lights_buffer, s_res->lights_memory, 0);
            vkMapMemory(device, s_res->lights_memory, 0, lights_size, 0, &s_res->lights_mapped);
        }

        build_rt_pipeline();
        build_svgf_pipeline();
    }

    void Renderer3DPathTracer::shutdown() {
        if (!s_res) return;

        VkDevice device = s_res->vk_ctx->get_device();
        vkDeviceWaitIdle(device);

        for (auto& [mesh, blas] : s_res->blas_cache) {
            s_res->fn_destroy_as(device, blas.handle, nullptr);
            vkDestroyBuffer(device, blas.buffer, nullptr);
            vkFreeMemory(device, blas.memory, nullptr);
        }
        s_res->blas_cache.clear();

        if (s_res->blas_scratch_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, s_res->blas_scratch_buffer, nullptr);
            vkFreeMemory(device, s_res->blas_scratch_memory, nullptr);
        }

        if (s_res->tlas != VK_NULL_HANDLE) {
            s_res->fn_destroy_as(device, s_res->tlas, nullptr);
            vkDestroyBuffer(device, s_res->tlas_buffer, nullptr);
            vkFreeMemory(device, s_res->tlas_memory, nullptr);
        }

        if (s_res->tlas_scratch_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, s_res->tlas_scratch_buffer, nullptr);
            vkFreeMemory(device, s_res->tlas_scratch_memory, nullptr);
        }

        if (s_res->instance_buffer != VK_NULL_HANDLE) {
            vkUnmapMemory(device, s_res->instance_memory);
            vkDestroyBuffer(device, s_res->instance_buffer, nullptr);
            vkFreeMemory(device, s_res->instance_memory, nullptr);
        }

        if (s_res->geometry_lookup_buffer != VK_NULL_HANDLE) {
            vkUnmapMemory(device, s_res->geometry_lookup_memory);
            vkDestroyBuffer(device, s_res->geometry_lookup_buffer, nullptr);
            vkFreeMemory(device, s_res->geometry_lookup_memory, nullptr);
        }

        if (s_res->material_buffer != VK_NULL_HANDLE) {
            vkUnmapMemory(device, s_res->material_memory);
            vkDestroyBuffer(device, s_res->material_buffer, nullptr);
            vkFreeMemory(device, s_res->material_memory, nullptr);
        }

        if (s_res->lights_buffer != VK_NULL_HANDLE) {
            vkUnmapMemory(device, s_res->lights_memory);
            vkDestroyBuffer(device, s_res->lights_buffer, nullptr);
            vkFreeMemory(device, s_res->lights_memory, nullptr);
        }

        destroy_accum_image();

        if (s_res->sbt_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, s_res->sbt_buffer, nullptr);
            vkFreeMemory(device, s_res->sbt_memory, nullptr);
        }
        if (s_res->rt_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, s_res->rt_pipeline, nullptr);
        if (s_res->rt_pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, s_res->rt_pipeline_layout, nullptr);
        if (s_res->rt_desc_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, s_res->rt_desc_pool, nullptr);
        if (s_res->rt_desc_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, s_res->rt_desc_layout, nullptr);

        if (s_res->svgf_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, s_res->svgf_pipeline, nullptr);
        if (s_res->svgf_pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, s_res->svgf_pipeline_layout, nullptr);
        if (s_res->svgf_desc_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, s_res->svgf_desc_pool, nullptr);
        if (s_res->svgf_desc_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, s_res->svgf_desc_layout, nullptr);

        delete s_res;
        s_res = nullptr;
    }

    bool Renderer3DPathTracer::is_initialized() {
        return s_res != nullptr;
    }

    void Renderer3DPathTracer::set_camera(const glm::mat4& inv_view, const glm::mat4& inv_proj) {
        if (s_res) {
            s_res->inv_view = inv_view;
            s_res->inv_proj = inv_proj;
        }
    }

    void Renderer3DPathTracer::invalidate_accumulation() {
        if (s_res)
            s_res->accum_frame_count = 0;
    }

    void Renderer3DPathTracer::set_lights(const LightsUBO& lights) {
        if (s_res && s_res->lights_mapped)
            memcpy(s_res->lights_mapped, &lights, sizeof(LightsUBO));
    }

    void Renderer3DPathTracer::invalidate_resources() {
        if (!s_res) return;

        VkDevice device = s_res->vk_ctx->get_device();
        vkDeviceWaitIdle(device);

        for (auto& [mesh, blas] : s_res->blas_cache) {
            s_res->fn_destroy_as(device, blas.handle, nullptr);
            vkDestroyBuffer(device, blas.buffer, nullptr);
            vkFreeMemory(device, blas.memory, nullptr);
        }
        s_res->blas_cache.clear();
        s_res->pending_blas.clear();

        if (s_res->blas_scratch_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, s_res->blas_scratch_buffer, nullptr);
            vkFreeMemory(device, s_res->blas_scratch_memory, nullptr);
            s_res->blas_scratch_buffer = VK_NULL_HANDLE;
            s_res->blas_scratch_memory = VK_NULL_HANDLE;
            s_res->blas_scratch_addr   = 0;
            s_res->blas_scratch_size   = 0;
        }

        if (s_res->tlas != VK_NULL_HANDLE) {
            s_res->fn_destroy_as(device, s_res->tlas, nullptr);
            vkDestroyBuffer(device, s_res->tlas_buffer, nullptr);
            vkFreeMemory(device, s_res->tlas_memory, nullptr);
            s_res->tlas              = VK_NULL_HANDLE;
            s_res->tlas_buffer       = VK_NULL_HANDLE;
            s_res->tlas_memory       = VK_NULL_HANDLE;
            s_res->tlas_backing_size = 0;
            s_res->tlas_last_instance_count = UINT32_MAX;
        }

        if (s_res->tlas_scratch_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, s_res->tlas_scratch_buffer, nullptr);
            vkFreeMemory(device, s_res->tlas_scratch_memory, nullptr);
            s_res->tlas_scratch_buffer = VK_NULL_HANDLE;
            s_res->tlas_scratch_memory = VK_NULL_HANDLE;
            s_res->tlas_scratch_addr   = 0;
            s_res->tlas_scratch_size   = 0;
        }

        destroy_accum_image();
        s_res->accum_width  = 0;
        s_res->accum_height = 0;
    }

    void Renderer3DPathTracer::register_frame_graph_executors() {
        auto& registry = FrameGraphRegistry::get();

        // Single pass: BLAS builds (new meshes only) → TLAS build/refit → trace rays →
        // SVGF denoise → blit to editorViewport.  All work goes into the live graphics
        // command buffer via submit_vulkan_graphics_raw, eliminating the per-submit
        // vkWaitForFences stalls that the old submit_vulkan_compute path imposed.
        registry.register_executor("pathtracing.trace", [](FrameGraphPassContext& ctx) {
            if (!s_res || !s_res->pipeline_built)
                return;

            Scene* scene = Scene::get_active_scene();
            if (!scene)
                return;

            Ref<Framebuffer> fb = ctx.get_output_framebuffer("editorViewport");
            if (!fb) return;
            const auto& spec = fb->get_specification();
            uint32_t w = spec.width, h = spec.height;
            if (w == 0 || h == 0) return;

            auto* vk_fb = dynamic_cast<VulkanFramebuffer*>(fb.get());
            if (!vk_fb) return;
            VkImage dst_image = vk_fb->get_color_image(0);

            if (w != s_res->accum_width || h != s_res->accum_height || s_res->accum_image == VK_NULL_HANDLE)
                create_accum_image(w, h);

            // CPU work: gather scene data, prepare BLAS/TLAS state, upload to mapped buffers.
            if (!prepare_tlas_cpu(scene))
                return;

            // Update descriptor sets now that TLAS handle and bound_textures are finalised.
            // The descriptor write happens on the CPU; the GPU will see the result after the
            // command buffer containing the build + trace is submitted.
            update_desc_set();
            if (s_res->svgf_pipeline_built)
                update_svgf_desc_set();

            // Snapshot frame-local values for capture by the GPU recording lambdas.
            glm::mat4 inv_view      = s_res->inv_view;
            glm::mat4 inv_proj      = s_res->inv_proj;
            uint32_t  frame_count   = s_res->accum_frame_count;
            uint32_t  trace_w       = s_res->accum_width;
            uint32_t  trace_h       = s_res->accum_height;
            bool      need_init     = s_res->accum_needs_layout_init;
            bool      gbuf_init     = s_res->gbuffer_needs_layout_init;
            bool      ping_init     = s_res->ping_needs_layout_init;
            bool      filtered_init = s_res->filtered_needs_layout_init;
            bool      svgf_ready    = s_res->svgf_pipeline_built;
            // blend_factor decays as temporal accumulation converges: full filter at frame 0,
            // half at frame 8, near-zero at frame 64 — avoids blurring a clean image.
            float     blend_factor  = std::min(1.0f, 8.0f / (float)(frame_count + 1));

            // --- BLAS builds (new meshes) + TLAS build/refit + ray trace ---
            // Everything goes into the live graphics command buffer; no fence stalls.
            ctx.submit_vulkan_graphics_raw([inv_view, inv_proj, frame_count, trace_w, trace_h,
                                            need_init, gbuf_init, ping_init, filtered_init](VkCommandBuffer cmd) {
                // One-time layout transitions for images still in UNDEFINED.
                auto transition_to_general = [&](VkImage img) {
                    VkImageMemoryBarrier imb{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                    imb.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
                    imb.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
                    imb.image            = img;
                    imb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    imb.srcAccessMask    = 0;
                    imb.dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
                    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &imb);
                };
                if (need_init)     transition_to_general(s_res->accum_image);
                if (gbuf_init)     transition_to_general(s_res->gbuffer_image);
                if (ping_init)     transition_to_general(s_res->ping_image);
                if (filtered_init) transition_to_general(s_res->filtered_image);

                // BLAS/TLAS build stage.
                {
                    HN_GPU_SCOPE(cmd, "BLAS/TLAS build");
                    record_pending_blas_builds(cmd);
                    record_tlas_build(cmd);
                }

                struct PC { glm::mat4 inv_view; glm::mat4 inv_proj; uint32_t frame_count; };
                PC pc{ inv_view, inv_proj, frame_count };

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, s_res->rt_pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                    s_res->rt_pipeline_layout, 0, 1, &s_res->rt_desc_set, 0, nullptr);
                vkCmdPushConstants(cmd, s_res->rt_pipeline_layout,
                    VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(PC), &pc);

                // Ray tracing stage.
                {
                    HN_GPU_SCOPE(cmd, "TraceRays");
                   s_res->fn_cmd_trace_rays(cmd,
                       &s_res->sbt_raygen, &s_res->sbt_miss, &s_res->sbt_hit, &s_res->sbt_callable,
                       trace_w, trace_h, 1);
                }
            });

            s_res->accum_frame_count++;
            s_res->accum_needs_layout_init    = false;
            s_res->gbuffer_needs_layout_init  = false;
            s_res->ping_needs_layout_init     = false;
            s_res->filtered_needs_layout_init = false;

            // --- SVGF À-Trous denoise (2 passes, final result in filtered_image) ---
            if (svgf_ready) {
                ctx.submit_vulkan_graphics_raw([trace_w, trace_h, blend_factor](VkCommandBuffer cmd) {
                    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    // Wait for raygen to finish writing accum_image and gbuffer_image.
                    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, &mb, 0, nullptr, 0, nullptr);

                    {
                        HN_GPU_SCOPE(cmd, "SVGF denoise");
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_res->svgf_pipeline);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                           s_res->svgf_pipeline_layout, 0, 1, &s_res->svgf_desc_set, 0, nullptr);

                        uint32_t gx = (trace_w + 7) / 8;
                        uint32_t gy = (trace_h + 7) / 8;

                        // 2 passes: step 1 then 2. Result lands in filtered_image (pass_idx 0→ping, 1→filtered).
                        // blend_factor on the last pass blends filtered↔original to avoid over-blurring
                        // converged (many-frame) images.
                        struct SVGF_PC { int32_t step_size; int32_t pass_idx; int32_t width; int32_t height; float blend_factor; };
                        const int step_sizes[2] = { 1, 2 };

                        for (int pass = 0; pass < 2; pass++) {
                            SVGF_PC pc{ step_sizes[pass], pass, (int32_t)trace_w, (int32_t)trace_h, blend_factor };
                            vkCmdPushConstants(cmd, s_res->svgf_pipeline_layout,
                                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SVGF_PC), &pc);
                            vkCmdDispatch(cmd, gx, gy, 1);

                            if (pass < 1) {
                                vkCmdPipelineBarrier(cmd,
                                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                    0, 1, &mb, 0, nullptr, 0, nullptr);
                           }
                        }
                    }
                });
            }

            // --- Blit filtered_image (or accum_image if SVGF unavailable) → editorViewport ---
            ctx.submit_vulkan_graphics_raw([dst_image, trace_w, trace_h, svgf_ready](VkCommandBuffer cmd) {

                {
                    HN_GPU_SCOPE(cmd, "Blit");
                    VkImage src_image = svgf_ready ? s_res->filtered_image : s_res->accum_image;

                   VkImageMemoryBarrier src_bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                   src_bar.oldLayout        = VK_IMAGE_LAYOUT_GENERAL;
                   src_bar.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                   src_bar.image            = src_image;
                   src_bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                   src_bar.srcAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
                   src_bar.dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT;

                   VkImageMemoryBarrier dst_bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                   dst_bar.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
                   dst_bar.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                   dst_bar.image            = dst_image;
                   dst_bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                   dst_bar.srcAccessMask    = 0;
                   dst_bar.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;

                   VkImageMemoryBarrier pre[2] = { src_bar, dst_bar };
                   vkCmdPipelineBarrier(cmd,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR
                       | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0, 0, nullptr, 0, nullptr, 2, pre);

                   VkImageBlit blit{};
                   blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                   blit.srcOffsets[1]  = {(int32_t)trace_w, (int32_t)trace_h, 1};
                   blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                   blit.dstOffsets[1]  = {(int32_t)trace_w, (int32_t)trace_h, 1};
                   vkCmdBlitImage(cmd,
                       src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_NEAREST);

                   // Return src_image to GENERAL for next frame.
                   src_bar.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                   src_bar.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
                   src_bar.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                   src_bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

                   // editorViewport → SHADER_READ_ONLY_OPTIMAL for ImGui.
                   dst_bar.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                   dst_bar.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                   dst_bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                   dst_bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                   VkImageMemoryBarrier post[2] = { src_bar, dst_bar };
                   vkCmdPipelineBarrier(cmd,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                       | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       0, 0, nullptr, 0, nullptr, 2, post);

                }
            });
        });
    }

} // namespace Honey