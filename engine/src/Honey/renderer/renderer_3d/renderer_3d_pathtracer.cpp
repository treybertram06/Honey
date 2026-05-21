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
            PFN_vkCmdWriteAccelerationStructuresPropertiesKHR fn_cmd_write_as_props = nullptr;
            PFN_vkCmdCopyAccelerationStructureKHR             fn_cmd_copy_as        = nullptr;

            // Cached once at init — physical device memory properties never change.
            VkPhysicalDeviceMemoryProperties cached_mem_props{};

            // Per-instance geometry lookup (one entry per TLAS instance, rebuilt each frame)
            struct GeometryInfo {
                VkDeviceAddress vertex_buffer_addr;
                VkDeviceAddress index_buffer_addr;
            };

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

            // Pending BLAS builds accumulated by prepare_tlas_cpu(), consumed by build_and_compact_pending_blas().
            struct PendingBlas {
                const Submesh* submesh_key; // look up handle in blas_cache during recording
                VkDeviceAddress vbuf_addr;
                VkDeviceAddress ibuf_addr;
                uint32_t tri_count;
                uint32_t max_vertex;
            };
            std::vector<PendingBlas> pending_blas;

            VkQueryPool blas_compact_query_pool     = VK_NULL_HANDLE;
            uint32_t    blas_compact_query_capacity = 0;

            // Per-frame resources: double-buffered so frame N and frame N+1 can be in flight
            // simultaneously without WAW hazards on the TLAS buffer or host-visible SSBOs.
            // k_pt_frames must match VulkanContext::k_max_frames_in_flight.
            static constexpr uint32_t k_pt_frames = 2;
            static constexpr uint32_t k_max_shadow_per_path = 3;
            struct PerFrameSlot {
                // TLAS handle and backing buffer.
                VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
                VkBuffer       tlas_buffer       = VK_NULL_HANDLE;
                VkDeviceMemory tlas_memory       = VK_NULL_HANDLE;
                VkDeviceSize   tlas_backing_size = 0;
                uint32_t       tlas_last_instance_count = UINT32_MAX;
                // TLAS scratch buffer (buildScratchSize >= updateScratchSize, covers both modes).
                VkBuffer        tlas_scratch_buffer = VK_NULL_HANDLE;
                VkDeviceMemory  tlas_scratch_memory = VK_NULL_HANDLE;
                VkDeviceAddress tlas_scratch_addr   = 0;
                VkDeviceSize    tlas_scratch_size   = 0;
                // State set by prepare_tlas_cpu(), consumed by record_tlas_build().
                uint32_t tlas_prim_count  = 0;
                bool     tlas_mode_update = false;
                // Instance upload buffer (host visible, written CPU-side each frame).
                VkBuffer       instance_buffer          = VK_NULL_HANDLE;
                VkDeviceMemory instance_memory          = VK_NULL_HANDLE;
                void*          instance_mapped          = nullptr;
                uint32_t       instance_buffer_capacity = 0;
                // Geometry lookup SSBO (host visible, written CPU-side each frame).
                VkBuffer       geometry_lookup_buffer   = VK_NULL_HANDLE;
                VkDeviceMemory geometry_lookup_memory   = VK_NULL_HANDLE;
                void*          geometry_lookup_mapped   = nullptr;
                uint32_t       geometry_lookup_capacity = 0;
                // Material SSBO (host visible, written CPU-side each frame).
                VkBuffer       material_buffer   = VK_NULL_HANDLE;
                VkDeviceMemory material_memory   = VK_NULL_HANDLE;
                void*          material_mapped   = nullptr;
                uint32_t       material_capacity = 0;
                // Per-frame RT descriptor set (references the per-frame TLAS + SSBOs).
                VkDescriptorSet wf_set0;   // shared: TLAS + geometry + material + textures + lights
                VkDescriptorSet wf_set1;   // path state SOA
                VkDescriptorSet wf_set2_extend;  // queues for Extend.rgen + Material.comp
                VkDescriptorSet wf_set2_shadow;  // queues for Shadow.rgen
                VkDescriptorSet wf_set2_logic;   // queues for Logic.comp + NewPath.comp
                VkDescriptorSet wf_set3;   // accum image (Logic.comp only)

                // Path state buffers for SOA storage
                VkBuffer path_ray_origin_buf = VK_NULL_HANDLE; // vec4[n]
                VkDeviceMemory path_ray_origin_mem = VK_NULL_HANDLE;
                VkBuffer path_ray_dir_buf = VK_NULL_HANDLE; // vec4[n]
                VkDeviceMemory path_ray_dir_mem = VK_NULL_HANDLE;
                VkBuffer path_throughput_buf = VK_NULL_HANDLE; // vec4[n]
                VkDeviceMemory path_throughput_mem = VK_NULL_HANDLE;
                VkBuffer path_radiance_buf = VK_NULL_HANDLE; // vec4[n]
                VkDeviceMemory path_radiance_mem = VK_NULL_HANDLE;
                VkBuffer path_seed_buf = VK_NULL_HANDLE; // uint[n]
                VkDeviceMemory path_seed_mem = VK_NULL_HANDLE;
                VkBuffer path_bounce_buf = VK_NULL_HANDLE; // uint[n]
                VkDeviceMemory path_bounce_mem = VK_NULL_HANDLE;
                VkBuffer path_flags_buf = VK_NULL_HANDLE; // uint[n]
                VkDeviceMemory path_flags_mem = VK_NULL_HANDLE;
                VkBuffer path_shadow_start_buf = VK_NULL_HANDLE; // uint[n]
                VkDeviceMemory path_shadow_start_mem = VK_NULL_HANDLE;
                VkBuffer path_shadow_end_buf = VK_NULL_HANDLE; // uint[n]
                VkDeviceMemory path_shadow_end_mem = VK_NULL_HANDLE;
                uint32_t path_buf_capacity = 0;
                bool wf_desc_dirty = false;

                // Queue + hit-record buffers
                VkBuffer extend_queue_buf = VK_NULL_HANDLE; // uint[n]
                VkDeviceMemory extend_queue_mem = VK_NULL_HANDLE;
                VkBuffer hit_record_buf = VK_NULL_HANDLE; // HitRecord[n]
                VkDeviceMemory hit_record_mem = VK_NULL_HANDLE;
                VkBuffer queue_count_buf = VK_NULL_HANDLE; // extend_count at [0], shadow_count at [count_buf_shadow_offset]
                VkDeviceMemory queue_count_mem = VK_NULL_HANDLE;
                VkBuffer shadow_queue_buf = VK_NULL_HANDLE; // ShadowRayRequest[n * k_max_shadow_per_path]
                VkDeviceMemory shadow_queue_mem = VK_NULL_HANDLE;
                VkBuffer occlusion_buf = VK_NULL_HANDLE; // uint[n * k_max_shadow_per_path]
                VkDeviceMemory occlusion_mem = VK_NULL_HANDLE;
                VkBuffer pending_radiance_buf = VK_NULL_HANDLE; // vec4[n * k_max_shadow_per_path]
                VkDeviceMemory pending_radiance_mem = VK_NULL_HANDLE;
            };
            PerFrameSlot frame_slots[k_pt_frames]{};
            uint32_t current_slot = 0; // set at the top of each executor frame

            VkDescriptorSetLayout wf_set0_layout;
            VkDescriptorSetLayout wf_set1_layout;
            VkDescriptorSetLayout wf_set2_extend_layout;
            VkDescriptorSetLayout wf_set2_shadow_layout;
            VkDescriptorSetLayout wf_set2_logic_layout;
            VkDescriptorSetLayout wf_set3_layout;
            VkDescriptorPool      wf_desc_pool;

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
            // Counts down from k_pt_frames to 0; each frame the GPU path-state buffers are
            // re-initialized (flags → TERMINATED|NEEDS_REGEN, radiance/shadow → 0) so that
            // stale in-flight paths from before a camera/scene change don't pollute the new image.
            uint32_t path_state_reset_remaining = 0;
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
            bool svgf_desc_dirty = true; // set when accum images are (re)created

            // Camera matrices for ray generation push constants.
            glm::mat4 inv_view{1.0f};
            glm::mat4 inv_proj{1.0f};

            // RT pipelines
            VkPipeline logic_pipeline = VK_NULL_HANDLE;
            VkPipelineLayout logic_layout = VK_NULL_HANDLE;
            VkPipeline new_path_pipeline = VK_NULL_HANDLE;
            VkPipelineLayout new_path_layout = VK_NULL_HANDLE;
            VkPipeline material_pipeline = VK_NULL_HANDLE;
            VkPipelineLayout material_layout = VK_NULL_HANDLE;
            VkPipeline extend_rt_pipeline = VK_NULL_HANDLE;
            VkPipelineLayout extend_rt_layout = VK_NULL_HANDLE;
            VkPipeline shadow_rt_pipeline = VK_NULL_HANDLE;
            VkPipelineLayout shadow_rt_layout = VK_NULL_HANDLE;

            VkBuffer extend_sbt_buf = VK_NULL_HANDLE;
            VkDeviceMemory extend_sbt_mem = VK_NULL_HANDLE;
            VkStridedDeviceAddressRegionKHR extend_sbt_raygen{};
            VkStridedDeviceAddressRegionKHR extend_sbt_miss{};
            VkStridedDeviceAddressRegionKHR extend_sbt_hit{};
            VkStridedDeviceAddressRegionKHR extend_sbt_callable{};

            VkBuffer shadow_sbt_buf = VK_NULL_HANDLE;
            VkDeviceMemory shadow_sbt_mem = VK_NULL_HANDLE;
            VkStridedDeviceAddressRegionKHR shadow_sbt_raygen{};
            VkStridedDeviceAddressRegionKHR shadow_sbt_miss{};
            VkStridedDeviceAddressRegionKHR shadow_sbt_hit{};
            VkStridedDeviceAddressRegionKHR shadow_sbt_callable{};

            bool extend_built = false;
            bool shadow_built = false;

            VkDescriptorSetLayout empty_set_layout = VK_NULL_HANDLE;
            VkDescriptorSet       wf_dummy_set     = VK_NULL_HANDLE;

            // Byte offset of shadow_count inside queue_count_buf, aligned to
            // minStorageBufferOffsetAlignment. Queried once in init().
            VkDeviceSize count_buf_shadow_offset = 256;
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

        static std::pair<VkBuffer, VkDeviceMemory> alloc_device_local_buffer(VkDeviceSize size, VkBufferUsageFlags usage) {
            HN_PROFILE_FUNCTION();

            VkBuffer buf = VK_NULL_HANDLE;
            VkDeviceMemory mem = VK_NULL_HANDLE;
            VkDevice device = s_res->vk_ctx->get_device();

            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.size      = size;
            bi.usage     = usage;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateBuffer(device, &bi, nullptr, &buf);

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, buf, &req);

            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(device, &ai, nullptr, &mem);
            vkBindBufferMemory(device, buf, mem, 0);

            return {buf, mem};
        }

        static void free_device_local_buffer(VkBuffer& buf, VkDeviceMemory& mem) {
            HN_PROFILE_FUNCTION();

            if (buf != VK_NULL_HANDLE) {
                vkDestroyBuffer(s_res->vk_ctx->get_device(), buf, nullptr);
                buf = VK_NULL_HANDLE;
            }
            if (mem != VK_NULL_HANDLE) {
                vkFreeMemory(s_res->vk_ctx->get_device(), mem, nullptr);
                mem = VK_NULL_HANDLE;
            }
        }

        static void alloc_wavefront_buffers(uint32_t w, uint32_t h) {
            HN_PROFILE_FUNCTION();

            uint32_t N = w * h;
            VkDevice device = s_res->vk_ctx->get_device();
            constexpr uint32_t k_shadow = PathTracerResources::k_max_shadow_per_path;

            for (uint8_t i = 0; i < PathTracerResources::k_pt_frames; i++) {
                if (N == s_res->frame_slots[i].path_buf_capacity) continue;

                vkDeviceWaitIdle(device);

                auto& slot = s_res->frame_slots[i];

                // free all existing wavefront buffers
                free_device_local_buffer(slot.path_ray_origin_buf,    slot.path_ray_origin_mem);
                free_device_local_buffer(slot.path_ray_dir_buf,       slot.path_ray_dir_mem);
                free_device_local_buffer(slot.path_throughput_buf,    slot.path_throughput_mem);
                free_device_local_buffer(slot.path_radiance_buf,      slot.path_radiance_mem);
                free_device_local_buffer(slot.path_seed_buf,          slot.path_seed_mem);
                free_device_local_buffer(slot.path_bounce_buf,        slot.path_bounce_mem);
                free_device_local_buffer(slot.path_flags_buf,         slot.path_flags_mem);
                free_device_local_buffer(slot.path_shadow_start_buf,  slot.path_shadow_start_mem);
                free_device_local_buffer(slot.path_shadow_end_buf,    slot.path_shadow_end_mem);
                free_device_local_buffer(slot.extend_queue_buf,       slot.extend_queue_mem);
                free_device_local_buffer(slot.hit_record_buf,         slot.hit_record_mem);
                free_device_local_buffer(slot.queue_count_buf,        slot.queue_count_mem);
                free_device_local_buffer(slot.shadow_queue_buf,       slot.shadow_queue_mem);
                free_device_local_buffer(slot.occlusion_buf,          slot.occlusion_mem);
                free_device_local_buffer(slot.pending_radiance_buf,   slot.pending_radiance_mem);

                constexpr VkBufferUsageFlags kStorage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

                // path state SOA — vec4 = 16 bytes, uint = 4 bytes
                std::tie(slot.path_ray_origin_buf,   slot.path_ray_origin_mem)   = alloc_device_local_buffer(N * 16, kStorage);
                std::tie(slot.path_ray_dir_buf,       slot.path_ray_dir_mem)      = alloc_device_local_buffer(N * 16, kStorage);
                std::tie(slot.path_throughput_buf,    slot.path_throughput_mem)   = alloc_device_local_buffer(N * 16, kStorage);
                std::tie(slot.path_radiance_buf,      slot.path_radiance_mem)     = alloc_device_local_buffer(N * 16, kStorage);
                std::tie(slot.path_seed_buf,          slot.path_seed_mem)         = alloc_device_local_buffer(N * 4,  kStorage);
                std::tie(slot.path_bounce_buf,        slot.path_bounce_mem)       = alloc_device_local_buffer(N * 4,  kStorage);
                std::tie(slot.path_flags_buf,         slot.path_flags_mem)        = alloc_device_local_buffer(N * 4,  kStorage);
                std::tie(slot.path_shadow_start_buf,  slot.path_shadow_start_mem) = alloc_device_local_buffer(N * 4,  kStorage);
                std::tie(slot.path_shadow_end_buf,    slot.path_shadow_end_mem)   = alloc_device_local_buffer(N * 4,  kStorage);

                // queue + hit-record buffers
                std::tie(slot.extend_queue_buf,     slot.extend_queue_mem)     = alloc_device_local_buffer(N * 4,          kStorage);
                std::tie(slot.hit_record_buf,       slot.hit_record_mem)       = alloc_device_local_buffer(N * 64,         kStorage);
                std::tie(slot.queue_count_buf,      slot.queue_count_mem)      = alloc_device_local_buffer(
                    s_res->count_buf_shadow_offset + sizeof(uint32_t), kStorage);
                std::tie(slot.shadow_queue_buf,     slot.shadow_queue_mem)     = alloc_device_local_buffer(N * k_shadow * 48, kStorage);
                std::tie(slot.occlusion_buf,        slot.occlusion_mem)        = alloc_device_local_buffer(N * k_shadow * 4,  kStorage);
                std::tie(slot.pending_radiance_buf, slot.pending_radiance_mem) = alloc_device_local_buffer(N * k_shadow * 16, kStorage);

                // Initialize path_flags to FLAG_TERMINATED | FLAG_NEEDS_REGEN (0x3) so
                // Logic ignores shadow slots and NewPath regenerates all paths on frame 0.
                s_res->vk_ctx->submit_one_time_graphics([&slot, N](VkCommandBuffer cmd) {
                    vkCmdFillBuffer(cmd, slot.path_flags_buf, 0, N * 4, 0x03030303u);
                    vkCmdFillBuffer(cmd, slot.path_shadow_start_buf, 0, N * 4, 0u);
                    vkCmdFillBuffer(cmd, slot.path_shadow_end_buf,   0, N * 4, 0u);
                });

                slot.path_buf_capacity = N;
                slot.wf_desc_dirty = true;
            }
        }

        static void alloc_wavefront_desc_sets() {
            HN_PROFILE_FUNCTION();

            VkDevice device = s_res->vk_ctx->get_device();
            constexpr uint32_t F = PathTracerResources::k_pt_frames;

            // Count each descriptor type needed across all frame slots.
            // set0: 1 AS + 1 storage_image(gbuffer) + 2 SSBO + 512 CIS + 1 UBO  per slot
            // set1: 9 SSBO per slot
            // set2_extend: 6 SSBO per slot
            // set2_shadow: 3 SSBO per slot
            // set2_logic: 4 SSBO per slot
            // set3: 1 storage_image(accum) per slot
            // plus 1 set for wf_dummy_set (empty layout)

            VkDescriptorPoolSize pool_sizes[] = {
                { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, F * 1 },
                { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              F * 2 },          // gbuffer + accum
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             F * (9 + 6 + 3 + 4 + 2) + 4 }, // all SSBOs + headroom
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,     F * PathTracerResources::k_max_rt_textures },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,             F * 1 },
            };

            VkDescriptorPoolCreateInfo pool_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            pool_ci.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
            pool_ci.maxSets       = F * 6 + 1; // 6 sets per slot + 1 dummy
            pool_ci.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
            pool_ci.pPoolSizes    = pool_sizes;
            VkResult r = vkCreateDescriptorPool(device, &pool_ci, nullptr, &s_res->wf_desc_pool);
            HN_CORE_ASSERT(r == VK_SUCCESS, "alloc_wavefront_desc_sets: vkCreateDescriptorPool failed");

            // Allocate dummy set from empty_set_layout (used as placeholder when binding).
            {
                VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                ai.descriptorPool     = s_res->wf_desc_pool;
                ai.descriptorSetCount = 1;
                ai.pSetLayouts        = &s_res->empty_set_layout;
                r = vkAllocateDescriptorSets(device, &ai, &s_res->wf_dummy_set);
                HN_CORE_ASSERT(r == VK_SUCCESS, "alloc_wavefront_desc_sets: dummy set alloc failed");
            }

            // Allocate 6 descriptor sets per frame slot.
            for (uint32_t i = 0; i < F; i++) {
                auto& slot = s_res->frame_slots[i];

                VkDescriptorSetLayout layouts[6] = {
                    s_res->wf_set0_layout,
                    s_res->wf_set1_layout,
                    s_res->wf_set2_extend_layout,
                    s_res->wf_set2_shadow_layout,
                    s_res->wf_set2_logic_layout,
                    s_res->wf_set3_layout,
                };
                VkDescriptorSet sets[6] = {};

                VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                ai.descriptorPool     = s_res->wf_desc_pool;
                ai.descriptorSetCount = 6;
                ai.pSetLayouts        = layouts;
                r = vkAllocateDescriptorSets(device, &ai, sets);
                HN_CORE_ASSERT(r == VK_SUCCESS, "alloc_wavefront_desc_sets: slot alloc failed");

                slot.wf_set0         = sets[0];
                slot.wf_set1         = sets[1];
                slot.wf_set2_extend  = sets[2];
                slot.wf_set2_shadow  = sets[3];
                slot.wf_set2_logic   = sets[4];
                slot.wf_set3         = sets[5];
            }

            HN_CORE_INFO("[PathTracer] Wavefront descriptor sets allocated");
        }

        static void build_wavefront_layouts() {
            HN_PROFILE_FUNCTION();

            VkDevice device = s_res->vk_ctx->get_device();

            constexpr VkDescriptorBindingFlags k_uab  = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            constexpr VkDescriptorBindingFlags k_part = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | k_uab;
            constexpr VkDescriptorSetLayoutCreateFlags k_pool_bit =
                VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

            auto make_layout = [&](VkDescriptorSetLayoutBinding* b, VkDescriptorBindingFlags* f,
                                   uint32_t count, VkDescriptorSetLayout& out) {
                VkDescriptorSetLayoutBindingFlagsCreateInfo flags_ci{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
                flags_ci.bindingCount  = count;
                flags_ci.pBindingFlags = f;

                VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                ci.pNext        = &flags_ci;
                ci.flags        = k_pool_bit;
                ci.bindingCount = count;
                ci.pBindings    = b;
                vkCreateDescriptorSetLayout(device, &ci, nullptr, &out);
            };

            // empty layout — placeholder for unused set slots in pipeline layouts
            {
                VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                ci.flags        = k_pool_bit;
                ci.bindingCount = 0;
                vkCreateDescriptorSetLayout(device, &ci, nullptr, &s_res->empty_set_layout);
            }

            // wf_set0: TLAS, gbuffer image, geometry SSBO, material SSBO, texture array, lights UBO
            {
                constexpr VkShaderStageFlags kRT     = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
                constexpr VkShaderStageFlags kRTComp = kRT | VK_SHADER_STAGE_COMPUTE_BIT;

                VkDescriptorSetLayoutBinding b[6] = {};
                b[0] = { 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,  1, kRT,     nullptr };
                b[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,               1, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
                b[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,              1, kRTComp, nullptr };
                b[3] = { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,              1, kRTComp, nullptr };
                b[4] = { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,      PathTracerResources::k_max_rt_textures, kRTComp, nullptr };
                b[5] = { 5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,              1, kRTComp, nullptr };

                VkDescriptorBindingFlags f[6] = { k_uab, k_uab, k_uab, k_uab, k_part, k_uab };
                make_layout(b, f, 6, s_res->wf_set0_layout);
            }

            // wf_set1: path state SOA — nine SSBOs, bindings 0-8
            {
                constexpr VkShaderStageFlags kStages =
                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR;

                VkDescriptorSetLayoutBinding b[9] = {};
                VkDescriptorBindingFlags     f[9] = {};
                for (uint32_t i = 0; i < 9; i++) {
                    b[i] = { i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kStages, nullptr };
                    f[i] = k_uab;
                }
                make_layout(b, f, 9, s_res->wf_set1_layout);
            }

            // wf_set2_extend: queues for Extend.rgen + Material.comp, bindings 0-5
            {
                constexpr VkShaderStageFlags kStages =
                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR;

                VkDescriptorSetLayoutBinding b[6] = {};
                VkDescriptorBindingFlags     f[6] = {};
                for (uint32_t i = 0; i < 6; i++) {
                    b[i] = { i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kStages, nullptr };
                    f[i] = k_uab;
                }
                make_layout(b, f, 6, s_res->wf_set2_extend_layout);
            }

            // wf_set2_shadow: queues for Shadow.rgen, bindings 0-2
            {
                VkDescriptorSetLayoutBinding b[3] = {};
                VkDescriptorBindingFlags     f[3] = {};
                for (uint32_t i = 0; i < 3; i++) {
                    b[i] = { i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr };
                    f[i] = k_uab;
                }
                make_layout(b, f, 3, s_res->wf_set2_shadow_layout);
            }

            // wf_set2_logic: queues for Logic + NewPath — only bindings 2, 3, 4, 5 exist
            {
                const uint32_t indices[4] = { 2, 3, 4, 5 };
                VkDescriptorSetLayoutBinding b[4] = {};
                VkDescriptorBindingFlags     f[4] = {};
                for (uint32_t i = 0; i < 4; i++) {
                    b[i] = { indices[i], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
                    f[i] = k_uab;
                }
                make_layout(b, f, 4, s_res->wf_set2_logic_layout);
            }

            // wf_set3: accum storage image, binding 0, Logic.comp only
            {
                VkDescriptorSetLayoutBinding b = { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
                VkDescriptorBindingFlags     f = k_uab;
                make_layout(&b, &f, 1, s_res->wf_set3_layout);
            }

            HN_CORE_INFO("[PathTracer] Wavefront descriptor set layouts built");

            alloc_wavefront_desc_sets();
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
            // Wait for all in-flight frames to complete before destroying images that may
            // still be referenced by in-flight command buffers or descriptor sets.
            if (s_res->accum_image != VK_NULL_HANDLE)
                vkDeviceWaitIdle(s_res->vk_ctx->get_device());
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

            alloc_wavefront_buffers(w, h);

            s_res->accum_width    = w;
            s_res->accum_height   = h;
            s_res->accum_frame_count      = 0;
            s_res->accum_needs_layout_init    = true;
            s_res->gbuffer_needs_layout_init  = true;
            s_res->ping_needs_layout_init     = true;
            s_res->filtered_needs_layout_init = true;
            s_res->svgf_desc_dirty = true;
        }


        static void ensure_instance_buffer(uint32_t count) {
            HN_PROFILE_FUNCTION();

            auto& fs = s_res->frame_slots[s_res->current_slot];
            if (count <= fs.instance_buffer_capacity)
                return;

            VkDevice device = s_res->vk_ctx->get_device();

            if (fs.instance_buffer != VK_NULL_HANDLE) {
                vkUnmapMemory(device, fs.instance_memory);
                vkDestroyBuffer(device, fs.instance_buffer, nullptr);
                vkFreeMemory(device, fs.instance_memory, nullptr);
            }

            VkDeviceSize size = count * sizeof(VkAccelerationStructureInstanceKHR);

            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.size      = size;
            bi.usage     = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                         | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateBuffer(device, &bi, nullptr, &fs.instance_buffer);

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, fs.instance_buffer, &req);

            VkMemoryAllocateFlagsInfo flags_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
            flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.pNext           = &flags_info;
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            vkAllocateMemory(device, &ai, nullptr, &fs.instance_memory);
            vkBindBufferMemory(device, fs.instance_buffer, fs.instance_memory, 0);
            vkMapMemory(device, fs.instance_memory, 0, size, 0, &fs.instance_mapped);

            fs.instance_buffer_capacity = count;
        }

        static void ensure_geometry_lookup_buffer(uint32_t count) {
            auto& fs = s_res->frame_slots[s_res->current_slot];
            if (count <= fs.geometry_lookup_capacity)
                return;

            VkDevice device = s_res->vk_ctx->get_device();
            if (fs.geometry_lookup_buffer != VK_NULL_HANDLE) {
                vkUnmapMemory(device, fs.geometry_lookup_memory);
                vkDestroyBuffer(device, fs.geometry_lookup_buffer, nullptr);
                vkFreeMemory(device, fs.geometry_lookup_memory, nullptr);
            }

            VkDeviceSize size = count * sizeof(PathTracerResources::GeometryInfo);
            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.size      = size;
            bi.usage     = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateBuffer(device, &bi, nullptr, &fs.geometry_lookup_buffer);

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, fs.geometry_lookup_buffer, &req);

            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            vkAllocateMemory(device, &ai, nullptr, &fs.geometry_lookup_memory);
            vkBindBufferMemory(device, fs.geometry_lookup_buffer, fs.geometry_lookup_memory, 0);
            vkMapMemory(device, fs.geometry_lookup_memory, 0, size, 0, &fs.geometry_lookup_mapped);
            fs.geometry_lookup_capacity = count;
        }

        static void ensure_material_buffer(uint32_t count) {
            auto& fs = s_res->frame_slots[s_res->current_slot];
            if (count <= fs.material_capacity)
                return;

            VkDevice device = s_res->vk_ctx->get_device();
            if (fs.material_buffer != VK_NULL_HANDLE) {
                vkUnmapMemory(device, fs.material_memory);
                vkDestroyBuffer(device, fs.material_buffer, nullptr);
                vkFreeMemory(device, fs.material_memory, nullptr);
            }

            VkDeviceSize size = count * sizeof(PathTracerResources::MaterialInfo);
            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.size      = size;
            bi.usage     = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateBuffer(device, &bi, nullptr, &fs.material_buffer);

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, fs.material_buffer, &req);

            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            vkAllocateMemory(device, &ai, nullptr, &fs.material_memory);
            vkBindBufferMemory(device, fs.material_buffer, fs.material_memory, 0);
            vkMapMemory(device, fs.material_memory, 0, size, 0, &fs.material_mapped);
            fs.material_capacity = count;
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
        // Does NOT submit any GPU work — building is deferred to build_and_compact_pending_blas().
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
            triangles.vertexStride   = 24; // sizeof(VertexPBR) from gltf_loader.cpp
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
            build_info.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                                     | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
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

        // Builds all pending BLASes in a synchronous one-time submit, then compacts
        // them into smaller allocations before the TLAS build consumes their device addresses.
        // Stalls the CPU while GPU work completes — acceptable since this only runs when new
        // meshes are added (one-time per unique Submesh*).
        static void build_and_compact_pending_blas() {
            if (s_res->pending_blas.empty())
                return;

            VkDevice device = s_res->vk_ctx->get_device();

            // No vkDeviceWaitIdle here — these BLASes are brand new (never referenced by any
            // in-flight TLAS), so there is nothing to drain. Each submit_one_time_graphics call
            // waits on its own fence, which is sufficient. Calling vkDeviceWaitIdle from this
            // thread while the upload thread is concurrently calling vkQueueSubmit on the same
            // queue is a Vulkan threading violation (VkQueue is externally synchronized).

            const uint32_t count = (uint32_t)s_res->pending_blas.size();

            // Grow the compaction query pool if needed.
            if (count > s_res->blas_compact_query_capacity) {
                if (s_res->blas_compact_query_pool != VK_NULL_HANDLE)
                    vkDestroyQueryPool(device, s_res->blas_compact_query_pool, nullptr);
                VkQueryPoolCreateInfo qci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
                qci.queryType  = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
                qci.queryCount = count;
                VkResult r = vkCreateQueryPool(device, &qci, nullptr, &s_res->blas_compact_query_pool);
                HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateQueryPool failed (BLAS compaction)");
                s_res->blas_compact_query_capacity = count;
            }

            // --- Phase 1: build all BLASes and write compacted size queries ---
            bool ok = s_res->vk_ctx->submit_one_time_graphics([count](VkCommandBuffer cmd) {
                vkCmdResetQueryPool(cmd, s_res->blas_compact_query_pool, 0, count);

                std::vector<VkAccelerationStructureKHR> built_handles;
                built_handles.reserve(count);

                const auto& pending = s_res->pending_blas;
                for (size_t i = 0; i < pending.size(); ++i) {
                    const auto& pb = pending[i];
                    const PathTracerResources::BlasEntry& entry = s_res->blas_cache.at(pb.submesh_key);

                    VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
                    triangles.sType          = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                    triangles.vertexFormat   = VK_FORMAT_R32G32B32_SFLOAT;
                    triangles.vertexData.deviceAddress = pb.vbuf_addr;
                    triangles.vertexStride   = 24;
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
                                                         //| VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
                    build_info.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
                    build_info.dstAccelerationStructure  = entry.handle;
                    build_info.geometryCount             = 1;
                    build_info.pGeometries               = &geometry;
                    build_info.scratchData.deviceAddress = s_res->blas_scratch_addr;

                    VkAccelerationStructureBuildRangeInfoKHR range{};
                    range.primitiveCount = pb.tri_count;
                    const VkAccelerationStructureBuildRangeInfoKHR* range_ptr = &range;
                    s_res->fn_cmd_build_as(cmd, 1, &build_info, &range_ptr);

                    built_handles.push_back(entry.handle);

                    if (i + 1 < pending.size()) {
                        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                        mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                        mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                        mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
                                         | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                        vkCmdPipelineBarrier(cmd,
                            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                            0, 1, &mb, 0, nullptr, 0, nullptr);
                    }
                }

                VkMemoryBarrier final_mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                final_mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                final_mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                final_mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    0, 1, &final_mb, 0, nullptr, 0, nullptr);

                //s_res->fn_cmd_write_as_props(cmd, count, built_handles.data(),
                //    VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                //    s_res->blas_compact_query_pool, 0);
            });
            HN_CORE_ASSERT(ok, "BLAS build+query submit failed");

            /* TEMP: Disable BLAS compaction because it pmo rn
            // Read compacted sizes — GPU finished, fence already waited in submit_one_time_graphics.
            std::vector<VkDeviceSize> compact_sizes(count);
            VkResult r = vkGetQueryPoolResults(device,
                s_res->blas_compact_query_pool, 0, count,
                count * sizeof(VkDeviceSize), compact_sizes.data(), sizeof(VkDeviceSize),
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkGetQueryPoolResults failed (BLAS compaction)");
            */

            /*
            // --- Phase 2: allocate compact AS handles and copy ---
            struct CompactOp {
                PathTracerResources::BlasEntry compact;
                const Submesh*             key;
                VkAccelerationStructureKHR old_handle;
                VkBuffer                   old_buffer;
                VkDeviceMemory             old_memory;
                VkDeviceAddress            old_device_address; // pre-compaction address written to instance buffer
            };
            std::vector<CompactOp> compact_ops;
            compact_ops.reserve(count);

            for (uint32_t i = 0; i < count; ++i) {
                const auto& pb = s_res->pending_blas[i];
                const PathTracerResources::BlasEntry& old_entry = s_res->blas_cache.at(pb.submesh_key);

                PathTracerResources::BlasEntry compact{};
                {
                    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                    bi.size        = compact_sizes[i];
                    bi.usage       = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                                   | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                    VkResult br = vkCreateBuffer(device, &bi, nullptr, &compact.buffer);
                    HN_CORE_ASSERT(br == VK_SUCCESS, "vkCreateBuffer failed (BLAS compact)");

                    VkMemoryRequirements req{};
                    vkGetBufferMemoryRequirements(device, compact.buffer, &req);

                    VkMemoryAllocateFlagsInfo flags_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
                    flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

                    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                    ai.pNext           = &flags_info;
                    ai.allocationSize  = req.size;
                    ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                    br = vkAllocateMemory(device, &ai, nullptr, &compact.memory);
                    HN_CORE_ASSERT(br == VK_SUCCESS, "vkAllocateMemory failed (BLAS compact)");
                    vkBindBufferMemory(device, compact.buffer, compact.memory, 0);
                }

                VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
                ci.buffer = compact.buffer;
                ci.size   = compact_sizes[i];
                ci.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                VkResult cr = s_res->fn_create_as(device, &ci, nullptr, &compact.handle);
                HN_CORE_ASSERT(cr == VK_SUCCESS, "vkCreateAccelerationStructureKHR failed (compact)");

                VkAccelerationStructureDeviceAddressInfoKHR as_addr{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
                as_addr.accelerationStructure = compact.handle;
                compact.device_address = s_res->fn_get_as_addr(device, &as_addr);

                compact_ops.push_back({ compact, pb.submesh_key,
                    old_entry.handle, old_entry.buffer, old_entry.memory,
                    old_entry.device_address });
            }

            ok = s_res->vk_ctx->submit_one_time_graphics([&compact_ops](VkCommandBuffer cmd) {
                for (const auto& op : compact_ops) {
                    VkCopyAccelerationStructureInfoKHR copy_info{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
                    copy_info.src  = op.old_handle;
                    copy_info.dst  = op.compact.handle;
                    copy_info.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
                    s_res->fn_cmd_copy_as(cmd, &copy_info);
                }
            });
            HN_CORE_ASSERT(ok, "BLAS compaction copy submit failed");

            // Swap handles in blas_cache and destroy the uncompacted buffers.
            for (const auto& op : compact_ops) {
                s_res->fn_destroy_as(device, op.old_handle, nullptr);
                vkDestroyBuffer(device, op.old_buffer, nullptr);
                vkFreeMemory(device, op.old_memory, nullptr);
                s_res->blas_cache[op.key] = op.compact;
            }

            // The instance buffer was written by prepare_tlas_cpu() with pre-compaction
            // addresses.  Patch it now so the TLAS build doesn't reference freed memory.
            bool any_patched = false;
            for (auto& inst : s_res->frame_instances) {
                for (const auto& op : compact_ops) {
                    if (inst.accelerationStructureReference == op.old_device_address) {
                        inst.accelerationStructureReference = op.compact.device_address;
                        any_patched = true;
                        break;
                    }
                }
            }
            if (any_patched) {
                auto& fs = s_res->frame_slots[s_res->current_slot];
                memcpy(fs.instance_mapped, s_res->frame_instances.data(),
                       s_res->frame_instances.size() * sizeof(VkAccelerationStructureInstanceKHR));
            }
            */

            s_res->pending_blas.clear();
        }

        // CPU-only: iterates the scene, uploads instance/material/geo data, prepares TLAS state.
        // New BLASes are allocated but not built — they are appended to pending_blas.
        // Returns false if the scene has no renderable instances.
        static bool prepare_tlas_cpu(Scene* scene) {
            VkDevice device = s_res->vk_ctx->get_device();
            auto& fs = s_res->frame_slots[s_res->current_slot];

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

                const uint32_t max_vertex = bufs.vertex_buffer->get_size() / 24u - 1u;

                const auto& submeshes = mrc.mesh->get_submeshes();
                for (size_t si = 0; si < submeshes.size(); ++si) {
                    const Submesh& sm = submeshes[si];
                    if (!sm.meshlets.has_value()) continue;
                    if (sm.meshlets->flat_index_tri_count == 0) continue;

                    const uint32_t tri_count = sm.meshlets->flat_index_tri_count;
                    const VkDeviceAddress ibuf_addr = ibuf_base_addr
                        + (VkDeviceAddress)sm.meshlets->flat_index_first * 3u * sizeof(uint32_t);

                    // Allocate BLAS if not cached — GPU build is deferred to build_and_compact_pending_blas().
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
                        const auto& pbr = mat->pbr();
                        mat_info.base_color_factor = mat->get_base_color_factor();
                        mat_info.emissive_factor   = glm::vec4(pbr.emissive_factor * pbr.extensions.emissive_strength.strength, 0.0f);
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
                memcpy(fs.geometry_lookup_mapped, s_res->frame_geo_infos.data(),
                       s_res->frame_geo_infos.size() * sizeof(PathTracerResources::GeometryInfo));

            ensure_material_buffer((uint32_t)s_res->frame_materials.size());
            if (!s_res->frame_materials.empty())
                memcpy(fs.material_mapped, s_res->frame_materials.data(),
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
            memcpy(fs.instance_mapped, s_res->frame_instances.data(),
                   s_res->frame_instances.size() * sizeof(VkAccelerationStructureInstanceKHR));

            // Determine if a full rebuild or an in-place refit is needed.
            const uint32_t prim_count   = (uint32_t)s_res->frame_instances.size();
            const bool count_changed    = (prim_count != fs.tlas_last_instance_count);
            const bool first_build      = (fs.tlas == VK_NULL_HANDLE);
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
                // With per-frame TLAS slots, each slot's backing buffer is exclusive to that
                // slot, so no other in-flight frame is using it when we rotate back here.
                if (sizes.accelerationStructureSize > fs.tlas_backing_size) {
                    if (fs.tlas != VK_NULL_HANDLE) {
                        s_res->fn_destroy_as(device, fs.tlas, nullptr);
                        vkDestroyBuffer(device, fs.tlas_buffer, nullptr);
                        vkFreeMemory(device, fs.tlas_memory, nullptr);
                        fs.tlas = VK_NULL_HANDLE;
                    }

                    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                    bi.size        = sizes.accelerationStructureSize;
                    bi.usage       = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                                   | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                    vkCreateBuffer(device, &bi, nullptr, &fs.tlas_buffer);

                    VkMemoryRequirements req{};
                    vkGetBufferMemoryRequirements(device, fs.tlas_buffer, &req);

                    VkMemoryAllocateFlagsInfo flags_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
                    flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

                    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                    ai.pNext           = &flags_info;
                    ai.allocationSize  = req.size;
                    ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                    vkAllocateMemory(device, &ai, nullptr, &fs.tlas_memory);
                    vkBindBufferMemory(device, fs.tlas_buffer, fs.tlas_memory, 0);
                    fs.tlas_backing_size = sizes.accelerationStructureSize;
                }

                // Create the TLAS handle if it was destroyed (or never existed).
                if (fs.tlas == VK_NULL_HANDLE) {
                    VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
                    ci.buffer = fs.tlas_buffer;
                    ci.size   = fs.tlas_backing_size;
                    ci.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
                    s_res->fn_create_as(device, &ci, nullptr, &fs.tlas);
                    // New handle — descriptors that reference this slot's TLAS must be re-written.
                    fs.wf_desc_dirty = true;
                }

                // Scratch: buildScratchSize is always >= updateScratchSize so one persistent
                // buffer covers both modes.
                ensure_scratch_buffer(sizes.buildScratchSize,
                                      fs.tlas_scratch_buffer, fs.tlas_scratch_memory,
                                      fs.tlas_scratch_addr,   fs.tlas_scratch_size);

                fs.tlas_mode_update        = false;
                fs.tlas_last_instance_count = prim_count;
            } else {
                // Same instance count — refit in place; scratch already sized from the initial build.
                fs.tlas_mode_update = true;
            }

            fs.tlas_prim_count = prim_count;
            return true;
        }

        // Records the TLAS build or refit into cmd, then emits an AS_WRITE → RT_READ barrier
        // so the immediately-following vkCmdTraceRaysKHR can consume the result.
        static void record_tlas_build(VkCommandBuffer cmd) {
            auto& fs = s_res->frame_slots[s_res->current_slot];
            VkBufferDeviceAddressInfo addr_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            addr_info.buffer = fs.instance_buffer;
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
            build_info.mode                      = fs.tlas_mode_update
                                                       ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                                                       : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            build_info.srcAccelerationStructure  = fs.tlas_mode_update ? fs.tlas : VK_NULL_HANDLE;
            build_info.dstAccelerationStructure  = fs.tlas;
            build_info.geometryCount             = 1;
            build_info.pGeometries               = &geometry;
            build_info.scratchData.deviceAddress = fs.tlas_scratch_addr;

            VkAccelerationStructureBuildRangeInfoKHR range{};
            range.primitiveCount = fs.tlas_prim_count;
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


        // Push constant layout shared by all three wavefront compute shaders:
        //   uint total_path_count (4) + uint frame_count (4) + uint width (4) + uint height (4)
        //   + mat4 inv_view (64) + mat4 inv_proj (64) = 144 bytes
        static constexpr uint32_t k_wf_pc_size = 144;

        struct WavefrontPC {
            uint32_t  total_path_count;
            uint32_t  frame_count;
            uint32_t  width;
            uint32_t  height;
            glm::mat4 inv_view;
            glm::mat4 inv_proj;
        };
        static_assert(sizeof(WavefrontPC) == k_wf_pc_size, "WavefrontPC size mismatch");

        // Returns {VkPipeline, VkPipelineLayout}. Caller stores them in PathTracerResources.
        // layouts must cover set indices 0..layout_count-1 (use empty_set_layout as placeholder).
        // pc_size=0 means no push constants.
        static std::pair<VkPipeline, VkPipelineLayout> build_compute_pipeline(
                const std::filesystem::path& shader_path,
                VkDescriptorSetLayout* layouts,
                uint32_t layout_count,
                uint32_t pc_size) {
            HN_PROFILE_FUNCTION();

            VkDevice device = s_res->vk_ctx->get_device();

            std::string src = ShaderCompiler::read_file(shader_path);
            if (src.empty()) {
                HN_CORE_ERROR("[PathTracer] build_compute_pipeline: failed to read {}", shader_path.string());
                return {};
            }
            auto spirv = ShaderCompiler::compile_single_stage(src, ShaderCompiler::ShaderStage::Compute);
            if (spirv.empty()) {
                HN_CORE_ERROR("[PathTracer] build_compute_pipeline: SPIRV compilation failed for {}", shader_path.string());
                return {};
            }

            VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            smci.codeSize = spirv.size() * 4;
            smci.pCode    = spirv.data();
            VkShaderModule mod = VK_NULL_HANDLE;
            vkCreateShaderModule(device, &smci, nullptr, &mod);

            VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            plci.setLayoutCount = layout_count;
            plci.pSetLayouts    = layouts;

            VkPushConstantRange pc_range{};
            if (pc_size > 0) {
                pc_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                pc_range.offset     = 0;
                pc_range.size       = pc_size;
                plci.pushConstantRangeCount = 1;
                plci.pPushConstantRanges    = &pc_range;
            }

            VkPipelineLayout layout = VK_NULL_HANDLE;
            vkCreatePipelineLayout(device, &plci, nullptr, &layout);

            VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            cpci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
            cpci.stage.module = mod;
            cpci.stage.pName  = "main";
            cpci.layout       = layout;

            VkPipeline pipeline = VK_NULL_HANDLE;
            VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline);
            vkDestroyShaderModule(device, mod, nullptr);

            if (r != VK_SUCCESS) {
                HN_CORE_ERROR("[PathTracer] build_compute_pipeline: vkCreateComputePipelines failed for {} ({})",
                              shader_path.filename().string(), (int)r);
                return {};
            }

            HN_CORE_INFO("[PathTracer] Compute pipeline built: {}", shader_path.filename().string());
            return { pipeline, layout };
        }

        static void build_wavefront_compute_pipelines() {
            HN_PROFILE_FUNCTION();

            std::filesystem::path shaders = std::filesystem::path(ASSET_ROOT) / "shaders" / "PathTrace";

            // Logic.comp uses sets 1, 2, 3. Set 0 is an empty placeholder so that
            // the layout indices align: pSetLayouts[0]=empty, [1]=set1, [2]=set2_logic, [3]=set3.
            {
                VkDescriptorSetLayout layouts[4] = {
                    s_res->empty_set_layout,
                    s_res->wf_set1_layout,
                    s_res->wf_set2_logic_layout,
                    s_res->wf_set3_layout,
                };
                auto [pipeline, layout] = build_compute_pipeline(
                    shaders / "PathTrace_Logic.comp", layouts, 4, k_wf_pc_size);
                s_res->logic_pipeline = pipeline;
                s_res->logic_layout   = layout;
            }

            // NewPath.comp uses sets 1 and 2 (logic layout). Set 0 is empty placeholder.
            {
                VkDescriptorSetLayout layouts[3] = {
                    s_res->empty_set_layout,
                    s_res->wf_set1_layout,
                    s_res->wf_set2_logic_layout,
                };
                auto [pipeline, layout] = build_compute_pipeline(
                    shaders / "PathTrace_NewPath.comp", layouts, 3, k_wf_pc_size);
                s_res->new_path_pipeline = pipeline;
                s_res->new_path_layout   = layout;
            }

            // Material.comp uses sets 0, 1, 2 (extend layout).
            {
                VkDescriptorSetLayout layouts[3] = {
                    s_res->wf_set0_layout,
                    s_res->wf_set1_layout,
                    s_res->wf_set2_extend_layout,
                };
                auto [pipeline, layout] = build_compute_pipeline(
                    shaders / "PathTrace_Material.comp", layouts, 3, k_wf_pc_size);
                s_res->material_pipeline = pipeline;
                s_res->material_layout   = layout;
            }

            HN_CORE_INFO("[PathTracer] Wavefront compute pipelines built");
        }

        static void build_svgf_pipeline() {
            VkDevice device = s_res->vk_ctx->get_device();

            // Descriptor layout: 4 storage images (accum, ping, filtered, gbuffer).
            // UPDATE_AFTER_BIND because the descriptor is written once on first frame / after resize,
            // but the previous frame may still be in flight at that point.
            VkDescriptorSetLayoutBinding svgf_bindings[4] = {};
            for (uint32_t i = 0; i < 4; i++) {
                svgf_bindings[i].binding        = i;
                svgf_bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                svgf_bindings[i].descriptorCount = 1;
                svgf_bindings[i].stageFlags     = VK_SHADER_STAGE_COMPUTE_BIT;
            }
            VkDescriptorBindingFlags svgf_flags[4] = {
                VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
                VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
                VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
                VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
            };
            VkDescriptorSetLayoutBindingFlagsCreateInfo svgf_flags_ci{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
            svgf_flags_ci.bindingCount  = 4;
            svgf_flags_ci.pBindingFlags = svgf_flags;
            VkDescriptorSetLayoutCreateInfo slci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            slci.pNext        = &svgf_flags_ci;
            slci.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            slci.bindingCount = 4;
            slci.pBindings    = svgf_bindings;
            vkCreateDescriptorSetLayout(device, &slci, nullptr, &s_res->svgf_desc_layout);

            VkDescriptorPoolSize pool_size{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 };
            VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            dpci.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
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

        static void build_extend_pipeline() {
            HN_PROFILE_FUNCTION();

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
            uint32_t sbt_total   = region_size * 3;


            // Create pipeline layout
            VkDescriptorSetLayout extend_layouts[] = {
                s_res->wf_set0_layout,
                s_res->wf_set1_layout,
                s_res->wf_set2_extend_layout,
            };

            VkPipelineLayoutCreateInfo pl_ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            pl_ci.setLayoutCount         = 3;
            pl_ci.pSetLayouts            = extend_layouts;
            pl_ci.pushConstantRangeCount = 0;
            pl_ci.pPushConstantRanges    = nullptr;
            vkCreatePipelineLayout(device, &pl_ci, nullptr, &s_res->extend_rt_layout);


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
            VkShaderModule extend_module = make_module(ShaderCompiler::read_file(
                (assets_dir / "shaders" / "PathTrace" / "PathTrace_Extend.rgen")), ShaderCompiler::ShaderStage::RayGen, "PathTrace_Extend");
            VkShaderModule miss_module = make_module(ShaderCompiler::read_file(
                (assets_dir / "shaders" / "PathTrace" / "PathTrace_Miss.rmiss")), ShaderCompiler::ShaderStage::Miss, "PathTrace_Miss");
            VkShaderModule hit_module = make_module(ShaderCompiler::read_file(
                (assets_dir / "shaders" / "PathTrace" / "PathTrace_ClosestHit.rchit")), ShaderCompiler::ShaderStage::ClosestHit, "PathTrace_ClosestHit");

            if (!compile_ok) {
                HN_CORE_ERROR("[PathTracer] Shader compilation failed — Extend pipeline will not be built");
                if (extend_module)      vkDestroyShaderModule(device, extend_module,      nullptr);
                if (miss_module)        vkDestroyShaderModule(device, miss_module,        nullptr);
                if (hit_module)         vkDestroyShaderModule(device, hit_module,         nullptr);
                return;
            }


            // Stages: 0=raygen, 1=miss, 2=hit
            VkPipelineShaderStageCreateInfo stages[3] = {};
            stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            stages[0].module = extend_module;
            stages[0].pName  = "main";
            stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
            stages[1].module = miss_module;
            stages[1].pName  = "main";
            stages[2].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[2].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            stages[2].module = hit_module;
            stages[2].pName  = "main";


            // Groups: 0=raygen, 1=miss, 2=hit
            VkRayTracingShaderGroupCreateInfoKHR groups[3] = {};
            groups[0] = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
            groups[0].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            groups[0].generalShader    = 0;
            groups[0].closestHitShader = groups[0].anyHitShader = groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

            groups[1] = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
            groups[1].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            groups[1].generalShader    = 1;
            groups[1].closestHitShader = groups[1].anyHitShader = groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

            groups[2] = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
            groups[2].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            groups[2].generalShader    = VK_SHADER_UNUSED_KHR;
            groups[2].closestHitShader = 2;
            groups[2].anyHitShader     = groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;


            VkRayTracingPipelineCreateInfoKHR rt_ci{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
            rt_ci.stageCount                   = 3;
            rt_ci.pStages                      = stages;
            rt_ci.groupCount                   = 3;
            rt_ci.pGroups                      = groups;
            rt_ci.maxPipelineRayRecursionDepth = 1;
            rt_ci.layout                       = s_res->extend_rt_layout;

            VkResult r = s_res->fn_create_rt_pipeline(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rt_ci, nullptr, &s_res->extend_rt_pipeline);

            vkDestroyShaderModule(device, hit_module,         nullptr);
            vkDestroyShaderModule(device, miss_module,        nullptr);
            vkDestroyShaderModule(device, extend_module,      nullptr);

            if (r != VK_SUCCESS) {
                HN_CORE_ERROR("[PathTracer] vkCreateRayTracingPipelinesKHR failed ({})", (int)r);
                return;
            }

            // Retrieve shader handles from pipeline (3 groups: raygen, hit, miss)
            std::vector<uint8_t> handles(handle_size * 3);
            r = s_res->fn_get_sbt_handles(device, s_res->extend_rt_pipeline, 0, 3, handle_size * 3, handles.data());
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkGetRayTracingShaderGroupHandlesKHR failed");


            // Allocate host-visible sbt buffer
            {
                VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                bi.size        = sbt_total;
                bi.usage       = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR
                               | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                vkCreateBuffer(device, &bi, nullptr, &s_res->extend_sbt_buf);

                VkMemoryRequirements req{};
                vkGetBufferMemoryRequirements(device, s_res->extend_sbt_buf, &req);

                VkMemoryAllocateFlagsInfo flags_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
                flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

                VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                ai.pNext           = &flags_info;
                ai.allocationSize  = req.size;
                ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                vkAllocateMemory(device, &ai, nullptr, &s_res->extend_sbt_mem);
                vkBindBufferMemory(device, s_res->extend_sbt_buf, s_res->extend_sbt_mem, 0);
            }


            // Copy handles: [raygen | miss | hit] - This raygen -> miss -> hit order is required per Vulkan spec
            void* mapped = nullptr;
            vkMapMemory(device, s_res->extend_sbt_mem, 0, sbt_total, 0, &mapped);
            uint8_t* dst = static_cast<uint8_t*>(mapped);
            memcpy(dst + 0 * region_size, handles.data() + 0 * handle_size, handle_size);
            memcpy(dst + 1 * region_size, handles.data() + 1 * handle_size, handle_size);
            memcpy(dst + 2 * region_size, handles.data() + 2 * handle_size, handle_size);
            vkUnmapMemory(device, s_res->extend_sbt_mem);

            VkBufferDeviceAddressInfo sbt_addr_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            sbt_addr_info.buffer = s_res->extend_sbt_buf;
            VkDeviceAddress sbt_base = s_res->fn_get_buf_addr(device, &sbt_addr_info);

            s_res->extend_sbt_raygen = { sbt_base + 0 * region_size, region_size, region_size };
            s_res->extend_sbt_miss   = { sbt_base + 1 * region_size, region_size, region_size };
            s_res->extend_sbt_hit    = { sbt_base + 2 * region_size, region_size, region_size };

            s_res->extend_built = true;
            HN_CORE_INFO("[PathTracer] Extend RT pipeline built successfully");
        }

        static void build_shadow_pipeline() {
            HN_PROFILE_FUNCTION();

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
            uint32_t sbt_total   = region_size * 3; // Will contain an empty hit shader


            // Create pipeline layout
            VkDescriptorSetLayout shadow_layouts[] = {
                s_res->wf_set0_layout,
                s_res->empty_set_layout,
                s_res->wf_set2_shadow_layout,
            };

            VkPipelineLayoutCreateInfo pl_ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            pl_ci.setLayoutCount         = 3;
            pl_ci.pSetLayouts            = shadow_layouts;
            pl_ci.pushConstantRangeCount = 0;
            pl_ci.pPushConstantRanges    = nullptr;
            vkCreatePipelineLayout(device, &pl_ci, nullptr, &s_res->shadow_rt_layout);


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
            VkShaderModule shadow_module = make_module(ShaderCompiler::read_file(
                (assets_dir / "shaders" / "PathTrace" / "PathTrace_Shadow.rgen")), ShaderCompiler::ShaderStage::RayGen, "PathTrace_Shadow");
            VkShaderModule miss_module = make_module(ShaderCompiler::read_file(
                (assets_dir / "shaders" / "PathTrace" / "PathTrace_ShadowMiss.rmiss")), ShaderCompiler::ShaderStage::Miss, "PathTrace_ShadowMiss");
            if (!compile_ok) {
                HN_CORE_ERROR("[PathTracer] Shader compilation failed — Shadow pipeline will not be built");
                if (shadow_module)      vkDestroyShaderModule(device, shadow_module,      nullptr);
                if (miss_module)        vkDestroyShaderModule(device, miss_module,        nullptr);
                return;
            }


            // Stages: 0=raygen, 1=miss
            VkPipelineShaderStageCreateInfo stages[2] = {};
            stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            stages[0].module = shadow_module;
            stages[0].pName  = "main";
            stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
            stages[1].module = miss_module;
            stages[1].pName  = "main";


            // Groups: 0=raygen, 1=miss, 2=hit
            VkRayTracingShaderGroupCreateInfoKHR groups[3] = {};
            groups[0] = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
            groups[0].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            groups[0].generalShader    = 0;
            groups[0].closestHitShader = groups[0].anyHitShader = groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

            groups[1] = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
            groups[1].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            groups[1].generalShader    = 1;
            groups[1].closestHitShader = groups[1].anyHitShader = groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

            groups[2] = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
            groups[2].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            groups[2].generalShader    = VK_SHADER_UNUSED_KHR;
            groups[2].closestHitShader = VK_SHADER_UNUSED_KHR;
            groups[2].anyHitShader     = groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;


            VkRayTracingPipelineCreateInfoKHR rt_ci{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
            rt_ci.stageCount                   = 2;
            rt_ci.pStages                      = stages;
            rt_ci.groupCount                   = 3;
            rt_ci.pGroups                      = groups;
            rt_ci.maxPipelineRayRecursionDepth = 1;
            rt_ci.layout                       = s_res->shadow_rt_layout;

            VkResult r = s_res->fn_create_rt_pipeline(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rt_ci, nullptr, &s_res->shadow_rt_pipeline);

            vkDestroyShaderModule(device, shadow_module,      nullptr);
            vkDestroyShaderModule(device, miss_module,        nullptr);

            if (r != VK_SUCCESS) {
                HN_CORE_ERROR("[PathTracer] vkCreateRayTracingPipelinesKHR failed ({})", (int)r);
                return;
            }

            // Retrieve shader handles from pipeline (3 groups: raygen, miss, hit)
            std::vector<uint8_t> handles(handle_size * 3);
            r = s_res->fn_get_sbt_handles(device, s_res->shadow_rt_pipeline, 0, 3, handle_size * 3, handles.data());
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkGetRayTracingShaderGroupHandlesKHR failed");


            // Allocate host-visible sbt buffer
            {
                VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                bi.size        = sbt_total;
                bi.usage       = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR
                               | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                vkCreateBuffer(device, &bi, nullptr, &s_res->shadow_sbt_buf);

                VkMemoryRequirements req{};
                vkGetBufferMemoryRequirements(device, s_res->shadow_sbt_buf, &req);

                VkMemoryAllocateFlagsInfo flags_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
                flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

                VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                ai.pNext           = &flags_info;
                ai.allocationSize  = req.size;
                ai.memoryTypeIndex = find_memory_type(s_res->cached_mem_props, req.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                vkAllocateMemory(device, &ai, nullptr, &s_res->shadow_sbt_mem);
                vkBindBufferMemory(device, s_res->shadow_sbt_buf, s_res->shadow_sbt_mem, 0);
            }


            // Copy handles: [raygen | miss | hit] - This raygen -> miss -> hit order is required per Vulkan spec
            void* mapped = nullptr;
            vkMapMemory(device, s_res->shadow_sbt_mem, 0, sbt_total, 0, &mapped);
            uint8_t* dst = static_cast<uint8_t*>(mapped);
            memcpy(dst + 0 * region_size, handles.data() + 0 * handle_size, handle_size);
            memcpy(dst + 1 * region_size, handles.data() + 1 * handle_size, handle_size);
            memcpy(dst + 2 * region_size, handles.data() + 2 * handle_size, handle_size);
            vkUnmapMemory(device, s_res->shadow_sbt_mem);

            VkBufferDeviceAddressInfo sbt_addr_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            sbt_addr_info.buffer = s_res->shadow_sbt_buf;
            VkDeviceAddress sbt_base = s_res->fn_get_buf_addr(device, &sbt_addr_info);

            s_res->shadow_sbt_raygen = { sbt_base + 0 * region_size, region_size, region_size };
            s_res->shadow_sbt_miss   = { sbt_base + 1 * region_size, region_size, region_size };
            s_res->shadow_sbt_hit    = { sbt_base + 2 * region_size, region_size, region_size };

            s_res->shadow_built = true;
            HN_CORE_INFO("[PathTracer] Shadow RT pipeline built successfully");
        }

        static void update_wavefront_desc_sets(uint32_t slot_idx) {
            HN_PROFILE_FUNCTION();

            auto& slot = s_res->frame_slots[slot_idx];
            if (!slot.wf_desc_dirty) return;

            // Guard: geometry and material buffers are allocated by prepare_tlas_cpu;
            // if they're null there are no scene instances and we can't write valid descriptors.
            if (slot.geometry_lookup_buffer == VK_NULL_HANDLE || slot.material_buffer == VK_NULL_HANDLE)
                return;

            VkDevice device = s_res->vk_ctx->get_device();

            // --- set0: TLAS, gbuffer, geometry, material, textures, lights ---
            {
                VkAccelerationStructureKHR tlas_handle = slot.tlas;
                VkWriteDescriptorSetAccelerationStructureKHR as_write{
                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
                as_write.accelerationStructureCount = 1;
                as_write.pAccelerationStructures    = &tlas_handle;

                VkDescriptorImageInfo  gbuffer_info{ VK_NULL_HANDLE, s_res->gbuffer_view, VK_IMAGE_LAYOUT_GENERAL };
                VkDescriptorBufferInfo geo_info    { slot.geometry_lookup_buffer, 0, VK_WHOLE_SIZE };
                VkDescriptorBufferInfo mat_info    { slot.material_buffer,         0, VK_WHOLE_SIZE };
                VkDescriptorBufferInfo lights_info { s_res->lights_buffer,         0, VK_WHOLE_SIZE };

                const uint32_t tex_count = (uint32_t)s_res->bound_textures.size();
                std::vector<VkDescriptorImageInfo> tex_infos(tex_count);
                for (uint32_t i = 0; i < tex_count; i++) {
                    tex_infos[i] = { s_res->bound_textures[i].second,
                                     s_res->bound_textures[i].first,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
                }

                VkWriteDescriptorSet writes[6] = {};
                uint32_t wc = 0;

                writes[wc].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[wc].pNext           = &as_write;
                writes[wc].dstSet          = slot.wf_set0;
                writes[wc].dstBinding      = 0;
                writes[wc].descriptorCount = 1;
                writes[wc].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                wc++;

                writes[wc].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[wc].dstSet          = slot.wf_set0;
                writes[wc].dstBinding      = 1;
                writes[wc].descriptorCount = 1;
                writes[wc].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                writes[wc].pImageInfo      = &gbuffer_info;
                wc++;

                writes[wc].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[wc].dstSet          = slot.wf_set0;
                writes[wc].dstBinding      = 2;
                writes[wc].descriptorCount = 1;
                writes[wc].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[wc].pBufferInfo     = &geo_info;
                wc++;

                writes[wc].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[wc].dstSet          = slot.wf_set0;
                writes[wc].dstBinding      = 3;
                writes[wc].descriptorCount = 1;
                writes[wc].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[wc].pBufferInfo     = &mat_info;
                wc++;

                if (tex_count > 0) {
                    writes[wc].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[wc].dstSet          = slot.wf_set0;
                    writes[wc].dstBinding      = 4;
                    writes[wc].descriptorCount = tex_count;
                    writes[wc].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    writes[wc].pImageInfo      = tex_infos.data();
                    wc++;
                }

                writes[wc].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[wc].dstSet          = slot.wf_set0;
                writes[wc].dstBinding      = 5;
                writes[wc].descriptorCount = 1;
                writes[wc].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writes[wc].pBufferInfo     = &lights_info;
                wc++;

                vkUpdateDescriptorSets(device, wc, writes, 0, nullptr);
            }

            // --- set1: path state SOA (bindings 0-8) ---
            {
                VkDescriptorBufferInfo buf_infos[9] = {
                    { slot.path_ray_origin_buf,   0, VK_WHOLE_SIZE },
                    { slot.path_ray_dir_buf,       0, VK_WHOLE_SIZE },
                    { slot.path_throughput_buf,    0, VK_WHOLE_SIZE },
                    { slot.path_radiance_buf,      0, VK_WHOLE_SIZE },
                    { slot.path_seed_buf,          0, VK_WHOLE_SIZE },
                    { slot.path_bounce_buf,        0, VK_WHOLE_SIZE },
                    { slot.path_flags_buf,         0, VK_WHOLE_SIZE },
                    { slot.path_shadow_start_buf,  0, VK_WHOLE_SIZE },
                    { slot.path_shadow_end_buf,    0, VK_WHOLE_SIZE },
                };
                VkWriteDescriptorSet writes[9] = {};
                for (uint32_t i = 0; i < 9; i++) {
                    writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[i].dstSet          = slot.wf_set1;
                    writes[i].dstBinding      = i;
                    writes[i].descriptorCount = 1;
                    writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    writes[i].pBufferInfo     = &buf_infos[i];
                }
                vkUpdateDescriptorSets(device, 9, writes, 0, nullptr);
            }

            // --- set2_extend: queues for Extend.rgen + Material.comp (bindings 0-5) ---
            // extend_count and shadow_count share queue_count_buf at aligned offsets.
            {
                const VkDeviceSize shadow_off = s_res->count_buf_shadow_offset;
                VkDescriptorBufferInfo infos[6] = {
                    { slot.queue_count_buf,      0,          sizeof(uint32_t) }, // extend_count
                    { slot.extend_queue_buf,     0,          VK_WHOLE_SIZE    }, // extend_paths
                    { slot.hit_record_buf,       0,          VK_WHOLE_SIZE    }, // hit_record
                    { slot.queue_count_buf,      shadow_off, sizeof(uint32_t) }, // shadow_count
                    { slot.shadow_queue_buf,     0,          VK_WHOLE_SIZE    }, // shadow_rays
                    { slot.pending_radiance_buf, 0,          VK_WHOLE_SIZE    }, // pending_radiance
                };
                VkWriteDescriptorSet writes[6] = {};
                for (uint32_t i = 0; i < 6; i++) {
                    writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[i].dstSet          = slot.wf_set2_extend;
                    writes[i].dstBinding      = i;
                    writes[i].descriptorCount = 1;
                    writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    writes[i].pBufferInfo     = &infos[i];
                }
                vkUpdateDescriptorSets(device, 6, writes, 0, nullptr);
            }

            // --- set2_shadow: queues for Shadow.rgen (bindings 0-2) ---
            {
                VkDescriptorBufferInfo infos[3] = {
                    { slot.queue_count_buf, s_res->count_buf_shadow_offset, sizeof(uint32_t) }, // shadow_count
                    { slot.shadow_queue_buf, 0,                VK_WHOLE_SIZE    }, // shadow_rays
                    { slot.occlusion_buf,    0,                VK_WHOLE_SIZE    }, // occlusion
                };
                VkWriteDescriptorSet writes[3] = {};
                for (uint32_t i = 0; i < 3; i++) {
                    writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[i].dstSet          = slot.wf_set2_shadow;
                    writes[i].dstBinding      = i;
                    writes[i].descriptorCount = 1;
                    writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    writes[i].pBufferInfo     = &infos[i];
                }
                vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
            }

            // --- set2_logic: queues for Logic + NewPath (bindings 2, 3, 4, 5 only) ---
            {
                VkDescriptorBufferInfo infos[4] = {
                    { slot.occlusion_buf,        0,                VK_WHOLE_SIZE    }, // binding 2
                    { slot.queue_count_buf,      0,                sizeof(uint32_t) }, // binding 3: extend_count
                    { slot.extend_queue_buf,     0,                VK_WHOLE_SIZE    }, // binding 4
                    { slot.pending_radiance_buf, 0,                VK_WHOLE_SIZE    }, // binding 5
                };
                VkWriteDescriptorSet writes[4] = {};
                for (uint32_t i = 0; i < 4; i++) {
                    writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[i].dstSet          = slot.wf_set2_logic;
                    writes[i].dstBinding      = i + 2; // bindings 2, 3, 4, 5
                    writes[i].descriptorCount = 1;
                    writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    writes[i].pBufferInfo     = &infos[i];
                }
                vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
            }

            // --- set3: accum storage image (binding 0) ---
            {
                VkDescriptorImageInfo accum_info{ VK_NULL_HANDLE, s_res->accum_view, VK_IMAGE_LAYOUT_GENERAL };
                VkWriteDescriptorSet write{};
                write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet          = slot.wf_set3;
                write.dstBinding      = 0;
                write.descriptorCount = 1;
                write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                write.pImageInfo      = &accum_info;
                vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            }

            slot.wf_desc_dirty = false;
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
            s_res->svgf_desc_dirty = false;
        }

    } // anonymous namespace

    void Renderer3DPathTracer::init(VulkanContext* ctx) {
        if (!s_res)
            s_res = new PathTracerResources{};

        s_res->vk_ctx = ctx;

        // Cache memory properties once; queried in find_memory_type throughout.
        vkGetPhysicalDeviceMemoryProperties(ctx->get_physical_device(), &s_res->cached_mem_props);

        // Align shadow_count's offset inside queue_count_buf to the device's
        // minStorageBufferOffsetAlignment so descriptor writes are valid.
        {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(ctx->get_physical_device(), &props);
            VkDeviceSize align = props.limits.minStorageBufferOffsetAlignment;
            s_res->count_buf_shadow_offset = align; // one aligned stride past extend_count at [0]
        }

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
        LOAD(fn_get_sbt_handles,      vkGetRayTracingShaderGroupHandlesKHR);
        LOAD(fn_cmd_write_as_props,   vkCmdWriteAccelerationStructuresPropertiesKHR);
        LOAD(fn_cmd_copy_as,          vkCmdCopyAccelerationStructureKHR);
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

        build_svgf_pipeline();
        build_wavefront_layouts();
        build_wavefront_compute_pipelines();
        build_extend_pipeline();
        build_shadow_pipeline();
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

        if (s_res->blas_compact_query_pool != VK_NULL_HANDLE)
            vkDestroyQueryPool(device, s_res->blas_compact_query_pool, nullptr);

        for (auto& fs : s_res->frame_slots) {
            if (fs.tlas != VK_NULL_HANDLE) {
                s_res->fn_destroy_as(device, fs.tlas, nullptr);
                vkDestroyBuffer(device, fs.tlas_buffer, nullptr);
                vkFreeMemory(device, fs.tlas_memory, nullptr);
            }
            if (fs.tlas_scratch_buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, fs.tlas_scratch_buffer, nullptr);
                vkFreeMemory(device, fs.tlas_scratch_memory, nullptr);
            }
            if (fs.instance_buffer != VK_NULL_HANDLE) {
                vkUnmapMemory(device, fs.instance_memory);
                vkDestroyBuffer(device, fs.instance_buffer, nullptr);
                vkFreeMemory(device, fs.instance_memory, nullptr);
            }
            if (fs.geometry_lookup_buffer != VK_NULL_HANDLE) {
                vkUnmapMemory(device, fs.geometry_lookup_memory);
                vkDestroyBuffer(device, fs.geometry_lookup_buffer, nullptr);
                vkFreeMemory(device, fs.geometry_lookup_memory, nullptr);
            }
            if (fs.material_buffer != VK_NULL_HANDLE) {
                vkUnmapMemory(device, fs.material_memory);
                vkDestroyBuffer(device, fs.material_buffer, nullptr);
                vkFreeMemory(device, fs.material_memory, nullptr);
            }

            // Wavefront path-state SOA buffers
            free_device_local_buffer(fs.path_ray_origin_buf,   fs.path_ray_origin_mem);
            free_device_local_buffer(fs.path_ray_dir_buf,      fs.path_ray_dir_mem);
            free_device_local_buffer(fs.path_throughput_buf,   fs.path_throughput_mem);
            free_device_local_buffer(fs.path_radiance_buf,     fs.path_radiance_mem);
            free_device_local_buffer(fs.path_seed_buf,         fs.path_seed_mem);
            free_device_local_buffer(fs.path_bounce_buf,       fs.path_bounce_mem);
            free_device_local_buffer(fs.path_flags_buf,        fs.path_flags_mem);
            free_device_local_buffer(fs.path_shadow_start_buf, fs.path_shadow_start_mem);
            free_device_local_buffer(fs.path_shadow_end_buf,   fs.path_shadow_end_mem);

            // Wavefront queue + hit-record buffers
            free_device_local_buffer(fs.extend_queue_buf,     fs.extend_queue_mem);
            free_device_local_buffer(fs.hit_record_buf,       fs.hit_record_mem);
            free_device_local_buffer(fs.queue_count_buf,      fs.queue_count_mem);
            free_device_local_buffer(fs.shadow_queue_buf,     fs.shadow_queue_mem);
            free_device_local_buffer(fs.occlusion_buf,        fs.occlusion_mem);
            free_device_local_buffer(fs.pending_radiance_buf, fs.pending_radiance_mem);
        }

        if (s_res->lights_buffer != VK_NULL_HANDLE) {
            vkUnmapMemory(device, s_res->lights_memory);
            vkDestroyBuffer(device, s_res->lights_buffer, nullptr);
            vkFreeMemory(device, s_res->lights_memory, nullptr);
        }

        destroy_accum_image();

        if (s_res->svgf_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, s_res->svgf_pipeline, nullptr);
        if (s_res->svgf_pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, s_res->svgf_pipeline_layout, nullptr);
        if (s_res->svgf_desc_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, s_res->svgf_desc_pool, nullptr);
        if (s_res->svgf_desc_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, s_res->svgf_desc_layout, nullptr);

        // Wavefront RT pipelines and SBTs
        if (s_res->shadow_rt_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, s_res->shadow_rt_pipeline, nullptr);
        if (s_res->shadow_rt_layout   != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, s_res->shadow_rt_layout, nullptr);
        if (s_res->shadow_sbt_buf     != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, s_res->shadow_sbt_buf, nullptr);
            vkFreeMemory(device, s_res->shadow_sbt_mem, nullptr);
        }
        if (s_res->extend_rt_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, s_res->extend_rt_pipeline, nullptr);
        if (s_res->extend_rt_layout   != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, s_res->extend_rt_layout, nullptr);
        if (s_res->extend_sbt_buf     != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, s_res->extend_sbt_buf, nullptr);
            vkFreeMemory(device, s_res->extend_sbt_mem, nullptr);
        }

        // Wavefront compute pipelines
        if (s_res->material_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, s_res->material_pipeline, nullptr);
        if (s_res->material_layout   != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, s_res->material_layout, nullptr);
        if (s_res->new_path_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, s_res->new_path_pipeline, nullptr);
        if (s_res->new_path_layout   != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, s_res->new_path_layout, nullptr);
        if (s_res->logic_pipeline    != VK_NULL_HANDLE) vkDestroyPipeline(device, s_res->logic_pipeline, nullptr);
        if (s_res->logic_layout      != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, s_res->logic_layout, nullptr);

        // Wavefront descriptor pool and set layouts (pool destruction frees all sets automatically)
        if (s_res->wf_desc_pool           != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, s_res->wf_desc_pool, nullptr);
        if (s_res->wf_set3_layout         != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, s_res->wf_set3_layout, nullptr);
        if (s_res->wf_set2_logic_layout   != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, s_res->wf_set2_logic_layout, nullptr);
        if (s_res->wf_set2_shadow_layout  != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, s_res->wf_set2_shadow_layout, nullptr);
        if (s_res->wf_set2_extend_layout  != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, s_res->wf_set2_extend_layout, nullptr);
        if (s_res->wf_set1_layout         != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, s_res->wf_set1_layout, nullptr);
        if (s_res->wf_set0_layout         != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, s_res->wf_set0_layout, nullptr);
        if (s_res->empty_set_layout       != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, s_res->empty_set_layout, nullptr);

        delete s_res;
        s_res = nullptr;
    }

    bool Renderer3DPathTracer::is_initialized() {
        return s_res != nullptr;
    }

    void Renderer3DPathTracer::set_camera(const glm::mat4& inv_view, const glm::mat4& inv_proj) {
        if (!s_res) return;
        if (memcmp(&s_res->inv_view, &inv_view, sizeof(glm::mat4)) != 0 ||
            memcmp(&s_res->inv_proj, &inv_proj, sizeof(glm::mat4)) != 0)
            invalidate_accumulation();
        s_res->inv_view = inv_view;
        s_res->inv_proj = inv_proj;
    }

    void Renderer3DPathTracer::invalidate_accumulation() {
        if (!s_res) return;
        s_res->accum_frame_count = 0;
        // Schedule GPU path-state resets for the next k_pt_frames frames so both
        // double-buffered slots get reinitialized (stale in-flight rays are discarded).
        s_res->path_state_reset_remaining = PathTracerResources::k_pt_frames;
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

        for (auto& fs : s_res->frame_slots) {
            if (fs.tlas != VK_NULL_HANDLE) {
                s_res->fn_destroy_as(device, fs.tlas, nullptr);
                vkDestroyBuffer(device, fs.tlas_buffer, nullptr);
                vkFreeMemory(device, fs.tlas_memory, nullptr);
                fs.tlas              = VK_NULL_HANDLE;
                fs.tlas_buffer       = VK_NULL_HANDLE;
                fs.tlas_memory       = VK_NULL_HANDLE;
                fs.tlas_backing_size = 0;
                fs.tlas_last_instance_count = UINT32_MAX;
            }
            if (fs.tlas_scratch_buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, fs.tlas_scratch_buffer, nullptr);
                vkFreeMemory(device, fs.tlas_scratch_memory, nullptr);
                fs.tlas_scratch_buffer = VK_NULL_HANDLE;
                fs.tlas_scratch_memory = VK_NULL_HANDLE;
                fs.tlas_scratch_addr   = 0;
                fs.tlas_scratch_size   = 0;
            }
        }

        destroy_accum_image();
        s_res->accum_width  = 0;
        s_res->accum_height = 0;
    }

    void Renderer3DPathTracer::register_frame_graph_executors() {
        auto& registry = FrameGraphRegistry::get();

        // New BLASes are built + compacted in a synchronous one-time submit before the main
        // frame command buffer.  TLAS build/refit → trace rays → SVGF denoise → blit all run
        // in the live graphics command buffer via submit_vulkan_graphics_raw.
        registry.register_executor("pathtracing.trace", [](FrameGraphPassContext& ctx) {
            if (!s_res)
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

            // Select the per-frame resource slot for this frame.
            s_res->current_slot = ctx.frame_index() % PathTracerResources::k_pt_frames;

            if (w != s_res->accum_width || h != s_res->accum_height || s_res->accum_image == VK_NULL_HANDLE)
                create_accum_image(w, h);

            // CPU work: gather scene data, prepare BLAS/TLAS state, upload to mapped buffers.
            if (!prepare_tlas_cpu(scene))
                return;

            if (s_res->svgf_pipeline_built && s_res->svgf_desc_dirty)
                update_svgf_desc_set();

            update_wavefront_desc_sets(s_res->current_slot);

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
            bool      do_path_reset = s_res->path_state_reset_remaining > 0;
            // blend_factor stays constant — EMA never fully converges so there's no point
            // in decaying it. SVGF always runs at full strength.
            float     blend_factor  = 1.0f;

            // Build + compact any new BLASes before the main frame submission.
            // This is a synchronous one-time cost (only runs when new meshes are added).
            build_and_compact_pending_blas();

            // --- Image layout init + TLAS build + wavefront dispatch ---
            ctx.submit_vulkan_graphics_raw([inv_view, inv_proj, frame_count, trace_w, trace_h,
                                            need_init, gbuf_init, ping_init, filtered_init,
                                            do_path_reset](VkCommandBuffer cmd) {
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

                // TLAS build stage (BLASes already built and compacted above).
                {
                    HN_GPU_SCOPE(cmd, "TLAS build");
                    record_tlas_build(cmd);
                }

                if (s_res->extend_built && s_res->shadow_built) {
                    uint32_t total_paths = trace_w * trace_h;
                    auto& slot = s_res->frame_slots[s_res->current_slot];

                    WavefrontPC pc{};
                    pc.total_path_count = total_paths;
                    pc.frame_count      = frame_count;
                    pc.width            = trace_w;
                    pc.height           = trace_h;
                    pc.inv_view         = inv_view;
                    pc.inv_proj         = inv_proj;

                    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};

                    // Step 1: clear extend_count + shadow_count before Logic writes them.
                    // On invalidation frames (camera moved / scene changed), also reinitialize
                    // path state so stale in-flight rays are discarded immediately.
                    {
                        HN_GPU_SCOPE(cmd, "WF: clear queues");
                        vkCmdFillBuffer(cmd, slot.queue_count_buf, 0, VK_WHOLE_SIZE, 0u);
                        if (do_path_reset) {
                            uint32_t N = trace_w * trace_h;
                            vkCmdFillBuffer(cmd, slot.path_flags_buf,        0, N * 4u, 0x03030303u);
                            vkCmdFillBuffer(cmd, slot.path_shadow_start_buf, 0, N * 4u, 0u);
                            vkCmdFillBuffer(cmd, slot.path_shadow_end_buf,   0, N * 4u, 0u);
                        }

                        mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                        vkCmdPipelineBarrier(cmd,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, 1, &mb, 0, nullptr, 0, nullptr);
                    }

                    // Step 2: Logic — resolve previous shadow results, write accum, enqueue live paths.
                    // Always runs. On reset frames, flags are TERMINATED|NEEDS_REGEN and radiance
                    // is stale; Logic writes it once (fc=0 → overwrite), then NewPath regenerates
                    // all paths. EMA's floor alpha dilutes that stale sample within ~20 frames.
                    {
                        HN_GPU_SCOPE(cmd, "WF: logic");
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_res->logic_pipeline);
                        VkDescriptorSet sets[] = { slot.wf_set1, slot.wf_set2_logic, slot.wf_set3 };
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            s_res->logic_layout, 1, 3, sets, 0, nullptr);
                        vkCmdPushConstants(cmd, s_res->logic_layout,
                            VK_SHADER_STAGE_COMPUTE_BIT, 0, k_wf_pc_size, &pc);
                        vkCmdDispatch(cmd, (total_paths + 63) / 64, 1, 1);

                        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                        vkCmdPipelineBarrier(cmd,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, 1, &mb, 0, nullptr, 0, nullptr);
                    }

                    // Step 3: NewPath — regenerate terminated paths and push them into the extend queue
                    {
                        HN_GPU_SCOPE(cmd, "WF: new path");
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_res->new_path_pipeline);
                        VkDescriptorSet sets[] = { slot.wf_set1, slot.wf_set2_logic };
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            s_res->new_path_layout, 1, 2, sets, 0, nullptr);
                        vkCmdPushConstants(cmd, s_res->new_path_layout,
                            VK_SHADER_STAGE_COMPUTE_BIT, 0, k_wf_pc_size, &pc);
                        vkCmdDispatch(cmd, (total_paths + 63) / 64, 1, 1);

                        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                        vkCmdPipelineBarrier(cmd,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                            0, 1, &mb, 0, nullptr, 0, nullptr);
                    }

                    // Step 4: Extend RT — trace rays for all queued paths, write hit records + gbuffer
                    {
                        HN_GPU_SCOPE(cmd, "WF: extend RT");
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            s_res->extend_rt_pipeline);
                        VkDescriptorSet sets[] = { slot.wf_set0, slot.wf_set1, slot.wf_set2_extend };
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            s_res->extend_rt_layout, 0, 3, sets, 0, nullptr);
                        s_res->fn_cmd_trace_rays(cmd,
                            &s_res->extend_sbt_raygen, &s_res->extend_sbt_miss,
                            &s_res->extend_sbt_hit,    &s_res->extend_sbt_callable,
                            total_paths, 1, 1);

                        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                        vkCmdPipelineBarrier(cmd,
                            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, 1, &mb, 0, nullptr, 0, nullptr);
                    }

                    // Step 5: Material — evaluate BSDFs, write updated path state + shadow queue
                    {
                        HN_GPU_SCOPE(cmd, "WF: material");
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_res->material_pipeline);
                        VkDescriptorSet sets[] = { slot.wf_set0, slot.wf_set1, slot.wf_set2_extend };
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            s_res->material_layout, 0, 3, sets, 0, nullptr);
                        vkCmdPushConstants(cmd, s_res->material_layout,
                            VK_SHADER_STAGE_COMPUTE_BIT, 0, k_wf_pc_size, &pc);
                        vkCmdDispatch(cmd, (total_paths + 63) / 64, 1, 1);

                        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                        vkCmdPipelineBarrier(cmd,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                            0, 1, &mb, 0, nullptr, 0, nullptr);
                    }

                    // Step 6: Shadow RT — test occlusion for every shadow ray request
                    {
                        HN_GPU_SCOPE(cmd, "WF: shadow RT");
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            s_res->shadow_rt_pipeline);
                        // Set 1 (path state) unused by Shadow.rgen; bind dummy to satisfy layout.
                        VkDescriptorSet sets[] = {
                            slot.wf_set0, s_res->wf_dummy_set, slot.wf_set2_shadow
                        };
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            s_res->shadow_rt_layout, 0, 3, sets, 0, nullptr);
                        s_res->fn_cmd_trace_rays(cmd,
                            &s_res->shadow_sbt_raygen, &s_res->shadow_sbt_miss,
                            &s_res->shadow_sbt_hit,    &s_res->shadow_sbt_callable,
                            total_paths * PathTracerResources::k_max_shadow_per_path, 1, 1);

                        // occlusion[] writes must be visible to next-frame Logic.comp.
                        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                        vkCmdPipelineBarrier(cmd,
                            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, 1, &mb, 0, nullptr, 0, nullptr);
                    }
                }
            });

            s_res->accum_frame_count++;
            if (s_res->path_state_reset_remaining > 0)
                s_res->path_state_reset_remaining--;
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
                    // accum_image written by Logic.comp (compute); gbuffer by Extend.rgen (RT).
                    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, &mb, 0, nullptr, 0, nullptr);

                    {
                        HN_GPU_SCOPE(cmd, "SVGF denoise");
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_res->svgf_pipeline);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                           s_res->svgf_pipeline_layout, 0, 1, &s_res->svgf_desc_set, 0, nullptr);

                        uint32_t gx = (trace_w + 7) / 8;
                        uint32_t gy = (trace_h + 7) / 8;

                        // 2 passes: step sizes 1, 2. Result lands in filtered_image.
                        // pass_idx: 0→ping, 1→filtered (final).
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