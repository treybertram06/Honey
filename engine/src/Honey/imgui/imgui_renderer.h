#pragma once
#include "Honey/core/base.h"
#include <imgui.h>

namespace Honey {

    class ImGuiRenderer {
    public:
        virtual ~ImGuiRenderer() = default;

        virtual void init(void* window) = 0;
        virtual void shutdown() = 0;
        virtual void new_frame() = 0;
        virtual void render_draw_data(ImDrawData* draw_data) = 0;
        virtual void update_platform_windows() = 0;
        virtual void render_platform_windows_default() = 0;

        static Scope<ImGuiRenderer> create(void* window);
    };

}
