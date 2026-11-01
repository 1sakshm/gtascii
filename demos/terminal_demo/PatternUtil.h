#pragma once

#include "Graphics/Color.h"

#include <cstddef>

namespace demo {

struct GradientStop {
    float t;
    av::Color color;
};

// Piecewise-linear gradient; stops must be sorted by t. t is clamped.
av::Color sampleGradient(const GradientStop* stops, std::size_t count, float t);

// Deep purple -> magenta -> coral -> gold. The demo's "Vice" palette.
av::Color sunsetGradient(float t);

inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kTwoPi = 6.28318530717959f;

} // namespace demo
