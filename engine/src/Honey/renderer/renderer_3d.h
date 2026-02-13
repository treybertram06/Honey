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

namespace Honey {

	class Renderer3D {
	public:
		static void init();
		static void shutdown();

		static void begin_scene(const PerspectiveCamera& camera);
		static void begin_scene(const EditorCamera& camera);
		static void end_scene();

		// Basic 3D primitives
		static void draw_cube(const glm::vec3& position, const glm::vec3& size, const glm::vec4& color = glm::vec4(1.0f));
		static void draw_cube(const glm::vec3& position, const glm::vec3& size, const Ref<Texture2D>& texture, const glm::vec4& color = glm::vec4(1.0f));

		static void draw_sphere(const glm::vec3& position, float radius, const glm::vec4& color = glm::vec4(1.0f));

		// Generic mesh rendering
		static void draw_mesh(const Ref<VertexArray>& vertex_array, const glm::mat4& transform);
		static void draw_mesh(const Ref<VertexArray>& vertex_array, const Ref<Material>& material, const glm::mat4& transform);


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
		};

		struct BatchKey {
			const VertexArray* va = nullptr;
			const Material* mat = nullptr;

			bool operator==(const BatchKey& o) const { return va == o.va && mat == o.mat; }
		};

	private:
		static void create_cube_geometry();
		static void create_sphere_geometry();

	};

}
