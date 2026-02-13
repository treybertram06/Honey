#pragma once

#include "Honey/core/base.h"

#include "Honey/renderer/mesh.h"

#include <filesystem>
#include <string>

namespace Honey {

    struct GltfLoadOptions {
        // If true, ignores glTF material textures and loads only baseColorFactor (useful for debugging geometry).
        bool disable_textures = false;

        // If true, missing NORMAL/TEXCOORD_0 will be filled with defaults.
        bool allow_missing_attributes = true;
    };

    // Loads the first scene's referenced meshes (or all meshes if scenes are empty) into a Mesh (Submesh per primitive).
    // Returns nullptr on failure.
    Ref<Mesh> load_gltf_mesh(const std::filesystem::path& path, const GltfLoadOptions& options = {});

} // namespace Honey