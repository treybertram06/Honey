#pragma once
#include "vk_texture.h"
#include "Honey/loaders/hdr_equirect_loader.h"

namespace Honey {

    class VulkanTextureCube : public TextureCube {
    public:
        VulkanTextureCube(const std::string& hdr_path);
        // Blank cube (contents undefined) for compute-baked resources -- e.g. an irradiance map
        // convolved from another VulkanTextureCube. See convolve_irradiance().
        VulkanTextureCube(uint32_t face_size, uint32_t mip_levels);
        ~VulkanTextureCube() override;

        uint32_t get_width() const override { return m_face_size; }
        uint32_t get_bindless_index() const override { return m_bindless_index; }

        // Full VK_IMAGE_VIEW_TYPE_CUBE view/sampler, for binding this cube as a samplerCube
        // source in another one-shot compute pass (e.g. irradiance convolution reading a skybox
        // cube). Mirrors VulkanTexture2D::get_vk_image_view()/get_vk_sampler().
        void* get_vk_image_view() const { return m_image_view; }
        void* get_vk_sampler() const { return m_sampler; }

        // Convolves `source`'s radiance over the hemisphere into this cube (must already be a
        // blank cube created via the (face_size, mip_levels) ctor above). Blocking -- see
        // OneShotComputePass::record() for why this must not be re-entered inside one command
        // buffer. `source` is downcast internally -- this backend only ever produces
        // VulkanTextureCube instances (see TextureCube::create()).
        void convolve_irradiance(const Ref<TextureCube>& source) override;

        // One blocking immediate_submit per mip (see OneShotComputePass::record()'s single-call-
        // per-command-buffer rule -- this is why it can't be one submission for the whole chain).
        void prefilter_specular(const Ref<TextureCube>& source) override;

    private:
        void fetch_device_handles();
        void create_image();
        void create_image_view();
        void create_sampler();
        void update_bindless_descriptor();

        void convert_equirect_to_cube(VulkanBackend* backend, const HdrEquirectImage& src,
            VkImage dst_cube_image, uint32_t face_size, uint32_t mip_levels);

    private:
        uint32_t m_face_size = 0;
        uint32_t m_mip_levels = 0;

        VulkanBackend* m_backend = nullptr;
        void* m_device = nullptr;
        void* m_physical_device = nullptr;

        void* m_image = nullptr;
        void* m_image_memory = nullptr;
        void* m_image_view = nullptr; // Full VK_IMAGE_VIEW_TYPE_CUBE
        VkImageViewCreateInfo m_image_view_ci{};
        void* m_sampler = nullptr;
        uint32_t m_bindless_index = UINT32_MAX;

        // false for the HDR-loaded skybox cube (only mip 0 is ever populated -- see
        // create_sampler()); true for blank cubes created via the (face_size, mip_levels) ctor,
        // whose caller is expected to fill every mip it declared.
        bool m_sample_all_mips = false;
    };

}
