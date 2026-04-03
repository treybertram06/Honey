#pragma once

#include "Honey/core/base.h"
#include "vertex_array.h"
#include "material.h"

#include <string>
#include <vector>
#include <optional>

namespace Honey {

    enum class GeometryPath : uint8_t {
        ClassicIndexed,
        Meshlet
    };

    struct MeshletBounds {
        glm::vec3 center{0.0f};
        float radius = 0.0f;

        glm::vec3 cone_axis{0.0f};
        float cone_cutoff = 0.0f;
    };

    struct MeshletGeometry {
        Ref<StorageBuffer> meshlets_buffer;          // meshopt_Meshlet[]
        Ref<StorageBuffer> meshlet_vertices_buffer;  // uint32_t[]
        Ref<StorageBuffer> meshlet_triangles_buffer; // uint8_t[] / packed triangle data
        Ref<StorageBuffer> meshlet_bounds_buffer;    // optional per-meshlet bounds

        uint32_t meshlet_count = 0;
        uint32_t max_vertices_per_meshlet = 0;
        uint32_t max_triangles_per_meshlet = 0;
    };

    struct Submesh {
        Ref<VertexArray> vao;
        Ref<Material> material;

        std::optional<MeshletGeometry> meshlets;

        // Optional debug name (useful when inspecting glTF primitives)
        std::string name;

        glm::mat4 transform = glm::mat4(1.0f);
    };

    class Mesh {
    public:
        Mesh() = default;
        explicit Mesh(std::string name);
        ~Mesh() = default;

        static Ref<Mesh> create(std::string name = {});

        const std::string& get_name() const { return m_name; }
        void set_name(std::string name) { m_name = std::move(name); }

        const std::vector<Submesh>& get_submeshes() const { return m_submeshes; }
        std::vector<Submesh>& get_submeshes() { return m_submeshes; }

        void add_submesh(Submesh submesh);

        bool empty() const { return m_submeshes.empty(); }
        size_t submesh_count() const { return m_submeshes.size(); }

    private:
        std::string m_name;
        std::vector<Submesh> m_submeshes;
    };

}