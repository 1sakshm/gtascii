#pragma once

#include "Math/Geometry.h"

#include <vector>

namespace av {

// Static collision geometry as seen by physics: a ground height field plus
// solid axis-aligned boxes. The world module implements this over streamed
// chunks; physics code depends only on this interface.
class CollisionWorld {
public:
    virtual ~CollisionWorld() = default;

    // Terrain surface height (not including boxes) at a world position.
    virtual float groundHeight(float x, float z) const = 0;

    // Water surface height at a world position, or a very low value where
    // there is no water (swimming, drowning).
    virtual float waterLevel(float x, float z) const
    {
        (void)x;
        (void)z;
        return -1.0e9f;
    }

    // Appends every solid box that may intersect `region`.
    virtual void gatherColliders(const AABB& region, std::vector<AABB>& out) const = 0;

    // Nearest hit distance along the ray against boxes and terrain, or
    // maxDistance if nothing is hit.
    float raycast(const Ray& ray, float maxDistance) const;
};

// Simple in-memory implementation: flat ground plus a list of boxes.
class BoxCollisionWorld : public CollisionWorld {
public:
    float groundY = 0.0f;
    std::vector<AABB> boxes;

    float groundHeight(float, float) const override { return groundY; }
    void gatherColliders(const AABB& region, std::vector<AABB>& out) const override;
};

} // namespace av
