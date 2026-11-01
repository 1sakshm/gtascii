#pragma once

#include "Graphics/Cell.h"

#include <cassert>
#include <vector>

namespace av {

// A width x height grid of cells, stored row-major. Rendering writes here;
// AnsiPresenter turns it into terminal output. The framebuffer itself knows
// nothing about terminals.
class Framebuffer {
public:
    Framebuffer() = default;
    Framebuffer(int width, int height, const Cell& fill = Cell{});

    // Changes dimensions; all cells are reset to `fill`. Negative sizes are treated as 0.
    void resize(int width, int height, const Cell& fill = Cell{});

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    bool empty() const noexcept { return cells_.empty(); }

    bool inBounds(int x, int y) const noexcept
    {
        return x >= 0 && y >= 0 && x < width_ && y < height_;
    }

    // Unchecked access (asserts in debug builds).
    Cell& at(int x, int y)
    {
        assert(inBounds(x, y));
        return cells_[index(x, y)];
    }
    const Cell& at(int x, int y) const
    {
        assert(inBounds(x, y));
        return cells_[index(x, y)];
    }

    // Bounds-checked writes; out-of-range coordinates are ignored.
    void set(int x, int y, const Cell& cell);

    // Writes `cell` only if it is closer than what is already stored
    // (cell.depth < existing depth). Returns true if written.
    bool setDepthTested(int x, int y, const Cell& cell);

    void clear(const Cell& fill = Cell{});
    void resetDepth(float depth = kFarDepth);

    const std::vector<Cell>& cells() const noexcept { return cells_; }

private:
    std::size_t index(int x, int y) const noexcept
    {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
    }

    int width_ = 0;
    int height_ = 0;
    std::vector<Cell> cells_;
};

} // namespace av
