#pragma once
#include <array>

#include "camera.h"
#include "gpu_types.h"

namespace Honey {
    enum class GlobalBinding {
        Camera,
        Lights,
        TiledLighting,
        ShadowMatrices,
        DirShadow,
        Count
    };
    enum class GlobalBufferKind { Uniform, Storage };

    struct GlobalBindingDesc {
        GlobalBinding       id;
        uint32_t            shader_binding;
        const char*         glsl_macro; // name emitted into the synthesised glsl include
        GlobalBufferKind    kind;
        uint32_t            size;
        const char*         debug_name;
    };

    inline constexpr std::array<GlobalBindingDesc, (size_t)GlobalBinding::Count> k_global_bindings = {{
        { GlobalBinding::Camera,         0, "HN_GBIND_CAMERA",          GlobalBufferKind::Uniform, sizeof(CameraUBO),             "Camera"        },
        { GlobalBinding::Lights,         1, "HN_GBIND_LIGHTS",          GlobalBufferKind::Uniform, sizeof(LightsUBO),             "Lights"        },
        { GlobalBinding::TiledLighting,  5, "HN_GBIND_TILED_LIGHTING",  GlobalBufferKind::Storage, sizeof(TiledLightingData),     "TiledLighting" },
        { GlobalBinding::ShadowMatrices, 6, "HN_GBIND_SHADOW_MATRICES", GlobalBufferKind::Storage, sizeof(ShadowMatricesSSBO),    "ShadowMatrices"},
        { GlobalBinding::DirShadow,      7, "HN_GBIND_DIR_SHADOW",      GlobalBufferKind::Storage, sizeof(DirectionalShadowSSBO), "DirShadow"     },
    }};

    inline constexpr uint32_t k_global_set = 0;
}
