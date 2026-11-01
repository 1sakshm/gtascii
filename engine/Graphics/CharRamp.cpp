#include "Graphics/CharRamp.h"

#include "Core/Utf8.h"

#include <algorithm>
#include <stdexcept>

namespace av {

CharRamp::CharRamp(std::string name, std::string_view glyphsUtf8)
    : name_(std::move(name))
    , glyphs_(decodeUtf8(glyphsUtf8))
{
    if (glyphs_.empty()) {
        throw std::invalid_argument("CharRamp '" + name_ + "' has no glyphs");
    }
}

char32_t CharRamp::pick(float intensity) const noexcept
{
    // NaN-safe clamp: comparisons with NaN are false, so it lands on 0.
    const float t = intensity > 0.0f ? std::min(intensity, 1.0f) : 0.0f;
    const auto n = glyphs_.size();
    const auto idx = std::min(static_cast<std::size_t>(t * static_cast<float>(n)), n - 1);
    return glyphs_[idx];
}

const std::vector<CharRamp>& builtinRamps()
{
    static const std::vector<CharRamp> ramps = {
        CharRamp("standard", " .:-=+*#%@"),
        CharRamp("detailed", " .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$"),
        // space, U+2591 light shade, U+2592 medium shade, U+2593 dark shade, U+2588 full block
        CharRamp("blocks", " \xE2\x96\x91\xE2\x96\x92\xE2\x96\x93\xE2\x96\x88"),
        CharRamp("minimal", " .oO@"),
    };
    return ramps;
}

const CharRamp* findRamp(std::string_view name)
{
    const auto& ramps = builtinRamps();
    const auto it = std::find_if(ramps.begin(), ramps.end(), [name](const CharRamp& r) { return r.name() == name; });
    return it == ramps.end() ? nullptr : &*it;
}

} // namespace av
