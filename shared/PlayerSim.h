#pragma once

#include "Protocol.h"

#include "Physics/CharacterMotor.h"
#include "Physics/Vehicle.h"

#include <vector>

// The exact player movement rules, used by the server (authoritative) and by
// the client (prediction). Keeping one implementation is what makes
// client-side prediction agree with the server.
namespace av::proto {

inline constexpr float kWalkSpeed = 1.7f;
inline constexpr float kRunSpeed = 5.6f;
inline constexpr float kCrouchSpeed = 1.0f;
inline constexpr float kTurnRate = 10.0f; // rad/s
inline constexpr float kSwimSpeed = 2.2f, kSwimFastSpeed = 3.6f;
inline constexpr float kJetpackSpeed = 9.0f, kJetpackFastSpeed = 15.0f;

struct OnFootState {
    CharacterMotor motor;
    float yaw = 0.0f;
    float turnRate = 0.0f; // for animation (rad/s)
    bool jetpack = false;  // set by the server (pickup / cheat)
    bool swimming = false; // result of the last step
};

// True where the water is deep enough to swim.
bool deepWater(const CollisionWorld& world, const Vec3& feet);

void simulateOnFoot(OnFootState& s, const InputFrame& in, float dt, const CollisionWorld& world);

VehicleControls drivingControls(const InputFrame& in);
void simulateDriving(VehicleState& v, VehicleModel model, const InputFrame& in, float dt, const CollisionWorld& world,
                     const std::vector<AABB>* obstacles);

} // namespace av::proto
