#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace av {

// 24-bit RGB colour, as sent to the terminal.
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    constexpr Color() = default;
    constexpr Color(std::uint8_t red, std::uint8_t green, std::uint8_t blue) : r(red), g(green), b(blue) {}

    constexpr bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b; }
    constexpr bool operator!=(const Color& o) const { return !(*this == o); }

    // Components in [0, 1]; out-of-range values are clamped.
    static Color fromFloat(float red, float green, float blue)
    {
        return {toByte(red), toByte(green), toByte(blue)};
    }

    // hue in degrees (any value, wrapped), saturation and value in [0, 1].
    static Color fromHsv(float hueDegrees, float saturation, float value)
    {
        float h = std::fmod(hueDegrees, 360.0f);
        if (h < 0.0f) {
            h += 360.0f;
        }
        const float s = std::clamp(saturation, 0.0f, 1.0f);
        const float v = std::clamp(value, 0.0f, 1.0f);

        const float c = v * s;
        const float hp = h / 60.0f;
        const float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
        float r1 = 0, g1 = 0, b1 = 0;
        switch (static_cast<int>(hp) % 6) {
        case 0: r1 = c; g1 = x; break;
        case 1: r1 = x; g1 = c; break;
        case 2: g1 = c; b1 = x; break;
        case 3: g1 = x; b1 = c; break;
        case 4: r1 = x; b1 = c; break;
        default: r1 = c; b1 = x; break;
        }
        const float m = v - c;
        return fromFloat(r1 + m, g1 + m, b1 + m);
    }

    static std::uint8_t toByte(float v)
    {
        return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
};

inline Color lerp(Color a, Color b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    auto mix = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t + 0.5f);
    };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)};
}

// Multiplies brightness by k (clamped to the displayable range).
inline Color scale(Color c, float k)
{
    auto mul = [k](std::uint8_t v) {
        return static_cast<std::uint8_t>(std::clamp(static_cast<float>(v) * k + 0.5f, 0.0f, 255.0f));
    };
    return {mul(c.r), mul(c.g), mul(c.b)};
}

inline Color addSaturate(Color a, Color b)
{
    auto add = [](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::min(255, static_cast<int>(x) + static_cast<int>(y)));
    };
    return {add(a.r, b.r), add(a.g, b.g), add(a.b, b.b)};
}

// Reduces each channel to `bits` of precision (1..8), rescaled so the
// maximum level still maps to 255. Used to trade colour fidelity for bandwidth.
inline Color quantize(Color c, int bits)
{
    if (bits >= 8) {
        return c;
    }
    bits = std::max(bits, 1);
    const int levels = (1 << bits) - 1;
    auto q = [bits, levels](std::uint8_t v) {
        const int level = v >> (8 - bits);
        return static_cast<std::uint8_t>((level * 255 + levels / 2) / levels);
    };
    return {q(c.r), q(c.g), q(c.b)};
}

// Relative luminance (Rec. 709 weights), in [0, 1].
inline float luminance(Color c)
{
    return (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / 255.0f;
}

} // namespace av
