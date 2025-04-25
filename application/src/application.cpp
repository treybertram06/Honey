#include <Honey.h>
#include "Honey/entry_point.h"


class ExampleLayer : public Honey::Layer {
public:
    ExampleLayer()
        : Layer("Example") {}

    void on_update() override {
        //HN_INFO("Example layer update");
    }

    void on_event(Honey::Event &event) override {
        //HN_TRACE(event);


    }
};

class Sandbox : public Honey::Application {
public:
    Sandbox() {
        push_layer(new ExampleLayer());
        push_overlay(new Honey::ImGuiLayer());
    }

    ~Sandbox() {}

};

Honey::Application* Honey::create_application() {

    return new Sandbox();

}