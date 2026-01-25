#pragma once

#include "camera.h"
#include "render_command.h"
#include "shader.h"
#include "shader_cache.h"

namespace Honey {

    class Renderer {
    public:

        static void init();
        static void shutdown();
        static void on_window_resize(uint32_t width, uint32_t height);

        static void begin_frame();

        static void set_render_target(const Ref<Framebuffer>& framebuffer);
        static void begin_pass();
        static void end_pass();

        static void begin_scene(OrthographicCamera& camera);
        static void end_scene();

        static void submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertex_array, const glm::mat4& transform = glm::mat4(1.0f));

        inline static RendererAPI::API get_api() { return RendererAPI::get_api(); }

    private:
        struct SceneData {
            glm::mat4 view_projection_matrix;
        };

        static SceneData* m_scene_data;

        static std::unique_ptr<ShaderCache> m_shader_cache;

        static Ref<Framebuffer> s_current_target; // nullptr means default window / swapchain
        static bool s_pass_open;
    };

}
