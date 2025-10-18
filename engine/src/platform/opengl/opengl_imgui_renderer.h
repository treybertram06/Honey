#pragma once
#include "imgui.h"
#include "Honey/imgui/imgui_renderer.h"

namespace Honey {
    class OpenGLImGuiRenderer : public ImGuiRenderer {
    public:
        virtual void init(void* window) override;
        virtual void shutdown() override;
        virtual void new_frame() override;
        virtual void render_draw_data(ImDrawData* draw_data) override;
        virtual void update_platform_windows() override;
        virtual void render_platform_windows_default() override;

    };
}
