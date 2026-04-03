#pragma once

#include "vk_backend.h"
#include "vk_queue_lease.h"
#include "Honey/renderer/graphics_context.h"
#include "Honey/renderer/vertex_array.h"
#include <glm/glm.hpp>
#include <functional>

#include "vk_pipeline.h"
#include "vk_types.h"

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

        double get_last_gpu_frame_time_ms() const override;

        void notify_framebuffer_resized();

        VkDevice get_device() const { return m_device; }
        VkPhysicalDevice get_physical_device() const { return m_physical_device; }
        VkDescriptorSetLayout get_global_set_layout() const { return m_global_set_layout; }
        VkDescriptorSetLayout get_font_set_layout()   const { return m_font_set_layout; }
        // Returns the font SSBO descriptor set for the given frame (all chunk sets are identical).
        VkDescriptorSet get_font_descriptor_set(uint32_t frame) const { return m_fonts_descriptor_sets[frame][0]; }

        uint32_t get_graphics_queue_family() const { return m_graphics_queue_family; }
        uint32_t get_compute_queue_family() const { return m_compute_queue_family; }
        VkQueue get_graphics_queue() const { return m_graphics_queue; }
        VkQueue get_compute_queue() const { return m_compute_queue; }
        bool has_dedicated_compute_queue() const { return m_has_dedicated_compute_queue; }
        bool supports_timeline_semaphore() const { return m_supports_timeline_semaphore; }
        VkSemaphore get_frame_graph_timeline_semaphore() const { return m_frame_graph_timeline_semaphore; }
        uint64_t get_frame_graph_timeline_value() const { return m_frame_graph_timeline_value; }
        uint64_t signal_frame_graph_timeline_cpu();
        bool wait_frame_graph_timeline_cpu(uint64_t value, uint64_t timeout_ns = UINT64_MAX);
        bool submit_one_time_graphics(const std::function<void(VkCommandBuffer)>& record);
        bool submit_one_time_compute(const std::function<void(VkCommandBuffer)>& record);
        bool submit_one_time_transfer(const std::function<void(VkCommandBuffer)>& record);
        VkCommandPool get_command_pool() const { return m_command_pool; }
        VkRenderPass get_render_pass() const { return m_render_pass; }

        void mark_pipeline_dirty() { /* m_pipeline_dirty = true; */ } // Pipelines are owned by renderer2d now
        const VulkanPipelineCacheBlob& get_pipeline_cache() const { return m_backend->get_pipeline_cache(); }
        void request_swapchain_recreation() { m_framebuffer_resized = true; }

        struct FramePacket {
            struct DrawCmd {
                Ref<VertexArray> va;
                uint32_t indexCount = 0;
                uint32_t instanceCount = 1;
                Ref<VertexBuffer> instanceVB;
                uint32_t instanceByteOffset = 0;
            };

            // Persistent-ish settings (can be overwritten by calls)
            glm::vec4 clearColor{0.1f, 0.1f, 0.1f, 1.0f};

            // One-shot per-frame flags/data
            bool clearRequested = false;

            CameraUBO cameraUBO{};
            bool hasCamera = false;

            LightsUBO lightUBO{};

            std::vector<GPUMaterial> materials{};
            uint32_t materials_ssbo_offset = 0;

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

                BindPipeline,
                BindGlobals,        // camera + textures for now
                PushConstantsMat4,
                PushConstants,
                DrawIndexed,
                CustomVulkan        // raw Vulkan record callback, index into custom_callbacks
            };

            struct CmdBindPipeline {
                VkPipeline pipeline = nullptr;
                VkPipelineLayout layout = nullptr;
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
                CameraUBO cameraUBO{};
                bool hasCamera = false;

                LightsUBO lightUBO{};

                std::vector<GPUMaterial> materials{};
                uint32_t materials_ssbo_offset = 0;

                std::array<void*, 32> textures{};
                uint32_t textureCount = 0;
                bool hasTextures = false;

                enum class Source : uint8_t { Unknown = 0, Renderer2D, Renderer3D } source = Source::Unknown;
            };
            CmdBindGlobals::Source sourceTag = CmdBindGlobals::Source::Unknown;

            struct CmdPushConstantsMat4 {
                glm::mat4 value{1.0f};
            };

            struct CmdPushConstants {
                std::array<std::byte, 128> bytes{}; // 128 is spec minimum
                uint32_t size = 0;
                uint32_t offset = 0;
                VkShaderStageFlags stageFlags = VK_SHADER_STAGE_ALL;
            };

            struct CmdDrawIndexed {
                Ref<VertexArray> va;
                uint32_t indexCount = 0;
                uint32_t instanceCount = 1;

                static constexpr uint32_t k_max_vertex_buffers = 4;
                std::array<Ref<VertexBuffer>, k_max_vertex_buffers> vertexBuffers{};
                std::array<uint32_t,        k_max_vertex_buffers> vertexBufferByteOffsets{};
                uint32_t vertexBufferCount = 0;
            };

            // Index into custom_callbacks; the callback records raw Vulkan commands.
            struct CmdCustomVulkan {
                uint32_t callback_index = 0;
            };

            struct Cmd {
                CmdType type{};
                CmdBeginSwapchainPass begin{};
                CmdBeginOffscreenPass offscreen{};
                CmdBindPipeline bindPipeline{};
                CmdBindGlobals globals{};
                CmdPushConstantsMat4 pushMat4{};
                CmdPushConstants push{};
                CmdDrawIndexed draw{};
                CmdCustomVulkan custom{};
            };

            std::vector<Cmd> cmds;
            // Storage for CustomVulkan callbacks; fn(cmd, pass_width, pass_height)
            std::vector<std::function<void(VkCommandBuffer, uint32_t, uint32_t)>> custom_callbacks;

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
                custom_callbacks.clear();
                frame_begun = true;

                sourceTag = FramePacket::CmdBindGlobals::Source::Unknown;

                // NOTE: we no longer auto-begin a swapchain pass here.
                // Pass boundaries are driven by Renderer::begin_pass / end_pass.
            }
        };

        FramePacket& frame_packet() { return m_frame_packet; }
        const FramePacket& frame_packet() const { return m_frame_packet; }

        uint32_t get_current_frame() const { return m_current_frame; }

        // Queues a custom Vulkan record callback into the current frame's FramePacket.
        // The callback fires during record_command_buffer, inside the currently-open render pass.
        // fn(VkCommandBuffer cmd, uint32_t pass_w, uint32_t pass_h)
        void queue_custom_vulkan_cmd(std::function<void(VkCommandBuffer, uint32_t, uint32_t)> fn);

        // Nulls out all pending CustomVulkan callbacks in the current frame packet so they
        // fire as no-ops. Call this before destroying GPU resources mid-frame (e.g. on_stop).
        void cancel_pending_custom_vulkan_cmds();

        uint32_t get_swapchain_image_format() const { return m_swapchain_image_format; }
        uint32_t get_swapchain_extent_width()  const { return m_swapchain_extent_width; }
        uint32_t get_swapchain_extent_height() const { return m_swapchain_extent_height; }

        void refresh_all_texture_samplers() override;

        // Uploads font curve data into both frames' SSBOs. Call once after loading a font.
        void upload_font_data(const void* band_table_data, uint32_t band_table_bytes,
                              const void* curve_data,       uint32_t curve_bytes);

        // Uploads icon curve data into the icon region of both frames' SSBOs (offset past font data).
        void upload_icon_data(const void* band_table_data, uint32_t band_table_bytes,
                              uint32_t band_table_byte_offset,
                              const void* curve_data,       uint32_t curve_bytes,
                              uint32_t curve_byte_offset);

    private:
        // Per-window only:
        void create_surface();

        void create_swapchain();
        void create_image_views();
        void create_render_pass();
        void create_framebuffers();

        void create_command_pool();
        void create_command_buffers();
        void create_secondary_command_pools();
        void reset_secondary_command_pools_for_frame(uint32_t frame_index);
        void cleanup_secondary_command_pools();

        void create_sync_objects();

        void create_timing_query_pool();

        void record_command_buffer(uint32_t image_index);

        void cleanup_swapchain();
        void recreate_swapchain_if_needed();
        bool wait_for_nonzero_framebuffer_size() const;

        void destroy();

        void create_global_descriptor_resources();
        void cleanup_global_descriptor_resources();
        void create_font_descriptor_resources();
        void cleanup_font_descriptor_resources();

        bool submit_one_time_on_queue(
            VkQueue queue,
            uint32_t queue_family,
            std::mutex* submit_mutex,
            VkCommandPool& inout_command_pool,
            VkFence& inout_fence,
            const std::function<void(VkCommandBuffer)>& record,
            const char* debug_label);

        inline void assert_render_thread() const {
#if defined(BUILD_DEBUG)
            HN_CORE_ASSERT(std::this_thread::get_id() == m_render_thread_id,
                           "VulkanContext method must be called from the render thread");
#endif
        }

