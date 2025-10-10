#pragma once

#include "Honey/renderer/graphics_context.h"

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <memory>

class GLFWwindow;

namespace Honey {
    class VulkanSwapchain;

    class VulkanContext : public GraphicsContext {
        public:
        VulkanContext(GLFWwindow* window_handle);
        ~VulkanContext();

        virtual void init() override;
        virtual void swap_buffers() override;

        static VkDevice get_device() {
            HN_CORE_ASSERT(s_instance, "VulkanContext instance is null!");
            return s_instance->m_device;
        }
        static VkPhysicalDevice get_physical_device() { return s_instance->m_physical_device; }
        static VkCommandPool get_command_pool() { return s_instance->m_command_pool; }
        static VkQueue get_graphics_queue() { return s_instance->m_graphics_queue; }
        static VulkanSwapchain* get_swapchain() { return s_instance->m_swapchain.get(); }

        // Frame management
        void begin_frame();
        void end_frame();
        VkCommandBuffer get_current_command_buffer() const { return m_command_buffers[m_current_frame]; }
        uint32_t get_current_image_index() const { return m_current_image_index; }
        VkFramebuffer get_current_swapchain_framebuffer() const;
        VkRenderPass get_swapchain_render_pass() const;

    private:

        struct QueueFamilyIndices {
            std::optional<uint32_t> graphics_family;
            std::optional<uint32_t> present_family;

            bool is_complete() {
                return graphics_family.has_value() && present_family.has_value();
            }
        };

        struct SwapchainSupportDetails {
            VkSurfaceCapabilitiesKHR capabilities;
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> present_modes;
        };

        void create_instance();
        void setup_debug_messenger();
        void create_surface();
        void pick_physical_device();
        void create_logical_device();
        void cleanup();
        void create_command_pool();
        void create_command_buffers();
        void create_sync_objects();
        void create_swapchain_framebuffers();

        int rate_device_suitability(VkPhysicalDevice device);
        QueueFamilyIndices find_queue_families(VkPhysicalDevice device);
        bool check_device_extension_support(VkPhysicalDevice device);
        SwapchainSupportDetails query_swapchain_support(VkPhysicalDevice device);
        bool check_validation_layer_support();


        GLFWwindow* m_window_handle;
        VkInstance m_instance;
        VkDebugUtilsMessengerEXT m_debug_messenger;
        VkSurfaceKHR m_surface;
        VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
        VkDevice m_device;
        VkQueue m_graphics_queue;
        VkQueue m_present_queue;
        VkCommandPool m_command_pool = VK_NULL_HANDLE;
        std::unique_ptr<VulkanSwapchain> m_swapchain;

        // Command buffers and synchronization
        static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
        std::vector<VkCommandBuffer> m_command_buffers;
        std::vector<VkSemaphore> m_image_available_semaphores;
        std::vector<VkSemaphore> m_render_finished_semaphores;
        std::vector<VkFence> m_in_flight_fences;
        uint32_t m_current_frame = 0;
        uint32_t m_current_image_index = 0;

        // Swapchain rendering
        std::vector<VkFramebuffer> m_swapchain_framebuffers;
        VkRenderPass m_swapchain_render_pass = VK_NULL_HANDLE;

        static VulkanContext* s_instance;

        const std::vector<const char*> m_validation_layers = {
            "VK_LAYER_KHRONOS_validation"
        };

        const std::vector<const char*> m_device_extensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };


#ifdef BUILD_DEBUG
        const bool m_enable_validation_layers = true;
#else
        const bool m_enable_validation_layers = false;
#endif
    };
}