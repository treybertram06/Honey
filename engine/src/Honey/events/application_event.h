#pragma once

#include "event.h"

#include <sstream>

namespace Honey {

    class HONEY_API WindowResizeEvent : public Event {
    public:
        WindowResizeEvent(unsigned int width, unsigned int height) : m_width(width), m_height(height) {}

        inline unsigned int get_width() const { return m_width; }
        inline unsigned int get_height() const { return m_height; }

        std::string to_string() const override {
            std::stringstream ss;
            ss << "WindowResizedEvent: " << m_width << ", " << m_height;
            return ss.str();
        }

        EVENT_CLASS_TYPE(window_resize)
        EVENT_CLASS_CATEGORY(event_category_application)
    private:
        unsigned int m_width, m_height;
    };

    class HONEY_API WindowCloseEvent : public Event {
    public:
        WindowCloseEvent() {}

        EVENT_CLASS_TYPE(window_closed)
        EVENT_CLASS_CATEGORY(event_category_application)
    };

    class HONEY_API AppTickEvent : public Event {
    public:
        AppTickEvent() {}

        EVENT_CLASS_TYPE(app_tick)
        EVENT_CLASS_CATEGORY(event_category_application)
    };

    class HONEY_API AppUpdateEvent : public Event {
    public:
        AppUpdateEvent() {}

        EVENT_CLASS_TYPE(app_update)
        EVENT_CLASS_CATEGORY(event_category_application)
    };

    class HONEY_API AppRenderEvent : public Event {
    public:
        AppRenderEvent() {}

        EVENT_CLASS_TYPE(app_render)
        EVENT_CLASS_CATEGORY(event_category_application)
    };




}