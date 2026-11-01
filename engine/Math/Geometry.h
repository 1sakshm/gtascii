#pragma once

#include "Math/Mat.h"
#include "Math/Vec.h"

#include <limits>

namespace av {

// Plane: dot(normal, p) + d = 0. Points with positive distance are "in front".
struct Plane {
    Vec3 normal{0, 1, 0};
    float d = 0;

    static Plane fromPointNormal(const Vec3& point, const Vec3& n)
    {
        const Vec3 nn = normalize(n);
        return {nn, -dot(nn, point)};
    }
    float distance(const Vec3& p) const { return dot(normal, p) + d; }
    Plane normalized() const
    {
        const float len = length(normal);
        return len > 1e-12f ? Plane{normal / len, d / len} : *this;
    }
};

struct Ray {
    Vec3 origin;
    Vec3 direction{0, 0, -1}; // expected to be unit length
    Vec3 at(float t) const { return origin + direction * t; }
};

struct AABB {
    Vec3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
             -std::numeric_limits<float>::max()};

    AABB() = default;
    AABB(const Vec3& mn, const Vec3& mx) : min(mn), max(mx) {}
    static AABB fromCenterExtents(const Vec3& c, const Vec3& e) { return {c - e, c + e}; }

    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    void expand(const Vec3& p) { min = minv(min, p); max = maxv(max, p); }
    void expand(const AABB& b) { if (b.valid()) { expand(b.min); expand(b.max); } }
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extents() const { return (max - min) * 0.5f; }
    Vec3 size() const { return max - min; }

    bool contains(const Vec3& p) const
    {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }
    bool intersects(const AABB& o) const
    {
        return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y && max.y >= o.min.y && min.z <= o.max.z &&
               max.z >= o.min.z;
    }
    // Bounds of this box after an affine transform.
    AABB transformed(const Mat4& m) const;
};

// Slab test. Returns the entry distance t >= 0 (0 if the origin is inside),
// or a negative value if the ray misses within [0, maxT].
float intersectRayAABB(const Ray& ray, const AABB& box, float maxT = std::numeric_limits<float>::max());

// Returns t of the hit, or negative if parallel / behind.
float intersectRayPlane(const Ray& ray, const Plane& plane);

// Six planes facing inwards, extracted from a view-projection matrix.
struct Frustum {
    enum { Left, Right, Bottom, Top, Near, Far, Count };
    Plane planes[Count];

    static Frustum fromMatrix(const Mat4& viewProjection);
    bool contains(const Vec3& p) const;
    // Conservative: may report true for boxes just outside a corner.
    bool intersects(const AABB& box) const;
};

} // namespace av
