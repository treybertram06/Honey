#include "hnpch.h"
#include "vk_framebuffer.h"

#include "Honey/renderer/framebuffer.h"
#include "platform/vulkan/vk_backend.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "imgui_impl_vulkan.h"

namespace Honey {
    namespace {

        static const char* vk_result_to_string(VkResult res) {
            switch (res) {
            case VK_SUCCESS: return "VK_SUCCESS";
            case VK_NOT_READY: return "VK_NOT_READY";
            case VK_TIMEOUT: return "VK_TIMEOUT";
            case VK_EVENT_SET: return "VK_EVENT_SET";
            case VK_EVENT_RESET: return "VK_EVENT_RESET";
            case VK_INCOMPLETE: return "VK_INCOMPLETE";
            case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
            case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
            case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
            case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
            case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
            case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
            case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
            case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
            case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
            case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
            case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
            case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
            case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
            case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
            default: return "VK_ERROR_<unknown>";
            }
        }

        static bool is_depth_format(FramebufferTextureFormat fmt) {
            switch (fmt) {
            case FramebufferTextureFormat::Depth:
                return true;
            default:
                return false;
            }
        }

        static VkFormat to_vk_format(FramebufferTextureFormat fmt) {
            switch (fmt) {
            case FramebufferTextureFormat::RGBA8:         return VK_FORMAT_R8G8B8A8_UNORM;
            case FramebufferTextureFormat::RED_INTEGER:   return VK_FORMAT_R32_SINT;
            case FramebufferTextureFormat::Depth:         return VK_FORMAT_D24_UNORM_S8_UINT;
            default:
                HN_CORE_ASSERT(false, "VulkanFramebuffer: unsupported FramebufferTextureFormat");
                return VK_FORMAT_UNDEFINED;
            }
        }

        static uint32_t find_memory_type(VkPhysicalDevice phys,
                                         uint32_t type_bits,
                                         VkMemoryPropertyFlags props)
        {
            VkPhysicalDeviceMemoryProperties mem_props{};
            vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);

