#pragma once

#include "Honey/renderer/frame_graph.h"
#include "Honey/renderer/gpu_types.h"

namespace Honey {

    class VulkanContext;

    // GPU-driven cubemap shadow maps for point lights.
    // Shadow cubemap is owned by the frame graph (shadowCubemap resource).
    // Shadow matrices SSBO is owned by VulkanContext (global binding 6).
    // No render work happens here — all passes are frame graph executors.
    class Renderer3DShadow {
    public:
        static void init(VulkanContext* ctx);
        static void shutdown();

        // Frame graph executor — registered as "shadow.draw".
        // Runs after GBuffer in the frame graph.
        // Reads shadow_draw_list from Renderer3DData, renders into the shadow cubemap.
        static void execute_draw(FrameGraphPassContext& ctx);
        static void execute_dir_draw(FrameGraphPassContext& ctx);

        static void register_frame_graph_executors();
        static bool is_initialized();

        // Call before rebuilding the frame graph so stale image views are cleared before
        // the old framebuffers are destroyed. Re-registration happens automatically on the next frame.
        static void invalidate_cubemap_resources();
        static void invalidate_dir_shadow_resources();

    private:
        Renderer3DShadow() = delete;
    };

} // namespace Honey