#pragma once

#include "Core/Json.h"
#include "Graphics/Cell.h"
#include "Graphics/CharRamp.h"
#include "Math/Vec.h"

#include <deque>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace av {

// Surface description used by the shading stage. Colours are 0..1 RGB.
struct Material {
    std::string name;
    Vec3 albedo{0.8f, 0.8f, 0.8f};
    Vec3 emissive{0, 0, 0};     // added after lighting; may exceed 1
    float specular = 0.0f;      // Blinn-Phong strength
    float shininess = 16.0f;
    float reflectivity = 0.0f;  // mirrors the sky (glass, water)
    float noise = 0.0f;         // per-position colour/glyph jitter (texture)
    float glyphScale = 1.0f;    // multiplies glyph density
    bool doubleSided = false;   // no backface culling (foliage, thin panels)
    bool nightEmissive = false; // emissive only after dusk (lit windows, lamps)
    bool water = false;         // animated wave normals
    bool unlit = false;         // ignores lighting (signs, sky objects)
    const CharRamp* ramp = nullptr; // glyph ramp override; nullptr = renderer default
};

// Owns all materials. MaterialId 0 (kNoMaterial) is reserved for "none";
// ids are stable for the library's lifetime.
class MaterialLibrary {
public:
    MaterialLibrary();

    MaterialId add(Material material);
    MaterialId find(const std::string& name) const;       // kNoMaterial if missing
    MaterialId require(const std::string& name) const;    // throws std::runtime_error if missing
    const Material& get(MaterialId id) const;             // id 0 / out of range -> fallback material
    std::size_t size() const noexcept { return materials_.size() - 1; }

    // Registers a glyph ramp that materials can reference by name. Built-in
    // ramps (standard, detailed, blocks, minimal) are always available.
    const CharRamp* addRamp(const std::string& name, const std::string& glyphsUtf8);
    const CharRamp* findRamp(const std::string& name) const;

    // Loads {"ramps": {name: glyphs}, "materials": {name: {...}}}. Colours
    // are [r, g, b] in 0-255. Existing names are replaced. Throws on errors.
    void loadJson(const JsonValue& doc);
    void loadFile(const std::filesystem::path& path);

private:
    std::vector<Material> materials_; // [0] = fallback (magenta)
    std::unordered_map<std::string, MaterialId> byName_;
    std::deque<CharRamp> ramps_;      // deque: stable addresses
};

} // namespace av
