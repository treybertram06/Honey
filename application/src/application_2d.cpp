#include "application_2d.h"

#include "imgui.h"
#include "glm/gtc/type_ptr.hpp"
#include "hnpch.h"
#include "Honey/core/settings.h"
#include "Honey/renderer/texture_cache.h"
#include "Honey/utils/gltf_loader.h"

namespace Honey {
    static const std::filesystem::path asset_root = ASSET_ROOT;

    Application2D::Application2D()
        : Layer("Application2D") {
    }


    void Application2D::on_attach() {

        auto texture_path_prefix = asset_root / "textures";
        m_chuck_texture = Texture2D::create(texture_path_prefix / "bung.png");

        auto& win = Application::get().get_window();
        uint32_t w = win.get_width();
        uint32_t h = win.get_height();
        m_camera = EditorCamera(w/h, 45.0f, 0.1f, 10000.0f);

        //m_test_mesh = load_gltf_mesh(asset_root / "models" / "Box" / "glTF" / "Box.gltf");
        //m_test_mesh = load_gltf_mesh(asset_root / "models" / "ABeautifulGame" / "glTF" / "ABeautifulGame.gltf");
        //m_test_mesh = load_gltf_mesh(asset_root / "models" / "Sponza" / "glTF" / "Sponza.gltf");
        //m_test_mesh = load_gltf_mesh(asset_root / "models" / "FlightHelmet" / "glTF" / "FlightHelmet.gltf");
        m_test_mesh = load_gltf_mesh(asset_root / "models" / "DragonAttenuation" / "glTF" / "DragonAttenuation.gltf");

        if (!m_test_mesh) {
            HN_CORE_ERROR("Failed to load mesh!");
        }

    }

    void Application2D::on_detach() {
    }

    void Application2D::on_update(Timestep ts) {
        HN_PROFILE_FUNCTION();

        m_frame_time = ts.get_millis();

        // update
        {
            HN_PROFILE_SCOPE("Application2D::camera_update");
            m_camera.on_update(ts);
        }

        //profiling
        {
            HN_PROFILE_SCOPE("Application2D::framerate");
            m_framerate_counter.update(ts);
            m_framerate = m_framerate_counter.get_smoothed_fps();
        }

        bool three_dee = true;

         if (!three_dee) {
            HN_PROFILE_SCOPE("Application2D::renderer_clear / begin pass");
            // render
            Renderer::set_render_target(nullptr);
            Renderer::begin_pass();

            RenderCommand::set_clear_color(m_clear_color);
            RenderCommand::clear();

            Renderer2D::begin_scene(m_camera);

             Renderer2D::draw_quad(glm::vec3(0, 0, 0), glm::vec2(5.0f, 5.0f), glm::vec4(1, 0, 0, 1));


             //const int grid_x = 315;
             //const int grid_y = 315;
             //const float spacing = 1.1f;
             //glm::vec2 origin = {
             //    -(grid_x - 1) * spacing * 0.5f,
             //    -(grid_y - 1) * spacing * 0.5f
             //};
             //for (int y = 0; y < grid_y; ++y) {
             //    for (int x = 0; x < grid_x; ++x) {
             //        glm::vec2 pos = origin + glm::vec2(x * spacing, y * spacing);
             //        Renderer2D::draw_quad(pos, glm::vec2(1.0f, 1.0f), glm::vec4(1, 0, 0, 1));
             //    }
             //}
            Renderer2D::quad_end_scene();
            Renderer::end_pass();
        }

        if (three_dee) {
            HN_PROFILE_SCOPE("Application2D::draw 3d")

            Renderer::set_render_target(nullptr);
            Renderer::begin_pass();

            //RenderCommand::set_clear_color(m_clear_color);
            //RenderCommand::clear();

            Renderer3D::begin_scene(m_camera);

            //glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.0f));
            glm::mat4 transform = glm::mat4(1.0f);

            if (m_test_mesh) {
                for (auto& sub_mesh : m_test_mesh->get_submeshes())
                    Renderer3D::draw_mesh(sub_mesh.vao, sub_mesh.material, transform * sub_mesh.transform);
            }
            Renderer3D::end_scene();

            Renderer::end_pass();

        }

    }

