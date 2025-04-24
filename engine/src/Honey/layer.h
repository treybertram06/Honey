#pragma once

#include "Honey/core.h"
#include "Honey/events/event.h"

namespace Honey {

    class HONEY_API Layer {
    public:
        Layer(const std::string& name = "Layer");
        virtual ~Layer();

        virtual void on_attach() {}
        virtual void on_detach() {}
        virtual void on_update() {}
        virtual void on_event(Event& event) {}

        inline const std::string& get_name() const { return m_debug_name; }

    protected:
        std::string m_debug_name;
    };
}