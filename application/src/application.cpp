#include <Honey.h>
#include <Honey/core/entry_point.h>
#include <imgui.h>

#include "application_2d.h"
#include "application_3d.h"
#include "../../engine/src/Honey/debug/instrumentor.h"
#include "examples.h"
#include "glm/gtc/type_ptr.inl"
#include "platform/opengl/opengl_shader.h"

class Sandbox : public Honey::Application {
public:
    Sandbox() {
        //push_layer(new PongLayer());
        //push_layer(new ExampleLayer());
        push_layer(new Honey::Application2D());
        //push_layer(new Application3D());

    }

    ~Sandbox() {}

};

Honey::Application* Honey::create_application() {

    return new Sandbox();

}
