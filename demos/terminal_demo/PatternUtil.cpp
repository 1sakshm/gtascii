#include "PatternUtil.h"

#include "Pattern.h"

#include <algorithm>
#include <iterator>

namespace demo {

av::Color sampleGradient(const GradientStop* stops, std::size_t count, float t)
{
    if (count == 0) {
        return {};
    }
    if (t <= stops[0].t) {
        return stops[0].color;
    }
    for (std::size_t i = 1; i < count; ++i) {
        if (t <= stops[i].t) {
            const float span = stops[i].t - stops[i - 1].t;
            const float local = span > 0.0f ? (t - stops[i - 1].t) / span : 0.0f;
            return av::lerp(stops[i - 1].color, stops[i].color, local);
        }
    }
    return stops[count - 1].color;
}

av::Color sunsetGradient(float t)
{
    static constexpr GradientStop stops[] = {
        {0.00f, {18, 4, 40}},
        {0.30f, {110, 20, 140}},
        {0.55f, {235, 50, 140}},
        {0.78f, {255, 120, 80}},
        {1.00f, {255, 220, 110}},
    };
    return sampleGradient(stops, std::size(stops), t);
}

std::vector<std::unique_ptr<Pattern>> createPatterns()
{
    std::vector<std::unique_ptr<Pattern>> patterns;
    patterns.push_back(makePlasma());
    patterns.push_back(makeTunnel());
    patterns.push_back(makeInterference());
    patterns.push_back(makeShapes());
    patterns.push_back(makeTestCard());
    return patterns;
}

} // namespace demo
