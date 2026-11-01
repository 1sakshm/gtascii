#pragma once

#include "Protocol.h"

#include "Animation/Humanoid.h"
#include "Animation/HumanoidAnimator.h"
#include "Entities/VehicleVisual.h"
#include "Graphics/Material.h"
#include "Graphics/Mesh.h"

#include <deque>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace av {
class Renderer;
}

namespace client {

// Visual for a character appearance seed (materials registered once).
class CharacterLooks {
public:
    explicit CharacterLooks(av::MaterialLibrary& materials) : materials_(materials) {}
    const av::HumanoidVisual& get(std::uint32_t seed, bool police);

private:
    av::MaterialLibrary& materials_;
    std::map<std::uint64_t, std::unique_ptr<av::HumanoidVisual>> cache_;
};

// Animates one character from replicated (or predicted) state.
struct CharacterAnimation {
    av::HumanoidAnimator animator;
    std::vector<av::Mat4> joints;
    std::uint8_t lastAction = 0;
    bool seenAction = false;
    float lastYaw = 0.0f;

    void update(const av::proto::EntityState& s, float dt);
};

// Everything the server replicates that is not the local player: other
// players, pedestrians, vehicles, pickups and markers. Snapshots are
// buffered and rendered ~100 ms in the past so motion is interpolated
// between two known states instead of jumping at 20 Hz.
class RemoteWorld {
public:
    struct Entity {
        std::deque<std::pair<double, av::proto::EntityState>> samples; // (server time, state)
        av::proto::EntityState current;
        double lastSeen = 0.0;
        CharacterAnimation anim;
    };

    RemoteWorld(av::MaterialLibrary& materials, CharacterLooks& looks);

    void ingest(const av::proto::Snapshot& snapshot, double localTime);
    void update(double localTime, float dt);
    // `hideVehicle`: the vehicle the local player is driving (drawn from prediction).
    void draw(av::Renderer& renderer, const av::VehicleVisuals& vehicles, float time, std::uint16_t hideVehicle) const;

    void drawMarker(av::Renderer& renderer, const av::Vec3& position, av::proto::MarkerKind kind, float time) const;
    const Entity* find(std::uint16_t netId) const;
    const std::unordered_map<std::uint16_t, Entity>& entities() const { return entities_; }
    // Vehicle bodies as collision boxes, for client-side prediction.
    void vehicleObstacles(std::vector<av::AABB>& out, std::uint16_t exclude) const;

    float interpolationDelay = 0.1f;
    float serverTime(double localTime) const { return static_cast<float>(localTime + clockOffset_); }
    float latestHour() const { return hour_; }

private:
    av::MaterialLibrary& materials_;
    CharacterLooks& looks_;
    std::unordered_map<std::uint16_t, Entity> entities_;
    double clockOffset_ = 0.0;
    bool clockValid_ = false;
    float hour_ = 12.0f;
    av::Mesh beam_, ring_, diamond_, flamingo_;
    av::MaterialId markerMats_[6] = {};
    av::MaterialId pickupMat_ = av::kNoMaterial;
};

} // namespace client
