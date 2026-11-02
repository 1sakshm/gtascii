#include "Physics/Vehicle.h"

#include <algorithm>
#include <cmath>

namespace av {

namespace {
// Name, length, width, height, wheelbase, wheel radius, max speed, max reverse,
// acceleration, braking, max steer, price.
const VehicleSpec kSpecs[] = {
    {"Sedan", 4.6f, 1.9f, 1.45f, 2.7f, 0.36f, 30.0f, 7.0f, 6.5f, 13.0f, 0.60f, 1500},
    {"Neon Coupe", 4.3f, 1.85f, 1.25f, 2.5f, 0.34f, 40.0f, 7.0f, 9.5f, 15.0f, 0.62f, 4500},
    {"Van", 5.0f, 2.0f, 2.1f, 3.0f, 0.38f, 24.0f, 6.0f, 4.8f, 11.0f, 0.55f, 2200},
    {"Cab", 4.7f, 1.9f, 1.5f, 2.75f, 0.36f, 30.0f, 7.0f, 6.5f, 13.0f, 0.60f, 1800},
    {"Cruiser", 4.8f, 1.95f, 1.5f, 2.8f, 0.37f, 38.0f, 8.0f, 9.0f, 16.0f, 0.62f, 999999},
    {"Vortex GT", 4.4f, 1.95f, 1.15f, 2.6f, 0.35f, 52.0f, 8.0f, 12.5f, 19.0f, 0.60f, 12000},
    {"Skyhawk", 8.6f, 2.2f, 2.8f, 3.0f, 0.15f, 38.0f, 10.0f, 7.0f, 9.0f, 0.0f, 60000, true},
};

// Helicopter: yaw with steer, forward/back with throttle, ascend/descend.
// Lands on the ground, on water and on roofs; walls stop it.
void stepHelicopter(VehicleState& s, const VehicleSpec& spec, const VehicleControls& in, float dt, const CollisionWorld& world)
{
    const bool wrecked = s.damage >= 1.0f;
    const bool active = !wrecked && (in.throttle != 0.0f || in.steer != 0.0f || in.ascend || in.descend);
    const float floorY = std::max(world.groundHeight(s.position.x, s.position.z), world.waterLevel(s.position.x, s.position.z));
    std::vector<AABB> boxes;
    const float r = spec.width;
    world.gatherColliders(AABB({s.position.x - 6.0f, s.position.y - 3.0f, s.position.z - 6.0f},
                               {s.position.x + 6.0f, s.position.y + spec.height + 1.0f, s.position.z + 6.0f}),
                          boxes);
    float ground = floorY;
    for (const AABB& b : boxes) {
        if (s.position.x > b.min.x && s.position.x < b.max.x && s.position.z > b.min.z && s.position.z < b.max.z &&
            b.max.y <= s.position.y + 0.6f) {
            ground = std::max(ground, b.max.y); // a roof under us
        }
    }
    const bool airborne = s.position.y > ground + 0.05f;
    s.rotor += clampv((active || airborne ? 1.0f : 0.0f) - s.rotor, -0.35f * dt, 0.5f * dt);
    s.rotor = wrecked ? std::max(0.0f, s.rotor - dt) : s.rotor;
    const float lift = s.rotor >= 0.75f ? 1.0f : 0.0f;

    s.yaw = wrapAngle(s.yaw + clampv(in.steer, -1.0f, 1.0f) * 1.5f * dt * (airborne ? 1.0f : 0.3f));
    const float targetSpeed = airborne ? (in.throttle >= 0.0f ? in.throttle * spec.maxSpeed : in.throttle * spec.maxReverse) : 0.0f;
    s.speed += clampv(targetSpeed - s.speed, -spec.braking * dt, spec.acceleration * dt);
    float targetVs = in.ascend ? 6.0f : (in.descend ? -5.0f : 0.0f);
    if (lift < 1.0f) targetVs = -12.0f; // not spun up (or wrecked): it falls
    if (s.position.y > 140.0f) targetVs = std::min(targetVs, 0.0f);
    s.verticalSpeed += clampv(targetVs - s.verticalSpeed, -9.0f * dt, 7.0f * dt);

    Vec3 next = s.position + s.forward() * (s.speed * dt);
    next.y += s.verticalSpeed * dt;
    // Walls: a box taller than our skids that overlaps the body stops us.
    for (const AABB& b : boxes) {
        if (b.max.y <= next.y + 0.4f || b.min.y >= next.y + spec.height) continue;
        const float cx = clampv(next.x, b.min.x, b.max.x), cz = clampv(next.z, b.min.z, b.max.z);
        const float dx = next.x - cx, dz = next.z - cz;
        if (dx * dx + dz * dz < r * r) {
            next.x = s.position.x;
            next.z = s.position.z;
            s.lastImpact = std::max(s.lastImpact, std::fabs(s.speed));
            s.damage = std::min(1.0f, s.damage + std::fabs(s.speed) * 0.01f);
            s.speed *= -0.3f;
            break;
        }
    }
    s.position = next;
    const float newFloor = std::max(ground, std::max(world.groundHeight(next.x, next.z), world.waterLevel(next.x, next.z)));
    if (s.position.y <= newFloor) {
        if (s.verticalSpeed < -9.0f) s.damage = std::min(1.0f, s.damage + (-s.verticalSpeed - 9.0f) * 0.08f);
        s.position.y = newFloor;
        s.verticalSpeed = std::max(0.0f, s.verticalSpeed);
        s.speed *= std::exp(-4.0f * dt);
    }
    s.pitch = clampv(-s.speed / spec.maxSpeed * 0.25f, -0.25f, 0.25f) * (airborne ? 1.0f : 0.0f);
    s.roll = clampv(in.steer * 0.12f, -0.2f, 0.2f) * (airborne ? 1.0f : 0.0f);
    s.steer = 0.0f;
    s.wheelSpin = std::fmod(s.wheelSpin + s.rotor * 35.0f * dt, 6.2831853f);
}

float sign(float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }
} // namespace

const VehicleSpec& vehicleSpec(VehicleModel model)
{
    const auto i = static_cast<std::size_t>(model);
    return kSpecs[i < std::size(kSpecs) ? i : 0];
}

std::array<Vec3, 3> vehicleCircles(const VehicleState& s, const VehicleSpec& spec)
{
    const Vec3 f = s.forward();
    const float offset = (spec.length - spec.width) * 0.5f;
    return {s.position + f * offset, s.position, s.position - f * offset};
}

void vehicleBoxes(const VehicleState& s, const VehicleSpec& spec, std::vector<AABB>& out)
{
    const float r = spec.width * 0.5f;
    for (const Vec3& c : vehicleCircles(s, spec)) {
        out.push_back(AABB({c.x - r, s.position.y, c.z - r}, {c.x + r, s.position.y + spec.height, c.z + r}));
    }
}

Mat4 vehicleMatrix(const VehicleState& s)
{
    return Mat4::translation(s.position) * Mat4::rotationY(s.yaw) * Mat4::rotationX(s.pitch) * Mat4::rotationZ(s.roll);
}

Vec3 vehicleSeatPosition(const VehicleState& s, const VehicleSpec& spec, int seat)
{
    const float side = (seat == 0 || seat == 2) ? 0.42f : -0.42f;
    const float back = seat >= 2 ? -0.9f : 0.0f;
    (void)spec;
    return s.position + s.left() * side + s.forward() * (back - 0.15f) + Vec3{0, 0.05f, 0};
}

Vec3 vehicleDoorPosition(const VehicleState& s, const VehicleSpec& spec, int seat)
{
    const float side = (seat == 0 || seat == 2) ? 1.0f : -1.0f;
    return s.position + s.left() * (side * (spec.width * 0.5f + 0.65f)) + s.forward() * (seat >= 2 ? -0.9f : -0.15f);
}

void stepVehicle(VehicleState& s, const VehicleSpec& spec, const VehicleControls& in, float dt, const CollisionWorld& world,
                 const std::vector<AABB>* extra)
{
    s.lastImpact = 0.0f;
    if (dt <= 0.0f) {
        return;
    }
    if (spec.flies) {
        stepHelicopter(s, spec, in, dt, world);
        return;
    }
    const float wreck = s.damage >= 1.0f || s.drowned ? 0.0f : 1.0f;
    const float throttle = clampv(in.throttle, -1.0f, 1.0f) * wreck;

    // Steering: slower and narrower at speed.
    const float steerLimit = spec.maxSteer / (1.0f + std::fabs(s.speed) * 0.05f);
    const float targetSteer = clampv(in.steer, -1.0f, 1.0f) * steerLimit;
    const float steerStep = 2.8f * dt;
    s.steer += clampv(targetSteer - s.steer, -steerStep, steerStep);

    // Longitudinal forces.
    float accel = 0.0f;
    if (throttle > 0.0f) {
        accel = s.speed < -0.3f ? spec.braking * throttle
                                : spec.acceleration * throttle * std::max(0.0f, 1.0f - s.speed / spec.maxSpeed);
    } else if (throttle < 0.0f) {
        accel = s.speed > 0.3f ? -spec.braking * -throttle
                               : -spec.acceleration * 0.6f * -throttle * (s.speed > -spec.maxReverse ? 1.0f : 0.0f);
    }
    if (in.handbrake) {
        accel -= sign(s.speed) * spec.braking * 1.2f;
    }
    const float resistance = sign(s.speed) * (0.5f + 0.004f * s.speed * s.speed);
    const float before = s.speed;
    s.speed += (accel - resistance) * dt;
    // Friction alone never reverses the car.
    if (throttle == 0.0f && (before * s.speed < 0.0f || std::fabs(s.speed) < 0.15f)) {
        s.speed = 0.0f;
    }
    if (s.drowned) {
        s.speed *= std::exp(-3.0f * dt);
    }

    const float yawRate = s.speed * std::tan(s.steer) / spec.wheelBase;
    s.yaw = wrapAngle(s.yaw + yawRate * dt);
    s.position += s.forward() * (s.speed * dt);

    // Collision: body circles against solid boxes (ignoring curbs and other
    // low geometry the wheels roll over).
    const float r = spec.width * 0.5f;
    std::vector<AABB> boxes;
    const float reach = spec.length * 0.5f + 1.0f;
    const AABB region({s.position.x - reach, s.position.y - 1.0f, s.position.z - reach},
                      {s.position.x + reach, s.position.y + spec.height + 1.0f, s.position.z + reach});
    world.gatherColliders(region, boxes);
    if (extra) {
        for (const AABB& b : *extra) {
            if (b.intersects(region)) {
                boxes.push_back(b);
            }
        }
    }
    for (int iteration = 0; iteration < 2; ++iteration) {
        for (const Vec3& c : vehicleCircles(s, spec)) {
            for (const AABB& b : boxes) {
                if (b.max.y <= s.position.y + 0.45f || b.min.y >= s.position.y + spec.height) {
                    continue;
                }
                const float cx = clampv(c.x, b.min.x, b.max.x);
                const float cz = clampv(c.z, b.min.z, b.max.z);
                const float dx = c.x - cx, dz = c.z - cz;
                const float d2 = dx * dx + dz * dz;
                if (d2 >= r * r) {
                    continue;
                }
                Vec3 n;
                float push;
                if (d2 > 1e-8f) {
                    const float d = std::sqrt(d2);
                    n = {dx / d, 0, dz / d};
                    push = r - d;
                } else {
                    n = -s.forward();
                    push = r;
                }
                s.position += n * push;
                Vec3 v = s.velocity();
                const float vn = dot(v, n);
                if (vn < 0.0f) {
                    v -= n * (vn * 1.25f);
                    s.lastImpact = std::max(s.lastImpact, -vn);
                    s.damage = std::min(1.0f, s.damage + std::max(0.0f, -vn - 3.0f) * 0.012f);
                    s.speed = dot(v, s.forward()) * 0.85f;
                }
            }
        }
    }

    // Ground following from the four wheel contact points.
    const Vec3 f = s.forward(), l = s.left();
    const float hb = spec.wheelBase * 0.5f, hw = spec.width * 0.45f;
    auto h = [&world](const Vec3& p) { return world.groundHeight(p.x, p.z); };
    const float fl = h(s.position + f * hb + l * hw), fr = h(s.position + f * hb - l * hw);
    const float rl = h(s.position - f * hb + l * hw), rr = h(s.position - f * hb - l * hw);
    s.position.y = (fl + fr + rl + rr) * 0.25f;
    if (h(s.position) < -0.6f) {
        s.drowned = true;
        s.damage = 1.0f;
    }

    // Suspension: terrain slope plus weight transfer from acceleration and cornering.
    const float slopePitch = std::atan2((rl + rr) - (fl + fr), 2.0f * spec.wheelBase);
    // Positive roll tilts the roof towards -X (right): lean towards lower wheels.
    const float slopeRoll = std::atan2((fl + rl) - (fr + rr), 2.0f * spec.width);
    const float targetPitch = slopePitch + clampv(-(s.speed - before) / dt * 0.012f, -0.08f, 0.08f);
    const float targetRoll = slopeRoll + clampv(s.speed * yawRate * 0.012f, -0.10f, 0.10f);
    s.pitch += (targetPitch - s.pitch) * damp(9.0f, dt);
    s.roll += (targetRoll - s.roll) * damp(9.0f, dt);
    s.wheelSpin = std::fmod(s.wheelSpin + s.speed / spec.wheelRadius * dt, kTwoPi);
}

} // namespace av
