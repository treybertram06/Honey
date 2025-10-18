#pragma once
#include "imgui.h"
#include "Honey/imgui/imgui_renderer.h"
#include <vulkan/vulkan.h>

namespace Honey {
    class VulkanImGuiRenderer : public ImGuiRenderer {
    public:
        virtual void init(void* window) override;
        virtual void shutdown() override;
        virtual void new_frame() override;
        virtual void render_draw_data(ImDrawData* draw_data) override;
        virtual void update_platform_windows() override;
        virtual void render_platform_windows_default() override;

    private:
        void create_descriptor_pool();

        VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
    };
}
