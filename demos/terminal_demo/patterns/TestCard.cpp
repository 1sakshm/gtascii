// Calibration screen: hue/value field, greyscale and RGB ramps, and the
// active character ramp. Smooth gradients confirm 24-bit colour is working;
// visible banding means the terminal (or --color-bits) is quantising.
#include "Pattern.h"
#include "PatternUtil.h"

#include "Core/Utf8.h"
#include "Graphics/Draw2D.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace demo {
namespace {

class TestCard final : public Pattern {
public:
    const char* name() const override { return "Test card"; }

    void render(av::Framebuffer& fb, const PatternContext& ctx) override
    {
        const int width = fb.width();
        const int height = fb.height();
        if (width == 0 || height == 0) {
            return;
        }
        const float t = static_cast<float>(ctx.time);
        const av::Color label{230, 230, 230};
        const av::Color labelBg{0, 0, 0};

        // Layout (top to bottom). Rows 0 and height-1 are left for the HUD;
        // the hue field takes whatever the 13 fixed rows below it leave.
        int y = 1;
        const int fieldRows = std::max(3, height - 2 - 1 - 13);

        // Hue (x) by value (y) field, with a sweeping marker column.
        av::draw::text(fb, 1, y++, "Hue x Value (24-bit background colours)", label, labelBg);
        const int marker = static_cast<int>((0.5f + 0.5f * std::sin(t * 0.7f)) * static_cast<float>(width - 1));
        for (int r = 0; r < fieldRows && y < height - 1; ++r, ++y) {
            const float value = 1.0f - static_cast<float>(r) / static_cast<float>(std::max(1, fieldRows - 1)) * 0.85f;
            for (int x = 0; x < width; ++x) {
                const float hue = 360.0f * static_cast<float>(x) / static_cast<float>(std::max(1, width - 1));
                av::Cell& c = fb.at(x, y);
                c.bg = av::Color::fromHsv(hue, 1.0f, value);
                c.ch = U' ';
                if (x == marker) {
                    c.ch = U'|';
                    c.fg = {255, 255, 255};
                }
            }
        }

        auto horizontalRamp = [&](const char* title, auto colorAt) {
            if (y >= height - 2) {
                return;
            }
            av::draw::text(fb, 1, y++, title, label, labelBg);
            for (int x = 0; x < width; ++x) {
                const float k = static_cast<float>(x) / static_cast<float>(std::max(1, width - 1));
                av::Cell& c = fb.at(x, y);
                c.ch = U' ';
                c.bg = colorAt(k);
            }
            ++y;
        };

        horizontalRamp("Greyscale", [](float k) { const auto v = av::Color::toByte(k); return av::Color{v, v, v}; });
        horizontalRamp("Red", [](float k) { return av::Color{av::Color::toByte(k), 0, 0}; });
        horizontalRamp("Green", [](float k) { return av::Color{0, av::Color::toByte(k), 0}; });
        horizontalRamp("Blue", [](float k) { return av::Color{0, 0, av::Color::toByte(k)}; });
        horizontalRamp("Sunset palette", [](float k) { return sunsetGradient(k); });

        // Character ramp: intensity rises left to right, so glyph density should too.
        if (y < height - 2) {
            const av::CharRamp& ramp = *ctx.ramp;
            std::string glyphs;
            for (std::size_t i = 0; i < ramp.size(); ++i) {
                av::appendUtf8(glyphs, ramp[i]);
            }
            const std::string title = "Character ramp '" + ramp.name() + "' (" + std::to_string(ramp.size()) +
                                      " glyphs): " + glyphs;
            av::draw::text(fb, 1, y++, title, label, labelBg);
            for (int row = 0; row < 2 && y < height - 1; ++row, ++y) {
                for (int x = 0; x < width; ++x) {
                    const float k = static_cast<float>(x) / static_cast<float>(std::max(1, width - 1));
                    av::Cell& c = fb.at(x, y);
                    c.ch = ramp.pick(k);
                    c.fg = row == 0 ? av::Color{255, 255, 255} : av::Color::fromHsv(k * 300.0f, 0.7f, 1.0f);
                    c.bg = {0, 0, 0};
                }
            }
        }
    }
};

} // namespace

std::unique_ptr<Pattern> makeTestCard() { return std::make_unique<TestCard>(); }

} // namespace demo
