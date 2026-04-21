#pragma once
#include <cstdint>

namespace Honey {
    static constexpr uint32_t k_max_point_lights = 32;
    static constexpr uint32_t k_tile_size        = 16;
    // At 16×16 tiles, a 3840×2160 viewport needs 240×135 = 32,400 tiles.
    static constexpr uint32_t k_max_tile_count_x = 240;
    static constexpr uint32_t k_max_tile_count_y = 135;
    static constexpr uint32_t k_max_tiles        = k_max_tile_count_x * k_max_tile_count_y;
}