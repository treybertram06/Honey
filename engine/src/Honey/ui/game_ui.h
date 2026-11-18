#pragma once

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace Honey {

    struct UIRect {
        glm::vec2 origin{0.0f};
        glm::vec2 size{0.0f};
    };

    class GameUI {
    public:

        enum class UIAnchor { top_left = 0, top = 1, top_right = 2, left, center, right, bottom_left, bottom, bottom_right }; // Must remain in row-major order for pivot calculation

        static void begin_frame(glm::vec2 origin, glm::vec2 size);
        static void end_frame();

        // size is in virtual px; a 0 on an axis auto-fits that axis. background=false => transparent panel.
        static void begin_panel(const char* id, UIAnchor anchor, glm::vec2 offset, glm::vec2 size, bool background = false);
        static void end_panel();

        static void label(const char* text, float size, glm::vec4 color);
        static bool button(const char* text, float width = 0.0f); // width in virtual px; 0 = fit text
        static void spacing(float h);
        static bool wants_mouse();

    private:
        static UIRect s_rect;
        static float s_scale;
        static uint32_t s_panel_depth;
        static bool s_frame_active; // between begin_frame and end_frame; widgets no-op outside it
    };

}
