#include "TestHarness.h"

#include "Math/Geometry.h"
#include "Math/Mat.h"
#include "Math/Quat.h"

using namespace av;

namespace {
bool near3(const Vec3& a, const Vec3& b, float eps = 1e-4f) { return nearlyEqual(a, b, eps); }
} // namespace

TEST_CASE("Vec3: arithmetic, dot, cross, normalize")
{
    const Vec3 a{1, 2, 3}, b{4, 5, 6};
    CHECK(a + b == Vec3(5, 7, 9));
    CHECK(b - a == Vec3(3, 3, 3));
    CHECK(a * 2.0f == Vec3(2, 4, 6));
    CHECK_EQ(dot(a, b), 32.0f);
    CHECK(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == Vec3(0, 0, 1));
    CHECK(cross(Vec3{0, 1, 0}, Vec3{0, 0, 1}) == Vec3(1, 0, 0));
    CHECK(nearlyEqual(length(normalize(Vec3{3, 4, 12})), 1.0f));
    CHECK(normalize(Vec3{0, 0, 0}) == Vec3(0, 0, 0)); // no NaN
    CHECK(near3(reflect(Vec3{1, -1, 0}, Vec3{0, 1, 0}), Vec3{1, 1, 0}));
}

TEST_CASE("Scalar helpers: wrapAngle, smoothstep, damp")
{
    CHECK(nearlyEqual(wrapAngle(3.0f * kPi), kPi, 1e-4f) || nearlyEqual(wrapAngle(3.0f * kPi), -kPi, 1e-4f));
    CHECK(nearlyEqual(wrapAngle(0.5f), 0.5f));
    CHECK(nearlyEqual(wrapAngle(-kTwoPi - 0.5f), -0.5f, 1e-4f));
    CHECK_EQ(smoothstep(0.0f, 1.0f, -1.0f), 0.0f);
    CHECK_EQ(smoothstep(0.0f, 1.0f, 2.0f), 1.0f);
    CHECK(nearlyEqual(smoothstep(0.0f, 1.0f, 0.5f), 0.5f));
    CHECK(damp(10.0f, 0.0f) == 0.0f);
    CHECK(damp(10.0f, 10.0f) > 0.999f);
}

TEST_CASE("Mat4: identity, translation, scale, multiplication order")
{
    const Mat4 t = Mat4::translation({1, 2, 3});
    const Mat4 s = Mat4::scale({2, 2, 2});
    CHECK(near3(t.transformPoint({0, 0, 0}), {1, 2, 3}));
    CHECK(near3(t.transformDirection({1, 0, 0}), {1, 0, 0}));
    // Column vectors: (t * s) scales first, then translates.
    CHECK(near3((t * s).transformPoint({1, 1, 1}), {3, 4, 5}));
    CHECK(near3((s * t).transformPoint({1, 1, 1}), {4, 6, 8}));
    CHECK(nearlyEqual(Mat4::identity() * t, t));
}

TEST_CASE("Mat4: rotations follow the right-hand rule")
{
    CHECK(near3(Mat4::rotationY(kHalfPi).transformPoint({0, 0, 1}), {1, 0, 0}));
    CHECK(near3(Mat4::rotationX(kHalfPi).transformPoint({0, 1, 0}), {0, 0, 1}));
    CHECK(near3(Mat4::rotationZ(kHalfPi).transformPoint({1, 0, 0}), {0, 1, 0}));
    CHECK(nearlyEqual(Mat4::rotationAxis({0, 1, 0}, 0.7f), Mat4::rotationY(0.7f)));
}

TEST_CASE("Mat4: inverse")
{
    const Mat4 m = Mat4::translation({3, -2, 5}) * Mat4::rotationAxis({1, 2, 3}, 0.8f) * Mat4::scale({2, 3, 0.5f});
    CHECK(nearlyEqual(m * m.inverse(), Mat4::identity(), 1e-4f));
    const Mat4 p = Mat4::perspective(radians(60.0f), 1.5f, 0.1f, 100.0f);
    CHECK(nearlyEqual(p * p.inverse(), Mat4::identity(), 1e-3f));
}

TEST_CASE("Mat4: lookAt puts the target on -Z")
{
    const Mat4 v = Mat4::lookAt({0, 5, 10}, {0, 5, 0}, {0, 1, 0});
    CHECK(near3(v.transformPoint({0, 5, 0}), {0, 0, -10}));
    CHECK(near3(v.transformPoint({1, 5, 10}), {1, 0, 0})); // +X is camera right
    // Looking straight down must not produce NaNs.
    const Mat4 down = Mat4::lookAt({0, 10, 0}, {0, 0, 0}, {0, 1, 0});
    CHECK(near3(down.transformPoint({0, 0, 0}), {0, 0, -10}));
}

TEST_CASE("Mat4: perspective maps near/far to NDC -1/+1")
{
    const Mat4 p = Mat4::perspective(radians(90.0f), 1.0f, 1.0f, 10.0f);
    const Vec4 n = p * Vec4{0, 0, -1, 1};
    const Vec4 f = p * Vec4{0, 0, -10, 1};
    CHECK(nearlyEqual(n.z / n.w, -1.0f));
    CHECK(nearlyEqual(f.z / f.w, 1.0f));
    const Vec4 edge = p * Vec4{1, 0, -1, 1}; // 45 degrees right at the near plane
    CHECK(nearlyEqual(edge.x / edge.w, 1.0f));
}

