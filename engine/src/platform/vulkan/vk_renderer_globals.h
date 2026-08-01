#pragma once

#include "vk_context.h"
#include "vk_globals.h"
#include "Honey/renderer/global_bindings.h"
#include "Honey/renderer/gpu_types.h"

#include <vulkan/vulkan.h>
#include <vector>

namespace Honey {

    class VulkanBackend;

    // Owns the data plane behind the descriptor heap's set-0 global bindings: the device-local
    // globals buffer, its host mirror and per-frame staging, the pending producer state, and — until
    // Part C of the migration lands — the materials SSBO. Its schema is k_global_bindings; every
    // noun in that table is a renderer concept, which is why this is renderer-owned state and not a
    // VulkanContext member. The descriptor heap itself stays on VulkanBackend: slot allocation is
    // mechanism, the bytes behind the slots are policy.
    //
    // Device lifetime: created once by Renderer::init, destroyed once by Renderer::shutdown. Frame
    // graph rebuilds, swapchain recreation and RendererType switches must not touch it —
    // VulkanDescriptorHeap::register_global_binding asserts on re-register, so any lifecycle that
    // re-inits this without a backend restart is a bug by construction.
    class VulkanRendererGlobals {
    public:
        // Per-frame accumulated GPU globals — populated by VulkanRendererAPI::submit_camera /
        // submit_lights / etc. and folded into the host mirror by flush_pending().
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

        void init(VulkanBackend* backend);
        void shutdown();

        // Clears the pending producer state and records this frame's staging -> globals-buffer
        // upload at the top of the frame's command buffer. Call once per frame, before any pass can
        // read set 0.
        void begin_frame(VkCommandBuffer cmd, uint32_t frame);

        // Snapshots the host mirror into this frame's staging buffer. Must run after every
        // begin_frame() and before the frame is submitted.
        void snapshot(uint32_t frame);

        // Folds the pending producer state into the host mirror.
        void flush_pending(uint32_t frame);

        void set_shadow_matrices(const ShadowMatricesSSBO& data);
        void set_directional_shadows(const DirectionalShadowSSBO& data);

        PendingGlobals& pending_globals() { return m_pending_globals; }
        const PendingGlobals& pending_globals() const { return m_pending_globals; }

    private:
        void create_globals_resources();
        void register_heap_bindings();

        // Per-frame host-visible materials SSBOs, bound into the heap by device address. Survivor of
        // the classic descriptor machinery — Part C replaces this with a single device-local buffer
        // fed by staging, at which point these and write_materials_heap_binding() go away.
        void create_materials_resources();
        void cleanup_materials_resources();
        void write_materials_heap_binding(uint32_t frame);

        VulkanBackend*   m_backend         = nullptr;
        VkDevice         m_device          = VK_NULL_HANDLE;
        VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;

        // Set-0 globals for heap-mode pipelines. m_globals_buffer is DEVICE_LOCAL and its device
        // address is baked into the descriptor-heap slots once at init (and from there into every
        // pipeline's descriptor mappings), so it must never move. Producers therefore write into
        // m_globals_cpu — plain host memory, invisible to the GPU — which is copied into the
        // current frame's staging buffer at end of recording and uploaded by a vkCmdCopyBuffer
        // recorded at the top of the frame. That keeps the CPU from overwriting bytes the GPU is
        // still reading for a previous in-flight frame.
        VkBuffer m_globals_buffer{};
        VkDeviceMemory m_globals_alloc{};
        std::vector<uint8_t> m_globals_cpu{};
        VkBuffer m_globals_staging[VulkanContext::k_max_frames_in_flight]{};
        VkDeviceMemory m_globals_staging_alloc[VulkanContext::k_max_frames_in_flight]{};
        uint8_t* m_globals_staging_mapped[VulkanContext::k_max_frames_in_flight]{};
        GlobalsLayout m_globals_layout{};

        void* m_materials_ssbo[VulkanContext::k_max_frames_in_flight]{};          // VkBuffer
        void* m_materials_ssbo_memories[VulkanContext::k_max_frames_in_flight]{}; // VkDeviceMemory
        uint32_t m_materials_ssbo_size = 0;

        PendingGlobals m_pending_globals{};

        // begin_frame()/snapshot() pairing is no longer structural now that the snapshot lives in
        // Renderer::end_frame() rather than inside end_frame_recording(). A frame that begins
        // without being snapshotted would upload the *previous* frame's staging content — globals
        // one frame stale, which reads as camera lag under motion rather than as a crash. Assert
        // the alternation so the failure is loud instead of subtle.
        bool m_snapshot_pending = false;
    };

}
