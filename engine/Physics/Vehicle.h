#pragma once

#include "Math/Mat.h"
#include "Physics/CollisionWorld.h"

#include <array>
#include <cstdint>
#include <vector>

namespace av {

enum class VehicleModel : std::uint8_t { Sedan, Coupe, Van, Cab, Cruiser, Sports, Helicopter, Count };

struct VehicleSpec {
    const char* name;
    float length, width, height;
    float wheelBase, wheelRadius;
    float maxSpeed, maxReverse;  // m/s
    float acceleration, braking; // m/s^2
    float maxSteer;              // radians
    int price;                   // dealership price ($)
    bool flies = false;          // helicopter: flight model instead of wheels
};

const VehicleSpec& vehicleSpec(VehicleModel model);

struct VehicleControls {
    float throttle = 0.0f; // -1 (brake / reverse) .. 1 (accelerate)
    float steer = 0.0f;    // -1 (right) .. 1 (left)
    bool handbrake = false;
    bool ascend = false, descend = false; // helicopter collective
};

// Kinematic bicycle-model car. `position` is the centre of the footprint at
// ground level; local +Z is forward, +X is the car's left (driver side).
struct VehicleState {
    Vec3 position;
    float yaw = 0.0f;
    float speed = 0.0f;     // signed, along the heading
    float steer = 0.0f;     // current front-wheel angle (radians)
    float wheelSpin = 0.0f; // radians
    float pitch = 0.0f, roll = 0.0f; // visual suspension tilt
    float damage = 0.0f;    // 0 (pristine) .. 1 (wrecked)
    float lastImpact = 0.0f; // largest impact speed in the last step (m/s)
    bool drowned = false;
    float verticalSpeed = 0.0f; // helicopter
    float rotor = 0.0f;         // helicopter rotor spin-up 0..1

    Vec3 forward() const { return {std::sin(yaw), 0.0f, std::cos(yaw)}; }
    Vec3 left() const { return {std::cos(yaw), 0.0f, -std::sin(yaw)}; }
    Vec3 velocity() const { return forward() * speed; }
};

// Advances one step: steering, engine/brakes/drag, heading, collision with
// static boxes (and `extraObstacles`), ground following, suspension tilt.
void stepVehicle(VehicleState& state, const VehicleSpec& spec, const VehicleControls& controls, float dt,
                 const CollisionWorld& world, const std::vector<AABB>* extraObstacles = nullptr);

// Three circles along the body used for collision (radius = width / 2).
std::array<Vec3, 3> vehicleCircles(const VehicleState& state, const VehicleSpec& spec);
// Axis-aligned boxes covering the body (for characters to collide with).
void vehicleBoxes(const VehicleState& state, const VehicleSpec& spec, std::vector<AABB>& out);

Mat4 vehicleMatrix(const VehicleState& state);
Vec3 vehicleSeatPosition(const VehicleState& state, const VehicleSpec& spec, int seat); // 0 = driver
Vec3 vehicleDoorPosition(const VehicleState& state, const VehicleSpec& spec, int seat);

} // namespace av
