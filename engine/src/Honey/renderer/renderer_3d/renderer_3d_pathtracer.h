#pragma once

#include "Honey/renderer/frame_graph.h"
#include "Honey/renderer/gpu_types.h"
#include <glm/glm.hpp>

namespace Honey {

    class VulkanContext;
    class Scene;

    class Renderer3DPathTracer {
    public:
        static void init(VulkanContext* ctx);
        static void shutdown();
        static bool is_initialized();

        static void register_frame_graph_executors();

        static void set_camera(const glm::mat4& inv_view, const glm::mat4& inv_proj);
        static void set_lights(const LightsUBO& lights);

        static void invalidate_accumulation();

        static void invalidate_resources();

    private:
        Renderer3DPathTracer() = delete;
    };

}