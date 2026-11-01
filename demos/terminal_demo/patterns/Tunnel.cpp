// Polar-mapped checkerboard tunnel (a 2D texture-mapping effect, not 3D
// geometry). Exercises the character aspect-ratio correction: with the right
// --aspect value the tunnel mouth is circular.
#include "Pattern.h"
#include "PatternUtil.h"

#include <algorithm>
#include <cmath>

namespace demo {
namespace {

class Tunnel final : public Pattern {
public:
    const char* name() const override { return "Tunnel"; }

    void render(av::Framebuffer& fb, const PatternContext& ctx) override
    {
        const float t = static_cast<float>(ctx.time);
        const float halfW = static_cast<float>(fb.width()) * 0.5f;
        const float halfH = static_cast<float>(fb.height()) * 0.5f;
        const float radiusUnit = std::max(1.0f, std::min(halfW, halfH * ctx.cellAspect));

        // The tunnel centre sways so the walls appear to bend.
        const float swayX = std::sin(t * 0.6f) * 0.25f * halfW;
        const float swayY = std::cos(t * 0.45f) * 0.20f * halfH;

        for (int y = 0; y < fb.height(); ++y) {
            const float dy = (static_cast<float>(y) + 0.5f - halfH - swayY) * ctx.cellAspect;
            for (int x = 0; x < fb.width(); ++x) {
                const float dx = static_cast<float>(x) + 0.5f - halfW - swayX;
                const float r = std::sqrt(dx * dx + dy * dy) / radiusUnit;
                const float angle = std::atan2(dy, dx) / kTwoPi; // [-0.5, 0.5]

                const float depth = 0.35f / std::max(r, 0.02f);
                const float u = depth + t * 0.9f;
                const float v = angle + t * 0.05f + 0.15f * std::sin(t * 0.3f);

                const int check = (static_cast<int>(std::floor(u * 4.0f)) + static_cast<int>(std::floor(v * 16.0f))) & 1;
                const float fog = std::clamp(r * 1.5f, 0.0f, 1.0f); // dark far away (centre)
                const float ripple = 0.80f + 0.20f * std::sin(v * kTwoPi * 8.0f + u * 3.0f);
                const float shade = (check ? 1.0f : 0.40f) * fog * ripple;

                // Magenta-to-cyan bands along the tunnel.
                const float hue = 320.0f - 140.0f * (0.5f + 0.5f * std::sin(u * 2.0f + angle * kTwoPi));
                const av::Color base = av::Color::fromHsv(hue, 0.85f, 1.0f);

                av::Cell& cell = fb.at(x, y);
                cell.ch = ctx.ramp->pick(shade);
                cell.fg = av::scale(base, 0.25f + 0.75f * shade);
                cell.bg = av::scale(base, shade * 0.22f);
            }
        }
    }
};

} // namespace

std::unique_ptr<Pattern> makeTunnel() { return std::make_unique<Tunnel>(); }

} // namespace demo
