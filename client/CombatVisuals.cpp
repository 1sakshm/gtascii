#include "CombatVisuals.h"

#include "EasterEggs.h"

#include "Animation/Humanoid.h"
#include "Graphics/Renderer.h"

#include <cmath>

namespace client {

using namespace av;
using namespace av::proto;

namespace {

MaterialId mat(const MaterialLibrary& lib, const char* name, const char* fallback = "metal_dark")
{
    const MaterialId m = lib.find(name);
    return m != kNoMaterial ? m : lib.require(fallback);
}

// A matrix whose columns are the given axes (z may be scaled) and origin.
Mat4 basis(const Vec3& x, const Vec3& y, const Vec3& z, const Vec3& origin)
{
    Mat4 m;
    for (int r = 0; r < 3; ++r) {
        m.at(r, 0) = r == 0 ? x.x : r == 1 ? x.y : x.z;
        m.at(r, 1) = r == 0 ? y.x : r == 1 ? y.y : y.z;
        m.at(r, 2) = r == 0 ? z.x : r == 1 ? z.y : z.z;
        m.at(r, 3) = r == 0 ? origin.x : r == 1 ? origin.y : origin.z;
    }
    return m;
}

// The character's orientation, moved to a joint.
Mat4 atJoint(const Mat4& world, const std::vector<Mat4>& joints, int joint)
{
    const Vec3 p = (world * joints[static_cast<std::size_t>(joint)]).translationPart();
    return Mat4::translation(p - world.translationPart()) * world;
}

} // namespace

CombatVisuals::CombatVisuals(MaterialLibrary& materials)
    : materials_(materials)
{
    const MaterialId metal = mat(materials, "gun_metal"), wood = mat(materials, "gun_wood", "door_wood"),
                     blade = mat(materials, "blade", "metal"), olive = mat(materials, "rocket_body"), dark = mat(materials, "metal_dark");
    fire_ = mat(materials, "fire", "neon_pink");
    smokeMat_ = mat(materials, "smoke", "concrete_dark");
    pickupGlow_ = mat(materials, "weapon_pickup", "pickup_pink");

    // Weapons in hand space: grip at the origin, barrel along +Z.
    {
        MeshBuilder b(weapons_[Bat]);
        b.addBox(AABB({-0.03f, -0.03f, -0.05f}, {0.03f, 0.03f, 0.25f}), wood);
        b.addBox(AABB({-0.045f, -0.045f, 0.25f}, {0.045f, 0.045f, 0.8f}), wood);
    }
    {
        MeshBuilder b(weapons_[Knife]);
        b.addBox(AABB({-0.015f, -0.02f, -0.06f}, {0.015f, 0.02f, 0.05f}), dark);
        b.addBox(AABB({-0.006f, -0.025f, 0.05f}, {0.006f, 0.02f, 0.26f}), blade);
    }
    {
        MeshBuilder b(weapons_[Pistol]);
        b.addBox(AABB({-0.025f, 0.0f, -0.04f}, {0.025f, 0.07f, 0.2f}), metal);
        b.addBox(AABB({-0.022f, -0.1f, -0.04f}, {0.022f, 0.0f, 0.02f}), dark);
    }
    {
        MeshBuilder b(weapons_[Smg]);
        b.addBox(AABB({-0.03f, 0.0f, -0.08f}, {0.03f, 0.08f, 0.26f}), metal);
        b.addBox(AABB({-0.02f, -0.18f, 0.02f}, {0.02f, 0.0f, 0.07f}), dark);
    }
    {
        MeshBuilder b(weapons_[Shotgun]);
        b.addBox(AABB({-0.03f, 0.02f, -0.1f}, {0.03f, 0.07f, 0.55f}), metal);
        b.addBox(AABB({-0.035f, -0.04f, -0.4f}, {0.035f, 0.05f, -0.05f}), wood);
        b.addBox(AABB({-0.03f, -0.02f, 0.1f}, {0.03f, 0.02f, 0.35f}), wood);
    }
    {
        MeshBuilder b(weapons_[Rifle]);
        b.addBox(AABB({-0.03f, 0.0f, -0.35f}, {0.03f, 0.09f, 0.55f}), metal);
        b.addBox(AABB({-0.025f, -0.17f, 0.05f}, {0.025f, 0.0f, 0.12f}), dark);
        b.addBox(AABB({-0.03f, -0.06f, -0.35f}, {0.03f, 0.05f, -0.15f}), wood);
    }
    {
        MeshBuilder b(weapons_[Sniper]);
        b.addBox(AABB({-0.03f, 0.0f, -0.4f}, {0.03f, 0.08f, 0.75f}), metal);
        b.addCylinder({0, 0.11f, -0.05f}, 0.3f, 0.03f, 0.03f, 6, dark, true, Mat4::rotationX(kHalfPi));
        b.addBox(AABB({-0.035f, -0.05f, -0.4f}, {0.035f, 0.05f, -0.15f}), wood);
    }
    {
        MeshBuilder b(weapons_[Minigun]);
        for (int i = 0; i < 6; ++i) {
            const float a = static_cast<float>(i) * kTwoPi / 6.0f;
            const float x = std::cos(a) * 0.05f, y = std::sin(a) * 0.05f + 0.05f;
            b.addBox(AABB({x - 0.012f, y - 0.012f, 0.0f}, {x + 0.012f, y + 0.012f, 0.75f}), metal);
        }
        b.addBox(AABB({-0.09f, -0.04f, -0.25f}, {0.09f, 0.14f, 0.05f}), dark);
    }
    {
        MeshBuilder b(weapons_[RocketLauncher]);
        b.addCylinder({0, 0.08f, -0.45f}, 1.0f, 0.075f, 0.075f, 8, olive, true, Mat4::rotationX(kHalfPi));
        b.addBox(AABB({-0.02f, -0.1f, -0.05f}, {0.02f, 0.04f, 0.02f}), dark);
    }
    {
        MeshBuilder b(weapons_[Grenades]);
        b.addEllipsoid({0, 0.04f, 0.03f}, {0.045f, 0.055f, 0.045f}, 4, 6, olive);
    }
    for (Mesh& m : weapons_) m.computeBounds();

    {
        MeshBuilder b(jetpack_);
        const MaterialId jp = mat(materials, "jetpack", "metal");
        b.addCylinder({-0.13f, -0.25f, -0.22f}, 0.55f, 0.09f, 0.09f, 8, jp, true);
        b.addCylinder({0.13f, -0.25f, -0.22f}, 0.55f, 0.09f, 0.09f, 8, jp, true);
        b.addBox(AABB({-0.2f, -0.05f, -0.16f}, {0.2f, 0.25f, -0.12f}), dark);
        jetpack_.computeBounds();
    }
    MeshBuilder(flame_).addCylinder({0, -0.9f, 0}, 0.6f, 0.02f, 0.08f, 6, fire_, true);
    flame_.computeBounds();
    MeshBuilder(tracer_).addBox(AABB({-0.025f, -0.025f, 0.0f}, {0.025f, 0.025f, 1.0f}), mat(materials, "tracer", "lamp_glow"));
    tracer_.computeBounds();
    MeshBuilder(fireball_).addEllipsoid({0, 0, 0}, {1, 1, 1}, 6, 10, fire_);
    fireball_.computeBounds();
    MeshBuilder(smoke_).addEllipsoid({0, 0, 0}, {1, 1, 1}, 5, 8, smokeMat_);
    smoke_.computeBounds();
    {
        MeshBuilder b(rocket_);
        b.addBox(AABB({-0.05f, -0.05f, -0.35f}, {0.05f, 0.05f, 0.35f}), olive);
        b.addBox(AABB({-0.04f, -0.04f, -0.6f}, {0.04f, 0.04f, -0.35f}), fire_);
        rocket_.computeBounds();
    }
    MeshBuilder(grenade_).addEllipsoid({0, 0, 0}, {0.07f, 0.08f, 0.07f}, 4, 6, olive);
    grenade_.computeBounds();
    {
        MeshBuilder b(cash_);
        const MaterialId green = mat(materials, "cash_green", "pastel_mint");
        b.addBox(AABB({-0.18f, 0.0f, -0.09f}, {0.18f, 0.12f, 0.09f}), green);
        cash_.computeBounds();
    }
    {
        MeshBuilder b(health_);
        const MaterialId white = mat(materials, "trim_white", "wall_white"), red = mat(materials, "cross_red", "neon_pink");
        b.addBox(AABB({-0.2f, 0.0f, -0.14f}, {0.2f, 0.28f, 0.14f}), white);
        b.addBox(AABB({-0.05f, 0.04f, 0.141f}, {0.05f, 0.24f, 0.15f}), red);
        b.addBox(AABB({-0.12f, 0.1f, 0.141f}, {0.12f, 0.18f, 0.15f}), red);
        health_.computeBounds();
    }
    MeshBuilder(ring_).addTorus({0, 0.05f, 0}, 0.45f, 0.04f, 14, 4, pickupGlow_);
    ring_.computeBounds();

    const MaterialId gold = mat(materials, "gold", "pastel_yellow");
    MeshBuilder(egg_).addEllipsoid({0, 0, 0}, {0.22f, 0.3f, 0.22f}, 6, 10, gold);
    egg_.computeBounds();
    {
        // Saucer: a wide flat disc, a glass dome, a ring of lights underneath.
        MeshBuilder b(ufo_);
        const MaterialId hull = mat(materials, "metal", "metal_dark"), dome = mat(materials, "glass_teal", "car_glass");
        b.addEllipsoid({0, 0, 0}, {9.0f, 1.3f, 9.0f}, 6, 24, hull);
        b.addEllipsoid({0, 1.1f, 0}, {3.2f, 2.2f, 3.2f}, 6, 16, dome);
        ufo_.computeBounds();
        MeshBuilder lb(ufoLights_);
        const MaterialId lamp = mat(materials, "neon_cyan", "lamp_glow");
        for (int i = 0; i < 12; ++i) {
            const float a = static_cast<float>(i) * kTwoPi / 12.0f;
            lb.addEllipsoid({std::sin(a) * 7.2f, -0.7f, std::cos(a) * 7.2f}, {0.5f, 0.35f, 0.5f}, 3, 6, lamp);
        }
        ufoLights_.computeBounds();
        MeshBuilder(ufoBeam_).addCylinder({0, -70.0f, 0}, 69.0f, 6.0f, 1.5f, 16, mat(materials, "marker_green", "neon_cyan"), false);
        ufoBeam_.computeBounds();
    }
    {
        // A rubber duck the size of a house.
        MeshBuilder b(duck_);
        const MaterialId yellow = mat(materials, "pastel_yellow", "car_paint_7"), orange = mat(materials, "awning_red", "neon_pink"),
                         eye = mat(materials, "char_eye", "metal_dark");
        b.addEllipsoid({0, 1.2f, 0}, {2.6f, 1.7f, 3.4f}, 8, 16, yellow);      // body
        b.addEllipsoid({0, 1.9f, -2.4f}, {1.4f, 1.2f, 1.1f}, 6, 12, yellow);  // tail
        b.addEllipsoid({0, 3.6f, 1.6f}, {1.6f, 1.6f, 1.6f}, 8, 14, yellow);   // head
        b.addEllipsoid({0, 3.3f, 3.2f}, {0.9f, 0.35f, 0.7f}, 5, 10, orange);  // beak
        for (const float x : {-0.75f, 0.75f}) b.addEllipsoid({x, 4.2f, 2.85f}, {0.2f, 0.25f, 0.15f}, 4, 6, eye);
        duck_.computeBounds();
    }
}

void CombatVisuals::drawHeld(Renderer& renderer, const Mat4& world, const std::vector<Mat4>& joints, int weapon) const
{
    if (weapon <= Fists || weapon >= kWeaponCount || joints.size() <= JRHand) return;
    const bool big = weapon == RocketLauncher || weapon == Minigun || weapon == Rifle || weapon == Sniper || weapon == Shotgun;
    // Long guns sit at the hip pointing forward; small ones in the hand.
    const Mat4 base = big ? atJoint(world, joints, JRHand) * Mat4::translation({0.05f, 0.25f, 0.05f})
                          : atJoint(world, joints, JRHand) * Mat4::translation({0.0f, -0.02f, 0.04f});
    renderer.drawMesh(weapons_[static_cast<std::size_t>(weapon)], base);
}

void CombatVisuals::drawJetpack(Renderer& renderer, const Mat4& world, const std::vector<Mat4>& joints, bool thrust, float time) const
{
    if (joints.size() <= JChest) return;
    const Mat4 base = atJoint(world, joints, JChest);
    renderer.drawMesh(jetpack_, base);
    if (thrust) {
        const float flicker = 0.8f + 0.3f * std::sin(time * 40.0f);
        for (const float x : {-0.13f, 0.13f}) {
            renderer.drawMesh(flame_, base * Mat4::translation({x, -0.25f, -0.22f}) * Mat4::scale({1.0f, flicker, 1.0f}));
        }
    }
}

void CombatVisuals::drawEffect(Renderer& renderer, const EntityState& s, float age, float time) const
{
    switch (static_cast<EffectType>(s.subtype)) {
    case EffectType::Tracer: {
        if (age > 0.15f) return;
        const Vec3 span{std::sin(s.yaw) * s.speed, s.verticalSpeed, std::cos(s.yaw) * s.speed};
        const float len = length(span);
        if (len < 0.1f) return;
        const Vec3 z = span / len;
        const Vec3 x = normalize(cross(Vec3{0, 1, 0}, z) + Vec3{1e-4f, 0, 0});
        const Vec3 y = cross(z, x);
        renderer.drawMesh(tracer_, basis(x, y, z * len, s.position));
        break;
    }
    case EffectType::Explosion: {
        if (age > 1.2f) return;
        const float r = 1.0f + 4.5f * std::sqrt(std::min(age / 0.5f, 1.0f));
        if (age < 0.7f) {
            renderer.drawMesh(fireball_, Mat4::translation(s.position) * Mat4::scale({r, r * 0.8f, r}));
        }
        const float sr = r * (1.0f + age);
        renderer.drawMesh(smoke_, Mat4::translation(s.position + Vec3{0, age * 3.0f, 0}) * Mat4::scale({sr * 0.7f, sr * 0.6f, sr * 0.7f}));
        break;
    }
    case EffectType::Rocket:
        renderer.drawMesh(rocket_, Mat4::translation(s.position) * Mat4::rotationY(s.yaw));
        break;
    case EffectType::Grenade:
        renderer.drawMesh(grenade_, Mat4::translation(s.position) * Mat4::rotationY(time * 6.0f));
        break;
    }
}

void CombatVisuals::effectLight(const EntityState& s, float age, std::vector<PointLight>& out) const
{
    if (s.subtype == static_cast<std::uint8_t>(EffectType::Explosion) && age < 0.8f) {
        out.push_back({s.position + Vec3{0, 1.5f, 0}, {1.0f, 0.6f, 0.2f}, 25.0f, 3.0f * (1.0f - age / 0.8f)});
    } else if (s.subtype == static_cast<std::uint8_t>(EffectType::Tracer) && age < 0.08f) {
        out.push_back({s.position, {1.0f, 0.85f, 0.5f}, 6.0f, 1.5f});
    } else if (s.subtype == static_cast<std::uint8_t>(EffectType::Rocket)) {
        out.push_back({s.position, {1.0f, 0.6f, 0.3f}, 8.0f, 1.5f});
    }
}

void CombatVisuals::drawPickup(Renderer& renderer, const EntityState& s, float time) const
{
    const Mat4 spin = Mat4::translation(s.position + Vec3{0, 0.35f + 0.12f * std::sin(time * 2.5f), 0}) * Mat4::rotationY(time * 1.8f);
    const Mat4 ground = Mat4::translation(s.position);
    if (s.subtype == PickupType::GoldenEgg) {
        renderer.drawMesh(egg_, spin * Mat4::rotationZ(0.3f * std::sin(time * 3.0f)));
        renderer.drawMesh(ring_, ground * Mat4::scale({1.3f, 1.0f, 1.3f}) * Mat4::rotationY(-time * 2.0f));
        return;
    }
    if (s.subtype == PickupType::Cash) {
        renderer.drawMesh(cash_, spin);
    } else if (s.subtype == PickupType::Health) {
        renderer.drawMesh(health_, spin);
    } else if (s.subtype == PickupType::Jetpack) {
        renderer.drawMesh(jetpack_, spin * Mat4::translation({0, 0.3f, 0.2f}));
        renderer.drawMesh(ring_, ground * Mat4::scale({1.4f, 1.0f, 1.4f}));
    } else if (s.subtype >= PickupType::WeaponBase && s.subtype < PickupType::WeaponBase + kWeaponCount) {
        renderer.drawMesh(weapons_[s.subtype - PickupType::WeaponBase], spin * Mat4::scale({1.6f, 1.6f, 1.6f}) * Mat4::translation({0, 0, -0.15f}));
        renderer.drawMesh(ring_, ground);
    }
}

void CombatVisuals::drawSetPieces(Renderer& renderer, const Vec3& camera, float time, float hour) const
{
    if (distance(camera, kUfoPosition) < 600.0f) {
        const Vec3 bob{0.0f, 1.5f * std::sin(time * 0.7f), 0.0f};
        const Mat4 m = Mat4::translation(kUfoPosition + bob) * Mat4::rotationY(time * 0.3f) * Mat4::rotationX(0.08f * std::sin(time * 0.5f));
        renderer.drawMesh(ufo_, m);
        if (std::fmod(time * 2.0f, 1.0f) < 0.7f) renderer.drawMesh(ufoLights_, m);
        const bool night = hour < 6.0f || hour > 19.5f;
        if (night) renderer.drawMesh(ufoBeam_, Mat4::translation(kUfoPosition + bob));
    }
    if (distance(camera, kDuckPosition) < 250.0f) {
        const Mat4 m = Mat4::translation(kDuckPosition + Vec3{0.0f, -0.3f + 0.15f * std::sin(time * 1.3f), 0.0f}) *
                       Mat4::rotationY(0.6f + 0.2f * std::sin(time * 0.2f)) * Mat4::rotationZ(0.05f * std::sin(time * 1.1f));
        renderer.drawMesh(duck_, m);
    }
}

void CombatVisuals::setPieceLights(const Vec3& camera, float time, float hour, std::vector<PointLight>& out) const
{
    (void)time;
    const bool night = hour < 6.0f || hour > 19.5f;
    if (night && distance(camera, kUfoPosition) < 250.0f) {
        out.push_back({kUfoPosition + Vec3{0, -6, 0}, {0.3f, 1.0f, 0.5f}, 40.0f, 2.0f});
    }
}

void CombatVisuals::buildInteriors(const std::vector<InteriorDef>& interiors)
{
    rooms_.clear();
    for (const InteriorDef& d : interiors) {
        Room room;
        MeshBuilder b(room.mesh);
        for (const InteriorBox& box : d.boxes) {
            b.addBox(AABB(box.box.min - d.origin, box.box.max - d.origin), mat(materials_, box.material.c_str(), "wall_white"));
        }
        room.mesh.computeBounds();
        room.bounds = AABB(d.origin - Vec3{d.halfX + 1, 1, d.halfZ + 1}, d.origin + Vec3{d.halfX + 1, d.height + 1, d.halfZ + 1});
        room.lights = d.lights;
        // Remember where to draw it: the mesh is relative to the origin.
        room.lights.push_back({d.origin + Vec3{0, d.height - 0.3f, 0}, {1.0f, 0.95f, 0.9f}, d.halfX * 2.0f, 0.6f});
        rooms_.push_back(std::move(room));
        rooms_.back().bounds = room.bounds;
    }
}

void CombatVisuals::drawInteriors(Renderer& renderer, const Vec3& camera) const
{
    for (const Room& r : rooms_) {
        const Vec3 centre = (r.bounds.min + r.bounds.max) * 0.5f;
        if (distance(centre, camera) > 60.0f || !renderer.isVisible(r.bounds)) continue;
        const Vec3 origin{centre.x, r.bounds.min.y + 1.0f, centre.z};
        renderer.drawMesh(r.mesh, Mat4::translation(origin));
    }
}

void CombatVisuals::interiorLights(const Vec3& camera, std::vector<PointLight>& out) const
{
    for (const Room& r : rooms_) {
        if (camera.y > r.bounds.min.y - 2.0f && camera.y < r.bounds.max.y + 4.0f && distance((r.bounds.min + r.bounds.max) * 0.5f, camera) < 40.0f) {
            out.insert(out.end(), r.lights.begin(), r.lights.end());
        }
    }
}

} // namespace client
