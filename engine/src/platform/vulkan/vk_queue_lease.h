#pragma once

#include <cstdint>
#include <mutex>

typedef struct VkQueue_T* VkQueue;

namespace Honey {

    struct VulkanQueueLease {
        VkQueue graphicsQueue = nullptr;
        uint32_t graphicsFamily = UINT32_MAX;
        uint32_t graphicsQueueIndex = UINT32_MAX;

        VkQueue presentQueue = nullptr;
        uint32_t presentFamily = UINT32_MAX;
        uint32_t presentQueueIndex = UINT32_MAX;

        bool sharedGraphics = true;
        bool sharedPresent = true;

        std::mutex* graphicsSubmitMutex = nullptr;
        std::mutex* presentSubmitMutex = nullptr;

        // Non-copyable, movable
        VulkanQueueLease() = default;
        VulkanQueueLease(const VulkanQueueLease&) = delete;
        VulkanQueueLease& operator=(const VulkanQueueLease&) = delete;
        VulkanQueueLease(VulkanQueueLease&&) noexcept = default;
        VulkanQueueLease& operator=(VulkanQueueLease&&) noexcept = default;
    };

} // namespace Honey