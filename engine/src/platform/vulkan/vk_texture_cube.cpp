#include "hnpch.h"
#include "vk_texture_cube.h"

#include "vk_backend.h"
#include "vk_descriptor_heap.h"
#include "vk_one_shot_compute_pass.h"
#include "vk_utils.h"
#include "Honey/core/engine.h"
#include "vendor/tinygltf/stb_image.h"

#include <cmath>
#include <vector>

namespace Honey {

    namespace {
        // Kept in sync with create_image()'s format below -- anything that changes one must
        // change the other.
        constexpr VkFormat k_cube_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    }

    VulkanTextureCube::VulkanTextureCube(const std::string& hdr_path) {
        HN_PROFILE_FUNCTION();

        fetch_device_handles();

        // Only the equirect's dimensions are needed to size the cube faces here -- the actual
        // pixel data is loaded and converted by the equirect->cube compute pass (not written
        // yet). stbi_info reads just the file header, not the full float image, so this stays
        // cheap and doesn't require a throwaway GPU upload.
        int eq_width = 0, eq_height = 0, eq_channels = 0;
        int ok = stbi_info(hdr_path.c_str(), &eq_width, &eq_height, &eq_channels);
        HN_CORE_ASSERT(ok, "VulkanTextureCube: failed to read HDR header for '{0}'", hdr_path);

        // An equirect's height spans 180 degrees vertically; a cube face spans 90 degrees, so
        // half the equirect height is a reasonable face resolution without upsampling.
        m_face_size = static_cast<uint32_t>(eq_height) / 2;
        HN_CORE_ASSERT(m_face_size > 0, "VulkanTextureCube: degenerate face size from '{0}'", hdr_path);

        m_mip_levels = static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(m_face_size)))) + 1;

        create_image();
        create_image_view();
        create_sampler();
        update_bindless_descriptor();

        // TODO: dispatch the equirect->cube compute conversion here once it exists. Until
        // then this image's contents are undefined -- nothing samples this bindless index yet.
        auto src = load_hdr_equirect(hdr_path,
            reinterpret_cast<VkDevice>(m_device),
            reinterpret_cast<VkPhysicalDevice>(m_physical_device), m_backend);
        convert_equirect_to_cube(m_backend, src, reinterpret_cast<VkImage>(m_image), m_face_size, m_mip_levels);
        destroy_hdr_equirect(src, reinterpret_cast<VkDevice>(m_device));
    }

    VulkanTextureCube::~VulkanTextureCube() {
        HN_PROFILE_FUNCTION();
        if (!m_backend || !m_backend->initialized() || !m_device)
            return;

        VkDevice dev = reinterpret_cast<VkDevice>(m_device);
        HN_CORE_ASSERT(dev == m_backend->device(),
                       "VulkanTextureCube: device mismatch in destructor (backend was probably shut down earlier)");

        VulkanBackend::RetiredTextureResources retired{};
        retired.sampler = reinterpret_cast<VkSampler>(m_sampler);
        retired.image_view = reinterpret_cast<VkImageView>(m_image_view);
        retired.image = reinterpret_cast<VkImage>(m_image);
        retired.memory = reinterpret_cast<VkDeviceMemory>(m_image_memory);
        retired.bindless_index = m_bindless_index;

        m_backend->defer_destroy_texture_resources(retired);

        m_sampler = nullptr;
        m_image_view = nullptr;
        m_image = nullptr;
        m_image_memory = nullptr;
        m_bindless_index = UINT32_MAX;
    }

    void VulkanTextureCube::fetch_device_handles() {
        HN_PROFILE_FUNCTION();
        m_backend = &Application::get().get_vulkan_backend();
        HN_CORE_ASSERT(m_backend && m_backend->initialized(), "VulkanTextureCube: VulkanBackend not initialized");

        VkDevice dev = m_backend->device();
        VkPhysicalDevice phys = m_backend->physical_device();
        HN_CORE_ASSERT(dev && phys, "VulkanTextureCube: Vulkan device not created yet (need a Vulkan window/surface first)");

        m_device = reinterpret_cast<void*>(dev);
        m_physical_device = reinterpret_cast<void*>(phys);
    }

    void VulkanTextureCube::create_image() {
        VkPhysicalDevice phys = reinterpret_cast<VkPhysicalDevice>(m_physical_device);
        VkDevice dev = reinterpret_cast<VkDevice>(m_device);

        // R8G8B8A8_UNORM-style "just try it" isn't safe here the way it is for the LDR path --
        // R16G16B16A16_SFLOAT with SAMPLED + STORAGE isn't spec-mandatory, so check it once.
        VkFormatProperties fmt_props{};
        vkGetPhysicalDeviceFormatProperties(phys, k_cube_format, &fmt_props);
        constexpr VkFormatFeatureFlags k_required =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        HN_CORE_ASSERT((fmt_props.optimalTilingFeatures & k_required) == k_required,
                       "VulkanTextureCube: format {0} missing required optimal-tiling features on this device",
                       (int)k_cube_format);

        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent.width = m_face_size;
        ci.extent.height = m_face_size;
        ci.extent.depth = 1;
        ci.mipLevels = m_mip_levels;
        ci.arrayLayers = 6;
        ci.format = k_cube_format;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // STORAGE is unused until the compute-conversion pass writes into this image, but usage
        // flags are fixed at creation time, so it has to be declared now.
        ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkImage img = VK_NULL_HANDLE;
        VkResult r = vkCreateImage(dev, &ci, nullptr, &img);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateImage (cube) failed");

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(dev, img, &req);

        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = VulkanUtils::find_memory_type(phys, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkDeviceMemory mem = VK_NULL_HANDLE;
        r = vkAllocateMemory(dev, &ai, nullptr, &mem);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateMemory (cube image) failed");

        r = vkBindImageMemory(dev, img, mem, 0);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkBindImageMemory (cube image) failed");

        m_image = reinterpret_cast<void*>(img);
        m_image_memory = reinterpret_cast<void*>(mem);
    }

    void VulkanTextureCube::create_image_view() {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = reinterpret_cast<VkImage>(m_image);
        view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        view.format = k_cube_format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = m_mip_levels;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 6;
        m_image_view_ci = view;

        VkImageView iv = VK_NULL_HANDLE;
        VkResult r = vkCreateImageView(reinterpret_cast<VkDevice>(m_device), &view, nullptr, &iv);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateImageView (cube) failed");

        m_image_view = reinterpret_cast<void*>(iv);
    }

    void VulkanTextureCube::create_sampler() {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

        // Cube sampling addresses by direction vector, not UV -- there's no seam to wrap the
        // way the equirect source image has. CLAMP_TO_EDGE on all three axes avoids a face
        // sampling into the wrong neighboring face's edge texels under linear filtering.
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        si.unnormalizedCoordinates = VK_FALSE;
        si.compareEnable = VK_FALSE;
        si.compareOp = VK_COMPARE_OP_ALWAYS;
        si.mipLodBias = 0.0f;
        si.minLod = 0.0f;
        // Only mip 0 is ever written (see convert_equirect_to_cube) -- mips 1..N-1 are
        // transitioned to SHADER_READ_ONLY_OPTIMAL alongside it (a layout transition doesn't
        // require valid content) but never get real data until mip-chain generation exists.
        // Clamp sampling to mip 0 so nothing reads that uninitialized memory; raise this back
        // to (m_mip_levels - 1) once the mip chain is actually generated.
        si.maxLod = 0.0f;
        si.anisotropyEnable = VK_FALSE;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        VkSampler sampler = VK_NULL_HANDLE;
        VkResult r = vkCreateSampler(reinterpret_cast<VkDevice>(m_device), &si, nullptr, &sampler);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateSampler (cube) failed");

        m_sampler = reinterpret_cast<void*>(sampler);
    }

    void VulkanTextureCube::update_bindless_descriptor() {
        auto* heap = m_backend->get_descriptor_heap();
        if (m_bindless_index == UINT32_MAX)
            m_bindless_index = heap->alloc_bindless_index();

        // Registers the descriptor as SHADER_READ_ONLY_OPTIMAL even though the image is still
        // UNDEFINED right now -- fine, since nothing samples this index yet, and the
        // compute-conversion pass (not written yet) must transition the image to exactly this
        // layout before anything does.
        heap->write_bindless(m_bindless_index, m_image_view_ci, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // Can the contents of this be further abstracted away? Hmmmm
    void VulkanTextureCube::convert_equirect_to_cube(VulkanBackend* backend, const HdrEquirectImage& src,
        VkImage dst_cube_image, uint32_t face_size, uint32_t mip_levels) {
        VkDevice device = reinterpret_cast<VkDevice>(m_device);

        // Descriptors (ugh): a sampler2D and an image2DArray. stageFlags is filled in by
        // OneShotComputePass (compute-only by construction), so it's left unset here.
        std::vector<VkDescriptorSetLayoutBinding> bindings(2);
        // sampler2D
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        // image2DArray
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;

        // Cached on VulkanBackend and shared with every future equirect->cube conversion (and, in
        // time, the other one-shot IBL bakes) instead of rebuilding a pipeline per call.
        auto shader_path = std::filesystem::path(ASSET_ROOT) / "shaders" / "compute" / "EquirectToCube.comp";
        OneShotComputePass& pass = backend->get_or_create_one_shot_compute_pass(
            shader_path, bindings, sizeof(int32_t) /* face_size push constant */);

        // Created inside the lambda, destroyed after immediate_submit returns (it's fence-blocked,
        // so the GPU is done with everything the moment the call comes back).
        VkImageView throwaway_array_view = VK_NULL_HANDLE;

        backend->immediate_submit("EquirectToCube", [&](VkCommandBuffer cmd) {
            // 1. Throwaway VK_IMAGE_VIEW_TYPE_2D_ARRAY view over the same VkImage as the cube
            // view -- mip 0 only, all 6 layers. This is what lets the compute shader address the
            // destination as image2DArray while everything else still sees it as a cubemap.
            VkImageViewCreateInfo array_view_ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            array_view_ci.image = dst_cube_image;
            array_view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            array_view_ci.format = k_cube_format;
            array_view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            array_view_ci.subresourceRange.baseMipLevel = 0;
            array_view_ci.subresourceRange.levelCount = 1;
            array_view_ci.subresourceRange.baseArrayLayer = 0;
            array_view_ci.subresourceRange.layerCount = 6;
            VkResult vr = vkCreateImageView(device, &array_view_ci, nullptr, &throwaway_array_view);
            HN_CORE_ASSERT(vr == VK_SUCCESS, "Failed to create throwaway 2D array view for equirect->cube conversion");

            // 2. Write the descriptor set: binding 0 = equirect source (sampler2D), binding 1 =
            // the throwaway array view (storage image, GENERAL -- compute writes require it).
            VkDescriptorImageInfo src_info{};
            src_info.sampler = src.sampler;
            src_info.imageView = src.view;
            src_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo dst_info{};
            dst_info.imageView = throwaway_array_view;
            dst_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            std::vector<VkWriteDescriptorSet> writes(2);
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &src_info;

            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].pImageInfo = &dst_info;
            // sType/dstSet for both entries are filled in by OneShotComputePass::record() below.

            // 3. Barrier dst_cube_image: UNDEFINED -> GENERAL. Only mip 0 / all 6 layers get
            // written by this dispatch; mips 1..N stay undefined-content but get swept into the
            // final transition below regardless, since a layout transition doesn't require valid
            // content in the subresources it covers.
            VkImageMemoryBarrier to_general{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            to_general.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            to_general.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            to_general.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_general.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_general.image = dst_cube_image;
            to_general.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            to_general.subresourceRange.baseMipLevel = 0;
            to_general.subresourceRange.levelCount = 1;
            to_general.subresourceRange.baseArrayLayer = 0;
            to_general.subresourceRange.layerCount = 6;
            to_general.srcAccessMask = 0;
            to_general.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &to_general);

            // 4-5. Bind pipeline / descriptor set / push constant (face_size, matches
            // PC.face_size in EquirectToCube.comp) and dispatch -- local_size_x/y = 8 in the
            // shader, one dispatch covers all 6 faces via gl_GlobalInvocationID.z.
            int32_t pc_face_size = static_cast<int32_t>(face_size);
            uint32_t groups = (face_size + 7) / 8;
            pass.record(cmd, writes, &pc_face_size, sizeof(pc_face_size), groups, groups, 6);

            // 6. Barrier dst_cube_image -> SHADER_READ_ONLY_OPTIMAL, matching the layout
            // update_bindless_descriptor() already registered this image's bindless slot as.
            // Mip 0 actually got written and is GENERAL right now (from step 3); mips 1..N-1
            // were never touched by this dispatch and are still UNDEFINED (mip-chain generation
            // is a later step), so each range needs its own true oldLayout -- claiming GENERAL
            // for a subresource that's still UNDEFINED is exactly what the validation layer
            // caught here.
            VkImageMemoryBarrier to_read[2] = {};

            to_read[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            to_read[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            to_read[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            to_read[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_read[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_read[0].image = dst_cube_image;
            to_read[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            to_read[0].subresourceRange.baseMipLevel = 0;
            to_read[0].subresourceRange.levelCount = 1;
            to_read[0].subresourceRange.baseArrayLayer = 0;
            to_read[0].subresourceRange.layerCount = 6;
            to_read[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            to_read[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            to_read[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            to_read[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            to_read[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            to_read[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_read[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_read[1].image = dst_cube_image;
            to_read[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            to_read[1].subresourceRange.baseMipLevel = 1;
            to_read[1].subresourceRange.levelCount = mip_levels - 1;
            to_read[1].subresourceRange.baseArrayLayer = 0;
            to_read[1].subresourceRange.layerCount = 6;
            to_read[1].srcAccessMask = 0;
            to_read[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            uint32_t barrier_count = mip_levels > 1 ? 2 : 1;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                barrier_count, to_read);
        });

        // Only the throwaway view was per-call state -- the pipeline/layout/pool are owned by the
        // cached OneShotComputePass on VulkanBackend now and outlive this call.
        vkDestroyImageView(device, throwaway_array_view, nullptr);
    }
}
