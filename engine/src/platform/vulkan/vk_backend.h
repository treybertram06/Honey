#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "vk_pipeline_cache_blob.h"
#include "vk_queue_lease.h"

namespace Honey {

    static void set_debug_name(VkDevice device, VkObjectType type, uint64_t handle, const char* name) {
#if defined(BUILD_DEBUG)
        if (!device || !name)
            return;

        auto fpSetName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT"));
        if (!fpSetName)
            return;

        VkDebugUtilsObjectNameInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        info.objectType = type;
        info.objectHandle = handle;
        info.pObjectName = name;

        fpSetName(device, &info);
#else
        (void)device; (void)type; (void)handle; (void)name;
#endif
    }

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

        const VulkanPipelineCacheBlob& get_pipeline_cache() const { return m_pipeline_cache; }
        VkInstance get_instance() const { return m_instance; }
        VkPhysicalDevice get_physical_device() const { return m_physical_device; }
        VkDevice get_device() const { return m_device; }
        uint32_t get_graphics_queue_family_index() const { return m_families.graphicsFamily; }
        VkQueue get_graphics_queue() const { return !m_graphics_queues.empty() ? m_graphics_queues[0] : VK_NULL_HANDLE; }

        VkDescriptorPool get_imgui_descriptor_pool() const { return m_imgui_descriptor_pool; }
        VkSampler get_imgui_sampler() const { return m_imgui_sampler; }

        uint32_t get_min_image_count() const  { return m_min_image_count; }
        uint32_t get_image_count() const      { return m_image_count; }

        VkSampler get_sampler_nearest() const { return m_sampler_nearest; }
        VkSampler get_sampler_linear() const { return m_sampler_linear; }
        VkSampler get_sampler_anisotropic() const { return m_sampler_aniso; }


        VkCommandBuffer begin_single_time_commands();
        void end_single_time_commands(VkCommandBuffer cmd);

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

        void render_imgui_on_current_swapchain_image(VkCommandBuffer cmd, VkImageView target_view, VkExtent2D extent);

        float get_max_anisotropy() const { return m_max_anisotropy; }

        bool imgui_initialized() const { return m_imgui_initialized; }

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

        void init_imgui_resources();
        void shutdown_imgui_resources();
    private:
        bool m_initialized = false;

        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debug_messenger = VK_NULL_HANDLE;

        VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;

        VulkanPipelineCacheBlob m_pipeline_cache{};

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

        VkDescriptorPool m_imgui_descriptor_pool = VK_NULL_HANDLE;
        VkSampler m_imgui_sampler = VK_NULL_HANDLE;

        // Placeholder image counts for ImGui. If you already know your swapchain
        // image counts elsewhere, you can wire them in here.
        uint32_t m_min_image_count = 2;
        uint32_t m_image_count = 2;

        float m_max_anisotropy = 1.0f;

        VkSampler m_sampler_nearest = VK_NULL_HANDLE;
        VkSampler m_sampler_linear = VK_NULL_HANDLE;
        VkSampler m_sampler_aniso = VK_NULL_HANDLE;

        static constexpr uint32_t k_desired_queues_per_family = 4;

        bool m_imgui_initialized = false;
    };

} // namespace Honey