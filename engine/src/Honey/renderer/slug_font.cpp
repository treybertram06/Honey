#include "hnpch.h"

#include <stb_truetype.h>
#include "slug_font.h"

namespace Honey {

    namespace {
        static std::vector<char> read_file_bytes(const std::string& file_path) {
            // Open the file in binary mode and at the end to get size
            std::ifstream file(file_path, std::ios::binary | std::ios::ate);

            if (!file.is_open()) return {};

            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);

            std::vector<char> buffer(size);
            if (file.read(buffer.data(), size)) {
                return buffer;
            }

            return {};
        }
    }

    SlugFont::SlugFont(const std::filesystem::path& path) {
        m_font_data = read_file_bytes(path.string());
        if (m_font_data.empty()) {
            HN_CORE_ERROR("SlugFont: failed to read font file: {}", path.string());
            return;
        }

        if (!stbtt_InitFont(&m_font_info, reinterpret_cast<const unsigned char*>(m_font_data.data()), 0)) {
            HN_CORE_ERROR("SlugFont: stbtt_InitFont failed for: {}", path.string());
            m_font_data.clear();
            return;
        }

        int ascent, descent, line_gap;
        stbtt_GetFontVMetrics(&m_font_info, &ascent, &descent, &line_gap);
        m_ascent   = static_cast<float>(ascent);
        m_descent  = static_cast<float>(descent);
        m_line_gap = static_cast<float>(line_gap);

        for (int cp = FIRST_CODEPOINT; cp <= LAST_CODEPOINT; ++cp)
            build_glyph(cp);
    }

    float SlugFont::get_scale(float font_size_px) const {
        return stbtt_ScaleForPixelHeight(&m_font_info, font_size_px);
    }

    const GlyphData* SlugFont::get_glyph(int codepoint) const {
        auto it = m_glyph_map.find(codepoint);
        return (it != m_glyph_map.end()) ? &it->second : nullptr;
    }

    void SlugFont::build_glyph(int codepoint) {
        stbtt_vertex* verts    = nullptr;
        int           num_verts = stbtt_GetCodepointShape(&m_font_info, codepoint, &verts);

        std::vector<QuadBezier> glyph_curves = extract_glyph_curves(verts, num_verts);
        stbtt_FreeShape(&m_font_info, verts);

        int x0, y0, x1, y1;
        stbtt_GetCodepointBox(&m_font_info, codepoint, &x0, &y0, &x1, &y1);

        int adv_width, left_bearing;
        stbtt_GetCodepointHMetrics(&m_font_info, codepoint, &adv_width, &left_bearing);

        GlyphData gd;
        gd.bbox_min          = { static_cast<float>(x0), static_cast<float>(y0) };
        gd.bbox_max          = { static_cast<float>(x1), static_cast<float>(y1) };
        gd.advance           = static_cast<float>(adv_width);
        gd.left_bearing      = static_cast<float>(left_bearing);
        gd.num_bands         = NUM_BANDS;
        gd.band_table_offset = static_cast<uint32_t>(m_band_table.size());

        float height = static_cast<float>(y1 - y0);

        for (int b = 0; b < NUM_BANDS; ++b) {
            float band_y0 = static_cast<float>(y0) + (static_cast<float>(b)       / static_cast<float>(NUM_BANDS)) * height;
            float band_y1 = static_cast<float>(y0) + (static_cast<float>(b + 1)   / static_cast<float>(NUM_BANDS)) * height;

            BandEntry entry;
            entry.curve_offset = static_cast<uint32_t>(m_curves.size());
            entry.curve_count  = 0;

            for (const auto& c : glyph_curves) {
                float cy_min = std::min({c.p0.y, c.p1.y, c.p2.y});
                float cy_max = std::max({c.p0.y, c.p1.y, c.p2.y});
                if (cy_max >= band_y0 && cy_min <= band_y1) {
                    m_curves.push_back(c);
                    ++entry.curve_count;
                }
            }
            m_band_table.push_back(entry);
        }

        m_glyph_map[codepoint] = gd;
    }

    std::vector<QuadBezier> SlugFont::extract_glyph_curves(const stbtt_vertex* vertices, int num_vertices) {
        std::vector<QuadBezier> curves;
        glm::vec2 current = { 0.0f, 0.0f };

        for (int i = 0; i < num_vertices; ++i) {
            const auto& v = vertices[i];

            if (v.type == STBTT_vmove) {
                current = { static_cast<float>(v.x), static_cast<float>(v.y) };
            } else if (v.type == STBTT_vline) {
                glm::vec2 end = { static_cast<float>(v.x), static_cast<float>(v.y) };
                curves.push_back({ current, (current + end) * 0.5f, end });
                current = end;
            } else if (v.type == STBTT_vcurve) {
                glm::vec2 ctrl = { static_cast<float>(v.cx), static_cast<float>(v.cy) };
                glm::vec2 end  = { static_cast<float>(v.x),  static_cast<float>(v.y)  };
                curves.push_back({ current, ctrl, end });
                current = end;
            }
        }

        return curves;
    }
}
