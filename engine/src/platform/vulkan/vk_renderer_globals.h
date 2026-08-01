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
    // globals and materials buffers, their host mirrors and per-frame staging, and the pending
    // producer state. Its schema is k_global_bindings; every noun in that table is a renderer
    // concept, which is why this is renderer-owned state and not a VulkanContext member. The
    // descriptor heap itself stays on VulkanBackend: slot allocation is mechanism, the bytes
    // behind the slots are policy.
    //
    // Device lifetime: created once by Renderer::init, destroyed once by Renderer::shutdown. Frame
    // graph rebuilds, swapchain recreation and RendererType switches must not touch it —
    // VulkanDescriptorHeap::register_global_binding asserts on re-register, so any lifecycle that
    // re-inits this without a backend restart is a bug by construction.
    class VulkanRendererGlobals {
    public:
        // Element capacity of the materials buffer. A renderer concept, so it lives here rather
        // than on VulkanContext.
        static constexpr uint32_t k_max_material_count = 16384;

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

        void create_materials_resources();
        void cleanup_materials_resources();

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

        // Materials, on the same mirror -> staging -> device-local pattern as the globals above,
        // with one difference forced by timing: the copy region has to be fixed when the copy is
        // *recorded* (frame top, outside any render pass), but producers only supply materials
        // mid-frame, from inside the g-buffer pass. A dirty range describes what changed and can
        // therefore only be computed after the change — too late. m_materials_hwm is a bound
        // instead: a monotonic high-water element count, known at frame top, guaranteed to cover
        // everything live. Copying a superset is always correct; the surplus is a re-copy of
        // identical bytes. Cost tracks the live material count, not the ~3.5 MB capacity.
        //
        // Consequence, deliberate: on a frame whose material count exceeds the session high (scene
        // load, entity add) the new tail lands one frame late and reads the default material. The
        // count is view-independent, so this never fires on camera motion.
        static constexpr uint32_t k_materials_capacity_bytes = sizeof(GPUMaterial) * k_max_material_count;

        VkBuffer m_materials_buffer{};
        VkDeviceMemory m_materials_alloc{};
        std::vector<uint8_t> m_materials_cpu{};
        VkBuffer m_materials_staging[VulkanContext::k_max_frames_in_flight]{};
        VkDeviceMemory m_materials_staging_alloc[VulkanContext::k_max_frames_in_flight]{};
        uint8_t* m_materials_staging_mapped[VulkanContext::k_max_frames_in_flight]{};

        uint32_t m_materials_hwm = 0;
        // Element count begin_frame() recorded for that frame, read back by snapshot() so the
        // staged byte count and the recorded copy region cannot drift apart.
        uint32_t m_materials_upload_elems[VulkanContext::k_max_frames_in_flight]{};
        // Makes frame 0 a one-time full-capacity copy, so the device buffer holds default
        // materials from the start rather than uninitialised memory.
        bool m_materials_initial_upload = true;

        PendingGlobals m_pending_globals{};

        // begin_frame()/snapshot() pairing is no longer structural now that the snapshot lives in
        // Renderer::end_frame() rather than inside end_frame_recording(). A frame that begins
        // without being snapshotted would upload the *previous* frame's staging content — globals
        // one frame stale, which reads as camera lag under motion rather than as a crash. Assert
        // the alternation so the failure is loud instead of subtle.
        bool m_snapshot_pending = false;
    };

}
