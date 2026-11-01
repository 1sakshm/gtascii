#pragma once

#include "Physics/CollisionWorld.h"

#include <vector>

namespace av {

// Static world plus a per-frame list of moving obstacles (vehicle bodies),
// so characters collide with cars through the normal CollisionWorld path.
class DynamicCollisionWorld : public CollisionWorld {
public:
    explicit DynamicCollisionWorld(const CollisionWorld& base) : base_(base) {}

    std::vector<AABB> dynamicBoxes;

    float groundHeight(float x, float z) const override { return base_.groundHeight(x, z); }
    void gatherColliders(const AABB& region, std::vector<AABB>& out) const override
    {
        base_.gatherColliders(region, out);
        for (const AABB& b : dynamicBoxes) {
            if (b.intersects(region)) {
                out.push_back(b);
            }
        }
    }

private:
    const CollisionWorld& base_;
};

} // namespace av
