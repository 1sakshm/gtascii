#pragma once

#include "PlayerSim.h"
#include "Protocol.h"

#include <deque>
#include <vector>

namespace client {

// The locally controlled player, predicted on the client so movement responds
// instantly. Every input frame is applied locally and remembered; when a
// snapshot acknowledges frame N, the predicted state is reset to the
// server's state at N and the frames after N are re-applied. Any difference
// becomes a visual offset that fades out instead of a visible snap.
class LocalPlayer {
public:
    void reset(const av::Vec3& feet, float yaw, const av::CollisionWorld& world);

    void predict(const av::proto::InputFrame& frame, const av::CollisionWorld& world, const std::vector<av::AABB>* obstacles);
    void reconcile(const av::proto::Snapshot& snapshot, av::VehicleModel vehicleModel, const av::CollisionWorld& world,
                   const std::vector<av::AABB>* obstacles);
    void updateVisual(float dt);

    av::proto::CharacterMode mode() const { return mode_; }
    bool driving() const { return mode_ == av::proto::CharacterMode::InVehicle && seat_ == 0 && vehicle_ != 0; }
    std::uint16_t vehicleNetId() const { return vehicle_; }
    av::VehicleModel vehicleModel() const { return vehicleModel_; }

    // Rendered (smoothed) state.
    av::Vec3 position() const;
    float yaw() const { return driving() ? car_.yaw : foot_.yaw; }
    av::Vec3 velocity() const { return driving() ? car_.velocity() : foot_.motor.velocity; }
    bool grounded() const { return foot_.motor.grounded(); }
    bool jetpack() const { return foot_.jetpack; }
    bool swimming() const { return foot_.swimming; }
    float turnRate() const { return foot_.turnRate; }
    float landingImpact() const { return landingImpact_; }
    const av::VehicleState& vehicle() const { return carVisual_; }

    // Prediction quality (for the HUD / debugging).
    float lastCorrection() const { return lastCorrection_; }
    std::size_t pendingFrames() const { return history_.size(); }

private:
    av::proto::OnFootState foot_;
    av::VehicleState car_;
    av::VehicleState carVisual_;
    av::VehicleModel vehicleModel_ = av::VehicleModel::Sedan;
    std::deque<av::proto::InputFrame> history_;
    av::proto::CharacterMode mode_ = av::proto::CharacterMode::OnFoot;
    std::uint16_t vehicle_ = 0;
    int seat_ = 0;
    av::Vec3 serverPosition_; // for modes the client does not predict
    av::Vec3 followPosition_;
    av::Vec3 correction_;     // visual offset left over from reconciliation
    float lastCorrection_ = 0.0f;
    bool synced_ = false; // first snapshot places us exactly (spawn slot)
    float landingImpact_ = 0.0f;
};

} // namespace client
