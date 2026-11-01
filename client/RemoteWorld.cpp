#include "RemoteWorld.h"

#include "Graphics/Renderer.h"

#include <cmath>

namespace client {

using namespace av;
using namespace av::proto;

const HumanoidVisual& CharacterLooks::get(std::uint32_t seed, bool police)
{
    const std::uint64_t key = (static_cast<std::uint64_t>(police) << 32) | seed;
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return *it->second;
    }
    Appearance a = Appearance::fromCode(seed);
    if (police) {
        // Uniform: navy shirt and trousers, black shoes, cap, shades.
        a.shirt = {35, 50, 110};
        a.trousers = {25, 30, 60};
        a.shoes = {20, 20, 20};
        a.hat = HatStyle::Cap;
        a.hatColor = {25, 30, 60};
        a.longSleeves = false;
        a.shorts = false;
        a.backpack = false;
        a.sunglasses = true;
    }
    const std::string name = std::string(police ? "police/" : "npc/") + std::to_string(seed);
    auto visual = std::make_unique<HumanoidVisual>(a, registerAppearance(materials_, a, name));
    const HumanoidVisual& ref = *visual;
    cache_[key] = std::move(visual);
    return ref;
}

void CharacterAnimation::update(const EntityState& s, float dt)
{
    AnimationInput in;
    in.speed = s.mode == CharacterMode::OnFoot ? s.speed : 0.0f;
    in.verticalSpeed = s.verticalSpeed;
    in.grounded = s.grounded;
    in.crouching = s.crouch;
    in.talking = (s.flags & EntityFlags::Talking) != 0;
    in.sit = (s.mode == CharacterMode::InVehicle || s.mode == CharacterMode::Entering) ? 1.0f : 0.0f;
    in.knockedDown = s.mode == CharacterMode::KnockedDown;
    if (dt > 0.0f) {
        in.turnRate = clampv(wrapAngle(s.yaw - lastYaw) / dt, -10.0f, 10.0f);
    }
    lastYaw = s.yaw;
    // One-shot actions: the counter in the top 6 bits changes on each new action.
    if (seenAction && s.action != lastAction) {
        const int type = s.action & 3;
        in.wave = type == 1;
        in.punch = type == 2;
    }
    lastAction = s.action;
    seenAction = true;
    animator.update(in, dt);
    humanoidSkeleton().computeGlobal(animator.pose(), joints);
}

RemoteWorld::RemoteWorld(MaterialLibrary& materials, CharacterLooks& looks)
    : materials_(materials)
    , looks_(looks)
{
    MeshBuilder(beam_).addCylinder({0, 0, 0}, 6.0f, 0.22f, 0.05f, 8, kNoMaterial, false);
    MeshBuilder(ring_).addTorus({0, 0.08f, 0}, 0.9f, 0.07f, 18, 5, kNoMaterial);
    {
        // Octahedron "diamond" icon.
        MeshBuilder db(diamond_);
        const Vec3 top{0, 0.35f, 0}, bottom{0, -0.35f, 0};
        const Vec3 ring[4] = {{0.22f, 0, 0}, {0, 0, 0.22f}, {-0.22f, 0, 0}, {0, 0, -0.22f}};
        for (int i = 0; i < 4; ++i) {
            db.addTriangle(top, ring[(i + 1) % 4], ring[i], kNoMaterial);
            db.addTriangle(bottom, ring[i], ring[(i + 1) % 4], kNoMaterial);
        }
    }
    MeshBuilder fb(flamingo_);
    fb.addEllipsoid({0, 0.55f, 0}, {0.16f, 0.13f, 0.24f}, 5, 8, kNoMaterial);
    fb.addCylinder({0, 0.0f, 0.02f}, 0.45f, 0.025f, 0.025f, 4, kNoMaterial, false);
    fb.addCylinder({0, 0.6f, 0.15f}, 0.35f, 0.035f, 0.03f, 4, kNoMaterial, false, Mat4::rotationX(0.25f));
    fb.addEllipsoid({0, 1.0f, 0.24f}, {0.06f, 0.06f, 0.08f}, 4, 6, kNoMaterial);
    const char* names[6] = {"marker_pink", "marker_pink", "marker_yellow", "marker_yellow", "marker_cyan", "marker_green"};
    for (int i = 0; i < 6; ++i) {
        markerMats_[i] = materials.require(names[i]);
    }
    pickupMat_ = materials.require("pickup_pink");
}

void RemoteWorld::ingest(const Snapshot& snap, double localTime)
{
    // Server clock estimate: offset = serverTime - localTime, tracked with a
    // filter that follows the fastest (least delayed) packets.
    const double offset = snap.serverTime - localTime;
    if (!clockValid_ || offset > clockOffset_) {
        clockOffset_ = offset;
        clockValid_ = true;
    } else {
        clockOffset_ += (offset - clockOffset_) * 0.05;
    }
    hour_ = snap.hour;
    for (const EntityState& e : snap.entities) {
        Entity& ent = entities_[e.netId];
        ent.samples.emplace_back(snap.serverTime, e);
        while (ent.samples.size() > 12) {
            ent.samples.pop_front();
        }
        ent.lastSeen = localTime;
        if (ent.samples.size() == 1) {
            ent.current = e;
        }
    }
}

