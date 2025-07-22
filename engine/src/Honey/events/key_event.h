#pragma once

#include "event.h"
#include "Honey/core/keycodes.h"


namespace Honey {

    class KeyEvent : public Event {
    public:
        inline KeyCode get_key_code() const { return m_key_code; }

        EVENT_CLASS_CATEGORY(event_category_keyboard | event_category_input)

    protected:
        explicit KeyEvent(KeyCode keycode) : m_key_code(keycode) {}

        KeyCode m_key_code;
    };

    class KeyPressedEvent : public KeyEvent {
    public:
        KeyPressedEvent(KeyCode keycode, int repeat_count) : KeyEvent(keycode), m_repeat_count(repeat_count) {}

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

    class KeyReleasedEvent : public KeyEvent {
    public:
        KeyReleasedEvent(KeyCode keycode) : KeyEvent(keycode) {}

        std::string to_string() const override {
            std::stringstream ss;
            ss << "key_released_event: " << m_key_code;
            return ss.str();
        }

        EVENT_CLASS_TYPE(key_released)
    };

    class KeyTypedEvent : public KeyEvent {
    public:
        KeyTypedEvent(KeyCode keycode) : KeyEvent(keycode) {}

        std::string to_string() const override {
            std::stringstream ss;
            ss << "key_typed_event: " << m_key_code;
            return ss.str();
        }

        EVENT_CLASS_TYPE(key_typed)


    };
}
