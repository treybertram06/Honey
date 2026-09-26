#pragma once
#include <cstdint>

namespace Honey {

    class VulkanBackend;

    // Owns the universal split-sum BRDF LUT (Karis 2013) used by every IBL-lit surface.
    // Construction only needs a device + descriptor heap; baking needs the shader cache
    // Renderer::init() sets up. By the time SceneViewportRenderer::initialize() runs both
    // are already available, so init() and bake() are called back-to-back there — see
    // VulkanBrdfLut's class comment for why the platform-layer object still splits them
    // into two steps.
    class Renderer3DIBL {
    public:
        static void init(VulkanBackend* backend);
        static void bake();
        static void shutdown();

        // UINT32_MAX if the descriptor heap isn't supported on this device, or bake() hasn't run yet.
        static uint32_t get_brdf_lut_bindless_index();

    private:
        Renderer3DIBL() = delete;
    };
}
