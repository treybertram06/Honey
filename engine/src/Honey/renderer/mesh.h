#pragma once

#include "Honey/core/base.h"
#include "vertex_array.h"
#include "material.h"

#include <string>
#include <vector>
#include <optional>
#include <array>

namespace Honey {

    enum class GeometryPath : uint8_t {
        Classic,
        Meshlet
    };

    // Byte-offset view of a persistent descriptor-heap block (6 STORAGE_BUFFER descriptors:
    // vertex/meshlets/meshlet_vertices/meshlet_triangles/meshlet_bounds/draw_data). Kept as a
    // plain POD here (rather than VulkanDescriptorHeap::Allocation) so mesh.h stays backend-agnostic.
    struct MeshletHeapBlock {
        uint32_t offset = 0;
        uint32_t size   = 0;
        uint32_t stride = 0;
        bool     valid  = false;
    };

    struct GlobalMeshletBuffers {
        // Keep this in sync with active Vulkan frames-in-flight.
        static constexpr uint32_t k_frame_ring_size = 2;

        Ref<StorageBuffer> vertex_buffer;
        Ref<StorageBuffer> meshlets_buffer;
        Ref<StorageBuffer> meshlet_vertices_buffer;
        Ref<StorageBuffer> meshlet_triangles_buffer;
        Ref<StorageBuffer> meshlet_bounds_buffer;
        Ref<StorageBuffer> flat_index_buffer;
        uint32_t flat_index_count = 0;
        std::array<Ref<StorageBuffer>, k_frame_ring_size> draw_data_buffers{}; // per-frame per-mesh GPUDrawData[]
        std::array<MeshletHeapBlock, k_frame_ring_size> meshlet_blocks{}; // persistent heap block per frame slot
    };

    struct MeshletBounds {
        glm::vec3 center{0.0f};
        float radius = 0.0f;

        int8_t cone_axis_s8[3]{};
        int8_t cone_cutoff_s8 = 0;
    };

    struct MeshletGeometry {
        uint32_t vertex_offset              = 0;
        uint32_t meshlets_offset            = 0;
        uint32_t meshlet_vertices_offset    = 0;
        uint32_t meshlet_triangles_offset   = 0;
        uint32_t bounds_offset              = 0;

        uint32_t meshlet_count              = 0;
        uint32_t max_vertices_per_meshlet   = 0;
        uint32_t max_triangles_per_meshlet  = 0;

        // Range of this submesh within the mesh-level flat_index_buffer (in triangles, not bytes).
        uint32_t flat_index_first     = 0;
        uint32_t flat_index_tri_count = 0;
    };

    struct Submesh {
        Ref<Material> material;

        Ref<VertexArray> vao; // TODO: pull out of submesh and replace with a ClassicGeometry type
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

        // Populated by the loader after all submeshes are built.
        // Null if no submesh in this mesh has meshlet geometry.
        std::optional<GlobalMeshletBuffers> meshlet_buffers;

    private:
        std::string m_name;
        std::vector<Submesh> m_submeshes;
    };

}
