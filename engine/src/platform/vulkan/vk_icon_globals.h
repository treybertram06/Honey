#pragma once

#include "vk_context.h"
#include "vk_globals.h"
#include "Honey/renderer/global_bindings.h"
#include "Honey/renderer/gpu_types.h"
#include "Honey/renderer/slug_font.h"

#include <vulkan/vulkan.h>
#include <vector>

namespace Honey {

    class VulkanBackend;

    class VulkanIconGlobals {
    public:
        // 8 bands × 95 printable ASCII glyphs (codepoints 32–126)
        static constexpr uint32_t k_max_font_band_entries = 760;
        static constexpr uint32_t k_max_font_curves       = 16384;
        static constexpr uint32_t k_max_icon_band_entries = 4096;
        static constexpr uint32_t k_max_icon_curves       = 32768;
        // Combined SSBO capacities
        static constexpr uint32_t k_total_band_entries    = k_max_font_band_entries + k_max_icon_band_entries;
        static constexpr uint32_t k_total_curves          = k_max_font_curves + k_max_icon_curves;

        void init(VulkanBackend* backend);
        void shutdown();

        // Slot allocator
        struct IconSlot { uint32_t band_offset, band_count, curve_offset, curve_count; };
        IconSlot allocate_icon_slot(uint32_t band_count, uint32_t curve_count);
        void free_icon_slot(const IconSlot& slot);

        // One-shot transient-staging upload
        void upload_icon_data(const BandEntry* bands, uint32_t band_count, uint32_t band_offset,
                            const QuadBezier* curves, uint32_t curve_count, uint32_t curve_offset);

    private:
        void create_globals_resources(); // one device local buffer, band region + aligned curve region
        void register_heap_bindings(); // two ExternalStorage global bindings, written once each

        VulkanBackend*   m_backend         = nullptr;
        VkDevice         m_device          = VK_NULL_HANDLE;
        VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;

        // Set-1(?) globals storing icon data
        VkBuffer m_icon_buffer{};
        VkDeviceMemory m_icon_alloc{};
        VkDeviceSize m_curve_region_offset = 0;

        std::mutex m_alloc_mutex;

        uint32_t m_band_cursor = 0, m_curve_cursor = 0;
        std::unordered_map<uint64_t, std::vector<IconSlot>> m_freelist; // keyed by (band_count<<32 | curve_count)

    };

}