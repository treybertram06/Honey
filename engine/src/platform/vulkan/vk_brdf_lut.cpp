#include "hnpch.h"
#include "vk_brdf_lut.h"

#include "vk_backend.h"
#include "vk_descriptor_heap.h"
#include "vk_one_shot_compute_pass.h"
#include "vk_utils.h"

#include <filesystem>
#include <vector>

namespace Honey {

    namespace {
        // rg16f: two floats (scale, bias) per texel -- see BrdfLut.comp. Not spec-mandatory with
        // SAMPLED + STORAGE, unlike the mandatory-support rgba8/rgba16f paths elsewhere, so its
        // support gets checked once below rather than assumed.
        constexpr VkFormat k_lut_format = VK_FORMAT_R16G16_SFLOAT;
    }

    VulkanBrdfLut::VulkanBrdfLut(VulkanBackend* backend, uint32_t size)
        : m_size(size), m_backend(backend) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(m_backend && m_backend->device() && m_backend->physical_device(),
                       "VulkanBrdfLut: backend must already have a device");
        HN_CORE_ASSERT(m_size > 0, "VulkanBrdfLut: degenerate size");

        create_image();
        create_image_view();
        update_bindless_descriptor();
        // bake() is NOT called here -- see the class comment in vk_brdf_lut.h for why. The
        // caller (VulkanBackend::bake_brdf_lut()) invokes it once Renderer::init() has run.
    }

    VulkanBrdfLut::~VulkanBrdfLut() {
        HN_PROFILE_FUNCTION();
        if (!m_backend || !m_backend->initialized() || !m_image)
            return;

        VulkanBackend::RetiredTextureResources retired{};
        retired.image_view = reinterpret_cast<VkImageView>(m_image_view);
        retired.image = reinterpret_cast<VkImage>(m_image);
        retired.memory = reinterpret_cast<VkDeviceMemory>(m_image_memory);
        retired.bindless_index = m_bindless_index;

        m_backend->defer_destroy_texture_resources(retired);

        m_image_view = nullptr;
        m_image = nullptr;
        m_image_memory = nullptr;
        m_bindless_index = UINT32_MAX;
    }

    void VulkanBrdfLut::create_image() {
        VkPhysicalDevice phys = m_backend->physical_device();
        VkDevice dev = m_backend->device();

        VkFormatProperties fmt_props{};
        vkGetPhysicalDeviceFormatProperties(phys, k_lut_format, &fmt_props);
        constexpr VkFormatFeatureFlags k_required =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
        HN_CORE_ASSERT((fmt_props.optimalTilingFeatures & k_required) == k_required,
                       "VulkanBrdfLut: format {0} missing required optimal-tiling features on this device",
                       (int)k_lut_format);

        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent.width = m_size;
        ci.extent.height = m_size;
        ci.extent.depth = 1;
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.format = k_lut_format;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkImage img = VK_NULL_HANDLE;
        VkResult r = vkCreateImage(dev, &ci, nullptr, &img);
        HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanBrdfLut: vkCreateImage failed");

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(dev, img, &req);

        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = VulkanUtils::find_memory_type(phys, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkDeviceMemory mem = VK_NULL_HANDLE;
        r = vkAllocateMemory(dev, &ai, nullptr, &mem);
        HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanBrdfLut: vkAllocateMemory failed");

        r = vkBindImageMemory(dev, img, mem, 0);
        HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanBrdfLut: vkBindImageMemory failed");

        m_image = reinterpret_cast<void*>(img);
        m_image_memory = reinterpret_cast<void*>(mem);
    }

    void VulkanBrdfLut::create_image_view() {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = reinterpret_cast<VkImage>(m_image);
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = k_lut_format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;
        m_image_view_ci = view;

        VkImageView iv = VK_NULL_HANDLE;
        VkResult r = vkCreateImageView(m_backend->device(), &view, nullptr, &iv);
        HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanBrdfLut: vkCreateImageView failed");

        m_image_view = reinterpret_cast<void*>(iv);
    }

    void VulkanBrdfLut::update_bindless_descriptor() {
        auto* heap = m_backend->get_descriptor_heap();
        if (m_bindless_index == UINT32_MAX)
            m_bindless_index = heap->alloc_bindless_index();

        // Registered SHADER_READ_ONLY_OPTIMAL even though bake() hasn't run yet -- fine, since
        // nothing reads this index until Step 5 wires it into EnvironmentUBO, well after bake()
        // (called from this same constructor, a few lines below) has already transitioned it.
        heap->write_bindless(m_bindless_index, m_image_view_ci, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void VulkanBrdfLut::bake() {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(!m_baked, "VulkanBrdfLut::bake() called more than once");
        m_baked = true;

        VkDevice device = m_backend->device();

        // A single storage-image binding -- no source texture to sample, the whole LUT is
        // generated analytically from (NdotV, roughness) alone. See BrdfLut.comp.
        std::vector<VkDescriptorSetLayoutBinding> bindings(1);
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;

        auto shader_path = std::filesystem::path(ASSET_ROOT) / "shaders" / "compute" / "BrdfLut.comp";
        OneShotComputePass& pass = m_backend->get_or_create_one_shot_compute_pass(shader_path, bindings);

        m_backend->immediate_submit("BrdfLut", [&](VkCommandBuffer cmd) {
            VkDescriptorImageInfo dst_info{};
            dst_info.imageView = reinterpret_cast<VkImageView>(m_image_view);
            dst_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            std::vector<VkWriteDescriptorSet> writes(1);
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[0].pImageInfo = &dst_info;

            VkImageMemoryBarrier to_general{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            to_general.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            to_general.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            to_general.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_general.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_general.image = reinterpret_cast<VkImage>(m_image);
            to_general.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            to_general.subresourceRange.baseMipLevel = 0;
            to_general.subresourceRange.levelCount = 1;
            to_general.subresourceRange.baseArrayLayer = 0;
            to_general.subresourceRange.layerCount = 1;
            to_general.srcAccessMask = 0;
            to_general.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &to_general);

            uint32_t group_count = (m_size + 7) / 8;
            pass.record(cmd, writes, nullptr, 0, group_count, group_count, 1);

            VkImageMemoryBarrier to_read{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            to_read.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            to_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_read.image = reinterpret_cast<VkImage>(m_image);
            to_read.subresourceRange = to_general.subresourceRange;
            to_read.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &to_read);
        });
    }

}