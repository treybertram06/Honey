#pragma once

#include "Honey/core/base.h"
#include "vertex_array.h"
#include "material.h"

#include <string>
#include <vector>

namespace Honey {

    struct Submesh {
        Ref<VertexArray> vao;
        Ref<Material> material;

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

} // namespace Honey