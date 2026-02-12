#pragma once
#include <Honey.h>

#include "../../engine/src/Honey/debug/instrumentor.h"

namespace Honey {
    class Application2D : public Layer {

    public:
        Application2D();
        virtual ~Application2D() = default;

        virtual void on_attach() override;
        virtual void on_detach() override;

        void on_update(Timestep ts) override;
        virtual void on_imgui_render() override;
        void on_event(Event &event) override;

    private:
        EditorCamera m_camera;

        Ref<Texture2D> m_chuck_texture;

        glm::vec4 m_clear_color = { 0.1f, 0.1f, 0.1f, 1.0f };
        glm::vec2 m_viewport_size = { 1280.0f, 720.0f };
        glm::vec3 m_square_position;

        FramerateCounter m_framerate_counter;
        int m_framerate = 0;
        float m_frame_time = 0.0f;


    };
}