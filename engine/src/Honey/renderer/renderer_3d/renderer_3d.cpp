#include "hnpch.h"
#include "renderer_3d.h"
#include "renderer_3d_internal.h"

#include "Honey/core/settings.h"
#include "Honey/renderer/renderer.h"

namespace Honey {

    Renderer3DInternal::Renderer3DData* Renderer3DInternal::g_renderer3d_data = nullptr;

    void Renderer3D::init() {
        HN_PROFILE_FUNCTION();

        if (!Renderer3DInternal::g_renderer3d_data)
            Renderer3DInternal::g_renderer3d_data = new Renderer3DInternal::Renderer3DData;

        auto& data = *Renderer3DInternal::g_renderer3d_data;
        auto& rs = Settings::get().renderer;

        data.geometry_path = rs.geometry_path;
        data.shader_cache = Renderer::get_shader_cache();
        data.default_material = Material::create();
        data.batches.clear();
        data.instance_vb.reset();
        data.instance_vb_capacity = 0;
        data.meshlet_draws.clear();

        data.max_texture_slots = VulkanRendererAPI::k_max_texture_slots;
        data.texture_slots.clear();
        data.texture_slots.resize(data.max_texture_slots);

        data.white_texture = Texture2D::create(1, 1);
        const uint32_t white = 0xFFFFFFFFu;
        data.white_texture->set_data((void*)&white, sizeof(uint32_t));
        data.texture_slots[0] = data.white_texture;

        data.default_material->set_base_color_texture(nullptr);
        data.default_material->set_base_color_factor(glm::vec4(1.0f));
    }

    void Renderer3D::shutdown() {
        HN_PROFILE_FUNCTION();

        if (Renderer::get_api() == RendererAPI::API::vulkan)
            VulkanRendererAPI::destroy_meshlet_resources();

        delete Renderer3DInternal::g_renderer3d_data;
        Renderer3DInternal::g_renderer3d_data = nullptr;
    }

