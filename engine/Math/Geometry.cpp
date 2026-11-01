#include "Math/Geometry.h"

#include <algorithm>
#include <cmath>

namespace av {

AABB AABB::transformed(const Mat4& m) const
{
    if (!valid()) {
        return *this;
    }
    // Arvo's method: per-axis min/max of the rotated extents.
    const Vec3 t = m.translationPart();
    AABB out(t, t);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const float a = m.at(row, col) * min[col];
            const float b = m.at(row, col) * max[col];
            out.min[row] += std::min(a, b);
            out.max[row] += std::max(a, b);
        }
    }
    return out;
}

float intersectRayAABB(const Ray& ray, const AABB& box, float maxT)
{
    float tMin = 0.0f;
    float tMax = maxT;
    for (int axis = 0; axis < 3; ++axis) {
        const float o = ray.origin[axis];
        const float d = ray.direction[axis];
        if (std::fabs(d) < 1e-12f) {
            if (o < box.min[axis] || o > box.max[axis]) {
                return -1.0f;
            }
            continue;
        }
        const float inv = 1.0f / d;
        float t0 = (box.min[axis] - o) * inv;
        float t1 = (box.max[axis] - o) * inv;
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMin > tMax) {
            return -1.0f;
        }
    }
    return tMin;
}

float intersectRayPlane(const Ray& ray, const Plane& plane)
{
    const float denom = dot(plane.normal, ray.direction);
    if (std::fabs(denom) < 1e-12f) {
        return -1.0f;
    }
    const float t = -plane.distance(ray.origin) / denom;
    return t >= 0.0f ? t : -1.0f;
}

Frustum Frustum::fromMatrix(const Mat4& vp)
{
    // Gribb & Hartmann: planes are sums/differences of matrix rows.
    auto row = [&vp](int r) { return Vec4{vp.at(r, 0), vp.at(r, 1), vp.at(r, 2), vp.at(r, 3)}; };
    const Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    auto make = [](const Vec4& v) { return Plane{{v.x, v.y, v.z}, v.w}.normalized(); };

    Frustum f;
    f.planes[Left] = make(r3 + r0);
    f.planes[Right] = make(r3 - r0);
    f.planes[Bottom] = make(r3 + r1);
    f.planes[Top] = make(r3 - r1);
    f.planes[Near] = make(r3 + r2);
    f.planes[Far] = make(r3 - r2);
    return f;
}

bool Frustum::contains(const Vec3& p) const
{
    for (const Plane& pl : planes) {
        if (pl.distance(p) < 0.0f) {
            return false;
        }
    }
    return true;
}

bool Frustum::intersects(const AABB& box) const
{
    for (const Plane& pl : planes) {
        // The box corner furthest along the plane normal.
        const Vec3 p{pl.normal.x >= 0 ? box.max.x : box.min.x, pl.normal.y >= 0 ? box.max.y : box.min.y,
                     pl.normal.z >= 0 ? box.max.z : box.min.z};
        if (pl.distance(p) < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace av
