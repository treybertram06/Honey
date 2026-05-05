#include "hnpch.h"
#include "renderer_3d_pathtracer.h"

#include "glm/gtc/type_ptr.hpp"
#include "Honey/renderer/frame_graph_registry.h"
#include "Honey/renderer/mesh.h"
#include "Honey/renderer/shader_compiler.h"
#include "Honey/scene/components.h"
#include "Honey/scene/scene.h"
#include "platform/vulkan/vk_context.h"
#include "platform/vulkan/vk_framebuffer.h"

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

            // One BLAS per unique Mesh* (keyed by pointer so it invalidates when mesh is replaced).
            struct BlasEntry {
                VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
                VkBuffer buffer = VK_NULL_HANDLE;
                VkDeviceMemory memory = VK_NULL_HANDLE;
                VkDeviceAddress device_address = 0;
            };
            std::unordered_map<const Mesh*, BlasEntry> blas_cache;

            // TLAS — rebuilt every frame.
            VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
            VkBuffer tlas_buffer = VK_NULL_HANDLE;
            VkDeviceMemory tlas_memory = VK_NULL_HANDLE;

            // Per-frame instance upload buffer (host visible)
            VkBuffer instance_buffer = VK_NULL_HANDLE;
            VkDeviceMemory instance_memory = VK_NULL_HANDLE;
            void* instance_mapped = nullptr;
            uint32_t instance_buffer_capacity = 0;

            // Output accumulation image (RGBA32F storage image).
            VkImage accum_image = VK_NULL_HANDLE;
            VkDeviceMemory accum_memory = VK_NULL_HANDLE;
            VkImageView accum_view = VK_NULL_HANDLE;
            uint32_t accum_width = 0;
            uint32_t accum_height = 0;
            uint32_t accum_frame_count = 0;
            bool accum_needs_layout_init = true; // transition UNDEFINED → GENERAL on first trace

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

        static uint32_t find_memory_type(VkPhysicalDevice phys, uint32_t type_filter, VkMemoryPropertyFlags props) {
            HN_PROFILE_FUNCTION();
            VkPhysicalDeviceMemoryProperties mem_props{};
            vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);

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

        static void destroy_accum_image() {
            VkDevice device = s_res->vk_ctx->get_device();
            if (s_res->accum_view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, s_res->accum_view, nullptr);
                s_res->accum_view = VK_NULL_HANDLE;
            }
            if (s_res->accum_image != VK_NULL_HANDLE) {
                vkDestroyImage(device, s_res->accum_image, nullptr);
                s_res->accum_image = VK_NULL_HANDLE;
            }
            if (s_res->accum_memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, s_res->accum_memory, nullptr);
                s_res->accum_memory = VK_NULL_HANDLE;
            }
        }

        static void create_accum_image(uint32_t w, uint32_t h) {
            VkDevice device = s_res->vk_ctx->get_device();
            VkPhysicalDevice phys = s_res->vk_ctx->get_physical_device();

            destroy_accum_image();

            VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            ici.imageType   = VK_IMAGE_TYPE_2D;
            ici.format      = VK_FORMAT_R32G32B32A32_SFLOAT;
            ici.extent      = {w, h, 1};
            ici.mipLevels   = 1;
            ici.arrayLayers = 1;
            ici.samples     = VK_SAMPLE_COUNT_1_BIT;
            ici.tiling      = VK_IMAGE_TILING_OPTIMAL;
            ici.usage       = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkResult r = vkCreateImage(device, &ici, nullptr, &s_res->accum_image);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateImage failed (accum)");

            VkMemoryRequirements req{};
            vkGetImageMemoryRequirements(device, s_res->accum_image, &req);

            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = find_memory_type(phys, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            r = vkAllocateMemory(device, &ai, nullptr, &s_res->accum_memory);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateMemory failed (accum)");

            vkBindImageMemory(device, s_res->accum_image, s_res->accum_memory, 0);

            VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vci.image    = s_res->accum_image;
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format   = VK_FORMAT_R32G32B32A32_SFLOAT;
            vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            r = vkCreateImageView(device, &vci, nullptr, &s_res->accum_view);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateImageView failed (accum)");

            s_res->accum_width  = w;
            s_res->accum_height = h;
            s_res->accum_frame_count = 0;
            s_res->accum_needs_layout_init = true;
        }

        static void update_desc_set() {
            VkDevice device = s_res->vk_ctx->get_device();

            VkWriteDescriptorSetAccelerationStructureKHR as_info{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
            as_info.accelerationStructureCount = 1;
            as_info.pAccelerationStructures    = &s_res->tlas;

            VkWriteDescriptorSet writes[2] = {};

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

            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
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
            ai.memoryTypeIndex = find_memory_type(s_res->vk_ctx->get_physical_device(), req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            vkAllocateMemory(device, &ai, nullptr, &s_res->instance_memory);
            vkBindBufferMemory(device, s_res->instance_buffer, s_res->instance_memory, 0);
            vkMapMemory(device, s_res->instance_memory, 0, size, 0, &s_res->instance_mapped);

            s_res->instance_buffer_capacity = count;
        }

        static PathTracerResources::BlasEntry build_blas(const Mesh* mesh) {
            HN_PROFILE_FUNCTION();

            VkDevice device = s_res->vk_ctx->get_device();
            VkPhysicalDevice physical_device = s_res->vk_ctx->get_physical_device();

            HN_CORE_ASSERT(mesh->meshlet_buffers.has_value(), "Meshlet buffers not loaded");
            auto& bufs = mesh->meshlet_buffers.value();
            VkBuffer vk_vbuf = reinterpret_cast<VkBuffer>(bufs.vertex_buffer->get_native_buffer());
            VkBuffer vk_ibuf = reinterpret_cast<VkBuffer>(bufs.flat_index_buffer->get_native_buffer());
            uint32_t tri_count = bufs.flat_index_count / 3;

            VkBufferDeviceAddressInfo addr_info = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            addr_info.buffer = vk_vbuf;
            VkDeviceAddress vbuf_addr = s_res->fn_get_buf_addr(device, &addr_info);
            addr_info.buffer = vk_ibuf;
            VkDeviceAddress ibuf_addr = s_res->fn_get_buf_addr(device, &addr_info);

            VkAccelerationStructureGeometryTrianglesDataKHR triangles = {};
            triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            triangles.vertexData.deviceAddress = vbuf_addr;
            triangles.vertexStride = 56; // sizeof(VertexPBR) from gltf_loader.cpp
            triangles.maxVertex = tri_count * 3 - 1; // TODO: this is an overestimate which *should* not corrupt anything, but is wrong.
            triangles.indexType = VK_INDEX_TYPE_UINT32;
            triangles.indexData.deviceAddress = ibuf_addr;

            VkAccelerationStructureGeometryKHR geometry = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.geometry.triangles = triangles;
            geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

            VkAccelerationStructureBuildGeometryInfoKHR build_info{};
            build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            build_info.geometryCount = 1;
            build_info.pGeometries = &geometry;

            VkAccelerationStructureBuildSizesInfoKHR sizes = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
            s_res->fn_get_as_sizes(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &tri_count, &sizes);

            // Allocate output buffer and create AS handle
            PathTracerResources::BlasEntry entry{};

            {
                VkBufferCreateInfo bi{};
                bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bi.size        = sizes.accelerationStructureSize;
                bi.usage       = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                               | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                VkResult r = vkCreateBuffer(device, &bi, nullptr, &entry.buffer);
                HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateBuffer failed (BLAS)");

                VkMemoryRequirements req{};
                vkGetBufferMemoryRequirements(device, entry.buffer, &req);

                VkMemoryAllocateFlagsInfo flags_info{};
                flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
                flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

                VkMemoryAllocateInfo ai{};
                ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                ai.pNext           = &flags_info;
                ai.allocationSize  = req.size;
                ai.memoryTypeIndex = find_memory_type(physical_device, req.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                r = vkAllocateMemory(device, &ai, nullptr, &entry.memory);
                HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateMemory failed (BLAS)");
                r = vkBindBufferMemory(device, entry.buffer, entry.memory, 0);
                HN_CORE_ASSERT(r == VK_SUCCESS, "vkBindBufferMemory failed (BLAS)");
            }

            VkAccelerationStructureCreateInfoKHR ci{};
            ci.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            ci.buffer = entry.buffer;
            ci.size   = sizes.accelerationStructureSize;
            ci.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            VkResult r = s_res->fn_create_as(device, &ci, nullptr, &entry.handle);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateAccelerationStructureKHR failed");

            VkBuffer scratch_buffer = VK_NULL_HANDLE;
            VkDeviceMemory scratch_memory = VK_NULL_HANDLE;
            {
                VkBufferCreateInfo bi{};
                bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bi.size        = sizes.buildScratchSize;
                bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                               | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

                VkResult br = vkCreateBuffer(device, &bi, nullptr, &scratch_buffer);
                HN_CORE_ASSERT(br == VK_SUCCESS, "vkCreateBuffer failed (BLAS scratch)");

                VkMemoryRequirements req{};
                vkGetBufferMemoryRequirements(device, scratch_buffer, &req);

                VkMemoryAllocateFlagsInfo flags_info{};
                flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
                flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

                VkMemoryAllocateInfo ai{};
                ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                ai.pNext           = &flags_info;
                ai.allocationSize  = req.size;
                ai.memoryTypeIndex = find_memory_type(physical_device, req.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                br = vkAllocateMemory(device, &ai, nullptr, &scratch_memory);
                HN_CORE_ASSERT(br == VK_SUCCESS, "vkAllocateMemory failed (BLAS scratch)");
                br = vkBindBufferMemory(device, scratch_buffer, scratch_memory, 0);
                HN_CORE_ASSERT(br == VK_SUCCESS, "vkBindBufferMemory failed (BLAS scratch)");
            }

            VkBufferDeviceAddressInfo scratch_addr_info{};
            scratch_addr_info.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            scratch_addr_info.buffer = scratch_buffer;
            VkDeviceAddress scratch_addr = s_res->fn_get_buf_addr(device, &scratch_addr_info);

            s_res->vk_ctx->submit_one_time_compute([&](VkCommandBuffer cmd) {
                build_info.mode                     = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
                build_info.dstAccelerationStructure = entry.handle;
                build_info.scratchData.deviceAddress = scratch_addr;

                VkAccelerationStructureBuildRangeInfoKHR range{};
                range.primitiveCount = tri_count;
                const VkAccelerationStructureBuildRangeInfoKHR* range_ptr = &range;
                s_res->fn_cmd_build_as(cmd, 1, &build_info, &range_ptr);

                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    0, 1, &barrier, 0, nullptr, 0, nullptr);
            });

            vkDestroyBuffer(device, scratch_buffer, nullptr);
            vkFreeMemory(device, scratch_memory, nullptr);

            VkAccelerationStructureDeviceAddressInfoKHR as_addr{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
            as_addr.accelerationStructure = entry.handle;
            entry.device_address = s_res->fn_get_as_addr(device, &as_addr);

            return entry;
        }

        // Returns false if no instances were found (empty scene).
        static bool build_tlas(Scene* scene) {
            VkDevice device = s_res->vk_ctx->get_device();
            VkPhysicalDevice physical_device = s_res->vk_ctx->get_physical_device();

            std::vector<VkAccelerationStructureInstanceKHR> instances;

            auto view = scene->get_registry().view<TransformComponent, MeshRendererComponent>();
            for (auto entity : view) {
                auto& mrc = view.get<MeshRendererComponent>(entity);
                auto& tc  = view.get<TransformComponent>(entity);

                if (!mrc.mesh || !mrc.mesh->meshlet_buffers.has_value())
                    continue;
                if (!mrc.mesh->meshlet_buffers->flat_index_buffer || !mrc.mesh->meshlet_buffers->vertex_buffer)
                    continue;

                const Mesh* mesh = mrc.mesh.get();

                if (!s_res->blas_cache.count(mesh))
                    s_res->blas_cache[mesh] = build_blas(mesh);

                auto& blas = s_res->blas_cache.at(mesh);

                VkAccelerationStructureInstanceKHR inst{};
                inst.transform                              = to_vk_transform(tc.world);
                inst.instanceCustomIndex                    = 0; // expand later for materials
                inst.mask                                   = 0xFF;
                inst.instanceShaderBindingTableRecordOffset = 0;
                inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                inst.accelerationStructureReference         = blas.device_address;

                instances.push_back(inst);
            }

            if (instances.empty()) {
                static bool warned = false;
                if (!warned) {
                    HN_CORE_WARN("[PathTracer] build_tlas: no renderable instances — scene has no meshes with meshlet+flat_index buffers");
                    warned = true;
                }
                return false;
            }

            ensure_instance_buffer((uint32_t)instances.size());
            memcpy(s_res->instance_mapped, instances.data(),
                   instances.size() * sizeof(VkAccelerationStructureInstanceKHR));

            VkBufferDeviceAddressInfo addr_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            addr_info.buffer = s_res->instance_buffer;
            VkDeviceAddress instances_addr = s_res->fn_get_buf_addr(device, &addr_info);

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
            build_info.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            build_info.geometryCount = 1;
            build_info.pGeometries   = &geometry;

            uint32_t prim_count = (uint32_t)instances.size();
            VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
            s_res->fn_get_as_sizes(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                   &build_info, &prim_count, &sizes);

            if (s_res->tlas != VK_NULL_HANDLE) {
                s_res->fn_destroy_as(device, s_res->tlas, nullptr);
                vkDestroyBuffer(device, s_res->tlas_buffer, nullptr);
                vkFreeMemory(device, s_res->tlas_memory, nullptr);
                s_res->tlas = VK_NULL_HANDLE;
            }

            {
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
                ai.memoryTypeIndex = find_memory_type(physical_device, req.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                vkAllocateMemory(device, &ai, nullptr, &s_res->tlas_memory);
                vkBindBufferMemory(device, s_res->tlas_buffer, s_res->tlas_memory, 0);
            }

            VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
            ci.buffer = s_res->tlas_buffer;
            ci.size   = sizes.accelerationStructureSize;
            ci.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            s_res->fn_create_as(device, &ci, nullptr, &s_res->tlas);

            VkBuffer scratch_buffer = VK_NULL_HANDLE;
            VkDeviceMemory scratch_memory = VK_NULL_HANDLE;
            {
                VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                bi.size        = sizes.buildScratchSize;
                bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                               | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                vkCreateBuffer(device, &bi, nullptr, &scratch_buffer);

                VkMemoryRequirements req{};
                vkGetBufferMemoryRequirements(device, scratch_buffer, &req);

                VkMemoryAllocateFlagsInfo flags_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
                flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

                VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                ai.pNext           = &flags_info;
                ai.allocationSize  = req.size;
                ai.memoryTypeIndex = find_memory_type(physical_device, req.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                vkAllocateMemory(device, &ai, nullptr, &scratch_memory);
                vkBindBufferMemory(device, scratch_buffer, scratch_memory, 0);
            }

            VkBufferDeviceAddressInfo scratch_addr_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            scratch_addr_info.buffer = scratch_buffer;
            VkDeviceAddress scratch_addr = s_res->fn_get_buf_addr(device, &scratch_addr_info);

            s_res->vk_ctx->submit_one_time_compute([&](VkCommandBuffer cmd) {
                build_info.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
                build_info.dstAccelerationStructure  = s_res->tlas;
                build_info.scratchData.deviceAddress = scratch_addr;

                VkAccelerationStructureBuildRangeInfoKHR range{};
                range.primitiveCount = prim_count;
                const VkAccelerationStructureBuildRangeInfoKHR* range_ptr = &range;
                s_res->fn_cmd_build_as(cmd, 1, &build_info, &range_ptr);

                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    0, 1, &barrier, 0, nullptr, 0, nullptr);
            });

            vkDestroyBuffer(device, scratch_buffer, nullptr);
            vkFreeMemory(device, scratch_memory, nullptr);

            return true;
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
            uint32_t sbt_total   = region_size * 3;

            VkDescriptorSetLayoutBinding bindings[2] = {};
            bindings[0].binding         = 0;
            bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            bindings[1].binding         = 1;
            bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

            VkDescriptorSetLayoutCreateInfo layout_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            layout_ci.bindingCount = 2;
            layout_ci.pBindings    = bindings;
            vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &s_res->rt_desc_layout);


            // Create descriptor pool and allocate the set
            VkDescriptorPoolSize pool_sizes[2] = {
                { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
                { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
            };
            VkDescriptorPoolCreateInfo pool_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            pool_ci.maxSets       = 1;
            pool_ci.poolSizeCount = 2;
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
            VkShaderModule raygen_module = make_module(ShaderCompiler::read_file(
                (assets_dir / "shaders" / "PathTrace_RayGen.rgen")),  ShaderCompiler::ShaderStage::RayGen,    "PathTrace_RayGen.rgen");
            VkShaderModule miss_module   = make_module(ShaderCompiler::read_file(
                (assets_dir / "shaders" / "PathTrace_Miss.rmiss")),   ShaderCompiler::ShaderStage::Miss,       "PathTrace_Miss.rmiss");
            VkShaderModule hit_module    = make_module(ShaderCompiler::read_file(
                (assets_dir / "shaders" / "PathTrace_ClosestHit.rchit")), ShaderCompiler::ShaderStage::ClosestHit, "PathTrace_ClosestHit.rchit");

            if (!compile_ok) {
                HN_CORE_ERROR("[PathTracer] Shader compilation failed — RT pipeline will not be built");
                if (raygen_module) vkDestroyShaderModule(device, raygen_module, nullptr);
                if (miss_module)   vkDestroyShaderModule(device, miss_module,   nullptr);
                if (hit_module)    vkDestroyShaderModule(device, hit_module,    nullptr);
                return;
            }


            // Create the RT pipeline
            VkPipelineShaderStageCreateInfo stages[3] = {};
            stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            stages[0].module = raygen_module;
            stages[0].pName  = "main";
            stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
            stages[1].module = miss_module;
            stages[1].pName  = "main";
            stages[2].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[2].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            stages[2].module = hit_module;
            stages[2].pName  = "main";

            VkRayTracingShaderGroupCreateInfoKHR groups[3] = {};
            // group 0: raygen (general type, stage 0)
            groups[0] = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
            groups[0].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            groups[0].generalShader    = 0;
            groups[0].closestHitShader = groups[0].anyHitShader = groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

            // group 1: miss (general type, stage 1)
            groups[1] = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
            groups[1].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            groups[1].generalShader    = 1;
            groups[1].closestHitShader = groups[1].anyHitShader = groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

            // group 2: closest hit (triangles type, no generalShader)
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
            rt_ci.layout                       = s_res->rt_pipeline_layout;

            VkResult r = s_res->fn_create_rt_pipeline(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rt_ci, nullptr, &s_res->rt_pipeline);

            vkDestroyShaderModule(device, raygen_module, nullptr);
            vkDestroyShaderModule(device, miss_module, nullptr);
            vkDestroyShaderModule(device, hit_module, nullptr);

            if (r != VK_SUCCESS) {
                HN_CORE_ERROR("[PathTracer] vkCreateRayTracingPipelinesKHR failed ({})", (int)r);
                return;
            }

            // Retrieve shader handles from pipeline
            std::vector<uint8_t> handles(handle_size * 3);
            r = s_res->fn_get_sbt_handles(device, s_res->rt_pipeline, 0, 3, handle_size * 3, handles.data());
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
                ai.memoryTypeIndex = find_memory_type(physical_device, req.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                vkAllocateMemory(device, &ai, nullptr, &s_res->sbt_memory);
                vkBindBufferMemory(device, s_res->sbt_buffer, s_res->sbt_memory, 0);
            }


            // Copy handles into correct offsets
            void* mapped = nullptr;
            vkMapMemory(device, s_res->sbt_memory, 0, sbt_total, 0, &mapped);
            uint8_t* dst = static_cast<uint8_t*>(mapped);
            memcpy(dst + 0 * region_size, handles.data() + 0 * handle_size, handle_size);
            memcpy(dst + 1 * region_size, handles.data() + 1 * handle_size, handle_size);
            memcpy(dst + 2 * region_size, handles.data() + 2 * handle_size, handle_size);
            vkUnmapMemory(device, s_res->sbt_memory);

            VkBufferDeviceAddressInfo sbt_addr_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            sbt_addr_info.buffer = s_res->sbt_buffer;
            VkDeviceAddress sbt_base = s_res->fn_get_buf_addr(device, &sbt_addr_info);

            s_res->sbt_raygen = { sbt_base + 0 * region_size, region_size, region_size };
            s_res->sbt_miss   = { sbt_base + 1 * region_size, region_size, region_size };
            s_res->sbt_hit    = { sbt_base + 2 * region_size, region_size, region_size };

            s_res->pipeline_built = true;
            HN_CORE_INFO("[PathTracer] RT pipeline built successfully");
        }

    } // anonymous namespace

    void Renderer3DPathTracer::init(VulkanContext* ctx) {
        if (!s_res)
            s_res = new PathTracerResources{};

        s_res->vk_ctx = ctx;

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

        build_rt_pipeline();
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

        if (s_res->tlas != VK_NULL_HANDLE) {
            s_res->fn_destroy_as(device, s_res->tlas, nullptr);
            vkDestroyBuffer(device, s_res->tlas_buffer, nullptr);
            vkFreeMemory(device, s_res->tlas_memory, nullptr);
        }

        if (s_res->instance_buffer != VK_NULL_HANDLE) {
            vkUnmapMemory(device, s_res->instance_memory);
            vkDestroyBuffer(device, s_res->instance_buffer, nullptr);
            vkFreeMemory(device, s_res->instance_memory, nullptr);
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

        if (s_res->tlas != VK_NULL_HANDLE) {
            s_res->fn_destroy_as(device, s_res->tlas, nullptr);
            vkDestroyBuffer(device, s_res->tlas_buffer, nullptr);
            vkFreeMemory(device, s_res->tlas_memory, nullptr);
            s_res->tlas        = VK_NULL_HANDLE;
            s_res->tlas_buffer = VK_NULL_HANDLE;
            s_res->tlas_memory = VK_NULL_HANDLE;
        }

        destroy_accum_image();
        s_res->accum_width  = 0;
        s_res->accum_height = 0;
    }

    void Renderer3DPathTracer::register_frame_graph_executors() {
        auto& registry = FrameGraphRegistry::get();

        // Single pass: trace rays into accum_image (compute), then blit to editorViewport (graphics).
        // Both submits block on the GPU so ordering is guaranteed.
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

            if (!build_tlas(scene))
                return;

            update_desc_set();

            glm::mat4 inv_view    = s_res->inv_view;
            glm::mat4 inv_proj    = s_res->inv_proj;
            uint32_t  frame_count = s_res->accum_frame_count;
            uint32_t  trace_w     = s_res->accum_width;
            uint32_t  trace_h     = s_res->accum_height;
            bool      need_init   = s_res->accum_needs_layout_init;

            // --- Ray trace into accum_image ---
            ctx.submit_vulkan_compute([inv_view, inv_proj, frame_count, trace_w, trace_h, need_init](VkCommandBuffer cmd) {
                if (need_init) {
                    VkImageMemoryBarrier imb{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                    imb.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
                    imb.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
                    imb.image            = s_res->accum_image;
                    imb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    imb.srcAccessMask    = 0;
                    imb.dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
                    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                        0, 0, nullptr, 0, nullptr, 1, &imb);
                }

                struct PC { glm::mat4 inv_view; glm::mat4 inv_proj; uint32_t frame_count; };
                PC pc{ inv_view, inv_proj, frame_count };

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, s_res->rt_pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                    s_res->rt_pipeline_layout, 0, 1, &s_res->rt_desc_set, 0, nullptr);
                vkCmdPushConstants(cmd, s_res->rt_pipeline_layout,
                    VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(PC), &pc);

                s_res->fn_cmd_trace_rays(cmd,
                    &s_res->sbt_raygen, &s_res->sbt_miss, &s_res->sbt_hit, &s_res->sbt_callable,
                    trace_w, trace_h, 1);
            });

            s_res->accum_frame_count++;
            s_res->accum_needs_layout_init = false;

            // --- Blit accum_image → editorViewport ---
            ctx.submit_vulkan_graphics_raw([dst_image, trace_w, trace_h](VkCommandBuffer cmd) {
                // accum_image GENERAL → TRANSFER_SRC_OPTIMAL
                VkImageMemoryBarrier src_bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                src_bar.oldLayout        = VK_IMAGE_LAYOUT_GENERAL;
                src_bar.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                src_bar.image            = s_res->accum_image;
                src_bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                src_bar.srcAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
                src_bar.dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT;

                // editorViewport UNDEFINED → TRANSFER_DST_OPTIMAL (discard old contents)
                VkImageMemoryBarrier dst_bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                dst_bar.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
                dst_bar.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                dst_bar.image            = dst_image;
                dst_bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                dst_bar.srcAccessMask    = 0;
                dst_bar.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;

                VkImageMemoryBarrier pre[2] = { src_bar, dst_bar };
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 2, pre);

                VkImageBlit blit{};
                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                blit.srcOffsets[1]  = {(int32_t)trace_w, (int32_t)trace_h, 1};
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                blit.dstOffsets[1]  = {(int32_t)trace_w, (int32_t)trace_h, 1};
                vkCmdBlitImage(cmd,
                    s_res->accum_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    dst_image,          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1, &blit, VK_FILTER_NEAREST);

                // accum_image TRANSFER_SRC_OPTIMAL → GENERAL for next frame's write
                src_bar.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                src_bar.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
                src_bar.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                src_bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

                // editorViewport TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL for ImGui
                dst_bar.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                dst_bar.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                dst_bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                dst_bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                VkImageMemoryBarrier post[2] = { src_bar, dst_bar };
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 2, post);
            });
        });
    }

} // namespace Honey
