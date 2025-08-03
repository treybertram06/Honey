#pragma once
#include "Honey/events/application_event.h"
#include "Honey/events/key_event.h"
#include "Honey/events/mouse_event.h"
#include "../core/layer.h"

namespace Honey {

    class ImGuiLayer : public Layer {

    public:
        ImGuiLayer();
        ~ImGuiLayer();

        virtual void on_attach() override;
        virtual void on_detach() override;
        virtual void on_event(Event& e) override;
        virtual void on_imgui_render() override;

        void begin();
        void end();

        void block_events(bool block) { m_block_events = block; }
    private:
        bool m_block_events = true;
        float m_time = 0.0f;


    };

} // Honey
