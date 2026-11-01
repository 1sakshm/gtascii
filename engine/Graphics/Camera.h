#pragma once

#include "Math/Mat.h"

namespace av {

struct Camera {
    Vec3 position{0, 2, 5};
    Vec3 target{0, 1, 0};
    Vec3 up{0, 1, 0};
    float fovY = radians(60.0f);
    float zNear = 0.1f;
    float zFar = 500.0f;

    Mat4 view() const { return Mat4::lookAt(position, target, up); }
    Mat4 projection(float aspect) const { return Mat4::perspective(fovY, aspect, zNear, zFar); }
    Vec3 forward() const { return normalize(target - position); }
};

} // namespace av
