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

#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_LINUX)
            glCreateTextures(texture_target(multisample), count, out_id);
#endif
#ifdef HN_PLATFORM_MACOS
            glGenTextures(count, out_id);
#endif
        }

        static void bind_texture(bool multisample, uint32_t id) {
            glBindTexture(texture_target(multisample), id);
        }

        static void attach_color_texture(uint32_t id, uint32_t samples, GLenum internal_format, GLenum format, uint32_t width, uint32_t height, int index) {
            bool multisample = samples > 1;
            if (multisample) {
                glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, internal_format, width, height, GL_FALSE);
            } else {
                glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, GL_UNSIGNED_BYTE, nullptr);

                //test
                GLint internal = 0;
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internal);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, texture_target(multisample), id, 0);
        }

       static void attach_depth_texture(uint32_t id, uint32_t samples, GLenum format, GLenum attachment, uint32_t width, uint32_t height) {
            const bool multisample = samples > 1;
            const GLenum tgt = texture_target(multisample);

#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_LINUX)
            // DSA path
            if (multisample) {
                // allocate immutable multisample storage
                glTextureStorage2DMultisample(id, samples, format, width, height, GL_FALSE);
            } else {
                // allocate immutable storage + params
                glTextureStorage2D(id, 1, format, width, height);
                glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTextureParameteri(id, GL_TEXTURE_WRAP_R,  GL_CLAMP_TO_EDGE);
                glTextureParameteri(id, GL_TEXTURE_WRAP_S,  GL_CLAMP_TO_EDGE);
                glTextureParameteri(id, GL_TEXTURE_WRAP_T,  GL_CLAMP_TO_EDGE);
            }

            // Attach to currently bound framebuffer
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, tgt, id, 0);
#endif

#ifdef HN_PLATFORM_MACOS
            glBindTexture(tgt, id);

            if (multisample) {
                // multisample is fine on macOS
                glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, format, width, height, GL_FALSE);
            } else {
                // --- Replace glTexStorage2D with glTexImage2D on macOS ---
                GLenum baseFormat = GL_DEPTH_COMPONENT;
                GLenum type       = GL_UNSIGNED_INT;

                // Pick correct base format + type for the given internal format
                switch (format) {
                    //case GL_DEPTH32F:
                    //    baseFormat = GL_DEPTH_COMPONENT;
                    //    type       = GL_FLOAT;
                    //    break;
                    case GL_DEPTH_COMPONENT32:
                    case GL_DEPTH_COMPONENT24:
                    case GL_DEPTH_COMPONENT16:
                        baseFormat = GL_DEPTH_COMPONENT;
                        // GL_UNSIGNED_INT is fine for 24/32; for 16 you can also use GL_UNSIGNED_SHORT.
                        type       = GL_UNSIGNED_INT;
                        break;
                    case GL_DEPTH24_STENCIL8:
                        baseFormat = GL_DEPTH_STENCIL;
                        type       = GL_UNSIGNED_INT_24_8;
                        break;
#ifdef GL_DEPTH32F_STENCIL8
                    case GL_DEPTH32F_STENCIL8:
                        baseFormat = GL_DEPTH_STENCIL;
                        type       = GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
                        break;
#endif
                    default:
                        // sensible fallback
                        baseFormat = GL_DEPTH_COMPONENT;
                        type       = GL_UNSIGNED_INT;
                        break;
                }

                glTexImage2D(GL_TEXTURE_2D, 0, format, width, height,
                             0, baseFormat, type, nullptr);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R,  GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,  GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,  GL_CLAMP_TO_EDGE);

                // avoid sampling undefined mip levels
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,  0);
            }

            glBindTexture(tgt, 0);

            glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, tgt, id, 0);
