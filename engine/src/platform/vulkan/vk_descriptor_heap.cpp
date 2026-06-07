#include "hnpch.h"
#include "vk_descriptor_heap.h"

#include "vk_context.h"
#include "vk_utils.h"

namespace Honey {

    namespace {
        static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        static VkDeviceSize align_down(VkDeviceSize value, VkDeviceSize alignment) {
            return value & ~(alignment - 1);
        }
    }

    VulkanDescriptorHeap::VulkanDescriptorHeap(VkInstance instance, VkPhysicalDevice phys, VkDevice device)
        : m_device(device), m_phys(phys), m_instance(instance) {

        // Populate fn pointers
        m_fnWriteSamplerDescriptors = reinterpret_cast<PFN_vkWriteSamplerDescriptorsEXT>
        (vkGetDeviceProcAddr(device, "vkWriteSamplerDescriptorsEXT"));
        m_fnWriteResourceDescriptors = reinterpret_cast<PFN_vkWriteResourceDescriptorsEXT>
        (vkGetDeviceProcAddr(device, "vkWriteResourceDescriptorsEXT"));
        m_fnBindResourceHeap = reinterpret_cast<PFN_vkCmdBindResourceHeapEXT>
        (vkGetDeviceProcAddr(device, "vkCmdBindResourceHeapEXT"));
        m_fnBindSamplerHeap = reinterpret_cast<PFN_vkCmdBindSamplerHeapEXT>
        (vkGetDeviceProcAddr(device, "vkCmdBindSamplerHeapEXT"));
        m_fnPushData = reinterpret_cast<PFN_vkCmdPushDataEXT>
        (vkGetDeviceProcAddr(device, "vkCmdPushDataEXT"));

        m_fnGetDescriptorSize = reinterpret_cast<PFN_vkGetPhysicalDeviceDescriptorSizeEXT>
        (vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceDescriptorSizeEXT"));

        HN_CORE_ASSERT(m_fnWriteSamplerDescriptors && m_fnWriteResourceDescriptors
            && m_fnBindResourceHeap && m_fnBindSamplerHeap && m_fnPushData && m_fnGetDescriptorSize,
            "VulkanDescriptorHeap: missing function pointers");


        // Query device properties and sizes
        m_props = {};
        m_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;

        VkPhysicalDeviceProperties2 props{};
        props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props.pNext = &m_props;

        vkGetPhysicalDeviceProperties2(phys, &props);

        m_descriptor_sizes.sampled_image    = (uint32_t)m_fnGetDescriptorSize(phys, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        m_descriptor_sizes.storage_image    = (uint32_t)m_fnGetDescriptorSize(phys, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        m_descriptor_sizes.uniform_buffer   = (uint32_t)m_fnGetDescriptorSize(phys, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        m_descriptor_sizes.storage_buffer   = (uint32_t)m_fnGetDescriptorSize(phys, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        m_descriptor_sizes.sampler          = (uint32_t)m_fnGetDescriptorSize(phys, VK_DESCRIPTOR_TYPE_SAMPLER);

        // Compute resource-heap regions
        HN_CORE_ASSERT(m_resource_capacity <= m_props.maxResourceHeapSize,
            "[VulkanDescriptorHeap] Resource heap capacity exceeds device limits!");

        m_resource_reserved_size = align_up(m_props.minResourceHeapReservedRange, m_props.resourceHeapAlignment);
        m_resource_persistent_offset = m_resource_reserved_size;
        m_resource_persistent_size = align_up(m_resource_persistent_capacity, m_props.resourceHeapAlignment);
        m_resource_persistent_cursor = m_resource_persistent_offset;

        // Persistent residents, mapped via CONSTANT_OFFSET in heap-mode pipelines.
        m_global_ubo_alloc = allocate_persistent_resource(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);

        // Transient
        VkDeviceSize transient_persistent_offset =
            align_up(m_resource_persistent_offset + m_resource_persistent_size, m_props.resourceHeapAlignment);
        HN_CORE_ASSERT(transient_persistent_offset <= m_resource_capacity,
            "[VulkanDescriptorHeap] Transient persistent offset exceeded resource heap capacity!");

        m_transient_reserved_size = m_resource_capacity - transient_persistent_offset;
        VkDeviceSize per_frame_stride =
            align_down(m_transient_reserved_size / VulkanContext::k_max_frames_in_flight,
                m_props.resourceHeapAlignment);
        HN_CORE_ASSERT(per_frame_stride > 0, "[VulkanDescriptorHeap] Per-frame stride is zero!");

        m_frame_slots.resize(VulkanContext::k_max_frames_in_flight);
        for (uint8_t i = 0; i < VulkanContext::k_max_frames_in_flight; ++i) {
            m_frame_slots[i].begin = transient_persistent_offset + i * per_frame_stride;
            m_frame_slots[i].end = m_frame_slots[i].begin + per_frame_stride;
            m_frame_slots[i].cursor = m_frame_slots[i].begin;
        }

        // Sampler heap
        m_sampler_reserved_size = align_up(m_props.minSamplerHeapReservedRange, m_props.samplerHeapAlignment);
        m_sampler_persistent_offset = m_sampler_reserved_size;
        m_sampler_persistent_size = m_sampler_capacity - m_sampler_reserved_size;

        auto to_KiB = [](size_t bytes) { return bytes / 1024.0f; };
        HN_CORE_INFO("[VulkanDescriptorHeap] Resource heap:\n   Cap: {0} KiB, Reserved: {1} KiB, Persistent: {2} KiB, Transient: {3} KiB ({4} frames x {5} KiB)",
            to_KiB(m_resource_capacity), to_KiB(m_resource_reserved_size), to_KiB(m_resource_persistent_size),
            to_KiB(per_frame_stride * VulkanContext::k_max_frames_in_flight),
            VulkanContext::k_max_frames_in_flight, to_KiB(per_frame_stride));
        HN_CORE_INFO("[VulkanDescriptorHeap] Sampler heap:\n   Cap: {0} KiB, Reserved: {1} KiB, Persistent: {2} KiB",
            to_KiB(m_sampler_capacity), to_KiB(m_sampler_reserved_size), to_KiB(m_sampler_persistent_size));

        // Create heap buffers
        create_heap_buffer(device, phys, m_resource_capacity, m_resource_heap_buffer, m_resource_heap_memory,
            m_resource_heap_mapped, m_resource_heap_addr);

        create_heap_buffer(device, phys, m_sampler_capacity, m_sampler_heap_buffer, m_sampler_heap_memory,
            m_sampler_heap_mapped, m_sampler_heap_addr);

        VkPhysicalDeviceProperties device_props{};
        vkGetPhysicalDeviceProperties(phys, &device_props);
        bake_static_samplers(device_props.limits.maxSamplerAnisotropy);
    }

    VulkanDescriptorHeap::~VulkanDescriptorHeap() {
    }

    VulkanDescriptorHeap::Allocation VulkanDescriptorHeap::allocate_transient_resource(VkDescriptorType type,
        uint32_t count) {

        const uint32_t stride = stride_for(type);
        HN_CORE_ASSERT(stride > 0, "[VulkanDescriptorHeap] Stride cannot be 0");

        auto& slot = m_frame_slots[m_current_frame];

        VkDeviceSize aligned = align_up(slot.cursor, stride);
        VkDeviceSize bytes = (VkDeviceSize)stride * count;
        HN_CORE_ASSERT(aligned + bytes <= slot.end, "[VulkanDescriptorHeap] Transient heap slot overflow");

        slot.cursor = aligned + bytes;

        // High-water mark
        VkDeviceSize used = slot.cursor - slot.begin;
        if (used > m_resource_max_size_reached) m_resource_max_size_reached = used;

        return Allocation{ (uint32_t)aligned, (uint32_t)bytes, stride };
    }

    VulkanDescriptorHeap::Allocation VulkanDescriptorHeap::allocate_transient_bytes(uint32_t size, uint32_t align) {
        HN_CORE_ASSERT(size > 0, "[VulkanDescriptorHeap] allocate_transient_bytes: size cannot be 0");
        HN_CORE_ASSERT(align > 0, "[VulkanDescriptorHeap] allocate_transient_bytes: align cannot be 0");

        auto& slot = m_frame_slots[m_current_frame];

        VkDeviceSize aligned = align_up(slot.cursor, align);
        HN_CORE_ASSERT(aligned + size <= slot.end, "[VulkanDescriptorHeap] Transient heap slot overflow");

        slot.cursor = aligned + size;

        VkDeviceSize used = slot.cursor - slot.begin;
        if (used > m_resource_max_size_reached) m_resource_max_size_reached = used;

        // stride = 1: callers index the block by raw byte offset via per-descriptor sub-allocations.
        return Allocation{ (uint32_t)aligned, size, 1u };
    }

    VulkanDescriptorHeap::Allocation VulkanDescriptorHeap::allocate_persistent_resource(VkDescriptorType type,
        uint32_t count) {

        const uint32_t stride = stride_for(type);
        HN_CORE_ASSERT(stride > 0, "[VulkanDescriptorHeap] Stride cannot be 0");

        VkDeviceSize aligned = align_up(m_resource_persistent_cursor, descriptor_alignment(type));
        VkDeviceSize bytes = (VkDeviceSize)stride * count;
        VkDeviceSize region_end = m_resource_persistent_offset + m_resource_persistent_size;
        HN_CORE_ASSERT(aligned + bytes <= region_end,
            "[VulkanDescriptorHeap] Resource persistent region overflow");

        m_resource_persistent_cursor = aligned + bytes;
        return Allocation{ (uint32_t)aligned, (uint32_t)bytes, stride };
    }

    VulkanDescriptorHeap::Allocation VulkanDescriptorHeap::allocate_persistent_sampler(uint32_t count) {
        const uint32_t stride = m_descriptor_sizes.sampler;
        VkDeviceSize aligned = align_up(m_sampler_persistent_cursor, stride);
        VkDeviceSize bytes = (VkDeviceSize)stride * count;
        VkDeviceSize region_end = m_sampler_persistent_offset + m_sampler_persistent_size;
        HN_CORE_ASSERT(aligned + bytes <= region_end, "[VulkanDescriptorHeap] Sampler persistent region overflow");

        m_sampler_persistent_cursor = aligned + bytes;
        return Allocation{ (uint32_t)aligned, (uint32_t)bytes, stride };
    }

    void VulkanDescriptorHeap::write_image(const Allocation& alloc, uint32_t index, const VkImageViewCreateInfo& view,
        VkImageLayout layout, VkDescriptorType type) {

        HN_CORE_ASSERT(alloc.stride == stride_for(type),
            "[VulkanDescriptorHeap] write_image: descriptor type does not match allocation");
        HN_CORE_ASSERT((VkDeviceSize)(index + 1) * alloc.stride <= alloc.size,
            "[VulkanDescriptorHeap] write_image: descriptor index past end of allocation");

        VkImageDescriptorInfoEXT img_info{};
        img_info.sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT;
        img_info.pView = &view;
        img_info.layout = layout;

        VkResourceDescriptorInfoEXT res_info{};
        res_info.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
        res_info.type = type;
        res_info.data.pImage = &img_info;

        char* base = (char*)m_resource_heap_mapped;
        void* dest = base + alloc.offset + (VkDeviceSize)index * alloc.stride;

        VkHostAddressRangeEXT range{};
        range.address = dest;
        range.size = alloc.stride;

        m_fnWriteResourceDescriptors(m_device, 1, &res_info, &range);
    }

    void VulkanDescriptorHeap::write_buffer(const Allocation& alloc, uint32_t index, VkDeviceAddress addr, VkDeviceSize range,
        VkDescriptorType type) {

        HN_CORE_ASSERT(alloc.stride == stride_for(type),
            "[VulkanDescriptorHeap] write_buffer: descriptor type does not match allocation");
        HN_CORE_ASSERT((VkDeviceSize)(index + 1) * alloc.stride <= alloc.size,
            "[VulkanDescriptorHeap] write_buffer: descriptor index past end of allocation");

        VkDeviceAddressRangeEXT addr_range{};
        addr_range.address = addr;
        addr_range.size = range;

        VkResourceDescriptorInfoEXT res_info{};
        res_info.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
        res_info.type = type;
        res_info.data.pAddressRange = &addr_range;

        char* base = (char*)m_resource_heap_mapped;
        void* dest = base + alloc.offset + (VkDeviceSize)index * alloc.stride;

        VkHostAddressRangeEXT host_range{};
        host_range.address = dest;
        host_range.size = alloc.stride;

        m_fnWriteResourceDescriptors(m_device, 1, &res_info, &host_range);
    }

    void VulkanDescriptorHeap::write_sampler(const Allocation& alloc, uint32_t index, const VkSamplerCreateInfo& sampler_ci) {
        HN_CORE_ASSERT(alloc.stride == m_descriptor_sizes.sampler,
            "[VulkanDescriptorHeap] write_sampler: allocation stride is not a sampler descriptor");
        HN_CORE_ASSERT((VkDeviceSize)(index + 1) * alloc.stride <= alloc.size,
            "[VulkanDescriptorHeap] write_sampler: descriptor index past end of allocation");

        char* base = (char*)m_sampler_heap_mapped;
        void* dest = base + alloc.offset + (VkDeviceSize)index * alloc.stride;

        VkHostAddressRangeEXT range{};
        range.address = dest;
        range.size        = m_descriptor_sizes.sampler;

        m_fnWriteSamplerDescriptors(m_device, 1, &sampler_ci, &range);
    }

    void VulkanDescriptorHeap::bake_static_samplers(float max_anisotropy) {
        m_static_sampler_alloc = allocate_persistent_sampler((uint32_t)StaticSampler::Count);

        write_sampler(m_static_sampler_alloc, (uint32_t)StaticSampler::Nearest, VulkanUtils::make_nearest_sampler_ci());
        write_sampler(m_static_sampler_alloc, (uint32_t)StaticSampler::Linear, VulkanUtils::make_linear_sampler_ci());
        write_sampler(m_static_sampler_alloc, (uint32_t)StaticSampler::Anisotropic, VulkanUtils::make_anisotropic_sampler_ci(max_anisotropy));
        write_sampler(m_static_sampler_alloc, (uint32_t)StaticSampler::ShadowCmp, VulkanUtils::make_shadow_cmp_sampler_ci());

        for (uint32_t i = 0; i < (uint32_t)StaticSampler::Count; ++i) {
            m_static_sampler_index[i] = (m_static_sampler_alloc.offset + i * m_static_sampler_alloc.stride)
            / m_static_sampler_alloc.stride; // The index is equal to the data's offset / stride
        }
    }

    void VulkanDescriptorHeap::push_pass_data(VkCommandBuffer cmd, const void* data, uint32_t size) {
        HN_CORE_ASSERT(m_fnPushData, "[VulkanDescriptorHeap] vkCmdPushDataEXT not loaded");
        HN_CORE_ASSERT(data && size > 0, "[VulkanDescriptorHeap] push_pass_data: empty data");
        HN_CORE_ASSERT(size <= m_props.maxPushDataSize,
                       "[VulkanDescriptorHeap] push_pass_data: size exceeds maxPushDataSize");

        VkPushDataInfoEXT info{};
        info.sType        = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT;
        info.offset       = 0;
        info.data.address = data;
        info.data.size    = size;
        m_fnPushData(cmd, &info);
    }

    void VulkanDescriptorHeap::begin_frame(uint32_t frame_in_flight) {
        HN_CORE_ASSERT(frame_in_flight < m_frame_slots.size(), "[VulkanDescriptorHeap] frame_in_flight out of range");
        m_current_frame = frame_in_flight;
        auto& slot = m_frame_slots[frame_in_flight];
        slot.cursor = slot.begin;
    }

    void VulkanDescriptorHeap::bind(VkCommandBuffer cmd) {
        VkBindHeapInfoEXT resource_bind_info{};
        resource_bind_info.sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT;
        resource_bind_info.heapRange = { m_resource_heap_addr, m_resource_capacity };
        resource_bind_info.reservedRangeOffset = 0;
        resource_bind_info.reservedRangeSize = m_resource_reserved_size;
        m_fnBindResourceHeap(cmd, &resource_bind_info);

        VkBindHeapInfoEXT sampler_bind_info{};
        sampler_bind_info.sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT;
        sampler_bind_info.heapRange = { m_sampler_heap_addr, m_sampler_capacity };
        sampler_bind_info.reservedRangeOffset = 0;
        sampler_bind_info.reservedRangeSize = m_sampler_reserved_size;
        m_fnBindSamplerHeap(cmd, &sampler_bind_info);
    }

    void VulkanDescriptorHeap::create_heap_buffer(VkDevice dev, VkPhysicalDevice phys, VkDeviceSize size,
                                                  VkBuffer& out_buffer, VkDeviceMemory& out_memory, void*& out_mapped, VkDeviceAddress& out_addr) {

        HN_PROFILE_FUNCTION();
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size;
        bi.usage = VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult r = vkCreateBuffer(dev, &bi, nullptr, &out_buffer);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateBuffer failed");

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(dev, out_buffer, &req);

        VkMemoryAllocateFlagsInfo flags_info{};
        flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.pNext = &flags_info;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = VulkanUtils::find_memory_type(phys, req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        r = vkAllocateMemory(dev, &ai, nullptr, &out_memory);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateMemory failed");

        r = vkBindBufferMemory(dev, out_buffer, out_memory, 0);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkBindBufferMemory failed");

        out_mapped = nullptr;
        r = vkMapMemory(dev, out_memory, 0, VK_WHOLE_SIZE, 0, &out_mapped);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkMapMemory failed");

        VkBufferDeviceAddressInfo addr_info{};
        addr_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addr_info.buffer = out_buffer;
        out_addr = vkGetBufferDeviceAddress(dev, &addr_info);
    }

    uint32_t VulkanDescriptorHeap::stride_for(VkDescriptorType type) const {
        switch (type) {
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:  return m_descriptor_sizes.sampled_image;
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:  return m_descriptor_sizes.storage_image;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return m_descriptor_sizes.uniform_buffer;
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return m_descriptor_sizes.storage_buffer;
            case VK_DESCRIPTOR_TYPE_SAMPLER:        return m_descriptor_sizes.sampler;
            default: HN_CORE_ASSERT(false, "stride_for: unsupported descriptor type"); return 0;
        }
    }

    uint32_t VulkanDescriptorHeap::descriptor_alignment(VkDescriptorType type) const {
        switch (type) {
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:  return (uint32_t)m_props.imageDescriptorAlignment;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return (uint32_t)m_props.bufferDescriptorAlignment;
            case VK_DESCRIPTOR_TYPE_SAMPLER:        return (uint32_t)m_props.samplerDescriptorAlignment;
            default: HN_CORE_ASSERT(false, "descriptor_alignment: unsupported descriptor type"); return 1;
        }
    }
}
