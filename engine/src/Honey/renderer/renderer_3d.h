// renderer_3d.h
#pragma once

#include "render_command.h"
#include "vertex_array.h"
#include "shader.h"
#include "texture.h"
#include "camera.h"

#include <glm/glm.hpp>

#include "editor_camera.h"
#include "material.h"
#include "mesh.h"
#include "platform/vulkan/vk_types.h"

namespace Honey {

	class Renderer3D {
	public:
		static void init();
		static void shutdown();

		static void begin_scene(const PerspectiveCamera& camera);
		static void begin_scene(const EditorCamera& camera);
		static void begin_scene(const Camera& camera, const glm::mat4& transform);
		static void begin_scene(const glm::mat4& view_proj, const glm::vec3& position);

		static void end_scene();

		static void submit_lights(const LightsUBO& lights);

		// Generic mesh rendering
		static void draw_mesh(const Ref<VertexArray>& vertex_array, const glm::mat4& transform, int entity_id = -1);
		static void draw_mesh(const Ref<VertexArray>& vertex_array, const Ref<Material>& material, const glm::mat4& transform, int entity_id = -1);
		static void submit_submesh(const Submesh& submesh, const Ref<Material>& material, const glm::mat4& transform, int entity_id = -1);
		static void submit_meshlet_submesh(const Submesh& submesh, const Ref<Material>& material, const glm::mat4& transform, int entity_id = -1);

		static void prewarm_pipelines(void* native_render_pass);

		static void set_geometry_render_path(GeometryPath path);

		// Statistics
		struct Statistics {
			uint32_t draw_calls = 0;

			// Mesh submission stats
			uint32_t mesh_submissions = 0;     // times draw_mesh() was called
			uint32_t unique_meshes = 0;        // unique VertexArray objects referenced this frame

			// Geometry stats (submitted)
			uint64_t vertex_count = 0;         // estimated (see impl notes)
			uint64_t index_count = 0;          // sum of used index counts
			uint64_t triangle_count = 0;       // derived from index_count (tri list)

			// State churn stats (helps diagnose CPU bottlenecks)
			uint32_t pipeline_binds = 0;
			uint32_t push_constant_updates = 0;
		};
		static Statistics get_stats();
		static void reset_stats();

		struct BatchValue {
			Ref<VertexArray> va;
			Ref<Material> material;
			std::vector<glm::mat4> transforms;
			std::vector<int32_t>   entity_ids;
		};

		struct BatchKey {
			const VertexArray* va = nullptr;
			const Material* mat = nullptr;

			bool operator==(const BatchKey& o) const { return va == o.va && mat == o.mat; }
		};

	};

}
