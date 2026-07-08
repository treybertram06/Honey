#pragma once
#include <vulkan/vulkan_core.h>

#include <mutex>

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

        Allocation allocate_persistent_block(VkDescriptorType type, uint32_t count);
        void free_persistent_block(const Allocation& alloc);

        void init_bindless_table(uint32_t capacity);
        uint32_t bindless_table_byte_offset() const { return m_bindless_table_alloc.offset; }
        uint32_t alloc_bindless_index();
        void free_bindless_index(uint32_t index);
        void write_bindless(uint32_t index, const VkImageViewCreateInfo& view, VkImageLayout layout);

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

        uint32_t static_sampler_byte_offset(StaticSampler s) const {
            HN_CORE_ASSERT(false, "Do not call this");
            return m_static_sampler_alloc.offset + (uint32_t)s * m_static_sampler_alloc.stride;
        }
        uint32_t sampler_descriptor_stride() const {
            HN_CORE_ASSERT(false, "Do not call this");
            return m_descriptor_sizes.sampler;
        }
        // Stable VkSamplerCreateInfo for a static sampler, usable as a mapping's pEmbeddedSampler.
        // Lives for the heap's lifetime, so the pointer stays valid through pipeline creation.
        const VkSamplerCreateInfo* static_sampler_ci(StaticSampler s) const { return &m_static_sampler_ci[(uint32_t)s]; }
        uint32_t descriptor_stride(VkDescriptorType type) const { return stride_for(type); }
        uint32_t descriptor_alignment(VkDescriptorType type) const;
        VkDeviceSize max_push_data_size() const { return m_props.maxPushDataSize; }

        void register_global_binding(uint32_t binding, const Allocation& slot);
        void write_global_buffer(uint32_t binding, VkDeviceAddress addr, VkDeviceSize range, VkDescriptorType type);
        uint32_t global_binding_offset(uint32_t binding) const;
        bool has_global_binding(uint32_t binding) const;

    private:
        static void create_heap_buffer(VkDevice dev,
                                        VkPhysicalDevice phys,
                                        VkDeviceSize size,
                                        VkBuffer& out_buffer,
                                        VkDeviceMemory& out_memory,
                                        void*& out_mapped,
                                        VkDeviceAddress& out_addr);

        uint32_t stride_for(VkDescriptorType type) const;

        // Bump-allocates from the persistent cursor. Caller must hold m_alloc_mutex.
        Allocation allocate_persistent_resource_unlocked(VkDescriptorType type, uint32_t count);

        static uint64_t persistent_block_key(uint32_t stride, uint32_t count) {
            return ((uint64_t)stride << 32) | count;
        }

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
        VkDeviceSize m_resource_persistent_capacity = 0;

        std::array<Allocation, 8> m_global_slots{}; // Probably shouldn't hardcode the size here
        std::array<bool, 8> m_global_slots_valid{};

        // Per frame transient regions
        VkDeviceSize m_transient_reserved_size = 0;
        VkDeviceSize m_transient_persistent_size = 0;
        std::vector<FrameSlot> m_frame_slots;
        uint32_t m_current_frame = 0;

        // Guards the persistent cursor, persistent freelist, bindless freelist, and sampler
        // cursor: allocations arrive from the main, upload, and loader threads while frees come
        // from the render thread's deferred-destroy sweep.
        std::mutex m_alloc_mutex;
        std::unordered_map<uint64_t, std::vector<Allocation>> m_persistent_freelist;
        Allocation m_bindless_table_alloc{};
        std::vector<uint32_t> m_bindless_free_indices;
        uint32_t m_bindless_capacity = 0;

        // Sampler heap layout
        VkDeviceSize m_sampler_reserved_size = 0;
        VkDeviceSize m_sampler_persistent_offset = 0;
        VkDeviceSize m_sampler_persistent_size = 0;
        VkDeviceSize m_sampler_persistent_cursor = 0;
        Allocation m_static_sampler_alloc{};
        VkSamplerCreateInfo m_static_sampler_ci[(uint32_t)StaticSampler::Count] = {}; // embedded-sampler source, kept stable

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