            for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
                if ((type_bits & (1u << i)) &&
                    ((mem_props.memoryTypes[i].propertyFlags & props) == props))
                {
                    return i;
                }
            }
            HN_CORE_ASSERT(false, "VulkanFramebuffer: failed to find suitable memory type");
            return 0;
        }

        static VkImageAspectFlags aspect_from_format(FramebufferTextureFormat fmt) {
            if (is_depth_format(fmt))
                return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            // colour / integer
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }

    } // anonymous namespace


    VulkanFramebuffer::VulkanFramebuffer(const FramebufferSpecification& spec, VulkanBackend* backend)
        : m_spec(spec)
        , m_backend(backend)
    {
        HN_CORE_ASSERT(m_backend, "VulkanFramebuffer: backend is null");
        m_device         = m_backend->get_device();
        m_physical_device = m_backend->get_physical_device();
        m_samples        = std::max(1u, spec.samples);

        // Split attachment specs into colour + depth, mirroring the OpenGL implementation
        for (auto& ts : m_spec.attachments.attachments) {
            if (is_depth_format(ts.texture_format))
                m_depth_spec = ts;
            else
                m_color_specs.push_back(ts);
        }

        invalidate();
    }

    VulkanFramebuffer::~VulkanFramebuffer() {
        destroy();
    }

    void VulkanFramebuffer::destroy() {
        if (!m_device)
            return;

        if (!m_imgui_texture_sets.empty()) {
            for (VkDescriptorSet id : m_imgui_texture_sets) {
                if (id)
                    ImGui_ImplVulkan_RemoveTexture(id);
            }
            m_imgui_texture_sets.clear();
        }

        if (m_framebuffer) {
            vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
            m_framebuffer = VK_NULL_HANDLE;
        }

        if (m_render_pass) {
            vkDestroyRenderPass(m_device, m_render_pass, nullptr);
            m_render_pass = VK_NULL_HANDLE;
        }

        for (auto& att : m_color_attachments) {
            if (att.view)   vkDestroyImageView(m_device, att.view, nullptr);
            if (att.image)  vkDestroyImage(m_device, att.image, nullptr);
            if (att.memory) vkFreeMemory(m_device, att.memory, nullptr);
            att = {};
        }
        m_color_attachments.clear();

        if (m_depth_attachment.view)   vkDestroyImageView(m_device, m_depth_attachment.view, nullptr);
        if (m_depth_attachment.image)  vkDestroyImage(m_device, m_depth_attachment.image, nullptr);
        if (m_depth_attachment.memory) vkFreeMemory(m_device, m_depth_attachment.memory, nullptr);
        m_depth_attachment = {};
    }

    void VulkanFramebuffer::invalidate() {
        HN_PROFILE_FUNCTION();

        HN_CORE_ASSERT(m_backend && m_backend->initialized(), "VulkanFramebuffer::invalidate: backend not initialized");
        HN_CORE_ASSERT(m_spec.width > 0 && m_spec.height > 0, "VulkanFramebuffer: width/height must be > 0");

        destroy();

        m_device          = m_backend->get_device();
        m_physical_device = m_backend->get_physical_device();

        // --- Create images ---

        const uint32_t width  = m_spec.width;
        const uint32_t height = m_spec.height;

        // Colour attachments
        m_color_attachments.resize(m_color_specs.size());
            m_imgui_texture_sets.clear();
            m_imgui_texture_sets.resize(m_color_specs.size(), 0);

            for (size_t i = 0; i < m_color_specs.size(); ++i) {
                const auto& spec = m_color_specs[i];
                const VkFormat format = to_vk_format(spec.texture_format);

                VkImageCreateInfo img{};
                img.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                img.imageType = VK_IMAGE_TYPE_2D;
                img.extent.width  = width;
                img.extent.height = height;
                img.extent.depth  = 1;
                img.mipLevels = 1;
                img.arrayLayers = 1;
                img.format = format;
                img.tiling = VK_IMAGE_TILING_OPTIMAL;
                img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                img.usage =
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT          |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                img.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                img.samples = (VkSampleCountFlagBits)m_samples;

                auto& out = m_color_attachments[i];

                VkResult r = vkCreateImage(m_device, &img, nullptr, &out.image);
                HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanFramebuffer: vkCreateImage (color) failed: {0}", vk_result_to_string(r));

                VkMemoryRequirements req{};
                vkGetImageMemoryRequirements(m_device, out.image, &req);

                VkMemoryAllocateInfo ai{};
                ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                ai.allocationSize = req.size;
                ai.memoryTypeIndex = find_memory_type(
                    m_physical_device,
                    req.memoryTypeBits,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                );

                r = vkAllocateMemory(m_device, &ai, nullptr, &out.memory);
                HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanFramebuffer: vkAllocateMemory (color) failed: {0}", vk_result_to_string(r));

                r = vkBindImageMemory(m_device, out.image, out.memory, 0);
                HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanFramebuffer: vkBindImageMemory (color) failed: {0}", vk_result_to_string(r));

                VkImageViewCreateInfo view{};
                view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                view.image = out.image;
                view.viewType = VK_IMAGE_VIEW_TYPE_2D;
                view.format = format;
                view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                view.subresourceRange.baseMipLevel = 0;
                view.subresourceRange.levelCount = 1;
                view.subresourceRange.baseArrayLayer = 0;
                view.subresourceRange.layerCount = 1;

                r = vkCreateImageView(m_device, &view, nullptr, &out.view);
                HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanFramebuffer: vkCreateImageView (color) failed: {0}", vk_result_to_string(r));

                {
                    VkSampler sampler = m_backend->get_imgui_sampler();
                    // Must match ad.finalLayout above, because ImGui will sample in this layout.
                    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                    m_imgui_texture_sets[i] = ImGui_ImplVulkan_AddTexture(
                        sampler,
                        out.view,
                        layout
                    );
                }
            }

        // Depth attachment (optional)
        bool has_depth = (m_depth_spec.texture_format != FramebufferTextureFormat::None);
        if (has_depth) {
            const VkFormat format = to_vk_format(m_depth_spec.texture_format);

            VkImageCreateInfo img{};
            img.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            img.imageType = VK_IMAGE_TYPE_2D;
            img.extent.width  = width;
            img.extent.height = height;
            img.extent.depth  = 1;
            img.mipLevels = 1;
            img.arrayLayers = 1;
            img.format = format;
            img.tiling = VK_IMAGE_TILING_OPTIMAL;
            img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            img.usage =
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT;       // if you ever sample depth
            img.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            img.samples = (VkSampleCountFlagBits)m_samples;

            VkResult r = vkCreateImage(m_device, &img, nullptr, &m_depth_attachment.image);
            HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanFramebuffer: vkCreateImage (depth) failed: {0}", vk_result_to_string(r));

            VkMemoryRequirements req{};
            vkGetImageMemoryRequirements(m_device, m_depth_attachment.image, &req);

            VkMemoryAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = req.size;
            ai.memoryTypeIndex = find_memory_type(
                m_physical_device,
                req.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );

            r = vkAllocateMemory(m_device, &ai, nullptr, &m_depth_attachment.memory);
            HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanFramebuffer: vkAllocateMemory (depth) failed: {0}", vk_result_to_string(r));

            r = vkBindImageMemory(m_device, m_depth_attachment.image, m_depth_attachment.memory, 0);
            HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanFramebuffer: vkBindImageMemory (depth) failed: {0}", vk_result_to_string(r));

            VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = m_depth_attachment.image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = format;
            view.subresourceRange.aspectMask = aspect;
            view.subresourceRange.baseMipLevel = 0;
            view.subresourceRange.levelCount = 1;
            view.subresourceRange.baseArrayLayer = 0;
            view.subresourceRange.layerCount = 1;

            r = vkCreateImageView(m_device, &view, nullptr, &m_depth_attachment.view);
            HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanFramebuffer: vkCreateImageView (depth) failed: {0}", vk_result_to_string(r));
        }

        // --- Create render pass ---

        std::vector<VkAttachmentDescription> attachments;
        attachments.reserve(m_color_specs.size() + (has_depth ? 1 : 0));

        std::vector<VkAttachmentReference> color_refs;
        color_refs.resize(m_color_specs.size());

        for (size_t i = 0; i < m_color_specs.size(); ++i) {
            const auto& spec = m_color_specs[i];
            VkAttachmentDescription ad{};
            ad.format         = to_vk_format(spec.texture_format);
            ad.samples        = (VkSampleCountFlagBits)m_samples;
            ad.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            ad.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            ad.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            ad.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            ad.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            // Render as color attachment, end in shader-read layout for ImGui sampling
            ad.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            color_refs[i].attachment = static_cast<uint32_t>(attachments.size());
            // Must be a color attachment layout during the subpass
            color_refs[i].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            attachments.push_back(ad);
        }

        std::optional<VkAttachmentReference> depth_ref;
        if (has_depth) {
            VkAttachmentDescription ad{};
            ad.format = to_vk_format(m_depth_spec.texture_format);
            ad.samples = (VkSampleCountFlagBits)m_samples;
            ad.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            ad.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            ad.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            ad.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            ad.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            ad.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference ref{};
            ref.attachment = static_cast<uint32_t>(attachments.size());
            ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            attachments.push_back(ad);
            depth_ref = ref;
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<uint32_t>(color_refs.size());
        subpass.pColorAttachments = color_refs.empty() ? nullptr : color_refs.data();
        if (depth_ref.has_value()) {
            subpass.pDepthStencilAttachment = &depth_ref.value();
        }

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = static_cast<uint32_t>(attachments.size());
        rp.pAttachments = attachments.data();
        rp.subpassCount = 1;
        rp.pSubpasses = &subpass;
        rp.dependencyCount = 1;
        rp.pDependencies = &dep;

        VkResult r = vkCreateRenderPass(m_device, &rp, nullptr, &m_render_pass);
        HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanFramebuffer: vkCreateRenderPass failed: {0}", vk_result_to_string(r));

        // --- Create framebuffer ---

        std::vector<VkImageView> views;
        views.reserve(attachments.size());

        for (auto& c : m_color_attachments)
            views.push_back(c.view);
        if (has_depth)
            views.push_back(m_depth_attachment.view);

        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = m_render_pass;
        fb.attachmentCount = static_cast<uint32_t>(views.size());
        fb.pAttachments = views.data();
        fb.width  = width;
        fb.height = height;
        fb.layers = 1;

        r = vkCreateFramebuffer(m_device, &fb, nullptr, &m_framebuffer);
        HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanFramebuffer: vkCreateFramebuffer failed: {0}", vk_result_to_string(r));

        // --- Transition images to usable layouts (optional for now) ---

        // For simplicity we leave colour in UNDEFINED -> first render pass will handle it using loadOp=CLEAR.
        // If you want them immediately in COLOR_ATTACHMENT_OPTIMAL outside of a pass, you could use
        // m_backend->immediate_submit with vkCmdPipelineBarrier here.
    }

    void VulkanFramebuffer::bind() {
        // In Vulkan there is no global FBO binding.
        // Higher-level code should know it is rendering into this FB
        // by using get_render_pass()/get_framebuffer().
    }

    void VulkanFramebuffer::unbind() {
        // No-op for Vulkan.
    }

    void VulkanFramebuffer::resize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0) {
            HN_CORE_WARN("VulkanFramebuffer::resize({0}, {1}) ignored (zero size)", width, height);
            return;
        }
        if (width == m_spec.width && height == m_spec.height)
            return;

        m_spec.width  = width;
        m_spec.height = height;
        invalidate();
    }

    int VulkanFramebuffer::read_pixel(uint32_t attachment_index, int x, int y) {
        HN_CORE_ASSERT(attachment_index < m_color_attachments.size(),
                       "VulkanFramebuffer::read_pixel: attachment index out of range");
        HN_CORE_ASSERT(m_backend && m_backend->initialized(),
                       "VulkanFramebuffer::read_pixel: backend not initialized");

        // Clamp / ignore out-of-bounds reads
        if (x < 0 || y < 0 ||
            x >= static_cast<int>(m_spec.width) ||
            y >= static_cast<int>(m_spec.height))
        {
            return 0;
        }

        // Vulkan origin is top-left for framebuffers in this engine, but image data
        // is addressed with (0,0) top-left as well in this setup, so we can use (x,y)
        // directly. If you treat origin differently elsewhere, flip Y here.
        const uint32_t px = static_cast<uint32_t>(x);
        const uint32_t py = static_cast<uint32_t>(y);

        const auto& tex_spec = m_color_specs[attachment_index];
        const bool is_integer =
            (tex_spec.texture_format == FramebufferTextureFormat::RED_INTEGER);

        VkImage src_image = m_color_attachments[attachment_index].image;

        VkDevice device          = m_backend->get_device();
        VkPhysicalDevice phys    = m_backend->get_physical_device();

        int result_value = 0;

        // We'll read back a single 32-bit pixel
        const VkDeviceSize buffer_size = sizeof(uint32_t);

        // --- Create staging buffer ---
        VkBuffer staging_buffer = VK_NULL_HANDLE;
        VkDeviceMemory staging_memory = VK_NULL_HANDLE;

        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = buffer_size;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult r = vkCreateBuffer(device, &bi, nullptr, &staging_buffer);
        HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanFramebuffer::read_pixel: vkCreateBuffer failed");

        VkMemoryRequirements mem_req{};
        vkGetBufferMemoryRequirements(device, staging_buffer, &mem_req);

        VkPhysicalDeviceMemoryProperties mem_props{};
        vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);

        uint32_t mem_type_index = UINT32_MAX;
        for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
            if ((mem_req.memoryTypeBits & (1u << i)) &&
                (mem_props.memoryTypes[i].propertyFlags &
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            {
                mem_type_index = i;
                break;
            }
        }
        HN_CORE_ASSERT(mem_type_index != UINT32_MAX, "VulkanFramebuffer::read_pixel: no suitable memory type");

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mem_req.size;
        mai.memoryTypeIndex = mem_type_index;

        r = vkAllocateMemory(device, &mai, nullptr, &staging_memory);
        HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanFramebuffer::read_pixel: vkAllocateMemory failed");

        r = vkBindBufferMemory(device, staging_buffer, staging_memory, 0);
        HN_CORE_ASSERT(r == VK_SUCCESS, "VulkanFramebuffer::read_pixel: vkBindBufferMemory failed");

        // --- Record transfer commands via immediate_submit ---
        m_backend->immediate_submit([&](VkCommandBuffer cmd) {
            VkImageSubresourceRange range{};
            range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel   = 0;
            range.levelCount     = 1;
            range.baseArrayLayer = 0;
            range.layerCount     = 1;

            // Transition image to TRANSFER_SRC_OPTIMAL for the copy
            VkImageMemoryBarrier barrier_to_src{};
            barrier_to_src.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier_to_src.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier_to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier_to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier_to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier_to_src.image = src_image;
            barrier_to_src.subresourceRange = range;
            barrier_to_src.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier_to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(
                cmd,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier_to_src
            );

            // Copy the single pixel at (px, py) into the staging buffer
            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength   = 0; // tightly packed
            region.bufferImageHeight = 0; // tightly packed
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { static_cast<int32_t>(px), static_cast<int32_t>(py), 0 };
            region.imageExtent = { 1, 1, 1 };

            vkCmdCopyImageToBuffer(
                cmd,
                src_image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                staging_buffer,
                1,
                &region
            );

            // Transition back to COLOR_ATTACHMENT_OPTIMAL
            VkImageMemoryBarrier barrier_from_src{};
            barrier_from_src.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier_from_src.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier_from_src.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier_from_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier_from_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier_from_src.image = src_image;
            barrier_from_src.subresourceRange = range;
            barrier_from_src.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier_from_src.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            vkCmdPipelineBarrier(
                cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier_from_src
            );
        });

        // --- Map buffer and read back the value ---
        void* mapped = nullptr;
        r = vkMapMemory(device, staging_memory, 0, buffer_size, 0, &mapped);
        HN_CORE_ASSERT(r == VK_SUCCESS && mapped, "VulkanFramebuffer::read_pixel: vkMapMemory failed");

        uint32_t raw = 0;
        std::memcpy(&raw, mapped, sizeof(uint32_t));

        vkUnmapMemory(device, staging_memory);

        // Interpret depending on attachment format
        if (is_integer) {
            result_value = static_cast<int32_t>(raw);
        } else {
            float as_float = 0.0f;
            std::memcpy(&as_float, &raw, sizeof(float));
            // For non-integer attachments this is somewhat arbitrary.
            // You can adapt this to your engine's expectations; for now,
            // we cast to int for compatibility with the API.
            result_value = static_cast<int>(as_float);
        }

        // --- Cleanup staging resources ---
        vkDestroyBuffer(device, staging_buffer, nullptr);
        vkFreeMemory(device, staging_memory, nullptr);

        return result_value;
    }

    void VulkanFramebuffer::clear_attachment_i32(uint32_t idx, int32_t v)   { clear_attachment(idx, &v); }
    void VulkanFramebuffer::clear_attachment_u32(uint32_t idx, uint32_t v)  { clear_attachment(idx, &v); }
    void VulkanFramebuffer::clear_attachment_f32(uint32_t idx, float v)     { clear_attachment(idx, &v); }

         void VulkanFramebuffer::clear_attachment(uint32_t attachment_index, const void* value) {
            HN_CORE_ASSERT(attachment_index < m_color_attachments.size(), "VulkanFramebuffer: clear_attachment index out of range");
            HN_CORE_ASSERT(m_backend && m_backend->initialized(), "VulkanFramebuffer::clear_attachment: backend not initialized");

            HN_CORE_ASSERT(attachment_index < m_color_specs.size(),
                           "VulkanFramebuffer: clear_attachment specs/index mismatch");

            const auto fmt = m_color_specs[attachment_index].texture_format;
            VkImage image   = m_color_attachments[attachment_index].image;

            // For the editor FB path we don't have a render pass that ever transitions
            // this image. So we do: UNDEFINED/SHADER_READ_ONLY_OPTIMAL -> TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL.
            m_backend->immediate_submit([&](VkCommandBuffer cmd) {
                VkImageSubresourceRange range{};
                range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                range.baseMipLevel   = 0;
                range.levelCount     = 1;
                range.baseArrayLayer = 0;
                range.layerCount     = 1;

                // Transition to TRANSFER_DST_OPTIMAL from "don't care" (treat as UNDEFINED here)
                VkImageMemoryBarrier to_clear{};
                to_clear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                to_clear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                to_clear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                to_clear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                to_clear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                to_clear.image = image;
                to_clear.subresourceRange = range;
                to_clear.srcAccessMask = 0;
                to_clear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                vkCmdPipelineBarrier(
                    cmd,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0,
                    0, nullptr,
                    0, nullptr,
                    1, &to_clear
                );

                VkClearColorValue clear_color{};

                switch (fmt) {
                    case FramebufferTextureFormat::RED_INTEGER: {
                        if (value)
                            std::memcpy(&clear_color.uint32[0], value, sizeof(uint32_t));
                        else
                            clear_color.uint32[0] = 0;
                        clear_color.uint32[1] = clear_color.uint32[0];
                        clear_color.uint32[2] = clear_color.uint32[0];
                        clear_color.uint32[3] = clear_color.uint32[0];
                        break;
                    }
                    case FramebufferTextureFormat::RGBA8:
                    default: {
                        float v = 0.0f;
                        if (value)
                            v = *static_cast<const float*>(value);
                        clear_color.float32[0] = v;
                        clear_color.float32[1] = v;
                        clear_color.float32[2] = v;
                        clear_color.float32[3] = 1.0f;
                        break;
                    }
                }

                vkCmdClearColorImage(
                    cmd,
                    image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    &clear_color,
                    1,
                    &range
                );

                // Transition to SHADER_READ_ONLY_OPTIMAL so ImGui can sample
                VkImageMemoryBarrier to_sample{};
                to_sample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                to_sample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                to_sample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                to_sample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                to_sample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                to_sample.image = image;
                to_sample.subresourceRange = range;
                to_sample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                to_sample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                vkCmdPipelineBarrier(
                    cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0,
                    0, nullptr,
                    0, nullptr,
                    1, &to_sample
                );
            });
        }

    uint32_t VulkanFramebuffer::get_color_attachment_renderer_id(uint32_t index) const {
        HN_CORE_ASSERT(index < m_color_attachments.size(),
                       "VulkanFramebuffer: get_color_attachment_renderer_id index out of range");
        // Not used for Vulkan ImGui path
        return 0;
    }

    ImTextureID VulkanFramebuffer::get_imgui_color_texture_id(uint32_t index) const {
        HN_CORE_ASSERT(index < m_imgui_texture_sets.size(),
                       "VulkanFramebuffer: get_imgui_color_texture_id index out of range");
        // ImTextureID is typedef'ed to VkDescriptorSet in ImGui's Vulkan backend
        return (ImTextureID)m_imgui_texture_sets[index];
    }

    VkImageView VulkanFramebuffer::get_color_image_view(uint32_t index) const {
        HN_CORE_ASSERT(index < m_color_attachments.size(), "VulkanFramebuffer: get_color_image_view index out of range");
        return m_color_attachments[index].view;
    }

    VkImage VulkanFramebuffer::get_color_image(uint32_t index) const {
        HN_CORE_ASSERT(index < m_color_attachments.size(), "VulkanFramebuffer: get_color_image index out of range");
        return m_color_attachments[index].image;
    }
} // namespace Honey