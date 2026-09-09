#pragma once
#include <algorithm>
#include <array>

#include "camera.h"
#include "gpu_types.h"

namespace Honey {
    enum class GlobalBinding {
        /* Renderer globals */ Camera, Lights, Materials, TiledLighting, ShadowMatrices, DirShadow, Environment,
        /* Icon globals */ IconBandTable, IconCurves,
        Count
    };
    enum class GlobalBufferKind { Uniform, Storage, ExternalStorage, };

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
        { GlobalBinding::Materials,      2, "HN_GBIND_MATERIALS",       GlobalBufferKind::ExternalStorage, 0,                     "Materials"     },
        { GlobalBinding::TiledLighting,  5, "HN_GBIND_TILED_LIGHTING",  GlobalBufferKind::Storage, sizeof(TiledLightingData),     "TiledLighting" },
        { GlobalBinding::ShadowMatrices, 6, "HN_GBIND_SHADOW_MATRICES", GlobalBufferKind::Storage, sizeof(ShadowMatricesSSBO),    "ShadowMatrices"},
        { GlobalBinding::DirShadow,      7, "HN_GBIND_DIR_SHADOW",      GlobalBufferKind::Storage, sizeof(DirectionalShadowSSBO), "DirShadow"     },
        { GlobalBinding::Environment,    10,"HN_GBIND_ENVIRONMENT",     GlobalBufferKind::Uniform, sizeof(EnvironmentUBO),        "Environment"   },
        { GlobalBinding::IconBandTable,  8, "HN_GBIND_ICON_BAND_TABLE", GlobalBufferKind::ExternalStorage, 0,                     "IconBandTable" },
        { GlobalBinding::IconCurves,     9, "HN_GBIND_ICON_CURVES",     GlobalBufferKind::ExternalStorage, 0,                     "IconCurves"    },
    }};

    inline constexpr uint32_t k_global_set = 0;

    // k_global_bindings must stay in the same order as the GlobalBinding enum declaration —
    // vk_icon_globals.cpp indexes this array directly by enum ordinal (k_global_bindings[(size_t)
    // GlobalBinding::X]) rather than searching by .id, so a mismatch here silently mis-registers
    // heap slots instead of failing to compile. This assert is what turns that into a build error.
    static_assert([] {
        for (size_t i = 0; i < k_global_bindings.size(); ++i) {
            if (k_global_bindings[i].id != (GlobalBinding)i) return false;
        }
        return true;
    }(), "k_global_bindings entries must appear in the same order as the GlobalBinding enum");

    // Largest raw shader_binding value in the table above; global-binding slot arrays must be sized
    // to this + 1 since they're indexed by shader_binding, not by table position (GlobalBinding::Count
    // undercounts once bindings skip numbers, as DirShadow's shader_binding=7 does today).
    inline constexpr uint32_t k_max_global_shader_binding = [] {
        uint32_t m = 0;
        for (const auto& b : k_global_bindings) m = std::max(m, b.shader_binding);
        return m;
    }();
}
