#include "hnpch.h"
#include "opengl_framebuffer.h"

#include <glad/glad.h>

namespace Honey {
	OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferSpecification &spec)
		: m_specification(spec) {
		invalidate();
	}

	OpenGLFramebuffer::~OpenGLFramebuffer() {
		glDeleteFramebuffers(1, &m_renderer_id);
	    glDeleteTextures(1, &m_color_attachment);
	    glDeleteTextures(1, &m_depth_attachment);
	}

	void OpenGLFramebuffer::invalidate() {

	    if (m_renderer_id) {
	        glDeleteFramebuffers(1, &m_renderer_id);
	        glDeleteTextures(1, &m_color_attachment);
	        glDeleteTextures(1, &m_depth_attachment);
	    }

#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_LINUX)
		glCreateFramebuffers(1, &m_renderer_id);
#endif
#if defined(HN_PLATFORM_MACOS)
		glGenFramebuffers(1, &m_renderer_id);
#endif
		glBindFramebuffer(GL_FRAMEBUFFER, m_renderer_id);

#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_LINUX)
		glCreateTextures(GL_TEXTURE_2D, 1, &m_color_attachment);
#endif
#if defined(HN_PLATFORM_MACOS)
		glGenTextures(1, &m_color_attachment);
#endif
		glBindTexture(GL_TEXTURE_2D, m_color_attachment);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_specification.width, m_specification.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color_attachment, 0);

#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_LINUX)
		glCreateTextures(GL_TEXTURE_2D, 1, &m_depth_attachment);
		glBindTexture(GL_TEXTURE_2D, m_depth_attachment);
		glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH24_STENCIL8, m_specification.width, m_specification.height);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_depth_attachment, 0);
#endif
#if defined(HN_PLATFORM_MACOS)
		glGenTextures(1, &m_depth_attachment);
		glBindTexture(GL_TEXTURE_2D, m_depth_attachment);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, m_specification.width, m_specification.height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_depth_attachment, 0);
#endif

		HN_CORE_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Framebuffer is incomplete!");

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void OpenGLFramebuffer::bind() {
		glBindFramebuffer(GL_FRAMEBUFFER, m_renderer_id);
	    glViewport(0, 0, m_specification.width, m_specification.height);
	}

	void OpenGLFramebuffer::unbind() {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

    void OpenGLFramebuffer::resize(uint32_t width, uint32_t height) {
	    m_specification.width = width;
	    m_specification.height = height;
	    invalidate();
    }

}
