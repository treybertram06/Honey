#pragma once

#include "vk_backend.h"
#include "vk_queue_lease.h"
#include "Honey/renderer/graphics_context.h"
#include "Honey/renderer/vertex_array.h"
#include <glm/glm.hpp>
#include <functional>

#include "vk_globals.h"
#include "vk_gpu_profiler.h"
#include "vk_pipeline.h"
#include "../../Honey/renderer/gpu_types.h"
#include "Honey/renderer/framebuffer.h"

struct GLFWwindow;

#include "vk_types.h"

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

        VulkanBackend* get_backend() const { return m_backend; }
        VkDevice get_device() const { return m_device; }
        VkPhysicalDevice get_physical_device() const { return m_physical_device; }
        VkDescriptorSetLayout get_global_set_layout() const { return m_global_set_layout; }
        VkDescriptorSetLayout get_font_set_layout()   const { return m_font_set_layout; }
        // Returns the frame's global descriptor set (chunk 0; shadow draw uses it to access binding 6).
        VkDescriptorSet get_global_descriptor_set(uint32_t frame) const { return m_global_descriptor_sets[frame][0]; }
        // Returns the font SSBO descriptor set for the given frame (all chunk sets are identical).
        VkDescriptorSet get_font_descriptor_set(uint32_t frame) const { return m_fonts_descriptor_sets[frame][0]; }

        // Shadow matrices SSBO (binding 6 in global set) — call once per frame before rendering.
        void upload_shadow_matrices(uint32_t frame, const ShadowMatricesSSBO& data);
        // Writes shadow cubemap view/sampler into set=0 binding 8 (forward pass global set).
        void set_shadow_cubemap_resources(VkImageView cube_array_view, VkSampler comparison_sampler);

        // Directional shadows SSBO (binding 7) - call once before rendering
        void upload_directional_shadows(uint32_t frame, const DirectionalShadowSSBO& data);
        // Writes directional shadow map view/sampler into set=0 binding 9 (forward pass global set).
        void set_dir_shadow_resources(VkImageView cube_array_view, VkSampler comparison_sampler);

        // RenderDoc / debug label helpers — no-ops when debug utils extension is absent.
        void cmd_begin_debug_label(VkCommandBuffer cmd, const char* name,
                                   float r = 1.0f, float g = 1.0f, float b = 0.0f) const {
            if (m_pfnCmdBeginDebugLabel) {
                VkDebugUtilsLabelEXT label{};
                label.sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
                label.pLabelName = name;
                label.color[0]   = r; label.color[1] = g; label.color[2] = b; label.color[3] = 1.0f;
                m_pfnCmdBeginDebugLabel(cmd, &label);
            }
        }
        void cmd_end_debug_label(VkCommandBuffer cmd) const {
            if (m_pfnCmdEndDebugLabel)
                m_pfnCmdEndDebugLabel(cmd);
        }

        VkCommandBuffer get_recording_cmd() const { return m_recording_cmd; }
        uint32_t get_recording_image_index() const { return m_recording_image_index; }
        bool is_recording() const { return m_recording_active; }
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

        // Swapchain per-image resources accessed by Renderer::begin_pass / end_pass.
        VkFramebuffer get_swapchain_framebuffer(uint32_t idx) const {
            return (idx < m_swapchain_framebuffers.size())
                   ? reinterpret_cast<VkFramebuffer>(m_swapchain_framebuffers[idx]) : VK_NULL_HANDLE;
        }
        VkImageView get_swapchain_image_view(uint32_t idx) const {
            return (idx < m_swapchain_image_views.size())
                   ? reinterpret_cast<VkImageView>(m_swapchain_image_views[idx]) : VK_NULL_HANDLE;
        }

        void mark_pipeline_dirty() { /* m_pipeline_dirty = true; */ } // Pipelines are owned by renderer2d now
        const VulkanPipelineCacheBlob& get_pipeline_cache() const { return m_backend->get_pipeline_cache(); }
        void request_swapchain_recreation() { m_framebuffer_resized = true; }

        // Per-frame accumulated GPU globals — populated by submit_camera / submit_lights / etc.
        // and consumed by flush_globals() → apply_pending_globals().
        struct PendingGlobals {
            CameraUBO cameraUBO{};
            bool hasCamera = false;

            LightsUBO lightUBO{};
            TiledLightingData tiledLighting{};

            std::vector<GPUMaterial> materials{};
            uint32_t materials_ssbo_offset = 0;

            std::vector<void*> textures;
            uint32_t textureCount = 0;
            bool hasTextures = false;

            enum class Source : uint8_t { Unknown = 0, Renderer2D, Renderer3D } source = Source::Unknown;

            void reset() {
                hasCamera   = false;
                hasTextures = false;
                textures.clear();
                textureCount = 0;
                source = Source::Unknown;
            }
        };

        PendingGlobals& pending_globals() { return m_pending_globals; }
        const PendingGlobals& pending_globals() const { return m_pending_globals; }

        void flush_globals_to_heap();

        glm::vec4 get_clear_color() const { return m_pending_clear_color; }
        void      set_clear_color(const glm::vec4& c) { m_pending_clear_color = c; }

        uint32_t get_current_frame() const { return m_current_frame; }

        // Queues a custom Vulkan record callback to fire during the current frame's active render pass.
        // fn(VkCommandBuffer cmd, uint32_t pass_w, uint32_t pass_h)
        void queue_custom_vulkan_cmd(std::function<void(VkCommandBuffer, uint32_t, uint32_t)> fn);

        // Nulls out all pending CustomVulkan callbacks in the current frame packet so they
        // fire as no-ops. Call this before destroying GPU resources mid-frame (e.g. on_stop).
        void cancel_pending_custom_vulkan_cmds();

        // --- Direct-recording pass state (set by Renderer::begin_pass/end_pass) ---
        void open_render_pass(bool is_swapchain, VkExtent2D extent) {
            m_render_pass_open         = true;
            m_current_pass_is_swapchain = is_swapchain;
            m_current_pass_extent      = extent;
            m_current_pipeline_layout  = VK_NULL_HANDLE;
        }
        void close_render_pass() {
            m_render_pass_open          = false;
            m_current_pass_is_swapchain = false;
            m_current_pipeline_layout   = VK_NULL_HANDLE;
        }
        bool           is_render_pass_open()           const { return m_render_pass_open; }
        bool           is_current_pass_swapchain()     const { return m_current_pass_is_swapchain; }
        VkExtent2D     get_current_pass_extent()       const { return m_current_pass_extent; }
        VkPipelineLayout get_current_pipeline_layout() const { return m_current_pipeline_layout; }
        void set_current_pipeline_layout(VkPipelineLayout layout) { m_current_pipeline_layout = layout; }

        // Uploads UBOs, updates the global descriptor set, and records vkCmdBindDescriptorSets.
        // Called directly by VulkanRendererAPI::flush_globals() in the direct-recording path.
        void apply_pending_globals(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t frame,
                                   const PendingGlobals& g);

        uint32_t get_swapchain_image_format() const { return m_swapchain_image_format; }
        uint32_t get_swapchain_extent_width()  const { return m_swapchain_extent_width; }
        uint32_t get_swapchain_extent_height() const { return m_swapchain_extent_height; }

        void begin_frame_recording();
        void end_frame_recording();

        void refresh_all_texture_samplers() override;

        // Uploads font curve data into both frames' SSBOs. Call once after loading a font.
        void upload_font_data(const void* band_table_data, uint32_t band_table_bytes,
                              const void* curve_data,       uint32_t curve_bytes);

        // Uploads icon curve data into the icon region of both frames' SSBOs (offset past font data).
        void upload_icon_data(const void* band_table_data, uint32_t band_table_bytes,
                              uint32_t band_table_byte_offset,
                              const void* curve_data,       uint32_t curve_bytes,
                              uint32_t curve_byte_offset);

        GpuProfiler& get_gpu_profiler() { return m_gpu_profiler; }

        uint32_t get_gpu_zone_count() const override { return m_gpu_profiler.get_slot_count(); }
        const char* get_gpu_zone_name(uint32_t slot) const override { return m_gpu_profiler.get_slot_name(slot); }
        double get_gpu_zone_time_ms(uint32_t slot) const override { return m_gpu_profiler.get_slot_time_ms(slot); }

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


        void cleanup_swapchain();
        void recreate_swapchain_if_needed();
        bool wait_for_nonzero_framebuffer_size() const;

        void destroy();

        void create_global_descriptor_resources();
        void cleanup_global_descriptor_resources();
        void create_global_descriptor_heap_resources();
        void cleanup_global_descriptor_heap_resources();
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

        PFN_vkCmdBeginDebugUtilsLabelEXT m_pfnCmdBeginDebugLabel = nullptr;
        PFN_vkCmdEndDebugUtilsLabelEXT   m_pfnCmdEndDebugLabel   = nullptr;

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

        VkCommandBuffer m_recording_cmd          = VK_NULL_HANDLE;
        uint32_t        m_recording_image_index  = 0;
        bool            m_recording_active       = false;

        // Direct-recording pass state (mirrors the locals that lived in record_command_buffer)
        bool             m_render_pass_open          = false;
        bool             m_current_pass_is_swapchain = false;
        VkExtent2D       m_current_pass_extent       = {};
        VkPipelineLayout m_current_pipeline_layout   = VK_NULL_HANDLE;

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

        VkBuffer m_globals_buffer{};
        VkDeviceMemory m_globals_alloc{};
        uint8_t* m_globals_mapped{};
        GlobalsLayout m_globals_layout{};

        void* m_camera_ubos[k_max_frames_in_flight]{};        // VkBuffer
        void* m_camera_ubo_memories[k_max_frames_in_flight]{}; // VkDeviceMemory
        uint32_t m_camera_ubo_size = 0;

        void* m_lights_ubos[k_max_frames_in_flight]{};        // VkBuffer
        void* m_lights_ubo_memories[k_max_frames_in_flight]{}; // VkDeviceMemory
        uint32_t m_lights_ubo_size = 0;

        void* m_materials_ssbo[k_max_frames_in_flight]{};        // VkBuffer
        void* m_materials_ssbo_memories[k_max_frames_in_flight]{}; // VkDeviceMemory
        uint32_t m_materials_ssbo_size = 0;

        void* m_tiled_lighting_ssbos[k_max_frames_in_flight]{};        // VkBuffer
        void* m_tiled_lighting_ssbo_memories[k_max_frames_in_flight]{}; // VkDeviceMemory
        uint32_t m_tiled_lighting_ssbo_size = 0;

        void* m_shadow_matrices_ssbos[k_max_frames_in_flight]{};        // VkBuffer
        void* m_shadow_matrices_ssbo_memories[k_max_frames_in_flight]{}; // VkDeviceMemory
        uint32_t m_shadow_matrices_ssbo_size = 0;

        void* m_dir_shadow_ssbos[k_max_frames_in_flight]{};        // VkBuffer
        void* m_dir_shadow_ssbo_memories[k_max_frames_in_flight]{}; // VkDeviceMemory
        uint32_t m_dir_shadow_ssbo_size = 0;

        std::vector<void*> m_last_bound_textures[k_max_frames_in_flight];  // up to VulkanRendererAPI::k_max_texture_slots entries
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

        PendingGlobals m_pending_globals{};
        glm::vec4     m_pending_clear_color{0.1f, 0.1f, 0.1f, 1.0f};

        uint32_t m_current_frame = 0;

        bool m_framebuffer_resized = false;
        bool m_initialized = false;

        VkQueryPool m_timestamp_query_pool = VK_NULL_HANDLE;
        std::vector<bool> m_timestamp_valid;
        std::vector<double> m_gpu_frame_time_ms;
        std::vector<bool>  m_timestamp_written;
        std::vector<bool>  m_timestamp_written_this_frame;

        GpuProfiler m_gpu_profiler;

        std::thread::id m_render_thread_id{};
    };

}
