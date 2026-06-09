#pragma once
#include "Honey/renderer/frame_graph.h"

namespace Honey {

    class VulkanContext;

    class Renderer3DSSAO {
    public:
        static void init(VulkanContext* ctx);
        static void shutdown();

        static void execute_draw(FrameGraphPassContext& ctx);
        static void execute_blur(FrameGraphPassContext& ctx);

        static Ref<Texture2D> get_noise_texture();

        static void register_frame_graph_executors();
        static bool is_initialized();

    private:
        Renderer3DSSAO() = delete;
    };
}
