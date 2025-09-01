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

	    virtual void resize(uint32_t width, uint32_t height) override;
	    virtual int read_pixel(uint32_t attachment_index, int x, int y) override;
	    virtual void clear_color_attachment_i(uint32_t attachment_index, int value) override;


		virtual uint32_t get_color_attachment_renderer_id(uint32_t index = 0) const override { HN_CORE_ASSERT(index < m_color_attachments.size(), "Incorrect index."); return m_color_attachments[index]; }

		virtual const FramebufferSpecification& get_specification() const override { return m_specification; }

	private:
		uint32_t m_renderer_id = 0;
		FramebufferSpecification m_specification;

	    std::vector<FramebufferTextureSpecification> m_color_attachment_specs;
	    FramebufferTextureSpecification m_depth_attachment_specs;

	    std::vector<uint32_t> m_color_attachments;
	    uint32_t m_depth_attachment = 0;

	};
}