#pragma once

#include "Math/Mat.h"
#include "Math/Quat.h"

#include <string>
#include <vector>

namespace av {

struct Joint {
    std::string name;
    int parent = -1;  // -1 for the root
    Vec3 offset;      // bind-pose translation from the parent joint
};

// Local joint rotations plus a translation offset for one joint (used for
// pelvis bob/crouch). Index-aligned with Skeleton joints.
struct Pose {
    std::vector<Quat> rotations;
    int offsetJoint = -1;
    Vec3 offset;
};

// Joint hierarchy. Parents must be added before their children, which lets
// global transforms be computed in a single forward pass.
class Skeleton {
public:
    int addJoint(const std::string& name, int parent, const Vec3& offset);

    std::size_t size() const noexcept { return joints_.size(); }
    const Joint& joint(std::size_t i) const { return joints_[i]; }
    int find(const std::string& name) const; // -1 if missing

    Pose bindPose() const;

    // Joint-to-model matrices: global[i] = global[parent] * T(offset) * R(rotation).
    void computeGlobal(const Pose& pose, std::vector<Mat4>& global) const;

private:
    std::vector<Joint> joints_;
};

} // namespace av
