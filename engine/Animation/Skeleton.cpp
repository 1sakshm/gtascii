#include "Animation/Skeleton.h"

#include <stdexcept>

namespace av {

int Skeleton::addJoint(const std::string& name, int parent, const Vec3& offset)
{
    if (parent >= static_cast<int>(joints_.size())) {
        throw std::invalid_argument("joint '" + name + "': parent must be added first");
    }
    if (parent < 0 && !joints_.empty()) {
        throw std::invalid_argument("joint '" + name + "': skeleton already has a root");
    }
    joints_.push_back({name, parent, offset});
    return static_cast<int>(joints_.size() - 1);
}

int Skeleton::find(const std::string& name) const
{
    for (std::size_t i = 0; i < joints_.size(); ++i) {
        if (joints_[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Pose Skeleton::bindPose() const
{
    Pose p;
    p.rotations.assign(joints_.size(), Quat::identity());
    return p;
}

void Skeleton::computeGlobal(const Pose& pose, std::vector<Mat4>& global) const
{
    global.resize(joints_.size());
    for (std::size_t i = 0; i < joints_.size(); ++i) {
        const Joint& j = joints_[i];
        Vec3 t = j.offset;
        if (static_cast<int>(i) == pose.offsetJoint) {
            t += pose.offset;
        }
        const Quat r = i < pose.rotations.size() ? pose.rotations[i] : Quat::identity();
        const Mat4 local = Mat4::translation(t) * r.toMat4();
        global[i] = j.parent < 0 ? local : global[static_cast<std::size_t>(j.parent)] * local;
    }
}

} // namespace av
