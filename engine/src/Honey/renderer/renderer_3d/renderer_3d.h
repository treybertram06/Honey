// renderer_3d.h
#pragma once

#include "../render_command.h"
#include "../vertex_array.h"
#include "../shader.h"
#include "../texture.h"
#include "../camera.h"

#include <glm/glm.hpp>

#include "../editor_camera.h"
#include "../material.h"
#include "../mesh.h"
#include "../gpu_types.h"
#include "../framebuffer.h"

namespace Honey {

	class Renderer3D {
	public:
        struct Statistics {
            uint32_t draw_calls = 0;
            uint32_t mesh_submissions = 0;
            uint32_t unique_meshes = 0;
            uint64_t vertex_count = 0;
            uint64_t index_count = 0;
            uint64_t triangle_count = 0;
            uint32_t pipeline_binds = 0;
            uint32_t push_constant_updates = 0;
        };

		static void init();
		static void shutdown();

		static void begin_scene(const PerspectiveCamera& camera);
		static void begin_scene(const EditorCamera& camera);
		static void begin_scene(const Camera& camera, const glm::mat4& transform);
		static void begin_scene(const glm::mat4& view_proj, const glm::vec3& position, const glm::mat4& view = glm::mat4(1.0f), const glm::mat4& projection = glm::mat4(1.0f), float exposure = 1.0f);

		static void end_scene();

		static void submit_lights(const LightsUBO& lights);
		static void submit_tiled_lighting_data(const TiledLightingData& data);

		// Deferred lighting pass — call after begin_scene/submit_lights for the GBuffer pass.
		static void write_ssao_fb_to_renderer_state(Ref<Framebuffer> ssao_fb);
		static void write_gbuffer_to_renderer_state(Ref<Framebuffer> gbuffer_fb);
		static void flush_deferred_lighting(Ref<Framebuffer> shadow_cube_fb, Ref<Framebuffer> shadow_dir_fb);
		// Call when the frame graph is rebuilt so stale shadow views are re-written before the next lighting pass.
		static void invalidate_gbuffer_descriptors();

		// Generic mesh rendering
		static void draw_mesh(const Ref<VertexArray>& vertex_array, const glm::mat4& transform, int entity_id = -1);
		static void draw_mesh(const Ref<VertexArray>& vertex_array, const Ref<Material>& material, const glm::mat4& transform, int entity_id = -1);
		static void submit_submesh(const Submesh& submesh, const Ref<Material>& material, const glm::mat4& transform, int entity_id = -1, const Mesh* mesh = nullptr);
		static void submit_meshlet_submesh(const Submesh& submesh, const Ref<Material>& material, const glm::mat4& transform, int entity_id = -1, const Mesh* mesh = nullptr);

		static void prewarm_pipelines(void* native_render_pass);

		static void set_geometry_render_path(GeometryPath path);

		// Directional shadow settings — call each frame before the shadow pass executes.
		static void set_directional_shadow_enabled(bool enabled, float shadow_distance = 50.0f);

		static Statistics get_stats();
		static void reset_stats();
	};

}
