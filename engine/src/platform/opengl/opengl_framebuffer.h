#pragma once

#include "Honey/renderer/framebuffer.h"

namespace Honey {

	class OpenGLFramebuffer : public Framebuffer {
	public:
		OpenGLFramebuffer(const FramebufferSpecification& spec);
		virtual ~OpenGLFramebuffer();

		void invalidate();

		virtual void bind() override;
		virtual void unbind() override;

		virtual uint32_t get_color_attachment_renderer_id() const override { return m_color_attachment; }

		virtual const FramebufferSpecification& get_specification() const override { return m_specification; }

	private:
		uint32_t m_renderer_id;
		uint32_t m_color_attachment, m_depth_attachment;
		FramebufferSpecification m_specification;

	};
}