#include "hnpch.h"
#include "mesh.h"

namespace Honey {

    Mesh::Mesh(std::string name)
        : m_name(std::move(name)) {
    }

    Ref<Mesh> Mesh::create(std::string name) {
        return CreateRef<Mesh>(std::move(name));
    }

    void Mesh::add_submesh(Submesh submesh) {
        m_submeshes.push_back(std::move(submesh));
    }

} // namespace Honey