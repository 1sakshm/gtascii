#pragma once

#include "Math/Vec.h"

namespace av {

// Conventions used throughout the engine:
//   * right-handed world, +Y up; a camera looks down its local -Z
//   * column vectors: p' = M * p, so transforms compose right to left
//   * column-major storage: m[col * N + row]
//   * clip space as OpenGL: visible z in [-w, w]

struct Mat3 {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    static Mat3 identity() { return {}; }

    float& at(int row, int col) { return m[col * 3 + row]; }
    float at(int row, int col) const { return m[col * 3 + row]; }

    Vec3 operator*(const Vec3& v) const
    {
        return {at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z,
                at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z,
                at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z};
    }

    Mat3 operator*(const Mat3& o) const;
    Mat3 transposed() const;
    float determinant() const;
    // Returns identity if singular.
    Mat3 inverse() const;
};

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 identity() { return {}; }
    static Mat4 translation(const Vec3& t);
    static Mat4 scale(const Vec3& s);
    static Mat4 rotationX(float radians);
    static Mat4 rotationY(float radians);
    static Mat4 rotationZ(float radians);
    static Mat4 rotationAxis(const Vec3& axis, float radians);
    // Right-handed perspective; fovY in radians; maps z to [-w, w].
    static Mat4 perspective(float fovY, float aspect, float zNear, float zFar);
    // View matrix for a camera at eye looking at target.
    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }

    Mat4 operator*(const Mat4& o) const;
    Vec4 operator*(const Vec4& v) const
    {
        return {at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z + at(0, 3) * v.w,
                at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z + at(1, 3) * v.w,
                at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z + at(2, 3) * v.w,
                at(3, 0) * v.x + at(3, 1) * v.y + at(3, 2) * v.z + at(3, 3) * v.w};
    }

    // Point (w = 1); assumes an affine matrix (no projective divide).
    Vec3 transformPoint(const Vec3& p) const
    {
        return {at(0, 0) * p.x + at(0, 1) * p.y + at(0, 2) * p.z + at(0, 3),
                at(1, 0) * p.x + at(1, 1) * p.y + at(1, 2) * p.z + at(1, 3),
                at(2, 0) * p.x + at(2, 1) * p.y + at(2, 2) * p.z + at(2, 3)};
    }
    // Direction (w = 0): translation ignored.
    Vec3 transformDirection(const Vec3& d) const
    {
        return {at(0, 0) * d.x + at(0, 1) * d.y + at(0, 2) * d.z,
                at(1, 0) * d.x + at(1, 1) * d.y + at(1, 2) * d.z,
                at(2, 0) * d.x + at(2, 1) * d.y + at(2, 2) * d.z};
    }

    Vec3 translationPart() const { return {at(0, 3), at(1, 3), at(2, 3)}; }
    Mat3 upper3x3() const;
    Mat4 transposed() const;
    // General 4x4 inverse; returns identity if singular.
    Mat4 inverse() const;
};

// Inverse-transpose of the upper 3x3: transforms normals correctly under
// non-uniform scale.
Mat3 normalMatrix(const Mat4& model);

bool nearlyEqual(const Mat4& a, const Mat4& b, float eps = 1e-4f);

} // namespace av
