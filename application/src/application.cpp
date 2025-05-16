#include <Honey.h>
#include "Honey/entry_point.h"
//#include "vendor/imgui/imgui.h"
#include <imgui.h>


class ExampleLayer : public Honey::Layer {
public:
    ExampleLayer()
        : Layer("Example") {}

    void on_update() override {
        //HN_INFO("Example layer update");
        if (Honey::Input::is_key_pressed(HN_KEY_TAB))
            HN_INFO("Tab is pressed (event)");


    }

    //doesnt work on windows
    virtual void on_imgui_render() override {
        ImGui::Begin("Test");
        ImGui::Text("Hello dingus");
        ImGui::End();
    }

    void on_event(Honey::Event &event) override {
        //HN_TRACE(event);

        if (event.get_event_type() == Honey::EventType::key_pressed) {
            auto& e = (Honey::KeyPressedEvent&)event;
            if (e.get_key_code() == HN_KEY_TAB)
                HN_INFO("Tab is pressed (event)");
            //HN_TRACE("{0}", (char)e.get_key_code());
        }


    }
};

class Sandbox : public Honey::Application {
public:
    Sandbox() {
        push_layer(new ExampleLayer());
    }

    ~Sandbox() {}



};

Honey::Application* Honey::create_application() {

    return new Sandbox();

}
