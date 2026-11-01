#include "Physics/CollisionWorld.h"

#include <algorithm>
#include <cmath>

namespace av {

float CollisionWorld::raycast(const Ray& ray, float maxDistance) const
{
    float best = maxDistance;

    AABB region;
    region.expand(ray.origin);
    region.expand(ray.at(maxDistance));
    std::vector<AABB> boxes;
    gatherColliders(region, boxes);
    for (const AABB& b : boxes) {
        const float t = intersectRayAABB(ray, b, best);
        if (t >= 0.0f && t < best) {
            best = t;
        }
    }

    // Terrain: march in small steps (the height field is not a mesh).
    constexpr float kStep = 0.25f;
    for (float t = 0.0f; t < best; t += kStep) {
        const Vec3 p = ray.at(t);
        if (p.y < groundHeight(p.x, p.z)) {
            best = std::max(0.0f, t - kStep);
            break;
        }
    }
    return best;
}

void BoxCollisionWorld::gatherColliders(const AABB& region, std::vector<AABB>& out) const
{
    for (const AABB& b : boxes) {
        if (b.intersects(region)) {
            out.push_back(b);
        }
    }
}

} // namespace av
