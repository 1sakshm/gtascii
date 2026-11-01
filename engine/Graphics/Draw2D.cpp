#include "Graphics/Draw2D.h"

#include "Core/Utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace av::draw {

int text(Framebuffer& fb, int x, int y, std::string_view utf8, Color fg, std::optional<Color> bg)
{
    if (y < 0 || y >= fb.height()) {
        return 0;
    }
    int written = 0;
    int cx = x;
    for (const char32_t ch : decodeUtf8(utf8)) {
        if (cx >= fb.width()) {
            break;
        }
        if (cx >= 0) {
            Cell& cell = fb.at(cx, y);
            cell.ch = ch;
            cell.fg = fg;
            if (bg) {
                cell.bg = *bg;
            }
            ++written;
        }
        ++cx;
    }
    return written;
}

void fillRect(Framebuffer& fb, int x, int y, int w, int h, const Cell& cell)
{
    const int x0 = std::max(x, 0);
    const int y0 = std::max(y, 0);
    const int x1 = std::min(x + w, fb.width());
    const int y1 = std::min(y + h, fb.height());
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            fb.at(px, py) = cell;
        }
    }
}

void line(Framebuffer& fb, int x0, int y0, int x1, int y1, const Cell& cell)
{
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        fb.set(x0, y0, cell);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void fillCircle(Framebuffer& fb, float cx, float cy, float radius, float cellAspect, const Cell& cell)
{
    if (radius <= 0.0f || cellAspect <= 0.0f) {
        return;
    }
    const float ry = radius / cellAspect;
    const int yMin = std::max(0, static_cast<int>(std::floor(cy - ry)));
    const int yMax = std::min(fb.height() - 1, static_cast<int>(std::ceil(cy + ry)));
    const int xMin = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int xMax = std::min(fb.width() - 1, static_cast<int>(std::ceil(cx + radius)));
    // Sample at cell centres.
    for (int y = yMin; y <= yMax; ++y) {
        const float ny = (static_cast<float>(y) + 0.5f - cy) / ry;
        for (int x = xMin; x <= xMax; ++x) {
            const float nx = (static_cast<float>(x) + 0.5f - cx) / radius;
            if (nx * nx + ny * ny <= 1.0f) {
                fb.at(x, y) = cell;
            }
        }
    }
}

} // namespace av::draw
