#pragma once
#include <Honey.h>

#include "../../engine/src/Honey/debug/instrumentor.h"

class Application2D : public Honey::Layer {

public:
    Application2D();
    virtual ~Application2D() = default;

    virtual void on_attach() override;
    virtual void on_detach() override;

    void on_update(Honey::Timestep ts) override;
    virtual void on_imgui_render() override;
    void on_event(Honey::Event &event) override;

private:
    Honey::OrthographicCameraController m_camera_controller;

    // temp
    Honey::Ref<Honey::VertexArray> m_square_vertex_array;
    Honey::Ref<Honey::Shader> m_shader;
    Honey::Ref<Honey::Texture2D> m_missing_texture;
    Honey::Ref<Honey::Texture2D> m_chuck_texture, m_sprite_sheet01, m_sprite_sheet02;
    Honey::Ref<Honey::SubTexture2D> m_bush_sprite, m_grass_sprite, m_player_sprite, m_water_sprite;

    Honey::Ref<Honey::Framebuffer> m_framebuffer;

    uint32_t m_map_width, m_map_height;

    glm::vec4 m_clear_color = { 0.1f, 0.1f, 0.1f, 1.0f };
    glm::vec3 m_square_position;

    std::unordered_map<char, Honey::Ref<Honey::SubTexture2D>> s_texture_map;

    Honey::FramerateCounter m_framerate_counter;
    int m_framerate = 0;
    float m_frame_time = 0.0f;


};