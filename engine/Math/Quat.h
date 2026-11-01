#pragma once

#include "Math/Mat.h"
#include "Math/Vec.h"

namespace av {

// Unit quaternion rotation (x, y, z vector part, w scalar part).
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;

    constexpr Quat() = default;
    constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quat identity() { return {}; }
    static Quat fromAxisAngle(const Vec3& axis, float radians)
    {
        const Vec3 n = normalize(axis);
        const float s = std::sin(radians * 0.5f);
        return {n.x * s, n.y * s, n.z * s, std::cos(radians * 0.5f)};
    }
    // Applied in order: roll (Z), then pitch (X), then yaw (Y). I.e. q = Y * X * Z.
    static Quat fromEuler(float pitchX, float yawY, float rollZ)
    {
        return fromAxisAngle({0, 1, 0}, yawY) * fromAxisAngle({1, 0, 0}, pitchX) * fromAxisAngle({0, 0, 1}, rollZ);
    }

    Quat operator*(const Quat& o) const
    {
        return {w * o.x + x * o.w + y * o.z - z * o.y,
                w * o.y - x * o.z + y * o.w + z * o.x,
                w * o.z + x * o.y - y * o.x + z * o.w,
                w * o.w - x * o.x - y * o.y - z * o.z};
    }

    Quat conjugate() const { return {-x, -y, -z, w}; }

    Vec3 rotate(const Vec3& v) const
    {
        // v' = v + 2w(q x v) + 2 q x (q x v)
        const Vec3 q{x, y, z};
        const Vec3 t = cross(q, v) * 2.0f;
        return v + t * w + cross(q, t);
    }

    Mat4 toMat4() const
    {
        Mat4 r;
        const float xx = x * x, yy = y * y, zz = z * z;
        const float xy = x * y, xz = x * z, yz = y * z;
        const float wx = w * x, wy = w * y, wz = w * z;
        r.at(0, 0) = 1 - 2 * (yy + zz);
        r.at(0, 1) = 2 * (xy - wz);
        r.at(0, 2) = 2 * (xz + wy);
        r.at(1, 0) = 2 * (xy + wz);
        r.at(1, 1) = 1 - 2 * (xx + zz);
        r.at(1, 2) = 2 * (yz - wx);
        r.at(2, 0) = 2 * (xz - wy);
        r.at(2, 1) = 2 * (yz + wx);
        r.at(2, 2) = 1 - 2 * (xx + yy);
        return r;
    }
};

inline float dot(const Quat& a, const Quat& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

inline Quat normalize(const Quat& q)
{
    const float len = std::sqrt(dot(q, q));
    if (len < 1e-8f) {
        return Quat::identity();
    }
    return {q.x / len, q.y / len, q.z / len, q.w / len};
}

// Spherical interpolation along the shortest arc.
inline Quat slerp(const Quat& a, Quat b, float t)
{
    float d = dot(a, b);
    if (d < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > 0.9995f) {
        // Nearly parallel: normalised lerp is accurate and avoids division by ~0.
        return normalize(Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                              a.w + (b.w - a.w) * t});
    }
    const float theta = std::acos(d);
    const float s = std::sin(theta);
    const float wa = std::sin((1.0f - t) * theta) / s;
    const float wb = std::sin(t * theta) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

// Translation, rotation, scale: matrix = T * R * S.
struct Transform {
    Vec3 position;
    Quat rotation;
    Vec3 scale{1, 1, 1};

    Mat4 toMatrix() const { return Mat4::translation(position) * rotation.toMat4() * Mat4::scale(scale); }
    Vec3 transformPoint(const Vec3& p) const { return position + rotation.rotate(p * scale); }
    Vec3 transformDirection(const Vec3& d) const { return rotation.rotate(d); }
};

} // namespace av
