#pragma once

#include "../core/core.h"
#include <string>

namespace Honey {

    class Texture {
    public:
        virtual ~Texture() = default;

        virtual std::uint32_t get_width() const = 0;
        virtual std::uint32_t get_height() const = 0;
        virtual std::uint32_t get_renderer_id() const = 0;

        virtual void set_data(void* data, std::uint32_t size) = 0;

        virtual void bind(std::uint32_t slot = 0) const = 0;

        virtual bool operator==(const Texture& other) const = 0;

    };

    class Texture2D : public Texture {
    public:
        static Ref<Texture2D> create(std::uint32_t width, std::uint32_t height);
        static Ref<Texture2D> create(const std::string& path);

    };

}