#include "hnpch.h"
#include "vk_renderer_globals.h"

#include "vk_backend.h"
#include "vk_descriptor_heap.h"
#include "vk_utils.h"

namespace Honey {

    namespace {
        CameraUBO make_gpu_camera(const CameraUBO& src) {
            glm::mat4 correction(1.0f);
            correction[1][1] = -1.0f;
            correction[2][2] = 0.5f;
            correction[3][2] = 0.5f;

            CameraUBO gpu_camera{};
            gpu_camera.view_proj = correction * src.view_proj;
            gpu_camera.position = src.position;
            gpu_camera.exposure = src.exposure;
            gpu_camera.inv_view_proj = glm::inverse(gpu_camera.view_proj);
            gpu_camera.view = src.view;
            gpu_camera.projection = correction * src.projection;
            gpu_camera.inv_projection = glm::inverse(gpu_camera.projection);
            return gpu_camera;
        }
    } // anonymous namespace

    void VulkanRendererGlobals::init(VulkanBackend* backend) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(backend, "VulkanRendererGlobals::init: backend is null");
        HN_CORE_ASSERT(!m_backend, "VulkanRendererGlobals::init called twice — this object has device lifetime");

        m_backend         = backend;
        m_device          = backend->get_device();
        m_physical_device = backend->get_physical_device();
        HN_CORE_ASSERT(m_device && m_physical_device, "VulkanRendererGlobals::init called without a device");

        VkPhysicalDeviceProperties physical_device_properties{};
        vkGetPhysicalDeviceProperties(m_physical_device, &physical_device_properties);
        m_globals_layout = GlobalsLayout::build(physical_device_properties.limits);

