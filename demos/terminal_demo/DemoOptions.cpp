#include "DemoOptions.h"

#include "Graphics/CharRamp.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace demo {

namespace {

bool parseDouble(const char* s, double& out)
{
    char* end = nullptr;
    out = std::strtod(s, &end);
    return end != s && *end == '\0';
}

bool parseInt(const char* s, long long& out)
{
    char* end = nullptr;
    out = std::strtoll(s, &end, 10);
    return end != s && *end == '\0';
}

} // namespace

const char* usageText()
{
    return "GRAND THEFT ASCII - Phase 1 terminal graphics demo\n"
           "\n"
           "Usage: terminal_demo [options]\n"
           "\n"
           "  --fps N           Target frame rate (default 60, 0 = uncapped)\n"
           "  --size WxH        Fixed framebuffer size, clamped to the terminal (default: fit terminal)\n"
           "  --aspect F        Character cell height/width ratio (default 2.0)\n"
           "  --ramp NAME       Character ramp: standard, detailed, blocks, minimal\n"
           "  --pattern N       Starting pattern 1-5 (plasma, tunnel, interference, shapes, test card)\n"
           "  --color-bits N    Colour precision per channel, 1-8 (default 8)\n"
           "  --no-bg           Use the terminal's default background instead of RGB backgrounds\n"
           "  --no-hud          Hide the status bars\n"
           "  --no-sync         Disable synchronized output (DEC mode 2026)\n"
           "  --auto [SECONDS]  Cycle patterns automatically (default every 8 s)\n"
           "  --frames N        Exit after N frames and print statistics\n"
           "  --help            Show this help\n";
}

ParseResult parseOptions(int argc, char** argv)
{
    ParseResult result;
    DemoOptions& o = result.options;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const char* next = i + 1 < argc ? argv[i + 1] : nullptr;
        auto fail = [&](const std::string& msg) {
            result.error = msg;
            return result;
        };
        auto requireValue = [&]() -> bool { return next != nullptr; };

        if (arg == "--help" || arg == "-h") {
            result.showHelp = true;
        } else if (arg == "--fps") {
            double v;
            if (!requireValue() || !parseDouble(next, v) || v < 0.0) {
                return fail("--fps needs a non-negative number");
            }
            o.fps = v;
            ++i;
        } else if (arg == "--size") {
            int w = 0, h = 0;
            if (!requireValue() || std::sscanf(next, "%dx%d", &w, &h) != 2 || w < 1 || h < 1) {
                return fail("--size needs WIDTHxHEIGHT, e.g. 100x30");
            }
            o.fixedWidth = w;
            o.fixedHeight = h;
            ++i;
        } else if (arg == "--aspect") {
            double v;
            if (!requireValue() || !parseDouble(next, v) || v <= 0.1 || v > 10.0) {
                return fail("--aspect needs a number between 0.1 and 10");
            }
            o.cellAspect = static_cast<float>(v);
            ++i;
        } else if (arg == "--ramp") {
            if (!requireValue() || av::findRamp(next) == nullptr) {
                return fail("--ramp must be one of: standard, detailed, blocks, minimal");
            }
            o.ramp = next;
            ++i;
        } else if (arg == "--pattern") {
            long long v;
            if (!requireValue() || !parseInt(next, v) || v < 1 || v > 5) {
                return fail("--pattern needs a number from 1 to 5");
            }
            o.pattern = static_cast<int>(v - 1);
            ++i;
        } else if (arg == "--color-bits") {
            long long v;
            if (!requireValue() || !parseInt(next, v) || v < 1 || v > 8) {
                return fail("--color-bits needs a number from 1 to 8");
            }
            o.colorBits = static_cast<int>(v);
            ++i;
        } else if (arg == "--no-bg") {
            o.background = false;
        } else if (arg == "--no-hud") {
            o.hud = false;
        } else if (arg == "--no-sync") {
            o.synchronizedOutput = false;
        } else if (arg == "--auto") {
            o.autoCycle = true;
            double v;
            if (next != nullptr && parseDouble(next, v)) {
                if (v <= 0.0) {
                    return fail("--auto interval must be positive");
                }
                o.cycleSeconds = v;
                ++i;
            }
        } else if (arg == "--frames") {
            long long v;
            if (!requireValue() || !parseInt(next, v) || v < 1) {
                return fail("--frames needs a positive number");
            }
            o.maxFrames = static_cast<std::uint64_t>(v);
            ++i;
        } else {
            return fail("unknown option: " + std::string(arg));
        }
    }
    return result;
}

} // namespace demo
