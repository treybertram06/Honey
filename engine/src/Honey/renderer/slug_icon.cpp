#include "hnpch.h"

#include "slug_icon.h"
#include "Honey/core/log.h"

#include "nanosvg.h"

#include <algorithm>
#include <cmath>

namespace Honey {

    namespace {

        // Evaluate a cubic Bezier at parameter t using de Casteljau.
        static glm::vec2 cubic_eval(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, float t) {
            float u = 1.0f - t;
            glm::vec2 q0 = u * p0 + t * p1;
            glm::vec2 q1 = u * p1 + t * p2;
            glm::vec2 q2 = u * p2 + t * p3;
            glm::vec2 r0 = u * q0 + t * q1;
            glm::vec2 r1 = u * q1 + t * q2;
            return u * r0 + t * r1;
        }

        // Split a cubic at parameter t, returning the two halves via de Casteljau.
        static void cubic_split(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, float t,
                                 glm::vec2 out_left[4], glm::vec2 out_right[4]) {
            float u = 1.0f - t;
            glm::vec2 q0 = u * p0 + t * p1;
            glm::vec2 q1 = u * p1 + t * p2;
            glm::vec2 q2 = u * p2 + t * p3;
            glm::vec2 r0 = u * q0 + t * q1;
            glm::vec2 r1 = u * q1 + t * q2;
            glm::vec2 s  = u * r0 + t * r1;

            out_left[0]  = p0;  out_left[1]  = q0;  out_left[2]  = r0;  out_left[3]  = s;
            out_right[0] = s;   out_right[1] = r1;  out_right[2] = q2;  out_right[3] = p3;
        }

        // Best-fit degree reduction of a cubic to a quadratic.
        // Only accurate for monotone cubics — always split at inflection points first.
        static QuadBezier degree_reduce(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3) {
            // Standard least-squares formula: P1_quad = (3*(P1+P2) - (P0+P3)) / 4
            glm::vec2 mid = (3.0f * (p1 + p2) - (p0 + p3)) * 0.25f;
            return { p0, mid, p3 };
        }

        // Subdivide a cubic into 2 quads by splitting at t=0.5 then degree-reducing each half.
        static void cubic_to_2_quads(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                                      std::vector<QuadBezier>& out) {
            glm::vec2 left[4], right[4];
            cubic_split(p0, p1, p2, p3, 0.5f, left, right);
            out.push_back(degree_reduce(left[0],  left[1],  left[2],  left[3]));
            out.push_back(degree_reduce(right[0], right[1], right[2], right[3]));
        }

        // Cross product of 2D vectors (scalar z component).
        static float cross2(glm::vec2 a, glm::vec2 b) {
            return a.x * b.y - a.y * b.x;
        }

        // Unpack a nanosvg ABGR color to a normalized vec4 (r,g,b,a) and multiply by opacity.
        static glm::vec4 unpack_nsvg_color(unsigned int abgr, float opacity) {
            float r = static_cast<float>((abgr)       & 0xFF) / 255.0f;
            float g = static_cast<float>((abgr >>  8) & 0xFF) / 255.0f;
            float b = static_cast<float>((abgr >> 16) & 0xFF) / 255.0f;
            float a = static_cast<float>((abgr >> 24) & 0xFF) / 255.0f;
            a *= opacity;
            return { r, g, b, a };
        }

    } // anonymous namespace

    std::vector<QuadBezier> SlugIcon::cubic_to_quads(glm::vec2 p0, glm::vec2 p1,
                                                       glm::vec2 p2, glm::vec2 p3) {
        std::vector<QuadBezier> out;
        out.reserve(6);

        // Compute inflection point t-values.
        // Using the standard formula for cubic Bezier inflection points.
        glm::vec2 a = p1 - p0;
        glm::vec2 b = p2 - p1 - a;
        glm::vec2 c = p3 - p2 - (p2 - p1);

        float denom = 2.0f * cross2(a, b);

        // Collect valid inflection t-values in (0, 1)
        float inflections[2];
        int   num_inflections = 0;

        constexpr float kEps = 1e-6f;
        if (std::abs(denom) > kEps) {
            // t1: from the cross product condition
            float t1_num = cross2(a, c);
            float t1 = t1_num / denom;
            if (t1 > kEps && t1 < 1.0f - kEps)
                inflections[num_inflections++] = t1;

            // t2: companion root
            float disc = t1_num * t1_num - cross2(b, c) * denom;
            if (disc >= 0.0f) {
                float t2 = (t1_num + std::sqrt(disc)) / denom;
                if (t2 > kEps && t2 < 1.0f - kEps && (num_inflections == 0 || std::abs(t2 - inflections[0]) > kEps))
                    inflections[num_inflections++] = t2;
            }
        }

        // Sort inflection points
        if (num_inflections == 2 && inflections[0] > inflections[1])
            std::swap(inflections[0], inflections[1]);

        // Split the cubic at inflection points and convert each monotone segment to 2 quads
        if (num_inflections == 0) {
            cubic_to_2_quads(p0, p1, p2, p3, out);
        } else if (num_inflections == 1) {
            glm::vec2 left[4], right[4];
            cubic_split(p0, p1, p2, p3, inflections[0], left, right);
            cubic_to_2_quads(left[0],  left[1],  left[2],  left[3],  out);
            cubic_to_2_quads(right[0], right[1], right[2], right[3], out);
        } else {
            // Two inflections: split at first, then split the right half at the remapped second
            glm::vec2 left1[4], right1[4];
            cubic_split(p0, p1, p2, p3, inflections[0], left1, right1);

            float t2_remapped = (inflections[1] - inflections[0]) / (1.0f - inflections[0]);
            t2_remapped = std::max(kEps, std::min(1.0f - kEps, t2_remapped));

            glm::vec2 left2[4], right2[4];
            cubic_split(right1[0], right1[1], right1[2], right1[3], t2_remapped, left2, right2);

            cubic_to_2_quads(left1[0],  left1[1],  left1[2],  left1[3],  out);
            cubic_to_2_quads(left2[0],  left2[1],  left2[2],  left2[3],  out);
            cubic_to_2_quads(right2[0], right2[1], right2[2], right2[3], out);
        }

        return out;
    }

