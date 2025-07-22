#pragma once

namespace Honey {

    // Scoped enumeration for mouse buttons
    enum class MouseButton : uint8_t {
        Button1 = 0,
        Button2 = 1,
        Button3 = 2,
        Button4 = 3,
        Button5 = 4,
        Button6 = 5,
        Button7 = 6,
        Button8 = 7,

        Last    = Button8,
        Left    = Button1,
        Right   = Button2,
        Middle  = Button3,
    };

    inline std::string to_string(MouseButton button) {
        switch (button) {
            case MouseButton::Button1: return "Button1 (Left)";
            case MouseButton::Button2: return "Button2 (Right)";
            case MouseButton::Button3: return "Button3 (Middle)";
            case MouseButton::Button4: return "Button4";
            case MouseButton::Button5: return "Button5";
            case MouseButton::Button6: return "Button6";
            case MouseButton::Button7: return "Button7";
            case MouseButton::Button8: return "Button8";
            default:                   return "Unknown";
        }
    }

    inline std::ostream& operator<<(std::ostream& os, MouseButton button) {
        return os << to_string(button);
    }


} // namespace Honey
