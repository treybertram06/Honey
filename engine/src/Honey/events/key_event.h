#pragma once

#include "event.h"

#include <sstream>

namespace Honey {

    class HONEY_API KeyEvent : public Event {
    public:
        inline int get_key_code() const { return m_key_code; }

        EVENT_CLASS_CATEGORY(event_category_keyboard | event_category_input)

    protected:
        KeyEvent(int keycode) : m_key_code(keycode) {}

        int m_key_code;
    };

    class HONEY_API KeyPressedEvent : public KeyEvent {
    public:
        KeyPressedEvent(int keycode, int repeat_count) : KeyEvent(keycode), m_repeat_count(repeat_count) {}

        inline int get_repeat_count() const { return m_repeat_count; }

        std::string to_string() const override {
            std::stringstream ss;
            ss << "key_pressed_event: " << m_key_code << " (" << m_repeat_count << " repeats)";
            return ss.str();
        }

        EVENT_CLASS_TYPE(key_pressed)

    private:
        int m_repeat_count;
    };

    class HONEY_API KeyReleasedEvent : public KeyEvent {
    public:
        KeyReleasedEvent(int keycode) : KeyEvent(keycode) {}

        std::string to_string() const override {
            std::stringstream ss;
            ss << "key_released_event: " << m_key_code;
            return ss.str();
        }

        EVENT_CLASS_TYPE(key_released)
    };
}