#endif
        }

        static bool is_depth_format(FramebufferTextureFormat format) {
            switch (format) {
                case FramebufferTextureFormat::Depth: return true;
            }
            return false;
        }

        struct ClearFormat { GLenum format; GLenum type; };
        static ClearFormat honey_tex_format_to_gl(FramebufferTextureFormat fmt) {
            switch (fmt) {
                case FramebufferTextureFormat::RGBA8:
                    return { GL_RGBA, GL_UNSIGNED_BYTE };        // not GL_RGBA8, not GL_FLOAT
                case FramebufferTextureFormat::RED_INTEGER:
                    return { GL_RED_INTEGER, GL_INT };
                case FramebufferTextureFormat::DEPTH24STENCIL8:
                    return { GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8 };
            }
            HN_CORE_ASSERT(false, "Unknown format!");
            return { GL_NONE, GL_NONE };
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
                    case FramebufferTextureFormat::RGBA8:       Utils::attach_color_texture(m_color_attachments[i], m_specification.samples, GL_RGBA8, GL_RGBA, m_specification.width, m_specification.height, i); break;
                    case FramebufferTextureFormat::RED_INTEGER: Utils::attach_color_texture(m_color_attachments[i], m_specification.samples, GL_R32I, GL_RED_INTEGER, m_specification.width, m_specification.height, i); break;
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

    int OpenGLFramebuffer::read_pixel(uint32_t attachment_index, int x, int y) {
        HN_CORE_ASSERT(attachment_index < m_color_attachments.size(), "Incorrect attachment index.");
        HN_CORE_ASSERT(m_color_attachment_specs[attachment_index].texture_format == FramebufferTextureFormat::RED_INTEGER, "read_pixel expects a RED_INTEGER attachment");
        // Save current READ FBO, bind ours for read
        GLint prevReadFbo = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_renderer_id);

        glReadBuffer(GL_COLOR_ATTACHMENT0 + attachment_index);
        int pixel_data = -1;
        glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixel_data);

        // Restore previous READ FBO
        glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
        return pixel_data;
    }

    void OpenGLFramebuffer::clear_attachment_i32(uint32_t idx, int32_t v)   { clear_attachment(idx, &v); }
    void OpenGLFramebuffer::clear_attachment_u32(uint32_t idx, uint32_t v)  { clear_attachment(idx, &v); }
    void OpenGLFramebuffer::clear_attachment_f32(uint32_t idx, float v)     { clear_attachment(idx, &v); }

    void OpenGLFramebuffer::clear_attachment(uint32_t attachment_index, const void* value) {
        HN_CORE_ASSERT(attachment_index < m_color_attachments.size(), "Incorrect attachment index.");

        auto format_type = m_color_attachment_specs[attachment_index].texture_format;
        auto [format, type] = Utils::honey_tex_format_to_gl(format_type);

        const GLuint tex = m_color_attachments[attachment_index];

        // Pick the texture target used for these attachments
        const bool multisample = (m_specification.samples > 1); // adjust to your actual source of 'samples'
        const GLenum tgt = multisample ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;

#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_LINUX)
        glClearTexImage(tex, 0, format, type, &value);
#endif
#ifdef HN_PLATFORM_MACOS
        // value must not be null
        HN_CORE_ASSERT(value != nullptr, "clear_attachment: value must not be null");

        GLint prevFBO = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFBO);

        GLuint tmpFBO = 0;
        glGenFramebuffers(1, &tmpFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tmpFBO);

        // Attach the texture we want to clear at COLOR_ATTACHMENT0
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tgt, tex, 0);

        // Make sure the draw buffer points at COLOR_ATTACHMENT0
        GLenum db = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &db);

        GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        HN_CORE_ASSERT(status == GL_FRAMEBUFFER_COMPLETE, "Temp FBO incomplete while clearing attachment");

        // Build a safe 4-component temp based on the declared 'type'
        switch (type) {
            case GL_INT: {
                // Accept 1–4 ints; replicate if fewer
                const GLint* in = static_cast<const GLint*>(value);
                GLint tmp[4] = { in[0], in[0], in[0], in[0] };
                glClearBufferiv(GL_COLOR, 0, tmp);
                break;
            }
            case GL_UNSIGNED_INT: {
                const GLuint* in = static_cast<const GLuint*>(value);
                GLuint tmp[4] = { in[0], in[0], in[0], in[0] };
                glClearBufferuiv(GL_COLOR, 0, tmp);
                break;
            }
            default: { // float formats
                const GLfloat* in = static_cast<const GLfloat*>(value);
                GLfloat tmp[4] = { in[0], in[0], in[0], in[0] };
                glClearBufferfv(GL_COLOR, 0, tmp);
                break;
            }
        }

        // Restore & cleanup
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevFBO);
        glDeleteFramebuffers(1, &tmpFBO);
#endif
    }

}