    void Renderer3D::begin_scene(const PerspectiveCamera& camera) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(false, "Renderer3D::begin_scene: Not implemented yet!");
    }

    void Renderer3D::begin_scene(const EditorCamera& camera) {
        HN_PROFILE_FUNCTION();
        reset_stats();

        auto& data = *Renderer3DInternal::g_renderer3d_data;

        CameraUBO camera_ubo{};
        camera_ubo.position = camera.get_position();
        camera_ubo.view_proj = camera.get_view_projection_matrix();
        camera_ubo.view = camera.get_view_matrix();

        data.scene_view_proj    = camera_ubo.view_proj;
        data.scene_view         = camera_ubo.view;
        data.scene_camera_pos   = camera_ubo.position;
        data.scene_camera_near  = camera.get_near_clip();
        data.scene_camera_far   = camera.get_far_clip();
        data.scene_camera_fov   = camera.get_fov();

        auto state = VulkanRendererAPI::get_globals_state();
        state.source = VulkanRendererAPI::GlobalsState::Source::Renderer3D;
        data.vk_globals_stack.push_back(state);
        VulkanRendererAPI::submit_camera(camera_ubo);

        data.texture_slot_index = 1;
        if (!data.texture_slots.empty())
            data.texture_slots[0] = data.white_texture;

        data.batches.clear();
        data.unique_meshes_this_frame.clear();
        data.meshlet_draws.clear();
    }

    void Renderer3D::begin_scene(const Camera& camera, const glm::mat4& transform) {
        HN_PROFILE_FUNCTION();
        reset_stats();

        auto& data = *Renderer3DInternal::g_renderer3d_data;

        CameraUBO camera_ubo{};
        camera_ubo.position = camera.get_position();
        camera_ubo.view_proj = camera.get_view_projection_matrix();

        auto state = VulkanRendererAPI::get_globals_state();
        state.source = VulkanRendererAPI::GlobalsState::Source::Renderer3D;
        data.vk_globals_stack.push_back(state);
        VulkanRendererAPI::submit_camera(camera_ubo);

        data.texture_slot_index = 1;
        if (!data.texture_slots.empty())
            data.texture_slots[0] = data.white_texture;

        data.batches.clear();
        data.unique_meshes_this_frame.clear();
        data.meshlet_draws.clear();
    }

    void Renderer3D::begin_scene(const glm::mat4& view_proj, const glm::vec3& position, const glm::mat4& view) {
        HN_PROFILE_FUNCTION();
        reset_stats();

        auto& data = *Renderer3DInternal::g_renderer3d_data;

        CameraUBO camera_ubo{};
        camera_ubo.position = position;
        camera_ubo.view_proj = view_proj;
        camera_ubo.view = view;

        data.scene_view_proj = view_proj;
        data.scene_view      = view;
        data.scene_camera_pos = position;

        auto state = VulkanRendererAPI::get_globals_state();
        state.source = VulkanRendererAPI::GlobalsState::Source::Renderer3D;
        data.vk_globals_stack.push_back(state);
        VulkanRendererAPI::submit_camera(camera_ubo);

        data.texture_slot_index = 1;
        if (!data.texture_slots.empty())
            data.texture_slots[0] = data.white_texture;

        data.batches.clear();
        data.unique_meshes_this_frame.clear();
        data.meshlet_draws.clear();
    }

    void Renderer3D::end_scene() {
        HN_PROFILE_FUNCTION();

        if (Renderer::get_api() != RendererAPI::API::vulkan)
            HN_CORE_ASSERT(false, "Renderer3D::end_scene: only Vulkan path implemented");

        auto& data = *Renderer3DInternal::g_renderer3d_data;

        switch (Settings::get().renderer.renderer_type) {
        case RendererSettings::RendererType::forward:
            Renderer3DInternal::flush_batches_vulkan(Renderer3DInternal::get_or_create_forward_pipeline);
            Renderer3DInternal::flush_meshlet_draws();
            break;
        case RendererSettings::RendererType::deferred:
            Renderer3DInternal::flush_batches_vulkan(Renderer3DInternal::get_or_create_gbuffer_pipeline);
            Renderer3DInternal::flush_meshlet_draws();
            break;
        default:
            HN_CORE_ASSERT(false, "Renderer3D::end_scene: unknown renderer type");
            break;
        }

        HN_CORE_ASSERT(!data.vk_globals_stack.empty(),
                       "Renderer3D Vulkan globals stack underflow (end_scene without matching begin_scene)");
        VulkanRendererAPI::set_globals_state(data.vk_globals_stack.back());
        data.vk_globals_stack.pop_back();
    }

    void Renderer3D::submit_lights(const LightsUBO& lights) {
        HN_PROFILE_FUNCTION();
        if (Renderer::get_api() != RendererAPI::API::vulkan) {
            HN_CORE_WARN("Renderer3D::submit_lights: only Vulkan path implemented");
            return;
        }

        auto& data = *Renderer3DInternal::g_renderer3d_data;
        data.scene_lights = lights;
        VulkanRendererAPI::submit_lights(lights);
    }

    void Renderer3D::submit_tiled_lighting_data(const TiledLightingData& data) {
        HN_PROFILE_FUNCTION();
        if (Renderer::get_api() != RendererAPI::API::vulkan) {
            HN_CORE_WARN("Renderer3D::submit_tiled_lighting_data: only Vulkan path implemented");
            return;
        }
        Renderer3DInternal::g_renderer3d_data->scene_tiled_lighting = data;
        VulkanRendererAPI::submit_tiled_lighting(data);
    }

    void Renderer3D::draw_mesh(const Ref<VertexArray>& vertex_array, const glm::mat4& transform, int entity_id) {
        draw_mesh(vertex_array, Renderer3DInternal::g_renderer3d_data->default_material, transform, entity_id);
    }

    void Renderer3D::draw_mesh(const Ref<VertexArray>& vertex_array,
                               const Ref<Material>& material,
                               const glm::mat4& transform,
                               int entity_id) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(vertex_array, "Renderer3D::draw_mesh: vertex_array is null");
        HN_CORE_ASSERT(material, "Renderer3D::draw_mesh: material is null");

        auto& data = *Renderer3DInternal::g_renderer3d_data;
        data.stats.mesh_submissions++;
        data.unique_meshes_this_frame.insert(vertex_array.get());
        data.stats.unique_meshes = (uint32_t)data.unique_meshes_this_frame.size();

        Renderer3DInternal::BatchKey key{};
        key.va = vertex_array.get();
        key.mat = material.get();

        auto it = data.batches.find(key);
        if (it == data.batches.end()) {
            Renderer3DInternal::BatchValue value{};
            value.va = vertex_array;
            value.material = material;
            value.transforms.reserve(128);
            value.entity_ids.reserve(128);
            it = data.batches.emplace(key, std::move(value)).first;
        }

        it->second.transforms.push_back(transform);
        it->second.entity_ids.push_back(entity_id);
    }

    void Renderer3D::submit_submesh(const Submesh& submesh,
                                    const Ref<Material>& material,
                                    const glm::mat4& transform,
                                    int entity_id,
                                    const Mesh* mesh) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(material, "Renderer3D::submit_submesh: material is null");

        if (Renderer3DInternal::g_renderer3d_data->geometry_path == GeometryPath::Meshlet && submesh.meshlets.has_value()) {
            submit_meshlet_submesh(submesh, material, transform, entity_id, mesh);
            return;
        }

        HN_CORE_ASSERT(submesh.vao, "Renderer3D::submit_submesh: submesh.vao is null");
        draw_mesh(submesh.vao, material, transform, entity_id);
    }

    void Renderer3D::submit_meshlet_submesh(const Submesh& submesh,
                                            const Ref<Material>& material,
                                            const glm::mat4& transform,
                                            int entity_id,
                                            const Mesh* mesh) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(material, "Renderer3D::submit_meshlet_submesh: material is null");
        HN_CORE_ASSERT(submesh.meshlets.has_value(), "Renderer3D::submit_meshlet_submesh: submesh.meshlets is null");

        Renderer3DInternal::g_renderer3d_data->meshlet_draws.push_back(
            Renderer3DInternal::MeshletDrawCommand{
                .submesh = &submesh,
                .mesh = mesh,
                .material = material.get(),
                .transform = transform,
                .entity_id = entity_id,
            });
    }

    void Renderer3D::prewarm_pipelines(void* native_render_pass) {
        HN_PROFILE_FUNCTION();

        if (Renderer::get_api() != RendererAPI::API::vulkan)
            return;

        HN_CORE_ASSERT(native_render_pass, "Renderer3D::prewarm_pipelines: native_render_pass is null");
    }

    void Renderer3D::set_geometry_render_path(GeometryPath path) {
        Renderer3DInternal::g_renderer3d_data->geometry_path = path;
    }

    void Renderer3D::set_directional_shadow_enabled(bool enabled, float shadow_distance) {
        auto* data = Renderer3DInternal::g_renderer3d_data;
        if (!data) return;
        data->directional_shadows_enabled = enabled;
        data->directional_shadow_distance  = shadow_distance;
    }

    Renderer3D::Statistics Renderer3D::get_stats() {
        return Renderer3DInternal::g_renderer3d_data->stats;
    }

    void Renderer3D::reset_stats() {
        if (!Renderer3DInternal::g_renderer3d_data)
            return;

        memset(&Renderer3DInternal::g_renderer3d_data->stats, 0, sizeof(Renderer3D::Statistics));
        Renderer3DInternal::g_renderer3d_data->unique_meshes_this_frame.clear();
    }
}