    void SlugIcon::build_shape(NSVGshape* shape) {
        std::vector<QuadBezier> shape_curves;

        for (NSVGpath* path = shape->paths; path != nullptr; path = path->next) {
            // nsvgpath pts: x0,y0, [cpx1,cpy1, cpx2,cpy2, x1,y1], ...
            // Each segment is 3 cubics (6 floats) starting after the initial moveto point.
            // npts is the total number of points (not floats). Segments: (npts-1)/3.
            int n_segs = (path->npts - 1) / 3;
            for (int i = 0; i < n_segs; ++i) {
                const float* p = &path->pts[i * 6];
                glm::vec2 cp0 = { p[0], p[1] };
                glm::vec2 cp1 = { p[2], p[3] };
                glm::vec2 cp2 = { p[4], p[5] };
                glm::vec2 cp3 = { p[6], p[7] };

                auto quads = cubic_to_quads(cp0, cp1, cp2, cp3);
                shape_curves.insert(shape_curves.end(), quads.begin(), quads.end());
            }
        }

        ShapeData sd;
        sd.bbox_min          = { shape->bounds[0], shape->bounds[1] };
        sd.bbox_max          = { shape->bounds[2], shape->bounds[3] };
        sd.fill_color        = unpack_nsvg_color(shape->fill.color, shape->opacity);
        sd.num_bands         = NUM_BANDS;
        sd.band_table_offset = static_cast<uint32_t>(m_band_table.size());

        float height = sd.bbox_max.y - sd.bbox_min.y;
        if (height <= 0.0f) height = 1.0f;  // degenerate guard

        for (int b = 0; b < NUM_BANDS; ++b) {
            float band_y0 = sd.bbox_min.y + (static_cast<float>(b)     / static_cast<float>(NUM_BANDS)) * height;
            float band_y1 = sd.bbox_min.y + (static_cast<float>(b + 1) / static_cast<float>(NUM_BANDS)) * height;

            BandEntry entry;
            entry.curve_offset = static_cast<uint32_t>(m_curves.size());
            entry.curve_count  = 0;

            for (const auto& c : shape_curves) {
                float cy_min = std::min({ c.p0.y, c.p1.y, c.p2.y });
                float cy_max = std::max({ c.p0.y, c.p1.y, c.p2.y });
                if (cy_max >= band_y0 && cy_min <= band_y1) {
                    m_curves.push_back(c);
                    ++entry.curve_count;
                }
            }
            m_band_table.push_back(entry);
        }

        m_shapes.push_back(sd);
    }

    SlugIcon::SlugIcon(const std::filesystem::path& path) {
        NSVGimage* img = nsvgParseFromFile(path.string().c_str(), "px", 96.0f);
        if (!img) {
            HN_CORE_ERROR("SlugIcon: failed to parse SVG: {}", path.string());
            return;
        }

        m_width  = img->width;
        m_height = img->height;

        for (NSVGshape* shape = img->shapes; shape != nullptr; shape = shape->next) {
            if (!(shape->flags & NSVG_FLAGS_VISIBLE))              continue;
            if (shape->fill.type != NSVG_PAINT_COLOR)              continue;  // skip gradients

            build_shape(shape);
        }

        nsvgDelete(img);

        if (m_shapes.empty())
            HN_CORE_WARN("SlugIcon: no solid-fill shapes found in SVG: {}", path.string());
    }

} // namespace Honey
