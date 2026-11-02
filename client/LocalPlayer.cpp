#include "LocalPlayer.h"

#include <cmath>

namespace client {

using namespace av;
using namespace av::proto;

void LocalPlayer::reset(const Vec3& feet, float yaw, const CollisionWorld& world)
{
    foot_.motor.teleport(feet, world);
    foot_.yaw = yaw;
    serverPosition_ = followPosition_ = feet;
    history_.clear();
    correction_ = {};
}

void LocalPlayer::predict(const InputFrame& frame, const CollisionWorld& world, const std::vector<AABB>* obstacles)
{
    history_.push_back(frame);
    while (history_.size() > 240) {
        history_.pop_front(); // 4 s without acknowledgement: something is badly wrong; stop growing
    }
    if (mode_ == CharacterMode::OnFoot) {
        simulateOnFoot(foot_, frame, kInputStep, world);
        landingImpact_ = std::max(landingImpact_, foot_.motor.landingImpact());
    } else if (driving()) {
        simulateDriving(car_, vehicleModel_, frame, kInputStep, world, obstacles);
    }
}

void LocalPlayer::reconcile(const Snapshot& snap, VehicleModel vehicleModel, const CollisionWorld& world,
                            const std::vector<AABB>* obstacles)
{
    while (!history_.empty() && history_.front().seq <= snap.ackInput) {
        history_.pop_front();
    }
    const SelfState& s = snap.self;
    const CharacterMode previous = mode_;
    const bool wasDriving = driving();
    mode_ = s.mode;
    vehicle_ = s.vehicle;
    seat_ = s.seat;
    vehicleModel_ = vehicleModel;
    serverPosition_ = s.position;

    if (mode_ == CharacterMode::OnFoot) {
        const Vec3 before = position();
        foot_.motor.setState(s.position, s.velocity, s.grounded);
        foot_.jetpack = s.jetpack;
        if (previous != CharacterMode::OnFoot) {
            foot_.yaw = s.yaw; // just got out / up: adopt the server's facing
        }
        for (const InputFrame& f : history_) {
            simulateOnFoot(foot_, f, kInputStep, world);
        }
        const Vec3 after = foot_.motor.position;
        if (previous == CharacterMode::OnFoot) {
            // `before` already includes the old offset: keep exactly what was on
            // screen and let the remaining error fade out.
            correction_ = before - after;
        } else {
            correction_ = {};
        }
        lastCorrection_ = synced_ ? length(before - after) : 0.0f;
        if (!synced_ || lastCorrection_ > 4.0f) {
            correction_ = {}; // teleport (respawn): don't smooth across the map
        }
        synced_ = true;
    } else if (driving()) {
        synced_ = true;
        const Vec3 before = car_.position;
        car_.position = s.vehiclePosition;
        car_.yaw = s.vehicleYaw;
        car_.speed = s.vehicleSpeed;
        car_.steer = s.vehicleSteer;
        car_.damage = s.vehicleDamage;
        car_.verticalSpeed = s.vehicleVerticalSpeed;
        car_.rotor = s.vehicleRotor;
        for (const InputFrame& f : history_) {
            simulateDriving(car_, vehicleModel_, f, kInputStep, world, obstacles);
        }
        if (wasDriving) {
            correction_ = before - car_.position;
            lastCorrection_ = length(correction_);
            if (lastCorrection_ > 6.0f) {
                correction_ = {};
            }
        } else {
            correction_ = {};
            carVisual_ = car_;
        }
    }
}

void LocalPlayer::updateVisual(float dt)
{
    correction_ *= std::exp(-10.0f * dt);
    followPosition_ = lerp(followPosition_, serverPosition_, damp(14.0f, dt));
    if (distance(followPosition_, serverPosition_) > 5.0f) {
        followPosition_ = serverPosition_;
    }
    if (driving()) {
        carVisual_ = car_;
        carVisual_.position += correction_;
    }
    landingImpact_ = 0.0f;
}

Vec3 LocalPlayer::position() const
{
    if (mode_ == CharacterMode::OnFoot) {
        return foot_.motor.position + correction_;
    }
    if (driving()) {
        return carVisual_.position;
    }
    return followPosition_;
}

} // namespace client
