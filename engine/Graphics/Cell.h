#pragma once

#include "Graphics/Color.h"

#include <cstdint>
#include <limits>

namespace av {

using MaterialId = std::uint16_t;
inline constexpr MaterialId kNoMaterial = 0;
inline constexpr float kFarDepth = std::numeric_limits<float>::infinity();

// One terminal character cell.
//
// `ch`, `fg` and `bg` are what the terminal displays. `depth` and `material`
// are renderer-side data (z-buffer and material lookup) and never reach the
// terminal, so they do not take part in change detection.
struct Cell {
    char32_t ch = U' ';
    Color fg{255, 255, 255};
    Color bg{0, 0, 0};
    float depth = kFarDepth;
    MaterialId material = kNoMaterial;

    constexpr bool sameAppearance(const Cell& o) const
    {
        return ch == o.ch && fg == o.fg && bg == o.bg;
    }
};

} // namespace av
