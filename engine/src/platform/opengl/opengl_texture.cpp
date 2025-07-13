#include "hnpch.h"
#include "opengl_texture.h"

#include "stb_image.h"

#include <glad/glad.h>

namespace Honey {

    OpenGLTexture2D::OpenGLTexture2D(uint32_t width, uint32_t height)
        : m_width(width), m_height(height) {
    	HN_PROFILE_FUNCTION();

        // Set format first
        m_internal_format = GL_RGBA8;
        m_format = GL_RGBA;

#if defined(HN_PLATFORM_WINDOWS)
        // New‐style (DSA) API, requires OpenGL ≥4.5
        glCreateTextures(GL_TEXTURE_2D, 1, &m_renderer_id);
        glTextureStorage2D(m_renderer_id, 1, m_internal_format, m_width, m_height);

        // Set texture parameters (DSA calls)
        //glTextureParameteri(m_renderer_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        //glTextureParameteri(m_renderer_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(m_renderer_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_renderer_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(m_renderer_id, GL_TEXTURE_WRAP_S,     GL_REPEAT);
        glTextureParameteri(m_renderer_id, GL_TEXTURE_WRAP_T,     GL_REPEAT);

#elif defined(HN_PLATFORM_MACOS)
        // Old‐style bind‐and‐upload API, since macOS max core is GL 4.1
        glGenTextures(1, &m_renderer_id);
        glBindTexture(GL_TEXTURE_2D, m_renderer_id);

        // Set texture parameters (legacy calls)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);

        // Allocate storage with null data
        glTexImage2D(GL_TEXTURE_2D, 0, m_internal_format, m_width, m_height, 0, m_format, GL_UNSIGNED_BYTE, nullptr);

        // Unbind to avoid accidental modification
        glBindTexture(GL_TEXTURE_2D, 0);

#else
    #error "Unsupported platform for OpenGLTexture2D creation"
#endif
    }

    OpenGLTexture2D::OpenGLTexture2D(const std::string& path)
        : m_path(path)
    {
    	HN_PROFILE_FUNCTION();

        int width, height, channels;
        stbi_set_flip_vertically_on_load(true);
    	stbi_uc* data = nullptr;
	    {
    		HN_PROFILE_SCOPE("OpenGLTexture2D::OpenGLTexture2D(const std::string& path) - stbi_load");
		    data = stbi_load(path.c_str(), &width, &height, &channels, 0);
	    }
        if (data == nullptr) {
            HN_CORE_ERROR("At path: {0}", path);
            HN_CORE_ASSERT(false, "Failed to load image.");
        }

        m_width  = width;
        m_height = height;

        // Choose internal format based on channel count
        m_internal_format = (channels == 4 ? GL_RGBA8 : GL_RGB8);
        m_format = (channels == 4 ? GL_RGBA : GL_RGB);

#if defined(HN_PLATFORM_WINDOWS)
        // New‐style (DSA) API, requires OpenGL ≥4.5
        glCreateTextures(GL_TEXTURE_2D, 1, &m_renderer_id);
        glTextureStorage2D(m_renderer_id, 1, m_internal_format, m_width, m_height);

        // Set texture parameters (DSA calls)
        glTextureParameteri(m_renderer_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(m_renderer_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(m_renderer_id, GL_TEXTURE_WRAP_S,     GL_REPEAT);
        glTextureParameteri(m_renderer_id, GL_TEXTURE_WRAP_T,     GL_REPEAT);

#elif defined(HN_PLATFORM_MACOS)
        // Old‐style bind‐and‐upload API, since macOS max core is GL 4.1
        glGenTextures(1, &m_renderer_id);
        glBindTexture(GL_TEXTURE_2D, m_renderer_id);

        // Set texture parameters (legacy calls)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);

        // Allocate storage with null data first
        glTexImage2D(GL_TEXTURE_2D, 0, m_internal_format, m_width, m_height, 0, m_format, GL_UNSIGNED_BYTE, nullptr);

        // Unbind to avoid accidental modification
        glBindTexture(GL_TEXTURE_2D, 0);

#else
    #error "Unsupported platform for OpenGLTexture2D creation"
#endif

        // Now use set_data to upload the pixel data
        uint32_t data_size = m_width * m_height * channels;
        set_data(data, data_size);

        stbi_image_free(data);
    }

    OpenGLTexture2D::~OpenGLTexture2D()
    {
    	HN_PROFILE_FUNCTION();

#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_MACOS)
        glDeleteTextures(1, &m_renderer_id);
#else
    #error "Unsupported platform for OpenGLTexture2D destruction"
#endif
    }

    void OpenGLTexture2D::set_data(void *data, uint32_t size) {
    	HN_PROFILE_FUNCTION();

        uint32_t bpp = m_format == GL_RGBA ? 4 : 3;
        HN_CORE_ASSERT(size == m_width * m_height * bpp, "Size parameter does not match data buffer size.");
        
#if defined(HN_PLATFORM_WINDOWS)
        glTextureSubImage2D(m_renderer_id, 0, 0, 0, m_width, m_height, m_format, GL_UNSIGNED_BYTE, data);
    
#elif defined(HN_PLATFORM_MACOS)
        // Legacy approach: bind texture, update data, then unbind
        glBindTexture(GL_TEXTURE_2D, m_renderer_id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, m_format, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_2D, 0);
    
#else
    #error "Unsupported platform for OpenGLTexture2D set_data()"
#endif
    }

    void OpenGLTexture2D::bind(uint32_t slot) const
    {
    	HN_PROFILE_FUNCTION();

#if defined(HN_PLATFORM_WINDOWS)
        // New‐style bindless API (requires GL ≥4.5)
        glBindTextureUnit(slot, m_renderer_id);

#elif defined(HN_PLATFORM_MACOS)
        // Legacy binding: activate slot and bind texture
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_renderer_id);

#else
    #error "Unsupported platform for OpenGLTexture2D bind()"
#endif
    }

} // namespace Honey