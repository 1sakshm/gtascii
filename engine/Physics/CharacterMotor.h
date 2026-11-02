#pragma once

#include "Physics/CollisionWorld.h"

#include <vector>

namespace av {

struct CharacterMotorSettings {
    float radius = 0.30f;
    float height = 1.80f;
    float crouchHeight = 1.20f;
    float stepHeight = 0.40f;   // climbs curbs and low steps without jumping
    float snapDown = 0.35f;     // stays glued to the ground when walking down steps
    float gravity = 22.0f;
    float jumpSpeed = 7.0f;     // ~1.1 m jump
    float groundAcceleration = 30.0f;
    float airAcceleration = 6.0f;
};

// Kinematic character body: a vertical capsule approximated as a cylinder
// for collision against axis-aligned boxes, with gravity, jumping, step-up
// and ground snapping. `position` is the feet.
class CharacterMotor {
public:
    explicit CharacterMotor(CharacterMotorSettings settings = {}) : settings_(settings) {}

    Vec3 position;
    Vec3 velocity;

    // desiredVelocity: horizontal target velocity (y ignored).
    void step(const Vec3& desiredVelocity, bool jump, bool crouching, float dt, const CollisionWorld& world);

    bool grounded() const noexcept { return grounded_; }
    // Downward speed at touchdown on the last step that landed (0 otherwise).
    float landingImpact() const noexcept { return landingImpact_; }
    const CharacterMotorSettings& settings() const noexcept { return settings_; }

    // Moves without gravity towards a 3D velocity (swimming, jetpack flight).
    // Walls still block; the ground stops a descent. `responsiveness` is the
    // acceleration in m/s^2.
    void stepFree(const Vec3& desiredVelocity, float responsiveness, float dt, const CollisionWorld& world);

    // Overwrites the full state (network reconciliation).
    void setState(const Vec3& feet, const Vec3& vel, bool grounded)
    {
        position = feet;
        velocity = vel;
        grounded_ = grounded;
    }

    // Places the character on the ground at (x, z).
    void teleport(const Vec3& feet, const CollisionWorld& world);

private:
    float findGround(const CollisionWorld& world, float maxTop) const;
    void resolveHorizontal(float height, Vec3& horizontalVelocity);

    CharacterMotorSettings settings_;
    bool grounded_ = false;
    float landingImpact_ = 0.0f;
    std::vector<AABB> nearby_; // scratch
};

} // namespace av
