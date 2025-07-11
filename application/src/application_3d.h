#pragma once
#include <Honey.h>

#include "../../engine/src/Honey/debug/instrumentor.h"

class Application3D : public Honey::Layer {

public:
	Application3D();
	virtual ~Application3D() = default;

	virtual void on_attach() override;
	virtual void on_detach() override;

	void on_update(Honey::Timestep ts) override;
	virtual void on_imgui_render() override;
	void on_event(Honey::Event &event) override;
	bool on_key_pressed_event(Honey::KeyPressedEvent& e);

private:
	Honey::PerspectiveCameraController m_camera_controller;

	// temp
	Honey::Ref<Honey::VertexArray> m_square_vertex_array;
	Honey::Ref<Honey::Shader> m_shader;
	Honey::Ref<Honey::Texture2D> m_chuck_texture, m_transparent_texture;
	Honey::Ref<Honey::Texture2D> m_missing_texture;

	glm::vec4 m_square_color = { 0.2f, 0.3f, 0.8f, 1.0f };
	glm::vec3 m_square_position;

	Honey::FramerateCounter m_framerate_counter;
	int m_framerate = 0;
	float m_frame_time = 0.0f;


};