#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>
#include <unordered_map>

#include "glm/vec2.hpp"
#include <stb_truetype.h>

namespace Honey {

    struct QuadBezier {
        glm::vec2 p0, p1, p2;
    };

    // One entry per band per glyph — indexes into the flat curve buffer.
    struct BandEntry {
        uint32_t curve_offset = 0;
        uint32_t curve_count  = 0;
    };

    // Per-glyph metadata stored in font units (scale at draw time via get_scale()).
    struct GlyphData {
        glm::vec2 bbox_min          = {};
        glm::vec2 bbox_max          = {};
        uint32_t  band_table_offset = 0;  // index into SlugFont::get_band_table()
        uint32_t  num_bands         = 0;
        float     advance           = 0.0f;
        float     left_bearing      = 0.0f;
    };

    class SlugFont {
    public:
        static constexpr int NUM_BANDS       = 8;
        static constexpr int FIRST_CODEPOINT = 32;   // space
        static constexpr int LAST_CODEPOINT  = 126;  // ~

        SlugFont() = default;
        explicit SlugFont(const std::filesystem::path& path);

        bool is_valid() const { return !m_font_data.empty(); }

        // Returns nullptr if the codepoint was not pre-built.
        const GlyphData* get_glyph(int codepoint) const;

        // Flat CPU-side buffers — upload these to SSBOs once after construction.
        const std::vector<QuadBezier>& get_curves()     const { return m_curves; }
        const std::vector<BandEntry>&  get_band_table() const { return m_band_table; }

        // Convert from unscaled font units to pixels / world units.
        float get_scale(float font_size_px) const;

        // Font-level vertical metrics (unscaled — multiply by get_scale()).
        float get_ascent()   const { return m_ascent; }
        float get_descent()  const { return m_descent; }
        float get_line_gap() const { return m_line_gap; }

    private:
        std::vector<QuadBezier> extract_glyph_curves(const stbtt_vertex* vertices, int num_vertices);
        void build_glyph(int codepoint);

        std::vector<char>  m_font_data;
        stbtt_fontinfo     m_font_info = {};

        // Flat buffers uploaded to the GPU as SSBOs.
        std::vector<QuadBezier>            m_curves;
        std::vector<BandEntry>             m_band_table;
        std::unordered_map<int, GlyphData> m_glyph_map;

        // Unscaled vertical metrics.
        float m_ascent   = 0.0f;
        float m_descent  = 0.0f;
        float m_line_gap = 0.0f;
    };

}