    void Application2D::on_imgui_render() {
        HN_PROFILE_FUNCTION();

        ImGui::Begin("Renderer Debug Panel");

        if (ImGui:: CollapsingHeader("Renderer Info", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Current API: %s", RendererAPI::to_string(RendererAPI::get_api()));

            static std::string vendor_cache;
            if (vendor_cache.empty()) {
                vendor_cache = RenderCommand::get_renderer_api()->get_vendor();
            }
            ImGui::Text("Vendor: %s", vendor_cache.c_str());
        }

        // Performance Section
        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Frame Rate: %d FPS", m_framerate);
            ImGui::Text("Frame Time: %.3f ms", m_frame_time);
            ImGui::Text("Smoothed FPS: %d", m_framerate_counter.get_smoothed_fps());

            ImGui::Separator();
            ImGui::Text("2D Stats: ");
            auto stats_2d = Renderer2D::get_stats();
            ImGui::Text("Draw Calls: %d", stats_2d.draw_calls);
            ImGui::Text("Quads: %d", stats_2d.quad_count);
            ImGui::Text("Vertices: %d", stats_2d.get_total_vertex_count());
            ImGui::Text("Indices: %d", stats_2d.get_total_index_count());

            if (ImGui::Button("Reset 2D Statistics")) {
                Renderer2D::reset_stats();
            }

            ImGui::Separator();

            ImGui::Text("3D Stats:");
            auto stats_3d = Renderer3D::get_stats();
            ImGui::Text("Draw Calls: %u", stats_3d.draw_calls);
            ImGui::Text("Mesh Submissions: %u", stats_3d.mesh_submissions);
            ImGui::Text("Unique Meshes: %u", stats_3d.unique_meshes);

            ImGui::Separator();
            ImGui::Text("Triangles: %llu", static_cast<unsigned long long>(stats_3d.triangle_count));
            ImGui::Text("Indices: %llu", static_cast<unsigned long long>(stats_3d.index_count));
            ImGui::Text("Vertices (est.): %llu", static_cast<unsigned long long>(stats_3d.vertex_count));

            ImGui::Separator();
            ImGui::Text("Pipeline Binds: %u", stats_3d.pipeline_binds);
            ImGui::Text("Push Constant Updates: %u", stats_3d.push_constant_updates);

            if (ImGui::Button("Reset 3D Statistics")) {
                Renderer3D::reset_stats();
            }
        }

        // Renderer Settings Section
        if (ImGui::CollapsingHeader("Renderer Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& renderer = Settings::get().renderer;

            if (ImGui::ColorEdit4("Clear Color", glm::value_ptr(renderer.clear_color))) {
                m_clear_color = renderer.clear_color;
            }

            if (ImGui::Checkbox("Wireframe Mode", &renderer.wireframe)) {
                RenderCommand::set_wireframe(renderer.wireframe);
            }

            if (ImGui::Checkbox("Depth Test", &renderer.depth_test)) {
                RenderCommand::set_depth_test(renderer.depth_test);
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Depth Write", &renderer.depth_write)) {
                RenderCommand::set_depth_write(renderer.depth_write);
            }

            if (ImGui::Checkbox("Face Culling", &renderer.face_culling)) {
                // RenderCommand::set_face_culling(renderer.face_culling);
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Blending", &renderer.blending)) {
                RenderCommand::set_blend(renderer.blending);
            }

            // Texture filter combo
            {
                // Names must match the enum ordering
                static const char* filter_names[] = {
                    "Nearest",
                    "Linear",
                    "Anisotropic"
                };

                int current = static_cast<int>(renderer.texture_filter);
                if (ImGui::Combo("Texture Filter", &current, filter_names, IM_ARRAYSIZE(filter_names))) {
                    renderer.texture_filter = static_cast<RendererSettings::TextureFilter>(current);

                    TextureCache::get().recreate_all_samplers();
                }
            }

            if (renderer.texture_filter == RendererSettings::TextureFilter::anisotropic) {
                static const float af_values[]  = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
                static const char* af_labels[]  = { "1x", "2x", "4x", "8x", "16x" };
                int current_index = 0;

                // Find closest entry to current value
                for (int i = 0; i < (int)std::size(af_values); ++i) {
                    if (std::abs(renderer.anisotropic_filtering_level - af_values[i]) < 0.5f) {
                        current_index = i;
                        break;
                    }
                }

                if (ImGui::Combo("Anisotropy", &current_index, af_labels, IM_ARRAYSIZE(af_labels))) {
                    renderer.anisotropic_filtering_level = af_values[current_index];
                    // Recreate samplers so the new level takes effect
                    TextureCache::get().recreate_all_samplers();
                }
            }

            // Cull mode
            {
                static const char* cull_mode_names[] = { "None", "Back", "Front" };
                int current_index = static_cast<int>(renderer.cull_mode);
                if (ImGui::Combo("Cull Mode", &current_index, cull_mode_names, IM_ARRAYSIZE(cull_mode_names))) {
                    renderer.cull_mode = static_cast<CullMode>(current_index);
                    RenderCommand::set_cull_mode(renderer.cull_mode);
                }
            }

            ImGui::SameLine();
            if (ImGui::Checkbox("V-Sync", &renderer.vsync)) {
                RenderCommand::set_vsync(renderer.vsync);
            }

            const char* api_names[] = { "OpenGL", "Vulkan" };
            int api_index = (static_cast<int>(renderer.api) - 1);
            if (ImGui::Combo("Renderer API", &api_index, api_names, IM_ARRAYSIZE(api_names))) {
                renderer.api = static_cast<RendererAPI::API>(++api_index);
                // Show small tooltip or text: "Takes effect on next restart"
                Settings::write_renderer_api_to_file( asset_root / ".." / "config" / "settings.yaml" );
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Renderer API changes will take effect after restarting the editor.");
            }

        }

        ImGui::End();




    }

    void Application2D::on_event(Event &event) {
        m_camera.on_event(event);
        // on viewport resize, update camera aspect ratio
        if (event.get_event_type() == EventType::window_resize) {
            WindowResizeEvent& e = (WindowResizeEvent&)event;
            m_camera.set_viewport_size(e.get_width(), e.get_height());
            m_viewport_size = { e.get_width(), e.get_height() };
        }
    }
}