TEST_CASE("Mat3: inverse and normal matrix")
{
    const Mat4 m = Mat4::scale({2, 1, 1});
    const Mat3 nm = normalMatrix(m);
    // A 45-degree normal under x-stretch tilts towards +Y.
    const Vec3 n = normalize(nm * normalize(Vec3{1, 1, 0}));
    CHECK(n.y > n.x);
    const Mat3 r = Mat4::rotationX(0.3f).upper3x3();
    const Mat3 i = r * r.inverse();
    CHECK(nearlyEqual(i.at(0, 0), 1.0f) && nearlyEqual(i.at(1, 1), 1.0f) && nearlyEqual(i.at(0, 1), 0.0f));
}

TEST_CASE("Quat: rotation matches matrices, slerp endpoints")
{
    const Quat q = Quat::fromAxisAngle({0, 1, 0}, kHalfPi);
    CHECK(near3(q.rotate({0, 0, 1}), {1, 0, 0}));
    CHECK(nearlyEqual(q.toMat4(), Mat4::rotationY(kHalfPi)));

    const Quat a = Quat::fromAxisAngle({1, 0, 0}, 0.4f);
    const Quat b = Quat::fromAxisAngle({0, 0, 1}, 1.1f);
    CHECK(nearlyEqual((a * b).toMat4(), a.toMat4() * b.toMat4()));

    const Quat s0 = slerp(a, b, 0.0f), s1 = slerp(a, b, 1.0f);
    CHECK(near3(s0.rotate({1, 2, 3}), a.rotate({1, 2, 3})));
    CHECK(near3(s1.rotate({1, 2, 3}), b.rotate({1, 2, 3})));
    const Quat half = slerp(Quat::identity(), Quat::fromAxisAngle({0, 1, 0}, 1.0f), 0.5f);
    CHECK(near3(half.rotate({0, 0, 1}), Quat::fromAxisAngle({0, 1, 0}, 0.5f).rotate({0, 0, 1})));

    const Quat e = Quat::fromEuler(0.2f, 0.5f, 0.1f);
    CHECK(nearlyEqual(e.toMat4(), Mat4::rotationY(0.5f) * Mat4::rotationX(0.2f) * Mat4::rotationZ(0.1f)));
}

TEST_CASE("Transform: TRS matrix agrees with direct transform")
{
    Transform t;
    t.position = {1, 2, 3};
    t.rotation = Quat::fromAxisAngle({0, 1, 0}, 0.6f);
    t.scale = {2, 1, 0.5f};
    const Vec3 p{0.3f, -1.0f, 2.0f};
    CHECK(near3(t.toMatrix().transformPoint(p), t.transformPoint(p)));
}

TEST_CASE("Geometry: plane, ray-plane, ray-AABB")
{
    const Plane ground = Plane::fromPointNormal({0, 2, 0}, {0, 1, 0});
    CHECK(nearlyEqual(ground.distance({5, 5, 5}), 3.0f));
    CHECK(nearlyEqual(intersectRayPlane({{0, 10, 0}, {0, -1, 0}}, ground), 8.0f));
    CHECK(intersectRayPlane({{0, 10, 0}, {0, 1, 0}}, ground) < 0.0f);

    const AABB box({-1, -1, -1}, {1, 1, 1});
    CHECK(nearlyEqual(intersectRayAABB({{-5, 0, 0}, {1, 0, 0}}, box), 4.0f));
    CHECK(intersectRayAABB({{-5, 3, 0}, {1, 0, 0}}, box) < 0.0f);
    CHECK_EQ(intersectRayAABB({{0, 0, 0}, {1, 0, 0}}, box), 0.0f); // inside
    CHECK(intersectRayAABB({{-5, 0, 0}, {1, 0, 0}}, box, 3.0f) < 0.0f); // beyond maxT
}

TEST_CASE("Geometry: AABB expand, intersects, transformed")
{
    AABB b;
    CHECK(!b.valid());
    b.expand({1, 2, 3});
    b.expand({-1, 0, 5});
    CHECK(b.min == Vec3(-1, 0, 3));
    CHECK(b.max == Vec3(1, 2, 5));
    CHECK(b.intersects(AABB({0, 1, 4}, {9, 9, 9})));
    CHECK(!b.intersects(AABB({2, 0, 0}, {3, 1, 1})));

    const AABB unit({-1, -1, -1}, {1, 1, 1});
    const AABB rotated = unit.transformed(Mat4::rotationY(kPi / 4.0f));
    CHECK(nearlyEqual(rotated.max.x, std::sqrt(2.0f), 1e-4f));
    const AABB moved = unit.transformed(Mat4::translation({10, 0, 0}));
    CHECK(near3(moved.center(), {10, 0, 0}));
}

TEST_CASE("Geometry: frustum culling")
{
    const Mat4 vp = Mat4::perspective(radians(60.0f), 1.0f, 0.1f, 100.0f) *
                    Mat4::lookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
    const Frustum f = Frustum::fromMatrix(vp);
    CHECK(f.contains({0, 0, -10}));
    CHECK(!f.contains({0, 0, 10}));   // behind
    CHECK(!f.contains({0, 0, -200})); // beyond far
    CHECK(f.intersects(AABB({-1, -1, -11}, {1, 1, -9})));
    CHECK(!f.intersects(AABB({50, -1, -11}, {52, 1, -9})));
    CHECK(f.intersects(AABB({-1, -1, -1}, {1, 1, 1}))); // straddles near plane
}
