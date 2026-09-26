#include "hnpch.h"
#include "renderer_3d_ibl.h"

#include "platform/vulkan/vk_backend.h"
#include "platform/vulkan/vk_brdf_lut.h"

namespace Honey {

    namespace {
        struct IBLResources {
            Scope<VulkanBrdfLut> brdf_lut;
        };
        static IBLResources* s_res = nullptr;
    }

    void Renderer3DIBL::init(VulkanBackend* backend) {
        if (s_res) return;
        s_res = new IBLResources{};
        if (backend->get_descriptor_heap())
            s_res->brdf_lut = CreateScope<VulkanBrdfLut>(backend, 512);
    }

    void Renderer3DIBL::bake() {
        if (s_res && s_res->brdf_lut) s_res->brdf_lut->bake();
    }

    void Renderer3DIBL::shutdown() {
        if (!s_res) return;
        delete s_res;
        s_res = nullptr;
    }

    uint32_t Renderer3DIBL::get_brdf_lut_bindless_index() {
        return (s_res && s_res->brdf_lut) ? s_res->brdf_lut->get_bindless_index() : UINT32_MAX;
    }
}
