// Two moving wave sources interfering, coloured with a sunset gradient.
#include "Pattern.h"
#include "PatternUtil.h"

#include <algorithm>
#include <cmath>

namespace demo {
namespace {

class Interference final : public Pattern {
public:
    const char* name() const override { return "Interference"; }

    void render(av::Framebuffer& fb, const PatternContext& ctx) override
    {
        const float t = static_cast<float>(ctx.time);
        const float w = static_cast<float>(fb.width());
        const float h = static_cast<float>(fb.height()) * ctx.cellAspect;
        const float unit = 1.0f / std::max(1.0f, std::min(w, h));

        // Lissajous paths for the sources, in normalised units.
        const float ax = (0.5f + 0.35f * std::sin(t * 0.50f)) * w * unit;
        const float ay = (0.5f + 0.35f * std::sin(t * 0.70f + 1.0f)) * h * unit;
        const float bx = (0.5f + 0.35f * std::cos(t * 0.40f)) * w * unit;
        const float by = (0.5f + 0.35f * std::cos(t * 0.60f + 2.0f)) * h * unit;

        constexpr float k = 38.0f; // wave number

        for (int y = 0; y < fb.height(); ++y) {
            const float py = (static_cast<float>(y) + 0.5f) * ctx.cellAspect * unit;
            for (int x = 0; x < fb.width(); ++x) {
                const float px = (static_cast<float>(x) + 0.5f) * unit;
                const float d1 = std::hypot(px - ax, py - ay);
                const float d2 = std::hypot(px - bx, py - by);
                const float wave = std::cos(d1 * k - t * 3.0f) + std::cos(d2 * k - t * 3.0f);
                const float n = (wave + 2.0f) * 0.25f; // [0, 1]

                // Waves decay with distance from the sources.
                const float falloff = std::clamp(1.4f - std::min(d1, d2) * 0.9f, 0.25f, 1.0f);
                const float intensity = n * falloff;

                av::Cell& cell = fb.at(x, y);
                cell.ch = ctx.ramp->pick(intensity);
                cell.fg = sunsetGradient(0.15f + 0.85f * intensity);
                cell.bg = av::scale(sunsetGradient(n * 0.5f), 0.35f * falloff);
            }
        }
    }
};

} // namespace

std::unique_ptr<Pattern> makeInterference() { return std::make_unique<Interference>(); }

} // namespace demo
