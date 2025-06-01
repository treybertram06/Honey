#include "hnpch.h"
#include "opengl_texture.h"

#include "stb_image.h"

#include <glad/glad.h>

namespace Honey {

    OpenGLTexture2D::OpenGLTexture2D(const std::string& path)
        : m_path(path)
    {
        int width, height, channels;
        stbi_set_flip_vertically_on_load(true);
        stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 0);
        if (data == nullptr) {
            HN_CORE_ERROR("At path: {0}", path);
            HN_CORE_ASSERT(false, "Failed to load image.");
        }

        m_width  = width;
        m_height = height;

#if defined(HN_PLATFORM_WINDOWS)
        // New‐style (DSA) API, requires OpenGL ≥4.5
        glCreateTextures(GL_TEXTURE_2D, 1, &m_renderer_id);
        // Choose internal format based on channel count (only RGB and RGBA shown)
        GLenum internalFormat = (channels == 4 ? GL_RGBA8 : GL_RGB8);
        GLenum dataFormat     = (channels == 4 ? GL_RGBA  : GL_RGB);

        glTextureStorage2D(m_renderer_id, 1, internalFormat, m_width, m_height);

        // Set texture parameters (DSA calls)
        glTextureParameteri(m_renderer_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(m_renderer_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(m_renderer_id, GL_TEXTURE_WRAP_S,     GL_REPEAT);
        glTextureParameteri(m_renderer_id, GL_TEXTURE_WRAP_T,     GL_REPEAT);

        // Upload the pixel data
        glTextureSubImage2D(
            m_renderer_id,
            0,                  // mip level
            0, 0,               // xoffset, yoffset
            m_width, m_height,
            dataFormat,
            GL_UNSIGNED_BYTE,
            data
        );

#elif defined(HN_PLATFORM_MACOS)
        // Old‐style bind‐and‐upload API, since macOS max core is GL 4.1
        glGenTextures(1, &m_renderer_id);
        glBindTexture(GL_TEXTURE_2D, m_renderer_id);

        // Choose internal/format based on channels
        GLenum internalFormat = (channels == 4 ? GL_RGBA8 : GL_RGB8);
        GLenum dataFormat     = (channels == 4 ? GL_RGBA  : GL_RGB);

        // Set texture parameters (legacy calls)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);

        // Allocate and upload
        glTexImage2D(
            GL_TEXTURE_2D,
            0,               // mip level
            internalFormat,  // internal format
            m_width, m_height,
            0,               // border = 0
            dataFormat,      // format
            GL_UNSIGNED_BYTE,
            data
        );

        // Unbind to avoid accidental modification
        glBindTexture(GL_TEXTURE_2D, 0);

#else
    #error "Unsupported platform for OpenGLTexture2D creation"
#endif

        stbi_image_free(data);
    }

    OpenGLTexture2D::~OpenGLTexture2D()
    {
#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_MACOS)
        glDeleteTextures(1, &m_renderer_id);
#else
    #error "Unsupported platform for OpenGLTexture2D destruction"
#endif
    }

    void OpenGLTexture2D::bind(uint32_t slot) const
    {
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
