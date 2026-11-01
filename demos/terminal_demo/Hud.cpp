#include "Hud.h"

#include "Graphics/Draw2D.h"

#include <cstdio>

namespace demo {

void drawHud(av::Framebuffer& fb, const HudInfo& info)
{
    if (fb.height() < 2) {
        return;
    }
    const av::Color barBg{10, 10, 20};
    const av::Color accent{255, 80, 180};
    const av::Color text{220, 220, 235};
    const av::Color dim{140, 140, 170};

    av::Cell bar;
    bar.ch = U' ';
    bar.bg = barBg;
    av::draw::fillRect(fb, 0, 0, fb.width(), 1, bar);
    av::draw::fillRect(fb, 0, fb.height() - 1, fb.width(), 1, bar);

    int x = 1;
    x += av::draw::text(fb, x, 0, "GRAND THEFT ASCII", accent, barBg);
    x += av::draw::text(fb, x, 0, " | Phase 1 ", dim, barBg);

    char target[16];
    if (info.targetFps > 0.0) {
        std::snprintf(target, sizeof target, "%.0f", info.targetFps);
    } else {
        std::snprintf(target, sizeof target, "max");
    }

    char status[256];
    std::snprintf(status, sizeof status,
                  "| [%d/%d] %.*s%s | %dx%d | %5.1f/%s fps | %4.1f ms | %5zu cells | %6.1f KB | %.*s | %d-bit%s | %s",
                  info.patternIndex + 1, info.patternCount,
                  static_cast<int>(info.patternName.size()), info.patternName.data(),
                  info.autoCycle ? " (auto)" : "",
                  fb.width(), fb.height(), info.fps, target, info.workMs, info.cellsChanged,
                  static_cast<double>(info.bytes) / 1024.0,
                  static_cast<int>(info.rampName.size()), info.rampName.data(), info.colorBits,
                  info.background ? "" : " no-bg", info.trueColor ? "truecolor" : "truecolor?");
    av::draw::text(fb, x, 0, status, text, barBg);

    av::draw::text(fb, 1, fb.height() - 1,
                   "1-5 pattern  N/P next/prev  A auto  R ramp  C colour bits  B background  +/- fps  H hud  Q/Esc quit",
                   dim, barBg);
}

} // namespace demo
