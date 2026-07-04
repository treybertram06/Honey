#pragma once
#include <vulkan/vulkan_core.h>

#include "vk_utils.h"
#include "Honey/renderer/global_bindings.h"
namespace Honey {

    struct GlobalsLayout {
        std::array<VkDeviceSize, (size_t)GlobalBinding::Count> offset{};
        VkDeviceSize total_size = 0;

        static GlobalsLayout build(const VkPhysicalDeviceLimits& limits) {
            // UBOs respect minUniformBufferOffsetAlignment; SSBOs respect minStorageBufferOffsetAlignment.
            GlobalsLayout l{};
            VkDeviceSize cursor = 0;
            for (const auto& b : k_global_bindings) {
                if (b.kind == GlobalBufferKind::ExternalStorage) {
                    l.offset[(size_t)b.id] = 0;
                    continue;
                }
                const VkDeviceSize a = (b.kind == GlobalBufferKind::Uniform)
                    ? limits.minUniformBufferOffsetAlignment
                    : limits.minStorageBufferOffsetAlignment;

                cursor = VulkanUtils::align_up(cursor, a);
                l.offset[(size_t)b.id] = cursor;
                cursor += b.size;
            }
            l.total_size = cursor;
            return l;
        }

    };

}
