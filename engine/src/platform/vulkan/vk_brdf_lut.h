#pragma once

#include <cstdint>
#include <vulkan/vulkan_core.h>

namespace Honey {

    class VulkanBackend;

    // The split-sum BRDF integration LUT (Karis 2013). Unlike SkyboxComponent's irradiance/
    // prefiltered cubes, this is universal -- one instance for the whole process, never per-
    // skybox. Read thereafter as a plain bindless texture2D (u_Textures[get_bindless_index()]),
    // same array the material system already uses.
    //
    // Construction and baking are deliberately split into two steps (see VulkanBackend):
    // the constructor only needs a device + descriptor heap and runs during
    // VulkanBackend::acquire_queue_lease() (i.e. during the very first window's construction),
    // but bake() dispatches a OneShotComputePass, which resolves its shader through
    // Renderer::get_shader_cache() -- and Renderer::init() hasn't run yet at that point
    // (Application::Application() creates the window, and with it the Vulkan device, *before*
    // calling Renderer::init()). Calling bake() from the constructor asserts inside
    // Renderer::get_shader_cache(). VulkanBackend::bake_brdf_lut() calls bake() once, from
    // Application::Application() right after Renderer::init() returns.
    class VulkanBrdfLut {
    public:
        // `backend` must already have a device and descriptor heap (this is constructed from
        // inside VulkanBackend, right after both exist).
        VulkanBrdfLut(VulkanBackend* backend, uint32_t size);
        ~VulkanBrdfLut();

        VulkanBrdfLut(const VulkanBrdfLut&) = delete;
        VulkanBrdfLut& operator=(const VulkanBrdfLut&) = delete;

        uint32_t get_bindless_index() const { return m_bindless_index; }

        // Dispatches the compute pass that actually fills the LUT. Requires Renderer::init() to
        // have already run (see class comment above). Must be called exactly once.
        void bake();

    private:
        void create_image();
        void create_image_view();
        void update_bindless_descriptor();

    private:
        uint32_t m_size = 0;

        VulkanBackend* m_backend = nullptr;

        void* m_image = nullptr;
        void* m_image_memory = nullptr;
        void* m_image_view = nullptr;
        VkImageViewCreateInfo m_image_view_ci{};
        uint32_t m_bindless_index = UINT32_MAX;
        bool m_baked = false;
    };

}