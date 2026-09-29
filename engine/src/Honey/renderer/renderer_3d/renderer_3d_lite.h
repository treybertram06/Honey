#pragma once

#include "renderer_3d_internal.h"
#include "Honey/renderer/frame_graph.h"
#include "platform/vulkan/vk_context.h"

namespace Honey {

    // Lite render tier: no descriptor heap, no bindless, no mesh shaders. Draws the meshes that
    // Renderer3D collected this frame (g_renderer3d_data->meshlet_draws) with plain descriptor sets
    // and vertex pulling. The "lite.scene" executor (scene_viewport_renderer.cpp) runs the scene
    // pass to collect draws, then calls record().
    class Renderer3DLite {
    public:
        static void init(VulkanContext* ctx);
        static void shutdown();
        static bool is_initialized();

        // Records this frame's mesh draws into the pass's live command buffer.
        static void record(FrameGraphPassContext& ctx);

        // Phase 4
        static void submit_debug_lines(const void* vertices, uint32_t vertex_count);
        static void submit_icon(const Renderer3DInternal::IconDrawCommand&);
    };

}