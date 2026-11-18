#pragma once

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace Honey {

    class GameUI {
    public:
        struct UIRect {
            glm::vec2 origin{0.0f};
            glm::vec2 size{0.0f};
        };

        // Visual style for all game UI. Set from scripts (UI.Theme in C#); the defaults below are only the
        // starting point. Sizes are in virtual px (1080-high reference). Button colours are derived from accent.
        struct UITheme {
            glm::vec4 panel_color{0.05f, 0.07f, 0.11f, 0.72f}; // translucent dark blue-black
            glm::vec4 text_color{0.93f, 0.95f, 1.00f, 1.00f};
            glm::vec4 accent_color{0.25f, 0.55f, 1.00f, 1.00f};
            float rounding = 10.0f;
            float panel_padding = 20.0f;
            float item_spacing = 12.0f;
            float button_font_size = 28.0f;
            float button_padding_x = 28.0f;
            float button_padding_y = 14.0f;
            int font = 0; // handle from load_font; 0 = engine default font
        };

        enum class UIAnchor { top_left = 0, top = 1, top_right = 2, left, center, right, bottom_left, bottom, bottom_right }; // Must remain in row-major order for pivot calculation

        static void begin_frame(glm::vec2 origin, glm::vec2 size);
        static void end_frame();

        // size is in virtual px; a 0 on an axis auto-fits that axis. background=false => transparent panel.
        static void begin_panel(const char* id, UIAnchor anchor, glm::vec2 offset, glm::vec2 size, bool background = false);
        static void end_panel();

        // align: 0 left, 1 center, 2 right (within the panel's content width)
        static void label(const char* text, float size, glm::vec4 color, int font = 0, int align = 0);
        static bool button(const char* text, float width = 0.0f); // width in virtual px; 0 = fit text
        static void spacing(float h);
        static bool wants_mouse();

        static void set_theme(const UITheme& theme);
        // Loads a TTF (asset-relative or absolute path). Returns a handle > 0, or 0 (default font) on failure.
        // The same path always returns the same handle.
        static int load_font(const char* path);

    private:
        static UIRect s_rect;
        static float s_scale;
        static uint32_t s_panel_depth;
        static UITheme s_theme;
        static bool s_frame_active; // between begin_frame and end_frame; widgets no-op outside it
    };

}
