#pragma once

#include <algorithm>
#include <cmath>

namespace av {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;
inline constexpr float kHalfPi = 0.5f * kPi;

constexpr float radians(float degrees) { return degrees * (kPi / 180.0f); }
constexpr float degrees(float radians) { return radians * (180.0f / kPi); }

template <typename T>
constexpr T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float saturate(float v) { return clampv(v, 0.0f, 1.0f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

inline float smoothstep(float edge0, float edge1, float x)
{
    const float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

// Wraps an angle to (-pi, pi].
inline float wrapAngle(float a)
{
    a = std::fmod(a + kPi, kTwoPi);
    if (a < 0.0f) {
        a += kTwoPi;
    }
    return a - kPi;
}

// Frame-rate independent exponential approach: fraction of the remaining
// distance covered after dt seconds with the given rate (1/s).
inline float damp(float rate, float dt) { return 1.0f - std::exp(-rate * dt); }

struct Vec2 {
    float x = 0, y = 0;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    constexpr bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
};

struct Vec3 {
    float x = 0, y = 0, z = 0;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; } // component-wise
    constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator*=(const Vec3& o) { x *= o.x; y *= o.y; z *= o.z; return *this; }
    constexpr bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const Vec3& o) const { return !(*this == o); }

    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
};

constexpr Vec3 operator*(float s, const Vec3& v) { return v * s; }

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;

    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}

    constexpr Vec4 operator+(const Vec4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    constexpr Vec4 operator-(const Vec4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    constexpr Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    constexpr bool operator==(const Vec4& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
    constexpr Vec3 xyz() const { return {x, y, z}; }
};

inline float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float dot(const Vec4& a, const Vec4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

inline Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float lengthSq(const Vec2& v) { return dot(v, v); }
inline float lengthSq(const Vec3& v) { return dot(v, v); }
inline float length(const Vec2& v) { return std::sqrt(dot(v, v)); }
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline float distance(const Vec3& a, const Vec3& b) { return length(a - b); }

// Returns the zero vector for (near-)zero input instead of NaNs.
inline Vec2 normalize(const Vec2& v)
{
    const float len = length(v);
    return len > 1e-8f ? v / len : Vec2{};
}
inline Vec3 normalize(const Vec3& v)
{
    const float len = length(v);
    return len > 1e-8f ? v / len : Vec3{};
}

inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec3 minv(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 maxv(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

// Reflects incident direction i about unit normal n.
inline Vec3 reflect(const Vec3& i, const Vec3& n) { return i - n * (2.0f * dot(i, n)); }

inline bool nearlyEqual(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }
inline bool nearlyEqual(const Vec3& a, const Vec3& b, float eps = 1e-5f)
{
    return nearlyEqual(a.x, b.x, eps) && nearlyEqual(a.y, b.y, eps) && nearlyEqual(a.z, b.z, eps);
}

} // namespace av
