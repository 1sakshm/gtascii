// Classic sum-of-sines plasma. Every cell changes every frame, so this is the
// worst case for the presenter's change detection: a bandwidth stress test.
#include "Pattern.h"
#include "PatternUtil.h"

#include <algorithm>
#include <cmath>

namespace demo {
namespace {

class Plasma final : public Pattern {
public:
    const char* name() const override { return "Plasma"; }

    void render(av::Framebuffer& fb, const PatternContext& ctx) override
    {
        const float t = static_cast<float>(ctx.time);
        const float w = static_cast<float>(fb.width());
        const float h = static_cast<float>(fb.height()) * ctx.cellAspect;
        // Normalise so patterns keep their shape regardless of terminal size.
        const float unit = 1.0f / std::max(1.0f, std::min(w, h));

        const float cx = (0.5f + 0.3f * std::sin(t * 0.40f)) * w * unit;
        const float cy = (0.5f + 0.3f * std::cos(t * 0.33f)) * h * unit;

        for (int y = 0; y < fb.height(); ++y) {
            const float py = (static_cast<float>(y) + 0.5f) * ctx.cellAspect * unit;
            for (int x = 0; x < fb.width(); ++x) {
                const float px = (static_cast<float>(x) + 0.5f) * unit;

                float v = std::sin(px * 10.0f + t);
                v += std::sin(py * 12.0f - t * 1.3f);
                v += std::sin((px + py) * 8.0f + t * 0.7f);
                const float dx = px - cx;
                const float dy = py - cy;
                v += std::sin(std::sqrt(dx * dx + dy * dy) * 14.0f - t * 2.0f);

                const float n = (v + 4.0f) / 8.0f; // [0, 1]
                const float intensity = 0.5f + 0.5f * std::sin(v * kPi * 0.75f + t);
                const av::Color base = av::Color::fromHsv(n * 540.0f + t * 30.0f, 0.80f, 1.0f);

                av::Cell& cell = fb.at(x, y);
                cell.ch = ctx.ramp->pick(intensity);
                cell.fg = av::scale(base, 0.35f + 0.65f * intensity);
                cell.bg = av::scale(base, 0.06f + 0.14f * (1.0f - intensity));
            }
        }
    }
};

} // namespace

std::unique_ptr<Pattern> makePlasma() { return std::make_unique<Plasma>(); }

} // namespace demo
