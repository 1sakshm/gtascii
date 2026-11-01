#pragma once

#include <cstdint>
#include <string>

namespace demo {

struct DemoOptions {
    double fps = 60.0;            // 0 = uncapped
    int fixedWidth = 0;           // 0 = follow terminal size
    int fixedHeight = 0;
    float cellAspect = 2.0f;      // terminal cell height / width
    std::string ramp = "standard";
    int pattern = 0;              // zero-based
    std::uint64_t maxFrames = 0;  // 0 = run until quit
    int colorBits = 8;
    bool background = true;
    bool hud = true;
    bool autoCycle = false;
    double cycleSeconds = 8.0;
    bool synchronizedOutput = true;
};

struct ParseResult {
    DemoOptions options;
    bool showHelp = false;
    std::string error; // non-empty on failure
};

ParseResult parseOptions(int argc, char** argv);
const char* usageText();

} // namespace demo
