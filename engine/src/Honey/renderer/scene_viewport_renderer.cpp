#include "hnpch.h"

#include "scene_viewport_renderer.h"

#include "Honey/core/engine.h"
#include "Honey/core/settings.h"
#include "Honey/renderer/frame_graph_loader.h"
#include "Honey/renderer/frame_graph_registry.h"
#include "Honey/renderer/renderer.h"
#include "Honey/renderer/renderer_2d.h"
#include "Honey/renderer/renderer_3d/renderer_3d.h"
#include "Honey/renderer/renderer_3d/renderer_3d_shadow.h"
#include "Honey/renderer/renderer_3d/renderer_3d_pathtracer.h"
#include "Honey/renderer/renderer_3d/renderer_3d_ssao.h"
#include "Honey/renderer/gpu_types.h"
#include "Honey/scene/components.h"
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
            Renderer3DPathTracer::register_frame_graph_executors();
            Renderer3DSSAO::register_frame_graph_executors();

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
                Ref<Framebuffer> ssao_fb = ctx.get_input_framebuffer("ssaoTexture");
                Ref<Framebuffer> shadow_cube_fb = ctx.get_input_framebuffer("shadowCubemap");
                Ref<Framebuffer> shadow_dir_fb = ctx.get_input_framebuffer("shadowDirMap");
                if (!gbuffer_fb || !ssao_fb || !shadow_cube_fb || !shadow_dir_fb) {
                    HN_CORE_WARN("deferred.lighting: Missing frame graph inputs, skipping");
                    return;
                }

                Renderer3D::write_ssao_fb_to_renderer_state(ssao_fb);
                Renderer3D::write_gbuffer_to_renderer_state(gbuffer_fb);
                Renderer3D::flush_deferred_lighting(ctx);
            });

            s_scene_viewport_executors_registered = true;
        }
    } // namespace

    void SceneViewportRenderer::initialize() {
        ensure_scene_viewport_frame_graph_executors_registered();

        // Initialize shadow system (requires mesh shader support)
        {
            auto* base   = Application::get().get_window().get_context();
            auto* vk_ctx = dynamic_cast<VulkanContext*>(base);
            if (vk_ctx) {
                if (Application::get().get_vulkan_backend().supports_mesh_shader()) {
                    Renderer3DShadow::init(vk_ctx);
                    Renderer3DPathTracer::init(vk_ctx);
                }
                Renderer3DSSAO::init(vk_ctx);
            }
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
        Renderer3DPathTracer::shutdown();
        Renderer3DSSAO::shutdown();
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

        if (Renderer3DPathTracer::is_initialized() &&
            m_settings.renderer_type == RendererSettings::RendererType::pathtracing) {
            glm::mat4 inv_view = glm::inverse(context.view);
            glm::mat4 inv_proj = glm::inverse(context.projection);
            Renderer3DPathTracer::set_camera(inv_view, inv_proj);

            LightsUBO lights_ubo{};

            auto dl_group = context.scene->get_registry().group<DirectionalLightComponent>(entt::get<TransformComponent>);
            for (auto ent : dl_group) {
                auto& dl = dl_group.get<DirectionalLightComponent>(ent);
                auto& tc = dl_group.get<TransformComponent>(ent);
                if (!dl.enabled) continue;
                lights_ubo.directional_light.color     = dl.color;
                lights_ubo.directional_light.intensity = dl.intensity;
                lights_ubo.directional_light.direction = glm::normalize(glm::vec3(tc.get_transform() * glm::vec4(0, -1, 0, 0)));
                break;
            }

            int pl_count = 0;
            auto pl_view = context.scene->get_registry().view<PointLightComponent, TransformComponent>();
            for (auto ent : pl_view) {
                if (pl_count >= (int)k_max_point_lights) break;
                auto& pl = pl_view.get<PointLightComponent>(ent);
                auto& tc = pl_view.get<TransformComponent>(ent);
                if (!pl.enabled) continue;
                lights_ubo.point_lights[pl_count].color     = pl.color;
                lights_ubo.point_lights[pl_count].intensity = pl.intensity;
                lights_ubo.point_lights[pl_count].range     = pl.range;
                lights_ubo.point_lights[pl_count].position  = glm::vec3(tc.get_transform() * glm::vec4(0, 0, 0, 1));
                pl_count++;
            }
            lights_ubo.set_point_light_count(pl_count);

            Renderer3DPathTracer::set_lights(lights_ubo);

            bool lights_changed = (memcmp(&lights_ubo, &m_last_pt_lights_ubo, sizeof(LightsUBO)) != 0);
            if (context.view != m_last_pt_view || lights_changed) {
                Renderer3DPathTracer::invalidate_accumulation();
                m_last_pt_view       = context.view;
                m_last_pt_lights_ubo = lights_ubo;
            }
        }

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
        Renderer3DShadow::invalidate_dir_shadow_resources();
        Renderer3DPathTracer::invalidate_resources();

        FGCompileDiagnostics diags;
        FGCompileOptions options{};
        options.external_framebuffers.emplace("editorViewport", m_output_framebuffer);
        options.external_framebuffers.emplace("gBuffer", m_gbuffer_framebuffer);
        if (auto noise = Renderer3DSSAO::get_noise_texture())
            options.imported_textures.emplace("ssaoNoise", noise);
        options.requested_output_resources.emplace_back("editorViewport");

#ifdef HN_PLATFORM_MACOS
        if (m_settings.renderer_type != RendererSettings::RendererType::forward) {
            auto& renderer_settings = Settings::get().renderer;
            renderer_settings.renderer_type = RendererSettings::RendererType::forward;
            m_settings.renderer_type = RendererSettings::RendererType::forward;
            HN_CORE_WARN("Only Forward renderer is supported on MacOS, renderer type selection will not be respected.");
        }
#endif

        std::string fg_file;
        switch (m_settings.renderer_type) {
            case RendererSettings::RendererType::deferred:
                fg_file = "deferred.hnfg";
                break;
            case RendererSettings::RendererType::forward:
                fg_file = "forward.hnfg";
                break;
            case RendererSettings::RendererType::pathtracing:
                fg_file = "pathtracing.hnfg";
                break;
        }

        m_frame_graph = FrameGraphLoader::load_and_compile_from_file(
            asset_root / "frame_graphs" / fg_file,
            diags,
            &options);

        FrameGraphLoader::log_diagnostics(diags, "Scene Viewport Frame Graph Rebuild");
        m_frame_graph_dirty = false;
    }

    void SceneViewportRenderer::execute_scene_pass(const SceneViewportRenderContext& context) const {
        HN_CORE_ASSERT(context.scene, "SceneViewportRenderer requires a valid scene");
        context.scene->render(context.view, context.projection, context.projection * context.view, context.camera_position, m_width, m_height, context.camera_exposure);
        if (context.post_scene_overlay_render)
            context.post_scene_overlay_render();
    }

} // namespace Honey
