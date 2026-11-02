#pragma once

#include "Interiors.h"
#include "Protocol.h"
#include "Weapons.h"

#include "Graphics/Lighting.h"
#include "Graphics/Material.h"
#include "Graphics/Mesh.h"

#include <array>
#include <vector>

namespace av {
class Renderer;
}

namespace client {

// Meshes for everything combat adds: weapons (in hand and as pickups), the
// jetpack, tracers, explosions, rockets and grenades, loot; plus the
// furnished interiors.
class CombatVisuals {
public:
    explicit CombatVisuals(av::MaterialLibrary& materials);

    // `world` is the character's model matrix, `joints` its posed skeleton.
    void drawHeld(av::Renderer& renderer, const av::Mat4& world, const std::vector<av::Mat4>& joints, int weapon) const;
    void drawJetpack(av::Renderer& renderer, const av::Mat4& world, const std::vector<av::Mat4>& joints, bool thrust, float time) const;
    // `age`: seconds since the effect appeared.
    void drawEffect(av::Renderer& renderer, const av::proto::EntityState& s, float age, float time) const;
    void drawPickup(av::Renderer& renderer, const av::proto::EntityState& s, float time) const;
    void effectLight(const av::proto::EntityState& s, float age, std::vector<av::PointLight>& out) const;

    // Easter-egg set pieces: the UFO over the sea and the giant duck in the canal.
    void drawSetPieces(av::Renderer& renderer, const av::Vec3& camera, float time, float hour) const;
    void setPieceLights(const av::Vec3& camera, float time, float hour, std::vector<av::PointLight>& out) const;

    void buildInteriors(const std::vector<av::proto::InteriorDef>& interiors);
    void drawInteriors(av::Renderer& renderer, const av::Vec3& camera) const;
    // Lights of the room the camera is in (if any).
    void interiorLights(const av::Vec3& camera, std::vector<av::PointLight>& out) const;

private:
    av::MaterialLibrary& materials_;
    std::array<av::Mesh, av::proto::kWeaponCount> weapons_;
    av::Mesh jetpack_, flame_, tracer_, fireball_, smoke_, rocket_, grenade_, cash_, health_, ring_;
    av::Mesh egg_, ufo_, ufoLights_, ufoBeam_, duck_, alley_, graffiti_;
    av::MaterialId fire_ = 0, smokeMat_ = 0, pickupGlow_ = 0;
    struct Room {
        av::Mesh mesh;
        av::AABB bounds;
        std::vector<av::PointLight> lights;
    };
    std::vector<Room> rooms_;
};

} // namespace client
