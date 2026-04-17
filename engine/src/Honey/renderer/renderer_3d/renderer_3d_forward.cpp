#include "hnpch.h"
#include "renderer_3d_internal.h"

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey::Renderer3DInternal {

    Ref<Pipeline> get_or_create_forward_pipeline(void* rp_native, bool blend, bool cull_none) {
        HN_CORE_ASSERT(rp_native, "get_or_create_forward_pipeline: rp_native is null");
        PipelineVariantKey key{rp_native, (uint8_t)(blend ? 1 : 0), (uint8_t)(cull_none ? 1 : 0)};

        auto it = g_renderer3d_data->vk_forward_pipelines.find(key);
        if (it != g_renderer3d_data->vk_forward_pipelines.end())
            return it->second;

        auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_Forward.glsl");
        if (!spec.perColorAttachmentBlend.empty())
            spec.perColorAttachmentBlend[0].enabled = blend;
        if (cull_none)
            spec.cullMode = CullMode::None;

        auto pipeline = Pipeline::create(spec, rp_native);
        g_renderer3d_data->vk_forward_pipelines.emplace(key, pipeline);
        return pipeline;
    }
}
