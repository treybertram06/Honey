#pragma once

#include "event.h"

#include <sstream>

namespace Honey {

    class HONEY_API MouseMovedEvent : public Event {
    public:
        MouseMovedEvent(float x, float y) : m_mouse_x(x), m_mouse_y(y) {}

        inline float get_x() const { return m_mouse_x; }
        inline float get_y() const { return m_mouse_y; }

        std::string to_string() const override {
            std::stringstream ss;
            ss << "MouseMovedEvent: " << m_mouse_x << ", " << m_mouse_y;
            return ss.str();
        }

        EVENT_CLASS_TYPE(mouse_moved)
        EVENT_CLASS_CATEGORY(event_category_mouse | event_category_input)

    private:
        float m_mouse_x, m_mouse_y;
    };

    class HONEY_API MouseScrolledEvent : public Event {
    public:
        MouseScrolledEvent(float xoffset, float yoffset) : m_xoffset(xoffset), m_yoffset(yoffset) {}

        inline float get_xoffset() const { return m_xoffset; }
        inline float get_yoffset() const { return m_yoffset; }

        std::string to_string() const override {
            std::stringstream ss;
            ss << "MouseScrolledEvent: " << get_xoffset << ", " << get_yoffset;
            return ss.str();
        }

        EVENT_CLASS_TYPE(mouse_scrolled)
        EVENT_CLASS_CATEGORY(event_category_mouse | event_category_input)

    private:
        float m_xoffset, m_yoffset;
    };

    class HONEY_API MouseButtonEvent : public Event {
    public:
        inline int get_mouse_button() const { return m_button; }

        EVENT_CLASS_CATEGORY(event_category_mouse | event_category_input)

    protected:
        MouseButtonEvent(int button) : m_button(button) {}

        int m_button;
    };

    class HONEY_API MouseButtonPressedEvent : MouseButtonEvent {
        MouseButtonPressedEvent(int button) : MouseButtonEvent(button) {}

        std::string to_string() const override {
            std::stringstream ss;
            ss << "MouseButtonPressedEvent: " << m_button;
            return ss.str();
        }

        EVENT_CLASS_TYPE(mouse_button_pressed)
    };

    class HONEY_API MouseButtonReleasedEvent : MouseButtonEvent {
        MouseButtonReleasedEvent(int button) : MouseButtonEvent(button) {}

        std::string to_string() const override {
            std::stringstream ss;
            ss << "MouseButtonReleasedEvent: " << m_button;
            return ss.str();
        }

        EVENT_CLASS_TYPE(mouse_button_released)
    };
}