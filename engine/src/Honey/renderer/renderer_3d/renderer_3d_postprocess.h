#pragma once
#include "Honey/renderer/frame_graph.h"

namespace Honey {

    class VulkanContext;

    class Renderer3DPostProcess {
    public:
        static void init(VulkanContext* ctx);
        static void shutdown();

        static void execute_draw(FrameGraphPassContext& ctx);

        static void register_frame_graph_executors();
        static bool is_initialized();

    private:
        Renderer3DPostProcess() = delete;
    };
}
