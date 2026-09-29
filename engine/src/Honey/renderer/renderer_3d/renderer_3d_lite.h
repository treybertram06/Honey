
#include "renderer_3d_internal.h"
#include "platform/vulkan/vk_context.h"

namespace Honey {

    class Renderer3DLite {
    public:
        static void init(VulkanContext* ctx);
        static void shutdown();
        static void register_frame_graph_executors();
        static bool is_initialized();

        // Per-frame CPU intake (called from Renderer3D / DebugRenderer3D / editor overlay in lite mode)
        static void begin_frame(const glm::mat4& view_proj, const glm::vec3& cam_pos);
        static void set_directional_light(const glm::vec3& dir_ws, const glm::vec3& color, float intensity);
        static void submit_mesh(const Renderer3DInternal::MeshletDrawCommand& cmd);
        static void submit_debug_lines(const void* vertices, uint32_t vertex_count);
        static void submit_icon(const Renderer3DInternal::IconDrawCommand&);
    };

}
