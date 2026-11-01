#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace av {

// Ordered set of glyphs from visually sparse to dense. Maps an intensity in
// [0, 1] to a character, so brightness can be expressed through glyph density
// as well as colour.
class CharRamp {
public:
    // `glyphsUtf8` lists glyphs sparse-to-dense, e.g. " .:-=+*#%@".
    // Throws std::invalid_argument if empty.
    CharRamp(std::string name, std::string_view glyphsUtf8);

    // Intensity is clamped to [0, 1]; the range is split into equal buckets.
    char32_t pick(float intensity) const noexcept;

    std::size_t size() const noexcept { return glyphs_.size(); }
    char32_t operator[](std::size_t i) const { return glyphs_[i]; }
    const std::string& name() const noexcept { return name_; }

private:
    std::string name_;
    std::u32string glyphs_;
};

// Built-in ramps: "standard", "detailed", "blocks", "minimal".
const std::vector<CharRamp>& builtinRamps();

// Returns the built-in ramp with that name, or nullptr.
const CharRamp* findRamp(std::string_view name);

} // namespace av
