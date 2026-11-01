#include "Physics/CharacterMotor.h"

#include <algorithm>
#include <cmath>

namespace av {

float CharacterMotor::findGround(const CollisionWorld& world, float maxTop) const
{
    // Terrain, or the top of any box under the character's footprint that is
    // low enough to stand on.
    float ground = world.groundHeight(position.x, position.z);
    const float footprint = settings_.radius * 0.7f;
    for (const AABB& b : nearby_) {
        if (b.max.y > maxTop || b.max.y <= ground) {
            continue;
        }
        const float cx = clampv(position.x, b.min.x, b.max.x);
        const float cz = clampv(position.z, b.min.z, b.max.z);
        const float dx = position.x - cx, dz = position.z - cz;
        if (dx * dx + dz * dz <= footprint * footprint) {
            ground = b.max.y;
        }
    }
    return ground;
}

void CharacterMotor::resolveHorizontal(float height, Vec3& hv)
{
    const float r = settings_.radius;
    for (int iteration = 0; iteration < 3; ++iteration) {
        bool any = false;
        for (const AABB& b : nearby_) {
            // Ignore what we can step onto and what is entirely above our head.
            if (b.max.y <= position.y + settings_.stepHeight || b.min.y >= position.y + height) {
                continue;
            }
            const float cx = clampv(position.x, b.min.x, b.max.x);
            const float cz = clampv(position.z, b.min.z, b.max.z);
            float dx = position.x - cx, dz = position.z - cz;
            const float distSq = dx * dx + dz * dz;
            if (distSq >= r * r) {
                continue;
            }
            float nx, nz, push;
            if (distSq > 1e-10f) {
                const float dist = std::sqrt(distSq);
                nx = dx / dist;
                nz = dz / dist;
                push = r - dist;
            } else {
                // Centre inside the box: leave through the nearest side.
                const float left = position.x - b.min.x, right = b.max.x - position.x;
                const float back = position.z - b.min.z, front = b.max.z - position.z;
                const float m = std::min({left, right, back, front});
                nx = m == left ? -1.0f : (m == right ? 1.0f : 0.0f);
                nz = nx != 0.0f ? 0.0f : (m == back ? -1.0f : 1.0f);
                push = m + r;
            }
            position.x += nx * push;
            position.z += nz * push;
            const float into = hv.x * nx + hv.z * nz;
            if (into < 0.0f) {
                hv.x -= nx * into;
                hv.z -= nz * into;
            }
            any = true;
        }
        if (!any) {
            break;
        }
    }
}

void CharacterMotor::step(const Vec3& desired, bool jump, bool crouching, float dt, const CollisionWorld& world)
{
    landingImpact_ = 0.0f;
    if (dt <= 0.0f) {
        return;
    }
    const float height = crouching ? settings_.crouchHeight : settings_.height;

    // Accelerate towards the desired horizontal velocity.
    Vec3 hv{velocity.x, 0.0f, velocity.z};
    const Vec3 target{desired.x, 0.0f, desired.z};
    Vec3 diff = target - hv;
    const float maxDelta = (grounded_ ? settings_.groundAcceleration : settings_.airAcceleration) * dt;
    if (length(diff) > maxDelta) {
        diff = normalize(diff) * maxDelta;
    }
    hv += diff;

    if (jump && grounded_ && !crouching) {
        velocity.y = settings_.jumpSpeed;
        grounded_ = false;
    }
    if (!grounded_) {
        velocity.y -= settings_.gravity * dt;
    }

    const Vec3 move = hv * dt;
    const float moveLen = length(move);
    AABB region({position.x - settings_.radius - moveLen - 1.0f, position.y - 2.0f, position.z - settings_.radius - moveLen - 1.0f},
                {position.x + settings_.radius + moveLen + 1.0f, position.y + height + 2.0f, position.z + settings_.radius + moveLen + 1.0f});
    nearby_.clear();
    world.gatherColliders(region, nearby_);

    // Horizontal movement in sub-steps no longer than half the radius, so
    // fast movement cannot tunnel through thin colliders.
    const int steps = std::max(1, static_cast<int>(std::ceil(moveLen / (settings_.radius * 0.5f))));
    for (int i = 0; i < steps; ++i) {
        position.x += move.x / static_cast<float>(steps);
        position.z += move.z / static_cast<float>(steps);
        resolveHorizontal(height, hv);
    }

    // Vertical movement, ground and ceiling.
    const float previousY = position.y;
    position.y += velocity.y * dt;
    const float ground = findGround(world, std::max(previousY, position.y) + settings_.stepHeight);

    const bool wasGrounded = grounded_;
    if (position.y <= ground || (wasGrounded && velocity.y <= 0.0f && position.y - ground <= settings_.snapDown)) {
        if (!wasGrounded) {
            landingImpact_ = std::max(0.0f, -velocity.y);
        }
        position.y = ground;
        velocity.y = 0.0f;
        grounded_ = true;
    } else {
        grounded_ = false;
    }

    if (velocity.y > 0.0f) {
        for (const AABB& b : nearby_) {
            const float cx = clampv(position.x, b.min.x, b.max.x);
            const float cz = clampv(position.z, b.min.z, b.max.z);
            const float dx = position.x - cx, dz = position.z - cz;
            if (dx * dx + dz * dz < settings_.radius * settings_.radius && previousY + height <= b.min.y + 1e-3f &&
                position.y + height > b.min.y) {
                position.y = b.min.y - height;
                velocity.y = 0.0f;
            }
        }
    }

    velocity.x = hv.x;
    velocity.z = hv.z;
}

void CharacterMotor::teleport(const Vec3& feet, const CollisionWorld& world)
{
    position = feet;
    velocity = {};
    nearby_.clear();
    world.gatherColliders(AABB({feet.x - 1, feet.y - 50, feet.z - 1}, {feet.x + 1, feet.y + 50, feet.z + 1}), nearby_);
    position.y = findGround(world, feet.y + settings_.stepHeight);
    grounded_ = true;
}

} // namespace av
