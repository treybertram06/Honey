#pragma once

#include "Honey/renderer/texture.h"
#include <glad/glad.h>

namespace Honey {

    class OpenGLTexture2D : public Texture2D {
    public:
        OpenGLTexture2D(uint32_t width, uint32_t height);
        OpenGLTexture2D(const std::string& path);
        virtual ~OpenGLTexture2D();
        static void create_async(const std::string& path, const Ref<Texture2D::AsyncHandle>& handle);

        virtual uint32_t get_width() const override { return m_width; }
        virtual uint32_t get_height() const override { return m_height; }
        virtual uint32_t get_renderer_id() const override { return m_renderer_id; }

        virtual void set_data(void *data, uint32_t size) override;

        virtual void bind(uint32_t slot = 0) const override;

        virtual bool operator==(const Texture& other) const override {
            const OpenGLTexture2D* other_gl = dynamic_cast<const OpenGLTexture2D*>(&other);
            if (!other_gl) {
                return false;  // Different types, not equal
            }
            return m_renderer_id == other_gl->m_renderer_id;
        }

        ImTextureID get_imgui_texture_id() override {
            // ImTextureID is ImU64; GL id is 32-bit. Widen it.
            return static_cast<ImTextureID>(m_renderer_id);
        }

        void refresh_sampler() override;

    private:
        std::string m_path;
        uint32_t m_width, m_height;
        uint32_t m_renderer_id;
        GLenum m_internal_format, m_format;
    };
}