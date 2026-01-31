#pragma once

#include "vk_backend.h"
#include "vk_queue_lease.h"
#include "Honey/renderer/graphics_context.h"
#include "Honey/renderer/vertex_array.h"
#include <glm/glm.hpp>

#include "vk_pipeline.h"

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
        VulkanContext(GLFWwindow* window_handle, VulkanBackend* backend);
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
        VkRenderPass get_render_pass() const { return m_render_pass; }

        struct FramePacket {
            struct DrawCmd {
                Ref<VertexArray> va;
                uint32_t indexCount = 0;
                uint32_t instanceCount = 1;
            };

            // Persistent-ish settings (can be overwritten by calls)
            glm::vec4 clearColor{0.1f, 0.1f, 0.1f, 1.0f};

            // One-shot per-frame flags/data
            bool clearRequested = false;

            glm::mat4 viewProjection{1.0f};
            bool hasCamera = false;

            std::array<void*, 32> textures{};
            uint32_t textureCount = 0;
            bool hasTextures = false;

            std::vector<DrawCmd> draws;
            size_t drawCursor = 0;

            bool frame_begun = false;

            enum class CmdType : uint8_t {
                BeginSwapchainPass,
                BeginOffscreenPass,
                EndPass,
                BindPipelineQuad2D,
                BindGlobals,        // camera + textures for now
                DrawIndexed
            };

            struct CmdBeginSwapchainPass {
                glm::vec4 clearColor{0.1f, 0.1f, 0.1f, 1.0f};
            };

            // New: offscreen pass description
            struct CmdBeginOffscreenPass {
                class VulkanFramebuffer* framebuffer = nullptr;
                glm::vec4 clearColor{0.1f, 0.1f, 0.1f, 1.0f};
            };

            struct CmdBindGlobals {
                glm::mat4 viewProjection{1.0f};
                bool hasCamera = false;

                std::array<void*, 32> textures{};
                uint32_t textureCount = 0;
                bool hasTextures = false;
            };

            struct CmdDrawIndexed {
                Ref<VertexArray> va;
                uint32_t indexCount = 0;
                uint32_t instanceCount = 1;
            };

            struct Cmd {
                CmdType type{};
                CmdBeginSwapchainPass begin{};
                CmdBeginOffscreenPass offscreen{};
                CmdBindGlobals globals{};
                CmdDrawIndexed draw{};
            };

            std::vector<Cmd> cmds;

            void begin_frame() {
                // Keep old fields for now (will be removed once everything is migrated)
                clearRequested = false;
                hasCamera = false;
                hasTextures = false;
                textures = {};
                textureCount = 0;
                draws.clear();
                drawCursor = 0;

                cmds.clear();
                frame_begun = true;

                // NOTE: we no longer auto-begin a swapchain pass here.
                // Pass boundaries are driven by Renderer::begin_pass / end_pass.
            }
        };

        FramePacket& frame_packet() { return m_frame_packet; }
        const FramePacket& frame_packet() const { return m_frame_packet; }

        uint32_t get_swapchain_image_format() const { return m_swapchain_image_format; }
        uint32_t get_swapchain_extent_width()  const { return m_swapchain_extent_width; }
        uint32_t get_swapchain_extent_height() const { return m_swapchain_extent_height; }

        void refresh_all_texture_samplers() override;

    private:
        // Per-window only:
        void create_surface();

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
        std::string shader_path(const char* filename) const;

        void create_global_descriptor_resources();
        void cleanup_global_descriptor_resources();

        PipelineSpec m_last_pipeline_spec{};
        bool m_pipeline_dirty = true;

        static constexpr uint32_t k_max_frames_in_flight = 2;

        GLFWwindow* m_window_handle = nullptr;

        VulkanBackend* m_backend = nullptr;
        VulkanQueueLease m_queue_lease{};

        // Shared handles (owned by backend, cached here)
        VkInstance m_instance = nullptr;
        VkPhysicalDevice m_physical_device = nullptr;
        VkDevice m_device = nullptr;
        VkSurfaceKHR m_surface = nullptr;

        uint32_t m_graphics_queue_family = UINT32_MAX;
        uint32_t m_present_queue_family = UINT32_MAX;
        VkQueue m_graphics_queue = nullptr;
        VkQueue m_present_queue = nullptr;

        VkSwapchainKHR m_swapchain = nullptr;
        std::vector<void*> m_swapchain_images;
        std::vector<void*> m_swapchain_image_views;

        uint32_t m_swapchain_image_format = 0; // VkFormat stored as uint32_t to avoid vulkan.h in header
        uint32_t m_swapchain_extent_width = 0;
        uint32_t m_swapchain_extent_height = 0;

        VkRenderPass m_render_pass = nullptr;
        std::vector<VkFramebuffer> m_swapchain_framebuffers;

        VulkanPipeline m_pipeline_quad;
        VulkanPipeline m_pipeline_quad_fb;

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

        FramePacket m_frame_packet{};

        uint32_t m_current_frame = 0;

        bool m_framebuffer_resized = false;
        bool m_initialized = false;
    };

}