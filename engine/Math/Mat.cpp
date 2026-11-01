#include "Math/Mat.h"

#include <cmath>

namespace av {

// ---- Mat3 ----------------------------------------------------------------

Mat3 Mat3::operator*(const Mat3& o) const
{
    Mat3 r;
    for (int c = 0; c < 3; ++c) {
        for (int rr = 0; rr < 3; ++rr) {
            r.at(rr, c) = at(rr, 0) * o.at(0, c) + at(rr, 1) * o.at(1, c) + at(rr, 2) * o.at(2, c);
        }
    }
    return r;
}

Mat3 Mat3::transposed() const
{
    Mat3 r;
    for (int c = 0; c < 3; ++c) {
        for (int rr = 0; rr < 3; ++rr) {
            r.at(rr, c) = at(c, rr);
        }
    }
    return r;
}

float Mat3::determinant() const
{
    return at(0, 0) * (at(1, 1) * at(2, 2) - at(1, 2) * at(2, 1)) -
           at(0, 1) * (at(1, 0) * at(2, 2) - at(1, 2) * at(2, 0)) +
           at(0, 2) * (at(1, 0) * at(2, 1) - at(1, 1) * at(2, 0));
}

Mat3 Mat3::inverse() const
{
    const float det = determinant();
    if (std::fabs(det) < 1e-12f) {
        return identity();
    }
    const float inv = 1.0f / det;
    Mat3 r;
    r.at(0, 0) = (at(1, 1) * at(2, 2) - at(1, 2) * at(2, 1)) * inv;
    r.at(0, 1) = (at(0, 2) * at(2, 1) - at(0, 1) * at(2, 2)) * inv;
    r.at(0, 2) = (at(0, 1) * at(1, 2) - at(0, 2) * at(1, 1)) * inv;
    r.at(1, 0) = (at(1, 2) * at(2, 0) - at(1, 0) * at(2, 2)) * inv;
    r.at(1, 1) = (at(0, 0) * at(2, 2) - at(0, 2) * at(2, 0)) * inv;
    r.at(1, 2) = (at(0, 2) * at(1, 0) - at(0, 0) * at(1, 2)) * inv;
    r.at(2, 0) = (at(1, 0) * at(2, 1) - at(1, 1) * at(2, 0)) * inv;
    r.at(2, 1) = (at(0, 1) * at(2, 0) - at(0, 0) * at(2, 1)) * inv;
    r.at(2, 2) = (at(0, 0) * at(1, 1) - at(0, 1) * at(1, 0)) * inv;
    return r;
}

// ---- Mat4 ----------------------------------------------------------------

Mat4 Mat4::translation(const Vec3& t)
{
    Mat4 r;
    r.at(0, 3) = t.x;
    r.at(1, 3) = t.y;
    r.at(2, 3) = t.z;
    return r;
}

Mat4 Mat4::scale(const Vec3& s)
{
    Mat4 r;
    r.at(0, 0) = s.x;
    r.at(1, 1) = s.y;
    r.at(2, 2) = s.z;
    return r;
}

Mat4 Mat4::rotationX(float a)
{
    const float c = std::cos(a), s = std::sin(a);
    Mat4 r;
    r.at(1, 1) = c;
    r.at(1, 2) = -s;
    r.at(2, 1) = s;
    r.at(2, 2) = c;
    return r;
}

Mat4 Mat4::rotationY(float a)
{
    const float c = std::cos(a), s = std::sin(a);
    Mat4 r;
    r.at(0, 0) = c;
    r.at(0, 2) = s;
    r.at(2, 0) = -s;
    r.at(2, 2) = c;
    return r;
}

Mat4 Mat4::rotationZ(float a)
{
    const float c = std::cos(a), s = std::sin(a);
    Mat4 r;
    r.at(0, 0) = c;
    r.at(0, 1) = -s;
    r.at(1, 0) = s;
    r.at(1, 1) = c;
    return r;
}

Mat4 Mat4::rotationAxis(const Vec3& axis, float a)
{
    const Vec3 n = normalize(axis);
    const float c = std::cos(a), s = std::sin(a), t = 1.0f - c;
    Mat4 r;
    r.at(0, 0) = t * n.x * n.x + c;
    r.at(0, 1) = t * n.x * n.y - s * n.z;
    r.at(0, 2) = t * n.x * n.z + s * n.y;
    r.at(1, 0) = t * n.x * n.y + s * n.z;
    r.at(1, 1) = t * n.y * n.y + c;
    r.at(1, 2) = t * n.y * n.z - s * n.x;
    r.at(2, 0) = t * n.x * n.z - s * n.y;
    r.at(2, 1) = t * n.y * n.z + s * n.x;
    r.at(2, 2) = t * n.z * n.z + c;
    return r;
}

Mat4 Mat4::perspective(float fovY, float aspect, float zNear, float zFar)
{
    const float f = 1.0f / std::tan(fovY * 0.5f);
    Mat4 r;
    r.at(0, 0) = f / aspect;
    r.at(1, 1) = f;
    r.at(2, 2) = (zFar + zNear) / (zNear - zFar);
    r.at(2, 3) = (2.0f * zFar * zNear) / (zNear - zFar);
    r.at(3, 2) = -1.0f;
    r.at(3, 3) = 0.0f;
    return r;
}

Mat4 Mat4::lookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
{
    const Vec3 f = normalize(target - eye);
    Vec3 s = normalize(cross(f, up));
    if (lengthSq(s) < 1e-12f) {
        // Looking straight along `up`: pick any perpendicular side vector.
        s = normalize(cross(f, std::fabs(f.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 0, 1}));
    }
    const Vec3 u = cross(s, f);
    Mat4 r;
    r.at(0, 0) = s.x;
    r.at(0, 1) = s.y;
    r.at(0, 2) = s.z;
    r.at(1, 0) = u.x;
    r.at(1, 1) = u.y;
    r.at(1, 2) = u.z;
    r.at(2, 0) = -f.x;
    r.at(2, 1) = -f.y;
    r.at(2, 2) = -f.z;
    r.at(0, 3) = -dot(s, eye);
    r.at(1, 3) = -dot(u, eye);
    r.at(2, 3) = dot(f, eye);
    return r;
}

Mat4 Mat4::operator*(const Mat4& o) const
{
    Mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int rr = 0; rr < 4; ++rr) {
            r.at(rr, c) = at(rr, 0) * o.at(0, c) + at(rr, 1) * o.at(1, c) + at(rr, 2) * o.at(2, c) +
                          at(rr, 3) * o.at(3, c);
        }
    }
    return r;
}

