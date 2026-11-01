#pragma once

#include "Graphics/Framebuffer.h"

#include <optional>
#include <string_view>

// Screen-space 2D drawing helpers. All functions clip to the framebuffer.
namespace av::draw {

// Writes UTF-8 text starting at (x, y). If `bg` is empty the existing
// background of each cell is kept. Returns the number of cells written.
int text(Framebuffer& fb, int x, int y, std::string_view utf8, Color fg, std::optional<Color> bg = std::nullopt);

void fillRect(Framebuffer& fb, int x, int y, int w, int h, const Cell& cell);

// Bresenham line, both endpoints inclusive.
void line(Framebuffer& fb, int x0, int y0, int x1, int y1, const Cell& cell);

// Filled ellipse that appears circular on screen: `radius` is in columns and
// the vertical radius is radius / cellAspect (cellAspect = cell height / width).
void fillCircle(Framebuffer& fb, float cx, float cy, float radius, float cellAspect, const Cell& cell);

} // namespace av::draw
