#pragma once

namespace Honey {

    // Scoped enumeration for keycodes
    enum class KeyCode : uint16_t {
        // Basic printable keys
        Space = 32,
        Apostrophe = 39, /* ' */
        Comma = 44,      /* , */
        Minus = 45,      /* - */
        Period = 46,     /* . */
        Slash = 47,      /* / */

        D0 = 48,         // Number keys 0-9
        D1 = 49,
        D2 = 50,
        D3 = 51,
        D4 = 52,
        D5 = 53,
        D6 = 54,
        D7 = 55,
        D8 = 56,
        D9 = 57,

        Semicolon = 59,  /* ; */
        Equal = 61,      /* = */

        // Alphabet keys
        A = 65,
        B = 66,
        C = 67,
        D = 68,
        E = 69,
        F = 70,
        G = 71,
        H = 72,
        I = 73,
        J = 74,
        K = 75,
        L = 76,
        M = 77,
        N = 78,
        O = 79,
        P = 80,
        Q = 81,
        R = 82,
        S = 83,
        T = 84,
        U = 85,
        V = 86,
        W = 87,
        X = 88,
        Y = 89,
        Z = 90,

        LeftBracket = 91,  /* [ */
        Backslash = 92,    /* \ */
        RightBracket = 93, /* ] */
        GraveAccent = 96,  /* ` */

        // Function keys
        Escape = 256,
        Enter = 257,
        Tab = 258,
        Backspace = 259,
        Insert = 260,
        Delete = 261,
        Right = 262,
        Left = 263,
        Down = 264,
        Up = 265,
        PageUp = 266,
        PageDown = 267,
        Home = 268,
        End = 269,
        CapsLock = 280,
        ScrollLock = 281,
        NumLock = 282,
        PrintScreen = 283,
        Pause = 284,

        // F1-F25
        F1 = 290,
        F2 = 291,
        F3 = 292,
        F4 = 293,
        F5 = 294,
        F6 = 295,
        F7 = 296,
        F8 = 297,
        F9 = 298,
        F10 = 299,
        F11 = 300,
        F12 = 301,
        F13 = 302,
        F14 = 303,
        F15 = 304,
        F16 = 305,
        F17 = 306,
        F18 = 307,
        F19 = 308,
        F20 = 309,
        F21 = 310,
        F22 = 311,
        F23 = 312,
        F24 = 313,
        F25 = 314,

        // Keypad
        Keypad0 = 320,
        Keypad1 = 321,
        Keypad2 = 322,
        Keypad3 = 323,
        Keypad4 = 324,
        Keypad5 = 325,
        Keypad6 = 326,
        Keypad7 = 327,
        Keypad8 = 328,
        Keypad9 = 329,
        KeypadDecimal = 330,
        KeypadDivide = 331,
        KeypadMultiply = 332,
        KeypadSubtract = 333,
        KeypadAdd = 334,
        KeypadEnter = 335,
        KeypadEqual = 336,

        // Modifier keys
        LeftShift = 340,
        LeftControl = 341,
        LeftAlt = 342,
        LeftSuper = 343,
        RightShift = 344,
        RightControl = 345,
        RightAlt = 346,
        RightSuper = 347,
        Menu = 348,

        Unknown = 0
    };