Mat3 Mat4::upper3x3() const
{
    Mat3 r;
    for (int c = 0; c < 3; ++c) {
        for (int rr = 0; rr < 3; ++rr) {
            r.at(rr, c) = at(rr, c);
        }
    }
    return r;
}

Mat4 Mat4::transposed() const
{
    Mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int rr = 0; rr < 4; ++rr) {
            r.at(rr, c) = at(c, rr);
        }
    }
    return r;
}

Mat4 Mat4::inverse() const
{
    // Cofactor expansion (as in the MESA GLU implementation), column-major.
    const float* a = m;
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] +
             a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] -
             a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] +
             a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] -
              a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] -
             a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] +
             a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] -
             a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] +
              a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
             a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] -
             a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] +
              a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] -
              a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] -
             a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
             a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
              a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
              a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

    const float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (std::fabs(det) < 1e-20f) {
        return identity();
    }
    const float invDet = 1.0f / det;
    Mat4 r;
    for (int i = 0; i < 16; ++i) {
        r.m[i] = inv[i] * invDet;
    }
    return r;
}

Mat3 normalMatrix(const Mat4& model)
{
    return model.upper3x3().inverse().transposed();
}

bool nearlyEqual(const Mat4& a, const Mat4& b, float eps)
{
    for (int i = 0; i < 16; ++i) {
        if (std::fabs(a.m[i] - b.m[i]) > eps) {
            return false;
        }
    }
    return true;
}

} // namespace av