        create_materials_resources();
        create_globals_resources();
        register_heap_bindings();
    }

    void VulkanRendererGlobals::create_globals_resources() {
        const VkDevice dev = m_device;

        // Device-local: the GPU-visible copy is written only by the per-frame vkCmdCopyBuffer in
        // begin_frame(). Making it host-visible is what caused the CSM shadow wiggle — producers
        // memcpy'd frame N's data over bytes the GPU was still reading for frame N-1.
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = m_globals_layout.total_size;
        bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult res = vkCreateBuffer(dev, &bi, nullptr, &m_globals_buffer);
        HN_CORE_ASSERT(res == VK_SUCCESS, "[VulkanRendererGlobals] Failed to create globals buffer");

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(dev, m_globals_buffer, &req);

        VkMemoryAllocateFlagsInfo flags{};
        flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.pNext = &flags;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = VulkanUtils::find_memory_type(m_physical_device, req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        res = vkAllocateMemory(dev, &ai, nullptr, &m_globals_alloc);
        HN_CORE_ASSERT(res == VK_SUCCESS, "[VulkanRendererGlobals] Failed to allocate memory for globals heap");
        vkBindBufferMemory(dev, m_globals_buffer, m_globals_alloc, 0);

        // Authoritative host-side state. Producers (flush_pending, set_shadow_matrices,
        // set_directional_shadows) write here; it is snapshotted into the current frame's
        // staging buffer at end of recording.
        //
        // Zero-initialized because globals whose producer doesn't run every frame
        // (ShadowMatrices/DirShadow when a scene has no shadow casters) would otherwise upload
        // garbage — a non-zero shadow_light_count/enabled drives the lighting shader's shadow
        // loops into garbage iteration counts. Start clean so an un-written region means
        // "no shadows".
        m_globals_cpu.assign(m_globals_layout.total_size, 0);

        for (uint32_t f = 0; f < VulkanContext::k_max_frames_in_flight; ++f) {
            VkBufferCreateInfo sbi{};
            sbi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            sbi.size        = m_globals_layout.total_size;
            sbi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            sbi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            res = vkCreateBuffer(dev, &sbi, nullptr, &m_globals_staging[f]);
            HN_CORE_ASSERT(res == VK_SUCCESS, "[VulkanRendererGlobals] Failed to create globals staging buffer");

            VkMemoryRequirements sreq{};
            vkGetBufferMemoryRequirements(dev, m_globals_staging[f], &sreq);

            VkMemoryAllocateInfo sai{};
            sai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            sai.allocationSize  = sreq.size;
            sai.memoryTypeIndex = VulkanUtils::find_memory_type(m_physical_device, sreq.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            res = vkAllocateMemory(dev, &sai, nullptr, &m_globals_staging_alloc[f]);
            HN_CORE_ASSERT(res == VK_SUCCESS, "[VulkanRendererGlobals] Failed to allocate globals staging memory");
            vkBindBufferMemory(dev, m_globals_staging[f], m_globals_staging_alloc[f], 0);

            void* smapped = nullptr;
            vkMapMemory(dev, m_globals_staging_alloc[f], 0, m_globals_layout.total_size, 0, &smapped);
            m_globals_staging_mapped[f] = static_cast<uint8_t*>(smapped);
            std::memset(m_globals_staging_mapped[f], 0, m_globals_layout.total_size);
        }
    }

    void VulkanRendererGlobals::register_heap_bindings() {
        VkBufferDeviceAddressInfo addr{};
        addr.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addr.buffer = m_globals_buffer;
        VkDeviceAddress base = vkGetBufferDeviceAddress(m_device, &addr);

        VkBufferDeviceAddressInfo mat_addr{};
        mat_addr.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        mat_addr.buffer = m_materials_buffer;
        const VkDeviceAddress materials_base = vkGetBufferDeviceAddress(m_device, &mat_addr);

        auto* heap = m_backend->get_descriptor_heap();
        for (const auto& g : k_global_bindings) {
            // Materials: its bytes live in their own buffer rather than in the globals buffer, but
            // the slot is written here, once, and never again. A per-frame rewrite of a persistent
            // heap slot is a host write to memory the GPU may be reading for a frame still in
            // flight — the bug this migration exists to remove.
            if (g.kind == GlobalBufferKind::ExternalStorage) {
                if (g.id == GlobalBinding::Materials) {
                    auto slot = heap->allocate_persistent_resource(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
                    heap->write_buffer(slot, 0, materials_base, k_materials_capacity_bytes,
                                       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                    heap->register_global_binding(g.shader_binding, slot);
                }
                continue;
            }
            const VkDescriptorType dt = (g.kind == GlobalBufferKind::Uniform)
                ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

            auto slot = heap->allocate_persistent_resource(dt, 1);
            heap->write_buffer(slot, 0, base + m_globals_layout.offset[(size_t)g.id], g.size, dt);
            heap->register_global_binding(g.shader_binding, slot);
        }
    }

    void VulkanRendererGlobals::create_materials_resources() {
        HN_PROFILE_FUNCTION();

        const VkDevice dev = m_device;

        // Device-local, single buffer — not per-frame. Staging plus the barrier pair around the
        // copy make one buffer safe, and it removes the inherited hazard where a frame that never
        // called submit_materials presented a frame-slot buffer two frames stale.
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = k_materials_capacity_bytes;
        bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult res = vkCreateBuffer(dev, &bi, nullptr, &m_materials_buffer);
        HN_CORE_ASSERT(res == VK_SUCCESS, "[VulkanRendererGlobals] Failed to create materials buffer");

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(dev, m_materials_buffer, &req);

        VkMemoryAllocateFlagsInfo flags{};
        flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.pNext = &flags;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = VulkanUtils::find_memory_type(m_physical_device, req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        res = vkAllocateMemory(dev, &ai, nullptr, &m_materials_alloc);
        HN_CORE_ASSERT(res == VK_SUCCESS, "[VulkanRendererGlobals] Failed to allocate materials memory");
        vkBindBufferMemory(dev, m_materials_buffer, m_materials_alloc, 0);

        // Authoritative host-side copy. Seeded with default-constructed GPUMaterials rather than
        // zeros: a zeroed material is black with texture ids 0, and 0 is a *valid* index into the
        // bindless table, so an un-uploaded slot would sample an unrelated texture. The defaulted
        // struct is white with every tex id -1, i.e. inert.
        m_materials_cpu.resize(k_materials_capacity_bytes);
        const GPUMaterial default_material{};
        for (uint32_t i = 0; i < k_max_material_count; ++i)
            std::memcpy(m_materials_cpu.data() + i * sizeof(GPUMaterial),
                        &default_material, sizeof(GPUMaterial));

        for (uint32_t f = 0; f < VulkanContext::k_max_frames_in_flight; ++f) {
            VkBufferCreateInfo sbi{};
            sbi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            sbi.size        = k_materials_capacity_bytes;
            sbi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            sbi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            res = vkCreateBuffer(dev, &sbi, nullptr, &m_materials_staging[f]);
            HN_CORE_ASSERT(res == VK_SUCCESS, "[VulkanRendererGlobals] Failed to create materials staging buffer");

            VkMemoryRequirements sreq{};
            vkGetBufferMemoryRequirements(dev, m_materials_staging[f], &sreq);

            VkMemoryAllocateInfo sai{};
            sai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            sai.allocationSize  = sreq.size;
            sai.memoryTypeIndex = VulkanUtils::find_memory_type(m_physical_device, sreq.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            res = vkAllocateMemory(dev, &sai, nullptr, &m_materials_staging_alloc[f]);
            HN_CORE_ASSERT(res == VK_SUCCESS, "[VulkanRendererGlobals] Failed to allocate materials staging memory");
            vkBindBufferMemory(dev, m_materials_staging[f], m_materials_staging_alloc[f], 0);

            void* smapped = nullptr;
            vkMapMemory(dev, m_materials_staging_alloc[f], 0, k_materials_capacity_bytes, 0, &smapped);
            m_materials_staging_mapped[f] = static_cast<uint8_t*>(smapped);
            std::memcpy(m_materials_staging_mapped[f], m_materials_cpu.data(), k_materials_capacity_bytes);
        }
    }

    void VulkanRendererGlobals::cleanup_materials_resources() {
        HN_PROFILE_FUNCTION();
        if (!m_device) return;

        for (uint32_t f = 0; f < VulkanContext::k_max_frames_in_flight; ++f) {
            if (!m_materials_staging[f]) continue;
            vkUnmapMemory(m_device, m_materials_staging_alloc[f]);
            vkDestroyBuffer(m_device, m_materials_staging[f], nullptr);
            vkFreeMemory(m_device, m_materials_staging_alloc[f], nullptr);
            m_materials_staging[f]        = nullptr;
            m_materials_staging_alloc[f]  = nullptr;
            m_materials_staging_mapped[f] = nullptr;
        }

        if (m_materials_buffer) {
            vkDestroyBuffer(m_device, m_materials_buffer, nullptr);
            vkFreeMemory(m_device, m_materials_alloc, nullptr);
            m_materials_buffer = nullptr;
            m_materials_alloc  = nullptr;
        }
        m_materials_cpu.clear();
        m_materials_hwm = 0;
    }

    void VulkanRendererGlobals::shutdown() {
        HN_PROFILE_FUNCTION();
        if (!m_device) return;

        cleanup_materials_resources();

        for (uint32_t f = 0; f < VulkanContext::k_max_frames_in_flight; ++f) {
            if (!m_globals_staging[f]) continue;
            vkUnmapMemory(m_device, m_globals_staging_alloc[f]);
            vkDestroyBuffer(m_device, m_globals_staging[f], nullptr);
            vkFreeMemory(m_device, m_globals_staging_alloc[f], nullptr);
            m_globals_staging[f]        = nullptr;
            m_globals_staging_alloc[f]  = nullptr;
            m_globals_staging_mapped[f] = nullptr;
        }

        if (m_globals_buffer) {
            vkDestroyBuffer(m_device, m_globals_buffer, nullptr);
            vkFreeMemory(m_device, m_globals_alloc, nullptr);
            m_globals_buffer = nullptr;
            m_globals_alloc  = nullptr;
        }
        m_globals_cpu.clear();

        m_device          = VK_NULL_HANDLE;
        m_physical_device = VK_NULL_HANDLE;
        m_backend         = nullptr;
    }

    void VulkanRendererGlobals::begin_frame(VkCommandBuffer cmd, uint32_t frame) {
        HN_CORE_ASSERT(frame < VulkanContext::k_max_frames_in_flight,
                       "VulkanRendererGlobals::begin_frame: frame index out of range");
        HN_CORE_ASSERT(m_globals_buffer && m_globals_layout.total_size > 0,
                       "VulkanRendererGlobals::begin_frame called before init()");
        HN_CORE_ASSERT(m_globals_staging[frame],
                       "VulkanRendererGlobals::begin_frame: staging buffer missing for frame {0}", frame);
        HN_CORE_ASSERT(!m_snapshot_pending,
                       "VulkanRendererGlobals: previous frame was never snapshotted — Renderer::end_frame() "
                       "must run before the next Renderer::begin_frame()");
        m_snapshot_pending = true;

        // Fix this frame's materials copy region *now*, while we are outside any render pass and
        // can still record a transfer. Producers only supply materials later, from inside the
        // g-buffer pass, so the region has to be a bound rather than a description of what
        // changed — see the m_materials_hwm comment in the header. snapshot() reads this back so
        // it stages exactly the range recorded here.
        const uint32_t materials_elems =
            m_materials_initial_upload ? k_max_material_count : m_materials_hwm;
        m_materials_upload_elems[frame] = materials_elems;
        m_materials_initial_upload = false;

        // The copies are recorded here, at the top of the frame, but execute after submit — by
        // which point snapshot() has filled this frame's staging buffers. Recording order is not
        // host-write timing.

        // Wait for the previous frame's shader reads of the globals before overwriting them. The
        // first synchronization scope covers everything earlier in submission order on this queue,
        // which is what orders this against frame N-1 (all graphics submits serialize under the
        // backend's shared graphics mutex). ALL_COMMANDS is deliberate: globals are read from
        // vertex, fragment, compute, task, mesh and ray-tracing stages, and this runs once a frame.
        VkMemoryBarrier pre{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        pre.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
        pre.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 1, &pre, 0, nullptr, 0, nullptr);

        VkBufferCopy region{};
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size      = m_globals_layout.total_size;
        vkCmdCopyBuffer(cmd, m_globals_staging[frame], m_globals_buffer, 1, &region);

        // Materials ride the same barrier pair rather than getting one of their own: the barriers
        // above and below are global VkMemoryBarriers, so they already cover this buffer. One
        // pre-barrier, two copies, one post-barrier.
        if (materials_elems > 0) {
            VkBufferCopy mat_region{};
            mat_region.srcOffset = 0;
            mat_region.dstOffset = 0;
            mat_region.size      = (VkDeviceSize)materials_elems * sizeof(GPUMaterial);
            vkCmdCopyBuffer(cmd, m_materials_staging[frame], m_materials_buffer, 1, &mat_region);
        }

        VkMemoryBarrier post{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        post.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        post.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 1, &post, 0, nullptr, 0, nullptr);

        m_pending_globals.reset();
    }

    void VulkanRendererGlobals::snapshot(uint32_t frame) {
        HN_CORE_ASSERT(frame < VulkanContext::k_max_frames_in_flight,
                       "VulkanRendererGlobals::snapshot: frame index out of range");
        HN_CORE_ASSERT(m_snapshot_pending,
                       "VulkanRendererGlobals::snapshot without a matching begin_frame()");
        HN_CORE_ASSERT(!m_globals_cpu.empty() && m_globals_staging_mapped[frame],
                       "VulkanRendererGlobals::snapshot called before init()");
        HN_CORE_ASSERT(m_materials_staging_mapped[frame],
                       "VulkanRendererGlobals::snapshot: materials staging missing for frame {0}", frame);
        m_snapshot_pending = false;

        // Snapshot the host-side globals into this frame's staging buffer. Safe to write
        // unsynchronized: the fence wait in VulkanContext::begin_frame_recording() guarantees the
        // GPU finished with m_globals_staging[frame] before this frame started recording. The copy
        // command recorded at the top of the frame reads it at execution time, i.e. after submit.
        std::memcpy(m_globals_staging_mapped[frame],
                    m_globals_cpu.data(), m_globals_layout.total_size);

        // Same argument for materials, over exactly the range begin_frame() recorded for this
        // frame — staging bytes and copy region are kept in lockstep by m_materials_upload_elems,
        // and the m_snapshot_pending assert above guarantees begin_frame() set it.
        const uint32_t materials_bytes = m_materials_upload_elems[frame] * (uint32_t)sizeof(GPUMaterial);
        if (materials_bytes > 0)
            std::memcpy(m_materials_staging_mapped[frame], m_materials_cpu.data(), materials_bytes);
    }

    void VulkanRendererGlobals::flush_pending(uint32_t frame) {
        HN_CORE_ASSERT(frame < VulkanContext::k_max_frames_in_flight,
                       "VulkanRendererGlobals::flush_pending: frame index out of range");
        HN_CORE_ASSERT(!m_globals_cpu.empty(), "VulkanRendererGlobals::flush_pending called before init()");

        auto& p = m_pending_globals;

        auto write = [&](GlobalBinding id, const void* src) {
            const auto& g = k_global_bindings[(size_t)id];
            std::memcpy(m_globals_cpu.data() + m_globals_layout.offset[(size_t)id], src, g.size);
        };

        // The camera needs the Vulkan clip-space correction
        const CameraUBO gpu_camera = make_gpu_camera(p.cameraUBO);
        write(GlobalBinding::Camera, &gpu_camera);
        write(GlobalBinding::Lights, &p.lightUBO);
        write(GlobalBinding::TiledLighting, &p.tiledLighting);
        // ShadowMatrices and DirShadow are NOT written here

        // Materials are host-only work here. This runs from inside the g-buffer render pass, where
        // no transfer may be recorded; the mirror is the source of truth and the upload happens at
        // the next frame top. Nothing in this function touches the device or the descriptor heap.
        if (!p.materials.empty()) {
            const uint32_t count = (uint32_t)p.materials.size();
            const uint32_t end   = p.materials_ssbo_offset + count;
            HN_CORE_ASSERT(end <= k_max_material_count,
                "VulkanRendererGlobals::flush_pending: materials range [{0}, {1}) exceeds the {2}-element "
                "capacity", p.materials_ssbo_offset, end, k_max_material_count);

            std::memcpy(m_materials_cpu.data() + (size_t)p.materials_ssbo_offset * sizeof(GPUMaterial),
                        p.materials.data(), (size_t)count * sizeof(GPUMaterial));
            m_materials_hwm = std::max(m_materials_hwm, end);
        }
    }

    void VulkanRendererGlobals::set_shadow_matrices(const ShadowMatricesSSBO& data) {
        // Heap-mode deferred lighting reads ShadowMatrices via the set-0 globals buffer
        // (k_global_bindings[ShadowMatrices]). flush_pending() intentionally skips this region,
        // so the producer writes it here at the same layout offset.
        HN_CORE_ASSERT(!m_globals_cpu.empty(), "[VulkanRendererGlobals] m_globals_cpu cannot be empty following init.");
        std::memcpy(m_globals_cpu.data() + m_globals_layout.offset[(size_t)GlobalBinding::ShadowMatrices],
            &data, sizeof(ShadowMatricesSSBO));
    }

    void VulkanRendererGlobals::set_directional_shadows(const DirectionalShadowSSBO& data) {
        // Heap-mode deferred lighting reads DirShadow via the set-0 globals buffer
        // (k_global_bindings[DirShadow]); mirror the ShadowMatrices write above.
        HN_CORE_ASSERT(!m_globals_cpu.empty(), "[VulkanRendererGlobals] m_globals_cpu cannot be empty after init.");
        std::memcpy(m_globals_cpu.data() + m_globals_layout.offset[(size_t)GlobalBinding::DirShadow],
            &data, sizeof(DirectionalShadowSSBO));
    }

}
