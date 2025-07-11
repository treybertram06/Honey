#pragma once

#include "core.h"

namespace Honey {

    class HONEY_API Input {
    public:
        inline static bool is_key_pressed(int keycode) { return s_instance->is_key_pressed_impl(keycode); }
        inline static bool is_mouse_button_pressed(int button) { return s_instance->is_mouse_button_pressed_impl(button); }
        inline static std::pair<float, float> get_mouse_position() { return s_instance->get_mouse_position_impl(); }
        inline static void set_cursor_locked(bool locked) { s_instance->set_cursor_locked_impl(locked); }
        inline static bool is_cursor_locked() { return s_instance->is_cursor_locked_impl(); }
        inline static float get_mouse_x() { return s_instance->get_mouse_x_impl(); }
        inline static float get_mouse_y() { return s_instance->get_mouse_y_impl(); }

    protected:
        virtual bool is_key_pressed_impl(int keycode) = 0;
        virtual bool is_mouse_button_pressed_impl(int button) = 0;
        virtual std::pair<float, float> get_mouse_position_impl() = 0;
        virtual void set_cursor_locked_impl(bool locked) = 0;
        virtual bool is_cursor_locked_impl() = 0;

        virtual float get_mouse_x_impl() = 0;
        virtual float get_mouse_y_impl() = 0;

    private:
        static Input* s_instance;
    };
}