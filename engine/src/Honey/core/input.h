#pragma once

#include "core.h"

namespace Honey {

    class Input {
    public:
        static bool is_key_pressed(int keycode);
        static bool is_mouse_button_pressed(int button);
        static std::pair<float, float> get_mouse_position();
        static void set_cursor_locked(bool locked);
        static bool is_cursor_locked();
        static float get_mouse_x();
        static float get_mouse_y();
    };
}