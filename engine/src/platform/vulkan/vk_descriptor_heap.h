#pragma once
#include <vulkan/vulkan_core.h>

namespace Honey {

    class VulkanDescriptorHeap {
    public:
        VulkanDescriptorHeap(VkInstance instance, VkPhysicalDevice phys, VkDevice device);
        ~VulkanDescriptorHeap();

        struct Allocation { uint32_t offset; uint32_t size; uint32_t stride; };
        // offset/size are BYTE offsets into the resource (or sampler) heap buffer.

        Allocation allocate_transient_resource(VkDescriptorType type, uint32_t count);
        // Transient block of `size` bytes (align `align`) for a pass that packs mixed descriptor
        // types. Returns a byte sub-range; callers build per-descriptor sub-allocations into it.
        Allocation allocate_transient_bytes(uint32_t size, uint32_t align);
        Allocation allocate_persistent_resource(VkDescriptorType type, uint32_t count); // resource heap, persistent region
        Allocation allocate_persistent_sampler(uint32_t count);   // sampler heap

        void write_image (const Allocation& alloc, uint32_t index,
                          const VkImageViewCreateInfo& view, VkImageLayout layout,
                          VkDescriptorType type);
        void write_buffer(const Allocation& alloc, uint32_t index,
                          VkDeviceAddress addr, VkDeviceSize range, VkDescriptorType type);
        void write_sampler(const Allocation& alloc, uint32_t index, const VkSamplerCreateInfo& sampler_ci);

        void bake_static_samplers(float max_anisotropy);

        void push_pass_data(VkCommandBuffer cmd, const void* data, uint32_t size);

        void begin_frame(uint32_t frame_in_flight); // reset that slot's bump cursor
        void bind(VkCommandBuffer cmd);             // bind both heaps

        enum class StaticSampler : uint32_t { Linear = 0, Nearest, Anisotropic, ShadowCmp, Count, };

        // resource accessors
        VkDeviceAddress resource_device_address() const { return m_resource_heap_addr; }
        VkDeviceAddress sampler_device_address()  const { return m_sampler_heap_addr; }
        uint32_t        static_sampler_index(StaticSampler s) const { return m_static_sampler_index[(uint32_t)s]; }


        uint32_t static_sampler_byte_offset(StaticSampler s) const {
            return m_static_sampler_alloc.offset + (uint32_t)s * m_static_sampler_alloc.stride;
        }
        uint32_t sampler_descriptor_stride() const { return m_descriptor_sizes.sampler; }
        uint32_t descriptor_stride(VkDescriptorType type) const { return stride_for(type); }
        uint32_t descriptor_alignment(VkDescriptorType type) const;
        uint32_t global_ubo_offset() const { return m_global_ubo_alloc.offset; }
        VkDeviceSize max_push_data_size() const { return m_props.maxPushDataSize; }


    private:
        static void create_heap_buffer(VkDevice dev,
                                        VkPhysicalDevice phys,
                                        VkDeviceSize size,
                                        VkBuffer& out_buffer,
                                        VkDeviceMemory& out_memory,
                                        void*& out_mapped,
                                        VkDeviceAddress& out_addr);

        uint32_t stride_for(VkDescriptorType type) const;
    private:
        VkInstance m_instance = nullptr;
        VkPhysicalDevice m_phys = nullptr;
        VkDevice m_device = nullptr;

        // EXT function pointers
        PFN_vkWriteSamplerDescriptorsEXT    m_fnWriteSamplerDescriptors = nullptr;
        PFN_vkWriteResourceDescriptorsEXT   m_fnWriteResourceDescriptors = nullptr;
        PFN_vkCmdBindResourceHeapEXT        m_fnBindResourceHeap = nullptr;
        PFN_vkCmdBindSamplerHeapEXT         m_fnBindSamplerHeap = nullptr;
        PFN_vkCmdPushDataEXT                m_fnPushData = nullptr;
        PFN_vkGetPhysicalDeviceDescriptorSizeEXT m_fnGetDescriptorSize = nullptr;

        // Cached descriptor sizes
        struct DescriptorSizes {
            uint32_t sampled_image = 0;
            uint32_t storage_image = 0;
            uint32_t uniform_buffer = 0;
            uint32_t storage_buffer = 0;
            uint32_t sampler = 0;
        };
        DescriptorSizes m_descriptor_sizes;

        VkPhysicalDeviceDescriptorHeapPropertiesEXT m_props;

        struct FrameSlot {
            VkDeviceSize begin;
            VkDeviceSize end;
            VkDeviceSize cursor;
        };

        // Layout state
        // Resource heap layout
        VkDeviceSize m_resource_reserved_size = 0;
        VkDeviceSize m_resource_persistent_offset = 0;
        VkDeviceSize m_resource_persistent_size = 0;
        VkDeviceSize m_resource_persistent_cursor = 0;
        VkDeviceSize m_resource_persistent_capacity = 64 * 1024; // 64 KiB carved for persistent residents (set-0 globals)
        Allocation   m_global_ubo_alloc{}; // set-0 engine-global UBO slot (CONSTANT_OFFSET target)

        // Per frame transient regions
        VkDeviceSize m_transient_reserved_size = 0;
        VkDeviceSize m_transient_persistent_size = 0;
        std::vector<FrameSlot> m_frame_slots;
        uint32_t m_current_frame = 0;

        // Sampler heap layout
        VkDeviceSize m_sampler_reserved_size = 0;
        VkDeviceSize m_sampler_persistent_offset = 0;
        VkDeviceSize m_sampler_persistent_size = 0;
        VkDeviceSize m_sampler_persistent_cursor = 0;
        Allocation m_static_sampler_alloc{};
        uint32_t m_static_sampler_index[(uint32_t)StaticSampler::Count] = {};

        // Capacities and tracking
        VkDeviceSize m_resource_capacity = 4 * 1024 * 1024; // 4 MiB
        VkDeviceSize m_sampler_capacity = 64 * 1024; // 64 KiB
        VkDeviceSize m_resource_max_size_reached = 0;

        // Resource heap handles
        VkBuffer m_resource_heap_buffer;
        VkDeviceMemory m_resource_heap_memory;
        void* m_resource_heap_mapped;
        VkDeviceAddress m_resource_heap_addr;

        // Sampler heap handles
        VkBuffer m_sampler_heap_buffer;
        VkDeviceMemory m_sampler_heap_memory;
        void* m_sampler_heap_mapped;
        VkDeviceAddress m_sampler_heap_addr;

    };

}
