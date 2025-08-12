#pragma once
#include "Honey/events/application_event.h"
#include "Honey/events/key_event.h"
#include "Honey/events/mouse_event.h"
#include "../core/layer.h"

namespace Honey {

    enum class UITheme {
        Monochrome = 0,
        HoneyAmber = 1,
        ForestGreen = 2,
        CaramelCream = 3,
        MaltAndHops = 4,
        Copper = 5
    };

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

        UITheme get_current_theme() const { return m_current_theme; }
        void set_theme(UITheme theme);
        const char* get_theme_name(UITheme theme) const;


    private:
        bool m_block_events = true;
        float m_time = 0.0f;
        UITheme m_current_theme = UITheme::Monochrome;


    };

} // Honey
