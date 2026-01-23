#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "vk_queue_lease.h"

namespace Honey {

    class VulkanBackend {
    public:
        VulkanBackend() = default;
        ~VulkanBackend();

        VulkanBackend(const VulkanBackend&) = delete;
        VulkanBackend& operator=(const VulkanBackend&) = delete;

        void init();
        void shutdown();
        bool initialized() const { return m_initialized; }

        VkInstance instance() const { return m_instance; }
        VkPhysicalDevice physical_device() const { return m_physical_device; }
        VkDevice device() const { return m_device; }

        // Acquire queues for a window/surface. If unique queues are not available, returns shared queues.
        VulkanQueueLease acquire_queue_lease(VkSurfaceKHR surface);

        // Called by contexts on destruction (optional if you choose to just keep leases until shutdown).
        void release_queue_lease(const VulkanQueueLease& lease);

        // Shared-queue mutexes (used when lease.shared* is true)
        std::mutex& graphics_mutex() { return m_shared_graphics_mutex; }
        std::mutex& present_mutex() { return m_shared_present_mutex; }

        // Thread-safe helper for one-off GPU work (uploads, layout transitions, etc.).
        // Serializes through an internal mutex and blocks until completion.
        void immediate_submit(const std::function<void(VkCommandBuffer)>& record);

    private:
        struct QueueFamilyInfo {
            uint32_t graphicsFamily = UINT32_MAX;
            uint32_t presentFamily = UINT32_MAX;
            bool sameFamily() const { return graphicsFamily == presentFamily; }
        };

        void create_instance();
        void setup_debug_messenger();
        void destroy_debug_messenger();

        void pick_physical_device();
        QueueFamilyInfo find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface);

        void create_logical_device(const QueueFamilyInfo& families, uint32_t desiredQueuesPerFamily);

        VkQueue get_device_queue(uint32_t family, uint32_t queueIndex);

        // queue pools
        void init_queue_pools(uint32_t graphicsQueueCount, uint32_t presentQueueCount);

        // Upload context (created after device exists)
        void init_upload_context();
        void shutdown_upload_context();

    private:
        bool m_initialized = false;

        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debug_messenger = VK_NULL_HANDLE;

        VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;

        // Chosen families (based on the first created surface; must remain compatible with all later surfaces)
        QueueFamilyInfo m_families{};

        // Queues we requested/created
        std::vector<VkQueue> m_graphics_queues;
        std::vector<VkQueue> m_present_queues;

        // Free lists for leasing (indices into vectors)
        std::vector<uint32_t> m_free_graphics_indices;
        std::vector<uint32_t> m_free_present_indices;

        std::mutex m_pool_mutex{};
        std::mutex m_shared_graphics_mutex{};
        std::mutex m_shared_present_mutex{};

        // Immediate submit / upload context (thread-safe, serialized)
        std::mutex m_upload_mutex{};
        VkCommandPool m_upload_command_pool = VK_NULL_HANDLE;
        VkFence m_upload_fence = VK_NULL_HANDLE;
        VkQueue m_upload_queue = VK_NULL_HANDLE;

        static constexpr uint32_t k_desired_queues_per_family = 4;
    };

} // namespace Honey