#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "glm/vec2.hpp"
#include "glm/vec4.hpp"
#include "slug_font.h"  // reuse QuadBezier and BandEntry

struct NSVGshape;

namespace Honey {

    // Per-shape metadata for a single filled SVG shape.
    struct ShapeData {
        glm::vec2 bbox_min          = {};
        glm::vec2 bbox_max          = {};
        uint32_t  band_table_offset = 0;  // index into SlugIcon::get_band_table()
        uint32_t  num_bands         = 0;
        glm::vec4 fill_color        = { 1.0f, 1.0f, 1.0f, 1.0f };
    };

    class SlugIcon {
    public:
        static constexpr int NUM_BANDS = 8;

        SlugIcon() = default;
        explicit SlugIcon(const std::filesystem::path& path);

        bool is_valid() const { return !m_shapes.empty(); }

        // Canvas size in SVG px units.
        float get_width()  const { return m_width; }
        float get_height() const { return m_height; }

        // Flat CPU-side buffers — upload these to the icon region of the shared SSBOs once.
        const std::vector<ShapeData>&  get_shapes()     const { return m_shapes; }
        const std::vector<QuadBezier>& get_curves()     const { return m_curves; }
        const std::vector<BandEntry>&  get_band_table() const { return m_band_table; }

    private:
        void build_shape(NSVGshape* shape);
        // Converts a single cubic Bezier to a sequence of quadratic Beziers.
        // Splits at inflection points, then subdivides each monotone segment at t=0.5,
        // and degree-reduces each half. Returns 2–6 QuadBezier per cubic input.
        std::vector<QuadBezier> cubic_to_quads(glm::vec2 p0, glm::vec2 p1,
                                                glm::vec2 p2, glm::vec2 p3);

        float m_width  = 0.0f;
        float m_height = 0.0f;

        std::vector<ShapeData>  m_shapes;
        std::vector<QuadBezier> m_curves;
        std::vector<BandEntry>  m_band_table;
    };

}
