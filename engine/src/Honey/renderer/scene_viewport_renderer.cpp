#include "hnpch.h"

#include "scene_viewport_renderer.h"

#include "Honey/core/engine.h"
#include "Honey/core/settings.h"
#include "Honey/renderer/frame_graph_loader.h"
#include "Honey/renderer/frame_graph_registry.h"
#include "Honey/renderer/renderer.h"
#include "Honey/renderer/renderer_2d.h"
#include "Honey/renderer/renderer_3d/renderer_3d_shadow.h"
#include "Honey/scene/scene.h"
#include "platform/vulkan/vk_context.h"

#include <filesystem>

namespace Honey {

    static const std::filesystem::path asset_root = ASSET_ROOT;

    namespace {
        struct SceneViewportFrameGraphExecutionContext {
            SceneViewportRenderer* renderer = nullptr;
            const SceneViewportRenderContext* render_context = nullptr;
        };

        bool s_scene_viewport_executors_registered = false;

        void ensure_scene_viewport_frame_graph_executors_registered() {
            if (s_scene_viewport_executors_registered)
                return;

            Renderer3DShadow::register_frame_graph_executors();

            auto& registry = FrameGraphRegistry::get();

            registry.register_executor("editor.scene", [](FrameGraphPassContext& ctx) {
                auto* exec = ctx.user_context_as<SceneViewportFrameGraphExecutionContext>();
                HN_CORE_ASSERT(exec && exec->renderer && exec->render_context,
                    "editor.scene executor requires SceneViewportFrameGraphExecutionContext");
                exec->renderer->execute_scene_pass(*exec->render_context);
            });

            registry.register_executor("deferred.gbuffer", [](FrameGraphPassContext& ctx) {
                auto* exec = ctx.user_context_as<SceneViewportFrameGraphExecutionContext>();
                HN_CORE_ASSERT(exec && exec->renderer && exec->render_context,
                    "deferred.gbuffer executor requires SceneViewportFrameGraphExecutionContext");
                exec->renderer->execute_scene_pass(*exec->render_context);
            });

            registry.register_executor("deferred.lighting", [](FrameGraphPassContext& ctx) {
                Ref<Framebuffer> gbuffer_fb = ctx.get_input_framebuffer("gBuffer");
                if (!gbuffer_fb) {
                    HN_CORE_WARN("deferred.lighting: could not get GBuffer framebuffer");
                    return;
                }

                Renderer3D::begin_deferred_lighting_scene(gbuffer_fb);
                Renderer3D::flush_deferred_lighting();
            });

            s_scene_viewport_executors_registered = true;
        }
    } // namespace

    void SceneViewportRenderer::initialize() {
        ensure_scene_viewport_frame_graph_executors_registered();

        // Initialize shadow system
        {
            auto* base = Application::get().get_window().get_context();
            auto* vk_ctx = dynamic_cast<VulkanContext*>(base);
            if (vk_ctx)
                Renderer3DShadow::init(vk_ctx);
        }

        FramebufferSpecification fb_spec;
        fb_spec.attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
        fb_spec.width = m_width;
        fb_spec.height = m_height;
        m_output_framebuffer = Framebuffer::create(fb_spec);

        FramebufferSpecification gbuffer_spec;
        gbuffer_spec.attachments = {
            FramebufferTextureFormat::RGBA8,
            FramebufferTextureFormat::RGBA16F,
            FramebufferTextureFormat::RGBA8,
            FramebufferTextureFormat::Depth
        };
        gbuffer_spec.width          = m_width;
        gbuffer_spec.height         = m_height;
        gbuffer_spec.depth_samplable = true;
        m_gbuffer_framebuffer = Framebuffer::create(gbuffer_spec);

        m_frame_graph_dirty = true;
        rebuild_if_needed();
    }

    void SceneViewportRenderer::shutdown() {
        Renderer3DShadow::shutdown();
        m_frame_graph.reset();
        m_output_framebuffer.reset();
        m_gbuffer_framebuffer.reset();
    }

