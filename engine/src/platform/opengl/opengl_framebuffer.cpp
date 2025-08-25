#include "hnpch.h"
#include "opengl_framebuffer.h"

#include <glad/glad.h>

namespace Honey {

    static const uint32_t s_max_framebuffer_size = 8192;

    namespace Utils {

        static GLenum texture_target(bool multisample) {
            return multisample ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
        }

        static void create_textures(bool multisample, uint32_t* out_id, uint32_t count) {
            glCreateTextures(texture_target(multisample), count, out_id);
        }

        static void bind_texture(bool multisample, uint32_t id) {
            glBindTexture(texture_target(multisample), id);
        }

        static void attach_color_texture(uint32_t id, uint32_t samples, GLenum format, uint32_t width, uint32_t height, int index) {
            bool multisample = samples > 1;
            if (multisample) {
                glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, format, width, height, GL_FALSE);
            } else {
                glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, texture_target(multisample), id, 0);
        }

        static void attach_depth_texture(uint32_t id, uint32_t samples, GLenum format, GLenum attachment, uint32_t width, uint32_t height) {
            bool multisample = samples > 1;
            if (multisample) {
                glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, format, width, height, GL_FALSE);
            } else {
                glTexStorage2D(GL_TEXTURE_2D, 1, format, width, height);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, texture_target(multisample), id, 0);
        }

        static bool is_depth_format(FramebufferTextureFormat format) {
            switch (format) {
                case FramebufferTextureFormat::Depth: return true;
            }
            return false;
        }
    }

	OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferSpecification &spec)
		: m_specification(spec) {

	    for (auto format : m_specification.attachments.attachments) {
            if (!Utils::is_depth_format(format.texture_format)) {
                m_color_attachment_specs.emplace_back(format);
            } else {
                m_depth_attachment_specs = format;
            }
	    }

		invalidate();
	}

	OpenGLFramebuffer::~OpenGLFramebuffer() {
		glDeleteFramebuffers(1, &m_renderer_id);
        glDeleteTextures(m_color_attachments.size(), m_color_attachments.data());
	    glDeleteTextures(1, &m_depth_attachment);
	}

	void OpenGLFramebuffer::invalidate() {

	    if (m_renderer_id) {
	        glDeleteFramebuffers(1, &m_renderer_id);
	        glDeleteTextures(m_color_attachments.size(), m_color_attachments.data());
	        glDeleteTextures(1, &m_depth_attachment);

	        m_color_attachments.clear();
	        m_depth_attachment = 0;
	    }

#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_LINUX)
		glCreateFramebuffers(1, &m_renderer_id);
#endif
#if defined(HN_PLATFORM_MACOS)
		glGenFramebuffers(1, &m_renderer_id);
#endif
		glBindFramebuffer(GL_FRAMEBUFFER, m_renderer_id);

        bool multisample = m_specification.samples > 1;

        //attachments

        if (m_color_attachment_specs.size()) {
            m_color_attachments.resize(m_color_attachment_specs.size());
            Utils::create_textures(multisample, m_color_attachments.data(), m_color_attachments.size());

            for (size_t i = 0; i < m_color_attachments.size(); i++) {
                Utils::bind_texture(multisample, m_color_attachments[i]);
                switch (m_color_attachment_specs[i].texture_format) {
                    case FramebufferTextureFormat::RGBA8: Utils::attach_color_texture(m_color_attachments[i], m_specification.samples, GL_RGBA8, m_specification.width, m_specification.height, i); break;
                }
            }
        }

        if (m_depth_attachment_specs.texture_format != FramebufferTextureFormat::None) {
            Utils::create_textures(multisample, &m_depth_attachment, 1);
            Utils::bind_texture(multisample, m_depth_attachment);
            switch (m_depth_attachment_specs.texture_format) {
                case FramebufferTextureFormat::DEPTH24STENCIL8: Utils::attach_depth_texture(m_depth_attachment, m_specification.samples, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL_ATTACHMENT, m_specification.width, m_specification.height); break;
            }
        }

        if (m_color_attachments.size() > 1) {
            HN_CORE_ASSERT(m_color_attachments.size() <= 4, "Only 1-4 color attachments are supported!");
            GLenum buffers[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
            glDrawBuffers(m_color_attachments.size(), buffers);
        } else if (m_color_attachments.empty()) {
            glDrawBuffer(GL_NONE);
        }

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

	    if (width == 0 || height == 0  ||  width > s_max_framebuffer_size || height > s_max_framebuffer_size ) {
	        HN_CORE_WARN("Attempted to resize framebuffer to {0}, {1}, which is outside of maximum.", width, height);
	        return;
	    }

	    m_specification.width = width;
	    m_specification.height = height;
	    invalidate();
    }

}
