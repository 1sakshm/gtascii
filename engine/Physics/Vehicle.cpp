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
};

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
