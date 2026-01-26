#pragma once

#include <vector>
#include <optional>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "Honey/renderer/framebuffer.h"
#include "platform/vulkan/vk_backend.h"

namespace Honey {

    class VulkanFramebuffer : public Framebuffer {
    public:
        VulkanFramebuffer(const FramebufferSpecification& spec, VulkanBackend* backend);
        ~VulkanFramebuffer() override;

        // Framebuffer interface
        void bind() override;
        void unbind() override;

        void resize(uint32_t width, uint32_t height) override;
        int  read_pixel(uint32_t attachment_index, int x, int y) override;

        void clear_attachment(uint32_t attachment_index, const void* value) override;
        void clear_attachment_i32(uint32_t idx, int32_t v) override;
        void clear_attachment_u32(uint32_t idx, uint32_t v) override;
        void clear_attachment_f32(uint32_t idx, float v) override;

        uint32_t get_color_attachment_renderer_id(uint32_t index = 0) const override;
        ImTextureID get_imgui_color_texture_id(uint32_t index = 0) const override;

        const FramebufferSpecification& get_specification() const override { return m_spec; }

        // Vulkan‑specific accessors, used by the Vulkan renderer
        VkRenderPass   get_render_pass() const      { return m_render_pass; }
        VkFramebuffer  get_framebuffer() const      { return m_framebuffer; }
        VkExtent2D     get_extent() const           { return { m_spec.width, m_spec.height }; }
        VkImageView    get_color_image_view(uint32_t index = 0) const;
        VkImage        get_color_image(uint32_t index = 0) const;

        uint32_t get_color_attachment_count() const { return (uint32_t)m_color_attachments.size(); }
        bool has_depth_attachment() const { return m_depth_spec.texture_format != FramebufferTextureFormat::None; }

        FramebufferTextureFormat get_color_attachment_format(uint32_t index) const {
            HN_CORE_ASSERT(index < m_color_specs.size(),
                           "VulkanFramebuffer::get_color_attachment_format: index out of range");
            return m_color_specs[index].texture_format;
        }

    private:
        struct ImageAttachment {
            VkImage        image  = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView    view   = VK_NULL_HANDLE;
        };

        void invalidate();  // (re)create images, views, render pass, framebuffer
        void destroy();     // free all Vulkan resources owned by this FB

    private:
        FramebufferSpecification m_spec{};
        VulkanBackend*           m_backend = nullptr;

        VkDevice                 m_device = VK_NULL_HANDLE;
        VkPhysicalDevice         m_physical_device = VK_NULL_HANDLE;

        // Per‑attachment specs
        std::vector<FramebufferTextureSpecification> m_color_specs;
        FramebufferTextureSpecification              m_depth_spec{};

        // Vulkan images/views
        std::vector<ImageAttachment> m_color_attachments;
        ImageAttachment              m_depth_attachment{};

        // Render pass + framebuffer that target these attachments
        VkRenderPass   m_render_pass  = VK_NULL_HANDLE;
        VkFramebuffer  m_framebuffer  = VK_NULL_HANDLE;

        std::vector<VkDescriptorSet> m_imgui_texture_sets;

        uint32_t       m_samples = 1;
    };

} // namespace Honey