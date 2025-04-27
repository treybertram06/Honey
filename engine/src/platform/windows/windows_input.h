#pragma once

#include "Honey/input.h"

namespace Honey {

    class  WindowsInput : public Input {
    protected:
        virtual bool is_key_pressed_impl(int keycode) override;
        virtual bool is_mouse_button_pressed_impl(int button) override;
        virtual std::pair<float, float> get_mouse_position_impl() override;
        virtual float get_mouse_x_impl() override;
        virtual float get_mouse_y_impl() override;
    };

}