void RemoteWorld::update(double localTime, float dt)
{
    const double renderTime = localTime + clockOffset_ - interpolationDelay;
    for (auto it = entities_.begin(); it != entities_.end();) {
        Entity& e = it->second;
        if (localTime - e.lastSeen > 1.0) {
            it = entities_.erase(it); // left our interest area or despawned
            continue;
        }
        // Find the two samples around renderTime.
        const auto& s = e.samples;
        EntityState st = s.back().second;
        for (std::size_t i = 1; i < s.size(); ++i) {
            if (s[i].first >= renderTime) {
                const auto& a = s[i - 1];
                const auto& b = s[i];
                const float t = b.first > a.first ? static_cast<float>(saturate(static_cast<float>((renderTime - a.first) / (b.first - a.first)))) : 1.0f;
                st = b.second;
                st.position = lerp(a.second.position, b.second.position, t);
                st.yaw = a.second.yaw + wrapAngle(b.second.yaw - a.second.yaw) * t;
                st.speed = lerpf(a.second.speed, b.second.speed, t);
                st.steer = lerpf(a.second.steer, b.second.steer, t);
                st.wheelSpin = a.second.wheelSpin + wrapAngle(b.second.wheelSpin - a.second.wheelSpin) * t;
                st.pitch = lerpf(a.second.pitch, b.second.pitch, t);
                st.roll = lerpf(a.second.roll, b.second.roll, t);
                break;
            }
        }
        e.current = st;
        if (st.kind == EntityKind::Character) {
            e.anim.update(st, dt);
        }
        ++it;
    }
}

const RemoteWorld::Entity* RemoteWorld::find(std::uint16_t netId) const
{
    const auto it = entities_.find(netId);
    return it == entities_.end() ? nullptr : &it->second;
}

void RemoteWorld::vehicleObstacles(std::vector<AABB>& out, std::uint16_t exclude) const
{
    for (const auto& [id, e] : entities_) {
        if (id == exclude || e.current.kind != EntityKind::Vehicle) {
            continue;
        }
        VehicleState vs;
        vs.position = e.current.position;
        vs.yaw = e.current.yaw;
        vehicleBoxes(vs, vehicleSpec(static_cast<VehicleModel>(e.current.model)), out);
    }
}

void RemoteWorld::drawMarker(Renderer& renderer, const Vec3& position, MarkerKind kind, float time) const
{
    const MaterialId mat = markerMats_[std::min<int>(static_cast<int>(kind), 5)];
    const Mat4 base = Mat4::translation(position);
    // Objectives get a tall beam so they can be found from afar; places you
    // walk into get a ground ring and a floating diamond.
    if (kind == MarkerKind::Objective || kind == MarkerKind::Checkpoint) {
        renderer.drawMesh(beam_, base, mat);
    }
    renderer.drawMesh(ring_, base * Mat4::rotationY(time * 1.5f), mat);
    renderer.drawMesh(diamond_, base * Mat4::translation({0, 1.7f + 0.12f * std::sin(time * 2.5f), 0}) * Mat4::rotationY(time * 2.0f), mat);
}

void RemoteWorld::draw(Renderer& renderer, const VehicleVisuals& vehicles, float time, std::uint16_t hideVehicle) const
{
    for (const auto& [id, e] : entities_) {
        const EntityState& s = e.current;
        switch (s.kind) {
        case EntityKind::Character: {
            if (e.anim.joints.empty()) {
                break;
            }
            const bool police = (s.flags & EntityFlags::Police) != 0;
            const Mat4 world = Mat4::translation(s.position) * Mat4::rotationY(s.yaw);
            looks_.get(s.appearance, police).draw(renderer, world, e.anim.joints);
            break;
        }
        case EntityKind::Vehicle: {
            if (id == hideVehicle) {
                break;
            }
            VehicleState vs;
            vs.position = s.position;
            vs.yaw = s.yaw;
            vs.steer = s.steer;
            vs.wheelSpin = s.wheelSpin;
            vs.pitch = s.pitch;
            vs.roll = s.roll;
            vs.damage = s.damage;
            VehicleVisuals::DrawFlags flags;
            flags.sirens = (s.flags & EntityFlags::Sirens) != 0;
            flags.time = time;
            vehicles.draw(renderer, static_cast<VehicleModel>(s.model), s.paint, vs, flags);
            break;
        }
        case EntityKind::Marker:
            drawMarker(renderer, s.position, static_cast<MarkerKind>(s.subtype), time);
            break;
        case EntityKind::Pickup: {
            const Mat4 m = Mat4::translation(s.position + Vec3{0, 0.3f + 0.15f * std::sin(time * 2.5f), 0}) *
                           Mat4::rotationY(time * 1.8f);
            renderer.drawMesh(flamingo_, m, pickupMat_);
            break;
        }
        }
    }
}

} // namespace client
