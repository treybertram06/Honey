#pragma once

#include "Honey/renderer/graphics_context.h"

struct GLFWwindow;

typedef struct VkInstance_T* VkInstance;
typedef struct VkSurfaceKHR_T* VkSurfaceKHR;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkDevice_T* VkDevice;
typedef struct VkQueue_T* VkQueue;
typedef struct VkSwapchainKHR_T* VkSwapchainKHR;
typedef struct VkImage_T* VkImage;
typedef struct VkImageView_T* VkImageView;
typedef struct VkRenderPass_T* VkRenderPass;
typedef struct VkFramebuffer_T* VkFramebuffer;
typedef struct VkCommandPool_T* VkCommandPool;
typedef struct VkCommandBuffer_T* VkCommandBuffer;
typedef struct VkSemaphore_T* VkSemaphore;
typedef struct VkFence_T* VkFence;
typedef struct VkDebugUtilsMessengerEXT_T* VkDebugUtilsMessengerEXT;
typedef struct VkPipelineLayout_T* VkPipelineLayout;
typedef struct VkPipeline_T* VkPipeline;
typedef struct VkShaderModule_T* VkShaderModule;
typedef struct VkDescriptorSetLayout_T* VkDescriptorSetLayout;
typedef struct VkDescriptorPool_T* VkDescriptorPool;
typedef struct VkDescriptorSet_T* VkDescriptorSet;

namespace Honey {
    class VulkanContext : public GraphicsContext {
    public:
        VulkanContext(GLFWwindow* window_handle);
        ~VulkanContext();

        virtual void init() override;
        virtual void swap_buffers() override;
        virtual void wait_idle() override;

        void notify_framebuffer_resized();

        VkDevice get_device() const { return m_device; }
        VkPhysicalDevice get_physical_device() const { return m_physical_device; }

        uint32_t get_graphics_queue_family() const { return m_graphics_queue_family; }
        VkQueue get_graphics_queue() const { return m_graphics_queue; }
        VkCommandPool get_command_pool() const { return m_command_pool; }

    private:
        void create_instance();
        void create_surface();

        bool check_validation_layer_support() const;
        std::vector<const char*> get_required_instance_extensions() const;
        void setup_debug_messenger();
        void destroy_debug_messenger();

        void pick_physical_device();
        void create_logical_device();

        void create_swapchain();
        void create_image_views();
        void create_render_pass();
        void create_framebuffers();

        void create_command_pool();
        void create_command_buffers();

        void create_sync_objects();

        void record_command_buffer(uint32_t image_index);

        void cleanup_swapchain();
        void recreate_swapchain_if_needed();
        bool wait_for_nonzero_framebuffer_size() const;

        void destroy();

        void create_graphics_pipeline();
        void cleanup_pipeline();
        VkShaderModule create_shader_module_from_file(const std::string& path);
        std::string shader_path(const char* filename) const;

        void create_global_descriptor_resources();
        void cleanup_global_descriptor_resources();

        static constexpr uint32_t k_max_frames_in_flight = 2;

        GLFWwindow* m_window_handle = nullptr;

        VkInstance m_instance = nullptr;
        VkSurfaceKHR m_surface = nullptr;

        VkDebugUtilsMessengerEXT m_debug_messenger = nullptr;

        VkPhysicalDevice m_physical_device = nullptr;
        VkDevice m_device = nullptr;

        uint32_t m_graphics_queue_family = UINT32_MAX;
        uint32_t m_present_queue_family = UINT32_MAX;
        VkQueue m_graphics_queue = nullptr;
        VkQueue m_present_queue = nullptr;

        VkSwapchainKHR m_swapchain = nullptr;
        std::vector<VkImage> m_swapchain_images;
        std::vector<VkImageView> m_swapchain_image_views;

        uint32_t m_swapchain_image_format = 0; // VkFormat stored as uint32_t to avoid vulkan.h in header
        uint32_t m_swapchain_extent_width = 0;
        uint32_t m_swapchain_extent_height = 0;

        VkRenderPass m_render_pass = nullptr;
        std::vector<VkFramebuffer> m_swapchain_framebuffers;

        VkPipelineLayout m_pipeline_layout = nullptr;
        VkPipeline m_pipeline = nullptr;
        VkShaderModule m_vert_module = nullptr;
        VkShaderModule m_frag_module = nullptr;

        VkDescriptorSetLayout m_global_set_layout = nullptr;
        VkDescriptorPool m_descriptor_pool = nullptr;
        VkDescriptorSet m_global_descriptor_sets[k_max_frames_in_flight]{};

        void* m_camera_ubos[k_max_frames_in_flight]{};        // VkBuffer
        void* m_camera_ubo_memories[k_max_frames_in_flight]{}; // VkDeviceMemory
        uint32_t m_camera_ubo_size = 0;

        VkCommandPool m_command_pool = nullptr;
        std::vector<VkCommandBuffer> m_command_buffers;

        std::vector<VkSemaphore> m_image_available_semaphores;
        std::vector<VkSemaphore> m_render_finished_semaphores;
        std::vector<VkFence> m_in_flight_fences;

        std::vector<VkFence> m_images_in_flight;

        uint32_t m_current_frame = 0;

        bool m_framebuffer_resized = false;
        bool m_initialized = false;
    };

}