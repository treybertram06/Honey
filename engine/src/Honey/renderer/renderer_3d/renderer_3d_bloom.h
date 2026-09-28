#pragma once
#include "Honey/renderer/frame_graph.h"

namespace Honey {

    class VulkanContext;

    class Renderer3DBloom {
    public:
        static void init(VulkanContext* ctx);
        static void shutdown();

        static void execute_threshold(FrameGraphPassContext& ctx);
        static void execute_downsample(FrameGraphPassContext& ctx);
        static void execute_upsample(FrameGraphPassContext& ctx);

        static void register_frame_graph_executors();
        static bool is_initialized();

    private:
        Renderer3DBloom() = delete;
    };
}
