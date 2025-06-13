#pragma once
#include "Honey/events/application_event.h"
#include "Honey/events/key_event.h"
#include "Honey/events/mouse_event.h"
#include "../core/layer.h"

namespace Honey {

    class HONEY_API ImGuiLayer : public Layer {

    public:
        ImGuiLayer();
        ~ImGuiLayer();

        virtual void on_attach() override;
        virtual void on_detach() override;
        virtual void on_imgui_render() override;

        void begin();
        void end();

    private:
        float m_time = 0.0f;


    };

} // Honey
