#include "Graphics/Framebuffer.h"

#include <algorithm>

namespace av {

Framebuffer::Framebuffer(int width, int height, const Cell& fill)
{
    resize(width, height, fill);
}

void Framebuffer::resize(int width, int height, const Cell& fill)
{
    width_ = std::max(width, 0);
    height_ = std::max(height, 0);
    if (width_ == 0 || height_ == 0) {
        width_ = height_ = 0;
    }
    cells_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), fill);
}

void Framebuffer::set(int x, int y, const Cell& cell)
{
    if (inBounds(x, y)) {
        cells_[index(x, y)] = cell;
    }
}

bool Framebuffer::setDepthTested(int x, int y, const Cell& cell)
{
    if (!inBounds(x, y)) {
        return false;
    }
    Cell& target = cells_[index(x, y)];
    if (cell.depth < target.depth) {
        target = cell;
        return true;
    }
    return false;
}

void Framebuffer::clear(const Cell& fill)
{
    std::fill(cells_.begin(), cells_.end(), fill);
}

void Framebuffer::resetDepth(float depth)
{
    for (Cell& c : cells_) {
        c.depth = depth;
    }
}

} // namespace av
