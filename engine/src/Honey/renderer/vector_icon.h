#pragma once
#include <filesystem>

#include "slug_icon.h"
#include "Honey/core/base.h"

namespace Honey {

    class VectorIcon {
    public:
        VectorIcon() = default;
        static Ref<VectorIcon> create(const std::filesystem::path& path);
        ~VectorIcon();

        float get_width() const { return m_width; }
        float get_height() const { return m_height; }
        const std::vector<ShapeData>& get_shapes() const { return m_shapes; }

        struct IconSlot { uint32_t band_offset, band_count, curve_offset, curve_count; };
        IconSlot get_slot() const {
            return { m_band_slot_offset,
                m_band_slot_count,
                m_curve_slot_offset,
                m_curve_slot_count };
        }

    private:
        float m_width = 0, m_height = 0;
        std::vector<ShapeData> m_shapes;

        uint32_t m_band_slot_offset = 0, m_band_slot_count = 0;
        uint32_t m_curve_slot_offset = 0, m_curve_slot_count = 0;
    };

}
