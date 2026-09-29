#include "hnpch.h"
#include "renderer_3d_lite.h"

namespace Honey {

    namespace {
        struct LiteRendererResources {
            VulkanContext* vk_ctx = nullptr;

            // pipelines
            // draw state

        };
        static LiteRendererResources* s_res = nullptr;

    }

    void Renderer3DLite::init(VulkanContext *ctx) {
        if (s_res) return;
        s_res = new LiteRendererResources{};
        s_res->vk_ctx = ctx;
    }

    void Renderer3DLite::shutdown() {
        if (!s_res) return;
        delete s_res;
        s_res = nullptr;
    }

    void Renderer3DLite::register_frame_graph_executors() {
    }

    bool Renderer3DLite::is_initialized() {
        return s_res != nullptr;
    }

    void Renderer3DLite::begin_frame(const glm::mat4 &view_proj, const glm::vec3 &cam_pos) {
    }

    void Renderer3DLite::set_directional_light(const glm::vec3 &dir_ws, const glm::vec3 &color, float intensity) {
    }

    void Renderer3DLite::submit_mesh(const Renderer3DInternal::MeshletDrawCommand &cmd) {
    }

    void Renderer3DLite::submit_debug_lines(const void *vertices, uint32_t vertex_count) {
    }

    void Renderer3DLite::submit_icon(const Renderer3DInternal::IconDrawCommand &) {
    }

    void Renderer3DInternal::flush_lite_draws() {

    }
}
