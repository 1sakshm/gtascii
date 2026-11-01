#include "Gameplay/ThirdPersonCamera.h"

#include <cmath>

namespace av {

ThirdPersonCamera::ThirdPersonCamera(ThirdPersonCameraSettings settings)
    : settings_(settings)
    , targetDistance_(settings.distance)
    , currentDistance_(settings.distance)
{
}

void ThirdPersonCamera::rotate(float deltaYaw, float deltaPitch)
{
    yaw_ = wrapAngle(yaw_ + deltaYaw);
    pitch_ = clampv(pitch_ + deltaPitch, settings_.minPitch, settings_.maxPitch);
    if (deltaYaw != 0.0f || deltaPitch != 0.0f) {
        recentering_ = false;
        idleTime_ = 0.0f;
    }
}

void ThirdPersonCamera::zoom(float delta)
{
    targetDistance_ = clampv(targetDistance_ + delta, settings_.minDistance, settings_.maxDistance);
}

Vec3 ThirdPersonCamera::viewDirection() const
{
    return {std::sin(yaw_) * std::cos(pitch_), -std::sin(pitch_), std::cos(yaw_) * std::cos(pitch_)};
}

Vec3 ThirdPersonCamera::forwardXZ() const { return {std::sin(yaw_), 0.0f, std::cos(yaw_)}; }
Vec3 ThirdPersonCamera::rightXZ() const { return {-std::cos(yaw_), 0.0f, std::sin(yaw_)}; }

void ThirdPersonCamera::snapTo(const Vec3& feet, float characterYaw)
{
    yaw_ = characterYaw;
    pivot_ = feet + Vec3{0, settings_.pivotHeight, 0};
    currentDistance_ = targetDistance_;
    position_ = pivot_ - viewDirection() * currentDistance_;
    initialised_ = true;
}

void ThirdPersonCamera::update(float dt, const Vec3& feet, float characterYaw, bool moving, const CollisionWorld* world)
{
    if (!initialised_) {
        snapTo(feet, characterYaw);
    }

    // Recenter: explicitly requested, or automatically after the player has
    // been moving for a while without touching the camera.
    idleTime_ += dt;
    if (moving && settings_.autoRecenterDelay > 0.0f && idleTime_ > settings_.autoRecenterDelay) {
        recentering_ = true;
    }
    if (recentering_) {
        const float delta = wrapAngle(characterYaw - yaw_);
        yaw_ = wrapAngle(yaw_ + delta * damp(settings_.recenterRate, dt));
        if (std::fabs(delta) < 0.01f && !moving) {
            recentering_ = false;
        }
    }

    const Vec3 targetPivot = feet + Vec3{0, vehicleMode_ ? 1.9f : settings_.pivotHeight, 0};
    pivot_ = lerp(pivot_, targetPivot, damp(settings_.followRate, dt));
    // Never lag so far that the character leaves the frame.
    if (distance(pivot_, targetPivot) > 2.0f) {
        pivot_ = targetPivot + normalize(pivot_ - targetPivot) * 2.0f;
    }

    const Vec3 back = -viewDirection();
    const float wanted = vehicleMode_ ? std::max(targetDistance_, 9.0f) : targetDistance_;
    float allowed = wanted;
    if (world != nullptr) {
        const float hit = world->raycast({pivot_, back}, wanted + settings_.collisionPadding);
        allowed = std::max(0.4f, std::min(wanted, hit - settings_.collisionPadding));
    }
    // Pull in instantly (never show a wall in front of the player); ease out.
    if (allowed < currentDistance_) {
        currentDistance_ = allowed;
    } else {
        currentDistance_ += (allowed - currentDistance_) * damp(settings_.zoomOutRate, dt);
    }

    position_ = pivot_ + back * currentDistance_;
    if (world != nullptr) {
        const float ground = world->groundHeight(position_.x, position_.z) + 0.25f;
        position_.y = std::max(position_.y, ground);
    }
}

Camera ThirdPersonCamera::camera() const
{
    Camera c;
    c.position = position_;
    c.target = pivot_;
    c.fovY = settings_.fovY;
    c.zNear = 0.08f;
    c.zFar = 400.0f;
    return c;
}

} // namespace av
