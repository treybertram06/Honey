// renderer_3d.h
#pragma once

#include "render_command.h"
#include "vertex_array.h"
#include "shader.h"
#include "texture.h"
#include "camera.h"

#include <glm/glm.hpp>

namespace Honey {

	class Renderer3D {
	public:
		static void init();
		static void shutdown();

		static void begin_scene(const PerspectiveCamera& camera);
		static void end_scene();

		// Basic 3D primitives
		static void draw_cube(const glm::vec3& position, const glm::vec3& size, const glm::vec4& color = glm::vec4(1.0f));
		static void draw_cube(const glm::vec3& position, const glm::vec3& size, const Ref<Texture2D>& texture, const glm::vec4& color = glm::vec4(1.0f));

		static void draw_sphere(const glm::vec3& position, float radius, const glm::vec4& color = glm::vec4(1.0f));

		// Generic mesh rendering
		static void draw_mesh(const Ref<VertexArray>& vertex_array, const glm::mat4& transform, const Ref<Shader>& shader);

		// Statistics
		struct Statistics {
			std::uint32_t draw_calls = 0;
			std::uint32_t vertex_count = 0;
			std::uint32_t index_count = 0;
		};
		static Statistics get_stats();
		static void reset_stats();

	private:
		static void create_cube_geometry();
		static void create_sphere_geometry();
	};

}