    void SceneViewportRenderer::resize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0)
            return;

        m_width = width;
        m_height = height;

        if (m_output_framebuffer)
            m_output_framebuffer->resize(width, height);
        if (m_gbuffer_framebuffer)
            m_gbuffer_framebuffer->resize(width, height);

        m_frame_graph_dirty = true;
    }

    void SceneViewportRenderer::set_settings(const SceneViewportRenderSettings& settings) {
        if (m_settings.renderer_type != settings.renderer_type)
            m_frame_graph_dirty = true;

        m_settings = settings;
        Renderer3D::set_geometry_render_path(m_settings.geometry_path);
        Renderer2D::set_debug_pick_enabled(m_settings.debug_pick_enabled);
    }

    void SceneViewportRenderer::rebuild_if_needed() {
        if (m_frame_graph_dirty || !m_frame_graph)
            rebuild_frame_graph();
    }

    void SceneViewportRenderer::render(const SceneViewportRenderContext& context) {
        if (!context.scene)
            return;

        rebuild_if_needed();
        if (!m_frame_graph)
            return;

        Scene::set_active_scene(context.scene);

        SceneViewportFrameGraphExecutionContext exec_data{};
        exec_data.renderer = this;
        exec_data.render_context = &context;

        FGExecutionContext fg_exec{};
        fg_exec.frame_index = m_frame_graph_frame_index++;
        fg_exec.user_context = &exec_data;
        fg_exec.collect_cpu_timings = m_settings.collect_frame_graph_timings;
        fg_exec.log_pass_execution = m_settings.log_frame_graph_pass_timings;
        fg_exec.out_stats = &m_frame_graph_stats;

        m_frame_graph->execute(fg_exec);
        Renderer::set_render_target(nullptr);
    }

    ImTextureID SceneViewportRenderer::get_imgui_texture_id() const {
        return m_output_framebuffer ? m_output_framebuffer->get_imgui_color_texture_id(0) : ImTextureID{};
    }

    Entity SceneViewportRenderer::pick_entity(uint32_t x, uint32_t y, Scene* scene) const {
        if (!m_output_framebuffer || !scene)
            return Entity{};

        const FramebufferSpecification& spec = m_output_framebuffer->get_specification();
        if (x >= spec.width || y >= spec.height)
            return Entity{};

        const int id = m_output_framebuffer->read_pixel(1, static_cast<int>(x), static_cast<int>(y));
        if (id == -1)
            return Entity{};

        const entt::entity raw = static_cast<entt::entity>(static_cast<uint32_t>(id));
        Entity picked{ raw, scene };
        return picked.is_valid() ? picked : Entity{};
    }

    void SceneViewportRenderer::log_debug_dump(const char* label) const {
        if (m_frame_graph)
            m_frame_graph->log_debug_dump(label);
    }

    void SceneViewportRenderer::rebuild_frame_graph() {
        // Clear stale shadow cubemap handles before the old frame graph (and its framebuffers)
        // are destroyed. Re-registration happens automatically on the first ShadowDraw execution.
        Renderer3DShadow::invalidate_cubemap_resources();

        FGCompileDiagnostics diags;
        FGCompileOptions options{};
        options.external_framebuffers.emplace("editorViewport", m_output_framebuffer);
        options.external_framebuffers.emplace("gBuffer", m_gbuffer_framebuffer);
        options.requested_output_resources.emplace_back("editorViewport");

        const char* fg_file =
            m_settings.renderer_type == RendererSettings::RendererType::deferred
                ? "main_deferred.hnfg"
                : "main_forward.hnfg";

        m_frame_graph = FrameGraphLoader::load_and_compile_from_file(
            asset_root / "frame_graphs" / fg_file,
            diags,
            &options);

        FrameGraphLoader::log_diagnostics(diags, "Scene Viewport Frame Graph Rebuild");
        m_frame_graph_dirty = false;
    }

    void SceneViewportRenderer::execute_scene_pass(const SceneViewportRenderContext& context) const {
        HN_CORE_ASSERT(context.scene, "SceneViewportRenderer requires a valid scene");
        context.scene->render(context.view, context.projection * context.view, context.camera_position, m_width, m_height);
        if (context.post_scene_overlay_render)
            context.post_scene_overlay_render();
    }

} // namespace Honey