public:
        static constexpr uint32_t k_max_frames_in_flight  = 2;
        static constexpr uint32_t k_max_chunks_per_frame  = 32;
        static constexpr uint32_t k_max_material_count    = 16384;
        // 8 bands × 95 printable ASCII glyphs (codepoints 32–126)
        static constexpr uint32_t k_max_font_band_entries = 760;
        // Upper bound for total curves after per-band replication
        static constexpr uint32_t k_max_font_curves       = 16384;
        // Icon region (lives at offset past the font region in the same SSBOs)
        static constexpr uint32_t k_max_icon_band_entries = 4096;
        static constexpr uint32_t k_max_icon_curves       = 32768;
        // Combined SSBO capacities
        static constexpr uint32_t k_total_band_entries    = k_max_font_band_entries + k_max_icon_band_entries;
        static constexpr uint32_t k_total_curves          = k_max_font_curves + k_max_icon_curves;

private:

        GLFWwindow* m_window_handle = nullptr;

        VulkanBackend* m_backend = nullptr;
        VulkanQueueLease m_queue_lease{};

        // Shared handles (owned by backend, cached here)
        VkInstance m_instance = nullptr;
        VkPhysicalDevice m_physical_device = nullptr;
        VkDevice m_device = nullptr;
        VkSurfaceKHR m_surface = nullptr;

        uint32_t m_graphics_queue_family = UINT32_MAX;
        uint32_t m_compute_queue_family = UINT32_MAX;
        uint32_t m_present_queue_family = UINT32_MAX;
        VkQueue m_graphics_queue = nullptr;
        VkQueue m_compute_queue = nullptr;
        VkQueue m_present_queue = nullptr;

        bool m_has_dedicated_compute_queue = false;
        bool m_supports_timeline_semaphore = false;

        VkSemaphore m_frame_graph_timeline_semaphore = VK_NULL_HANDLE;
        uint64_t m_frame_graph_timeline_value = 0;

        VkSwapchainKHR m_swapchain = nullptr;
        std::vector<void*> m_swapchain_images;
        std::vector<void*> m_swapchain_image_views;

        uint32_t m_swapchain_image_format = 0; // VkFormat stored as uint32_t to avoid vulkan.h in header
        uint32_t m_swapchain_extent_width = 0;
        uint32_t m_swapchain_extent_height = 0;

        VkRenderPass m_render_pass = nullptr;
        std::vector<VkFramebuffer> m_swapchain_framebuffers;

        VkFormat m_swapchain_depth_format = VK_FORMAT_UNDEFINED;
        std::vector<VkImage> m_swapchain_depth_images;
        std::vector<VkDeviceMemory> m_swapchain_depth_memories;
        std::vector<VkImageView> m_swapchain_depth_image_views;

        // Fonts
        VkDescriptorSetLayout m_font_set_layout = nullptr;
        VkDescriptorPool m_font_descriptor_pool = nullptr;
        VkDescriptorSet m_fonts_descriptor_sets[k_max_frames_in_flight][k_max_chunks_per_frame]{};
        //uint32_t m_chunk_ds_index[k_max_frames_in_flight]{};

        void* m_band_table_ubos[k_max_frames_in_flight]{};        // VkBuffer
        void* m_band_table_ubo_memories[k_max_frames_in_flight]{}; // VkDeviceMemory
        uint32_t m_band_table_ubo_size = 0;

        void* m_curve_ubos[k_max_frames_in_flight]{};        // VkBuffer
        void* m_curve_ubo_memories[k_max_frames_in_flight]{}; // VkDeviceMemory
        uint32_t m_curve_ubo_size = 0;


        VkDescriptorSetLayout m_global_set_layout = nullptr;
        VkDescriptorPool m_descriptor_pool = nullptr;
        VkDescriptorSet m_global_descriptor_sets[k_max_frames_in_flight][k_max_chunks_per_frame]{};
        uint32_t m_chunk_ds_index[k_max_frames_in_flight]{};

        void* m_camera_ubos[k_max_frames_in_flight]{};        // VkBuffer
        void* m_camera_ubo_memories[k_max_frames_in_flight]{}; // VkDeviceMemory
        uint32_t m_camera_ubo_size = 0;

        void* m_lights_ubos[k_max_frames_in_flight]{};        // VkBuffer
        void* m_lights_ubo_memories[k_max_frames_in_flight]{}; // VkDeviceMemory
        uint32_t m_lights_ubo_size = 0;

        void* m_materials_ssbo[k_max_frames_in_flight]{};        // VkBuffer
        void* m_materials_ssbo_memories[k_max_frames_in_flight]{}; // VkDeviceMemory
        uint32_t m_materials_ssbo_size = 0;

        std::array<void*, 32> m_last_bound_textures[k_max_frames_in_flight]{};
        uint32_t m_last_bound_texture_count[k_max_frames_in_flight]{};
        bool m_last_bound_textures_valid[k_max_frames_in_flight]{};

        VkCommandPool m_command_pool = nullptr;
        VkCommandPool m_async_graphics_command_pool = nullptr;
        VkCommandPool m_async_compute_command_pool = nullptr;
        VkFence m_async_graphics_fence = nullptr;
        VkFence m_async_compute_fence = nullptr;
        std::vector<VkCommandBuffer> m_command_buffers;

        // Per-frame secondary command pools.
        // Index 0 in each frame is reserved for serial/primary-thread secondary recording,
        // indices [1..] are used by worker threads for parallel secondary recording.
        std::array<std::vector<VkCommandPool>, k_max_frames_in_flight> m_secondary_command_pools{};
        uint32_t m_secondary_worker_count = 0;

        std::vector<VkSemaphore> m_image_available_semaphores;
        std::vector<VkSemaphore> m_render_finished_semaphores;
        std::vector<VkFence> m_in_flight_fences;

        std::vector<VkFence> m_images_in_flight;

        FramePacket m_frame_packet{};

        uint32_t m_current_frame = 0;

        bool m_framebuffer_resized = false;
        bool m_initialized = false;

        VkQueryPool m_timestamp_query_pool = VK_NULL_HANDLE;
        std::vector<bool> m_timestamp_valid;
        std::vector<double> m_gpu_frame_time_ms;
        std::vector<bool>  m_timestamp_written;
        std::vector<bool>  m_timestamp_written_this_frame;

        std::thread::id m_render_thread_id{};
    };

}