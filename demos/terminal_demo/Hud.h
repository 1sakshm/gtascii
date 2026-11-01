#pragma once

#include "Graphics/Framebuffer.h"

#include <cstddef>
#include <string_view>

namespace demo {

struct HudInfo {
    std::string_view patternName;
    int patternIndex = 0;
    int patternCount = 0;
    double fps = 0.0;
    double targetFps = 0.0;
    double workMs = 0.0;
    std::size_t cellsChanged = 0;
    std::size_t bytes = 0;
    std::string_view rampName;
    int colorBits = 8;
    bool background = true;
    bool autoCycle = false;
    bool trueColor = false;
};

// Draws a status bar on the top row and a key-help bar on the bottom row.
void drawHud(av::Framebuffer& fb, const HudInfo& info);

} // namespace demo
