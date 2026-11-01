#include "Graphics/Material.h"

#include "Core/FileUtil.h"

#include <stdexcept>

namespace av {

namespace {

Vec3 readColor(const JsonValue& v, const std::string& context)
{
    if (!v.isArray() || v.size() != 3) {
        throw std::runtime_error(context + ": colour must be [r, g, b] (0-255)");
    }
    return {static_cast<float>(v[0].asNumber() / 255.0), static_cast<float>(v[1].asNumber() / 255.0),
            static_cast<float>(v[2].asNumber() / 255.0)};
}

} // namespace

MaterialLibrary::MaterialLibrary()
{
    Material fallback;
    fallback.name = "<missing>";
    fallback.albedo = {1.0f, 0.0f, 1.0f};
    materials_.push_back(fallback);
}

MaterialId MaterialLibrary::add(Material material)
{
    const auto existing = byName_.find(material.name);
    if (existing != byName_.end()) {
        materials_[existing->second] = std::move(material);
        return existing->second;
    }
    if (materials_.size() >= 0xFFFF) {
        throw std::runtime_error("too many materials");
    }
    const auto id = static_cast<MaterialId>(materials_.size());
    if (!material.name.empty()) {
        byName_[material.name] = id;
    }
    materials_.push_back(std::move(material));
    return id;
}

MaterialId MaterialLibrary::find(const std::string& name) const
{
    const auto it = byName_.find(name);
    return it == byName_.end() ? kNoMaterial : it->second;
}

MaterialId MaterialLibrary::require(const std::string& name) const
{
    const MaterialId id = find(name);
    if (id == kNoMaterial) {
        throw std::runtime_error("material not defined: '" + name + "'");
    }
    return id;
}

const Material& MaterialLibrary::get(MaterialId id) const
{
    return id < materials_.size() ? materials_[id] : materials_[0];
}

const CharRamp* MaterialLibrary::addRamp(const std::string& name, const std::string& glyphsUtf8)
{
    for (CharRamp& r : ramps_) {
        if (r.name() == name) {
            r = CharRamp(name, glyphsUtf8);
            return &r;
        }
    }
    ramps_.emplace_back(name, glyphsUtf8);
    return &ramps_.back();
}

const CharRamp* MaterialLibrary::findRamp(const std::string& name) const
{
    for (const CharRamp& r : ramps_) {
        if (r.name() == name) {
            return &r;
        }
    }
    return av::findRamp(name);
}

void MaterialLibrary::loadJson(const JsonValue& doc)
{
    if (const JsonValue* ramps = doc.find("ramps")) {
        for (const auto& [name, glyphs] : ramps->asObject()) {
            addRamp(name, glyphs.asString());
        }
    }

    const JsonValue* mats = doc.find("materials");
    if (mats == nullptr || !mats->isObject()) {
        throw std::runtime_error("material file has no \"materials\" object");
    }
    for (const auto& [name, def] : mats->asObject()) {
        const std::string ctx = "material '" + name + "'";
        if (!def.isObject()) {
            throw std::runtime_error(ctx + " must be an object");
        }
        Material m;
        m.name = name;
        if (const JsonValue* c = def.find("color")) m.albedo = readColor(*c, ctx);
        if (const JsonValue* e = def.find("emissive")) {
            m.emissive = readColor(*e, ctx) * static_cast<float>(def.numberOr("emissiveStrength", 1.0));
        }
        m.specular = static_cast<float>(def.numberOr("specular", 0.0));
        m.shininess = static_cast<float>(def.numberOr("shininess", 16.0));
        m.reflectivity = static_cast<float>(def.numberOr("reflectivity", 0.0));
        m.noise = static_cast<float>(def.numberOr("noise", 0.0));
        m.glyphScale = static_cast<float>(def.numberOr("glyphScale", 1.0));
        m.doubleSided = def.boolOr("doubleSided", false);
        m.nightEmissive = def.boolOr("nightEmissive", false);
        m.water = def.boolOr("water", false);
        m.unlit = def.boolOr("unlit", false);
        if (const JsonValue* r = def.find("ramp")) {
            m.ramp = findRamp(r->asString());
            if (m.ramp == nullptr) {
                throw std::runtime_error(ctx + ": unknown ramp '" + r->asString() + "'");
            }
        }
        add(std::move(m));
    }
}

void MaterialLibrary::loadFile(const std::filesystem::path& path)
{
    const auto text = readTextFile(path);
    if (!text) {
        throw std::runtime_error("cannot read material file: " + path.string());
    }
    try {
        loadJson(parseJson(*text));
    } catch (const std::exception& e) {
        throw std::runtime_error(path.string() + ": " + e.what());
    }
}

} // namespace av
