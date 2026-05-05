#pragma once

#include "Honey/core/settings.h"
#include "Honey/core/timestep.h"
#include "Honey/renderer/frame_graph.h"
#include "Honey/renderer/framebuffer.h"
#include "Honey/renderer/renderer_3d/renderer_3d.h"
#include "Honey/scene/entity.h"

#include <functional>
#include <glm/glm.hpp>
#include <imgui.h>

namespace Honey {

    class Scene;

    struct SceneViewportRenderSettings {
        RendererSettings::RendererType renderer_type = RendererSettings::RendererType::forward;
        GeometryPath geometry_path = GeometryPath::Classic;
        bool collect_frame_graph_timings = true;
        bool log_frame_graph_pass_timings = false;
        bool debug_pick_enabled = false;
    };

    struct SceneViewportRenderContext {
        Scene* scene = nullptr;
        Timestep timestep{};
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::vec3 camera_position{0.0f};
        bool scene_is_runtime_camera = false;
        std::function<void()> post_scene_overlay_render;
    };

    class SceneViewportRenderer {
    public:
        void initialize();
        void shutdown();

        void resize(uint32_t width, uint32_t height);
        void set_settings(const SceneViewportRenderSettings& settings);
        void rebuild_if_needed();
        void render(const SceneViewportRenderContext& context);

        ImTextureID get_imgui_texture_id() const;
        Ref<Framebuffer> get_output_framebuffer() const { return m_output_framebuffer; }
        Entity pick_entity(uint32_t x, uint32_t y, Scene* scene) const;
        void log_debug_dump(const char* label) const;

        const FGExecutionStats& get_frame_graph_stats() const { return m_frame_graph_stats; }
        void mark_frame_graph_dirty() { m_frame_graph_dirty = true; }

        void execute_scene_pass(const SceneViewportRenderContext& context) const;

    private:
        void rebuild_frame_graph();

        Ref<Framebuffer> m_output_framebuffer;
        Ref<Framebuffer> m_gbuffer_framebuffer;
        std::shared_ptr<FrameGraphCompiled> m_frame_graph;
        FGExecutionStats m_frame_graph_stats{};
        SceneViewportRenderSettings m_settings{};
        uint32_t m_width = 1280;
        uint32_t m_height = 720;
        bool m_frame_graph_dirty = true;
        uint32_t m_frame_graph_frame_index = 0;
        glm::mat4 m_last_pt_view{0.0f}; // for pathtracer accumulation invalidation
    };

} // namespace Honey
