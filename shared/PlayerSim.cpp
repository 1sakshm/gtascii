#include "PlayerSim.h"

#include "Interiors.h"

#include <algorithm>
#include <cmath>

namespace av::proto {

bool deepWater(const CollisionWorld& world, const Vec3& feet)
{
    const float water = world.waterLevel(feet.x, feet.z);
    return water > world.groundHeight(feet.x, feet.z) + 1.2f && feet.y < water - 0.9f;
}

void simulateOnFoot(OnFootState& s, const InputFrame& in, float dt, const CollisionWorld& world)
{
    Vec3 dir = in.move();
    const float amount = std::min(1.0f, length(dir));
    dir = normalize(dir);
    const bool crouch = in.has(Buttons::Crouch);
    const bool fast = in.has(Buttons::Run);
    const Vec3 feet = s.motor.position;
    s.swimming = false;
    if (s.jetpack) {
        float vy = in.has(Buttons::Ascend) ? 6.5f : (in.has(Buttons::Descend) ? -6.0f : -0.5f);
        if (feet.y > kFlightCeiling && vy > 0.0f) vy = 0.0f;
        const float speed = fast ? kJetpackFastSpeed : kJetpackSpeed;
        s.motor.stepFree(dir * (speed * amount) + Vec3{0.0f, vy, 0.0f}, 14.0f, dt, world);
    } else if (deepWater(world, feet)) {
        // Float with the head above the surface and paddle.
        s.swimming = true;
        const float surfaceFeet = world.waterLevel(feet.x, feet.z) - 1.35f;
        const float vy = std::clamp((surfaceFeet - feet.y) * 4.0f, -3.0f, 3.0f);
        const float speed = fast ? kSwimFastSpeed : kSwimSpeed;
        s.motor.stepFree(dir * (speed * amount) + Vec3{0.0f, vy, 0.0f}, 8.0f, dt, world);
    } else {
        const float speed = crouch ? kCrouchSpeed : (fast ? kRunSpeed : kWalkSpeed);
        s.motor.step(dir * (speed * amount), in.has(Buttons::Jump), crouch, dt, world);
    }

    s.turnRate = 0.0f;
    if (in.has(Buttons::Fire)) {
        s.yaw = in.aimYaw(); // face where you shoot
    } else if (amount > 0.05f) {
        const float target = std::atan2(dir.x, dir.z);
        const float delta = wrapAngle(target - s.yaw);
        const float maxTurn = kTurnRate * dt;
        const float turn = clampv(delta, -maxTurn, maxTurn);
        s.yaw = wrapAngle(s.yaw + turn);
        s.turnRate = dt > 0.0f ? turn / dt : 0.0f;
    }
}

VehicleControls drivingControls(const InputFrame& in)
{
    VehicleControls c;
    c.throttle = in.throttle / 127.0f;
    c.steer = in.steer / 127.0f;
    c.handbrake = in.has(Buttons::Handbrake);
    c.ascend = in.has(Buttons::Ascend);
    c.descend = in.has(Buttons::Descend);
    return c;
}

void simulateDriving(VehicleState& v, VehicleModel model, const InputFrame& in, float dt, const CollisionWorld& world,
                     const std::vector<AABB>* obstacles)
{
    stepVehicle(v, vehicleSpec(model), drivingControls(in), dt, world, obstacles);
}

} // namespace av::proto
