#include "hnpch.h"
#include "vk_imgui_renderer.h"
#include "vk_context.h"
#include "backends/imgui_impl_vulkan.h"
#include "backends/imgui_impl_glfw.h"
#include <GLFW/glfw3.h>
#include "backends/imgui_impl_vulkan.cpp"

#include "vk_swapchain.h"

namespace Honey {
    void VulkanImGuiRenderer::create_descriptor_pool() {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000;
        pool_info.poolSizeCount = std::size(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        VkResult result = vkCreateDescriptorPool(VulkanContext::get_device(), &pool_info, nullptr, &m_descriptor_pool);
        HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to create descriptor pool for ImGui!");
    }

    void VulkanImGuiRenderer::init(void *window) {
        // Initialize GLFW for Vulkan
        ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(window), true);

        // Create descriptor pool for ImGui
        create_descriptor_pool();

        // Setup Vulkan ImGui implementation
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = VulkanContext::get_instance();
        init_info.PhysicalDevice = VulkanContext::get_physical_device();
        init_info.Device = VulkanContext::get_device();
        init_info.QueueFamily = 0; // Graphics queue family
        init_info.Queue = VulkanContext::get_graphics_queue();
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = m_descriptor_pool;
        init_info.Subpass = 0;
        init_info.MinImageCount = 2;
        init_info.ImageCount = VulkanContext::get_swapchain()->get_image_count();
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = nullptr;

        VkRenderPass render_pass = VulkanContext::get_render_pass();
        // Set the render pass in the init info and call the new single-parameter init function
        init_info.RenderPass = render_pass;
        ImGui_ImplVulkan_Init(&init_info);

        // The Vulkan backend now creates its own transient command buffer internally when uploading fonts.
        // We simply instruct the backend to create the fonts texture. It will upload and manage the temporary
        // resources itself. We then call DestroyFontsTexture() to free the transient upload objects.
        ImGui_ImplVulkan_CreateFontsTexture();
        ImGui_ImplVulkan_DestroyFontsTexture();
    }

    void VulkanImGuiRenderer::shutdown() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();

        if (m_descriptor_pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(VulkanContext::get_device(), m_descriptor_pool, nullptr);
            m_descriptor_pool = VK_NULL_HANDLE;
        }
    }

    void VulkanImGuiRenderer::new_frame() {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void VulkanImGuiRenderer::render_draw_data(ImDrawData *draw_data) {
        VkCommandBuffer command_buffer = VulkanContext::get_current_cmd_buffer();
        ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer);
    }

    void VulkanImGuiRenderer::update_platform_windows() {
        ImGui::UpdatePlatformWindows();
    }

    void VulkanImGuiRenderer::render_platform_windows_default() {
        ImGui::RenderPlatformWindowsDefault();
    }
}