    inline std::string to_string(KeyCode keyCode) {
        switch (keyCode) {
            // Printable keys
            case KeyCode::Space:              return "Space";
            case KeyCode::Apostrophe:         return "Apostrophe";
            case KeyCode::Comma:              return "Comma";
            case KeyCode::Minus:              return "Minus";
            case KeyCode::Period:             return "Period";
            case KeyCode::Slash:              return "Slash";
            case KeyCode::D0:                 return "0";
            case KeyCode::D1:                 return "1";
            case KeyCode::D2:                 return "2";
            case KeyCode::D3:                 return "3";
            case KeyCode::D4:                 return "4";
            case KeyCode::D5:                 return "5";
            case KeyCode::D6:                 return "6";
            case KeyCode::D7:                 return "7";
            case KeyCode::D8:                 return "8";
            case KeyCode::D9:                 return "9";
            case KeyCode::Semicolon:          return "Semicolon";
            case KeyCode::Equal:              return "Equal";

            // Alphabet keys
            case KeyCode::A:                  return "A";
            case KeyCode::B:                  return "B";
            case KeyCode::C:                  return "C";
            case KeyCode::D:                  return "D";
            case KeyCode::E:                  return "E";
            case KeyCode::F:                  return "F";
            case KeyCode::G:                  return "G";
            case KeyCode::H:                  return "H";
            case KeyCode::I:                  return "I";
            case KeyCode::J:                  return "J";
            case KeyCode::K:                  return "K";
            case KeyCode::L:                  return "L";
            case KeyCode::M:                  return "M";
            case KeyCode::N:                  return "N";
            case KeyCode::O:                  return "O";
            case KeyCode::P:                  return "P";
            case KeyCode::Q:                  return "Q";
            case KeyCode::R:                  return "R";
            case KeyCode::S:                  return "S";
            case KeyCode::T:                  return "T";
            case KeyCode::U:                  return "U";
            case KeyCode::V:                  return "V";
            case KeyCode::W:                  return "W";
            case KeyCode::X:                  return "X";
            case KeyCode::Y:                  return "Y";
            case KeyCode::Z:                  return "Z";

            // Bracket and misc keys
            case KeyCode::LeftBracket:        return "Left Bracket";
            case KeyCode::Backslash:          return "Backslash";
            case KeyCode::RightBracket:       return "Right Bracket";
            case KeyCode::GraveAccent:        return "Grave Accent";

            // Function keys
            case KeyCode::Escape:             return "Escape";
            case KeyCode::Enter:              return "Enter";
            case KeyCode::Tab:                return "Tab";
            case KeyCode::Backspace:          return "Backspace";
            case KeyCode::Insert:             return "Insert";
            case KeyCode::Delete:             return "Delete";
            case KeyCode::Right:              return "Right Arrow";
            case KeyCode::Left:               return "Left Arrow";
            case KeyCode::Down:               return "Down Arrow";
            case KeyCode::Up:                 return "Up Arrow";
            case KeyCode::PageUp:             return "Page Up";
            case KeyCode::PageDown:           return "Page Down";
            case KeyCode::Home:               return "Home";
            case KeyCode::End:                return "End";
            case KeyCode::CapsLock:           return "Caps Lock";
            case KeyCode::ScrollLock:         return "Scroll Lock";
            case KeyCode::NumLock:            return "Num Lock";
            case KeyCode::PrintScreen:        return "Print Screen";
            case KeyCode::Pause:              return "Pause";

            // F1 to F25 keys
            case KeyCode::F1:                 return "F1";
            case KeyCode::F2:                 return "F2";
            case KeyCode::F3:                 return "F3";
            case KeyCode::F4:                 return "F4";
            case KeyCode::F5:                 return "F5";
            case KeyCode::F6:                 return "F6";
            case KeyCode::F7:                 return "F7";
            case KeyCode::F8:                 return "F8";
            case KeyCode::F9:                 return "F9";
            case KeyCode::F10:                return "F10";
            case KeyCode::F11:                return "F11";
            case KeyCode::F12:                return "F12";
            case KeyCode::F13:                return "F13";
            case KeyCode::F14:                return "F14";
            case KeyCode::F15:                return "F15";
            case KeyCode::F16:                return "F16";
            case KeyCode::F17:                return "F17";
            case KeyCode::F18:                return "F18";
            case KeyCode::F19:                return "F19";
            case KeyCode::F20:                return "F20";
            case KeyCode::F21:                return "F21";
            case KeyCode::F22:                return "F22";
            case KeyCode::F23:                return "F23";
            case KeyCode::F24:                return "F24";
            case KeyCode::F25:                return "F25";

            // Keypad keys
            case KeyCode::Keypad0:            return "Keypad 0";
            case KeyCode::Keypad1:            return "Keypad 1";
            case KeyCode::Keypad2:            return "Keypad 2";
            case KeyCode::Keypad3:            return "Keypad 3";
            case KeyCode::Keypad4:            return "Keypad 4";
            case KeyCode::Keypad5:            return "Keypad 5";
            case KeyCode::Keypad6:            return "Keypad 6";
            case KeyCode::Keypad7:            return "Keypad 7";
            case KeyCode::Keypad8:            return "Keypad 8";
            case KeyCode::Keypad9:            return "Keypad 9";
            case KeyCode::KeypadDecimal:      return "Keypad Decimal";
            case KeyCode::KeypadDivide:       return "Keypad Divide";
            case KeyCode::KeypadMultiply:     return "Keypad Multiply";
            case KeyCode::KeypadSubtract:     return "Keypad Subtract";
            case KeyCode::KeypadAdd:          return "Keypad Add";
            case KeyCode::KeypadEnter:        return "Keypad Enter";
            case KeyCode::KeypadEqual:        return "Keypad Equal";

            // Modifier keys
            case KeyCode::LeftShift:          return "Left Shift";
            case KeyCode::LeftControl:        return "Left Control";
            case KeyCode::LeftAlt:            return "Left Alt";
            case KeyCode::LeftSuper:          return "Left Super";
            case KeyCode::RightShift:         return "Right Shift";
            case KeyCode::RightControl:       return "Right Control";
            case KeyCode::RightAlt:           return "Right Alt";
            case KeyCode::RightSuper:         return "Right Super";
            case KeyCode::Menu:               return "Menu";

            default:                          return "Unknown";
        }
    }


    // Overload operator<< for std::ostream
    inline std::ostream& operator<<(std::ostream& os, KeyCode keyCode) {
        return os << to_string(keyCode);
    }


} // namespace Honey