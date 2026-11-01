// Street furniture and vegetation.
#include "World/GenContext.h"

#include <cmath>

namespace av::gen {

namespace {

// Rotation taking +Y onto `dir` (unit).
Mat4 alignY(const Vec3& dir)
{
    const Vec3 up{0, 1, 0};
    const Vec3 axis = cross(up, dir);
    const float s = length(axis);
    const float c = dot(up, dir);
    if (s < 1e-6f) {
        return c > 0.0f ? Mat4::identity() : Mat4::rotationX(kPi);
    }
    return Mat4::rotationAxis(axis / s, std::atan2(s, c));
}

} // namespace

void addPalmTree(Context& ctx, const Vec3& base, float height)
{
    Rng& rng = ctx.rng;
    const float leanAngle = rng.range(0.0f, kTwoPi);
    const float lean = rng.range(0.10f, 0.32f);
    const Vec3 leanDir{std::cos(leanAngle), 0, std::sin(leanAngle)};

    // Gently curving trunk built from tapered segments.
    constexpr int segments = 5;
    Vec3 p = base;
    for (int i = 0; i < segments; ++i) {
        const float t = static_cast<float>(i + 1) / segments;
        const Vec3 dir = normalize(Vec3{0, 1, 0} + leanDir * (lean * t * 1.6f));
        const float segLen = height / segments;
        const float r0 = lerpf(0.24f, 0.14f, static_cast<float>(i) / segments);
        const float r1 = lerpf(0.24f, 0.14f, t);
        ctx.base.addCylinder({0, 0, 0}, segLen + 0.05f, r0, r1, 6, ctx.m.palmTrunk, false,
                             Mat4::translation(p) * alignY(dir));
        p += dir * segLen;
    }
    ctx.collider(AABB(base + Vec3{-0.3f, -0.2f, -0.3f}, base + Vec3{0.3f, 3.0f, 0.3f}));

    // Drooping fronds radiating from the crown.
    const int fronds = 8;
    for (int i = 0; i < fronds; ++i) {
        const float a = kTwoPi * static_cast<float>(i) / fronds + rng.range(-0.2f, 0.2f);
        const Vec3 d{std::cos(a), 0, std::sin(a)};
        const Vec3 side{-d.z, 0, d.x};
        const float len = rng.range(2.6f, 3.6f);
        const Vec3 mid = p + d * (len * 0.45f) + Vec3{0, 0.45f, 0};
        const Vec3 tip = p + d * len + Vec3{0, -rng.range(0.9f, 1.5f), 0};
        const float w = 0.55f;
        ctx.base.addQuad(p + side * 0.08f, p - side * 0.08f, mid - side * w, mid + side * w, ctx.m.palmLeaf);
        ctx.base.addTriangle(mid + side * w, mid - side * w, tip, ctx.m.palmLeaf);
    }
    ctx.base.addEllipsoid(p + Vec3{0, -0.15f, 0}, {0.32f, 0.3f, 0.32f}, 3, 6, ctx.m.palmTrunk);
}

void addRoundTree(Context& ctx, const Vec3& base, float scale)
{
    const float trunkH = 2.6f * scale;
    ctx.base.addCylinder(base, trunkH + 0.5f, 0.24f * scale, 0.17f * scale, 6, ctx.m.treeTrunk, false);
    const int blobs = ctx.rng.rangeInt(2, 3);
    for (int i = 0; i < blobs; ++i) {
        const Vec3 off{ctx.rng.range(-0.8f, 0.8f) * scale, ctx.rng.range(0.0f, 1.0f) * scale, ctx.rng.range(-0.8f, 0.8f) * scale};
        const float r = ctx.rng.range(1.6f, 2.3f) * scale;
        ctx.base.addEllipsoid(base + Vec3{0, trunkH + 1.2f * scale, 0} + off, {r, r * 0.85f, r}, 5, 8, ctx.m.treeLeaves);
    }
    ctx.collider(AABB(base + Vec3{-0.3f, -0.2f, -0.3f}, base + Vec3{0.3f, trunkH, 0.3f}));
}

void addStreetLight(Context& ctx, const Vec3& base, const Vec3& arm)
{
    constexpr float h = 6.5f, reach = 1.7f;
    ctx.base.addCylinder(base, h, 0.11f, 0.08f, 6, ctx.m.pole, false);
    const Vec3 top = base + Vec3{0, h, 0};
    const Vec3 end = top + arm * reach;
    const Vec3 lo = minv(top, end) + Vec3{-0.06f, -0.08f, -0.06f};
    const Vec3 hi = maxv(top, end) + Vec3{0.06f, 0.06f, 0.06f};
    ctx.base.addBox(AABB(lo, hi), ctx.m.pole);
    ctx.base.addBox(AABB(end + Vec3{-0.28f, -0.25f, -0.28f}, end + Vec3{0.28f, -0.05f, 0.28f}), ctx.m.lampGlow);
    ctx.collider(AABB(base + Vec3{-0.12f, 0, -0.12f}, base + Vec3{0.12f, h, 0.12f}));
    ctx.light(end + Vec3{0, -0.6f, 0}, {1.0f, 0.72f, 0.42f}, 13.0f, 1.4f);
}

void addBench(Context& ctx, const Vec3& b, bool alongX)
{
    const Vec3 half = alongX ? Vec3{0.9f, 0, 0.25f} : Vec3{0.25f, 0, 0.9f};
    ctx.detail.addBox(AABB(b + Vec3{-half.x, 0.42f, -half.z}, b + Vec3{half.x, 0.5f, half.z}), ctx.m.benchWood);
    const Vec3 backOff = alongX ? Vec3{0, 0, 0.22f} : Vec3{0.22f, 0, 0};
    const Vec3 backHalf = alongX ? Vec3{0.9f, 0, 0.04f} : Vec3{0.04f, 0, 0.9f};
    ctx.detail.addBox(AABB(b + backOff + Vec3{-backHalf.x, 0.5f, -backHalf.z}, b + backOff + Vec3{backHalf.x, 0.95f, backHalf.z}),
                      ctx.m.benchWood);
    for (const float s : {-0.75f, 0.75f}) {
        const Vec3 leg = alongX ? Vec3{s, 0, 0} : Vec3{0, 0, s};
        ctx.detail.addBox(AABB(b + leg + Vec3{-0.05f, 0, -0.2f}, b + leg + Vec3{0.05f, 0.42f, 0.2f}), ctx.m.metalDark);
    }
    ctx.collider(AABB(b + Vec3{-half.x, 0, -half.z}, b + Vec3{half.x, 0.95f, half.z}));
}

void addNewsbox(Context& ctx, const Vec3& base, float yaw)
{
    ctx.detail.append(ctx.newsbox, Mat4::translation(base) * Mat4::rotationY(yaw));
    ctx.collider(AABB(base + Vec3{-0.3f, 0, -0.3f}, base + Vec3{0.3f, 1.0f, 0.3f}));
}

void addFountain(Context& ctx, const Vec3& c)
{
    ctx.base.addCylinder(c, 0.6f, 3.0f, 3.0f, 16, ctx.m.concrete);
    ctx.base.addCylinder(c + Vec3{0, 0.5f, 0}, 0.12f, 2.75f, 2.75f, 16, ctx.m.poolWater);
    ctx.base.addCylinder(c, 2.0f, 0.4f, 0.3f, 8, ctx.m.concrete);
    ctx.base.addCylinder(c + Vec3{0, 2.0f, 0}, 0.3f, 1.0f, 1.1f, 12, ctx.m.concrete);
    ctx.base.addCylinder(c + Vec3{0, 2.2f, 0}, 0.12f, 0.9f, 0.9f, 12, ctx.m.poolWater);
    ctx.collider(AABB(c + Vec3{-3.0f, -0.2f, -3.0f}, c + Vec3{3.0f, 0.6f, 3.0f}));
}

void addBeachUmbrella(Context& ctx, const Vec3& base)
{
    ctx.detail.addCylinder(base, 2.3f, 0.04f, 0.04f, 4, ctx.m.metal, false);
    ctx.detail.addCylinder(base + Vec3{0, 1.9f, 0}, 0.55f, 1.5f, 0.03f, 10, ctx.m.umbrellas[ctx.rng.rangeInt(0, 1)], false);
    ctx.collider(AABB(base + Vec3{-0.08f, 0, -0.08f}, base + Vec3{0.08f, 2.3f, 0.08f}));
}

void addLifeguardTower(Context& ctx, const Vec3& b)
{
    for (const float sx : {-1.0f, 1.0f}) {
        for (const float sz : {-1.0f, 1.0f}) {
            const Vec3 p = b + Vec3{sx, 0, sz};
            ctx.base.addBox(AABB(p + Vec3{-0.08f, -0.5f, -0.08f}, p + Vec3{0.08f, 2.1f, 0.08f}), ctx.m.trimWhite);
        }
    }
    ctx.base.addBox(AABB(b + Vec3{-1.4f, 2.0f, -1.4f}, b + Vec3{1.4f, 2.2f, 1.4f}), ctx.m.trimWhite);
    ctx.base.addBox(AABB(b + Vec3{-1.1f, 2.2f, -1.1f}, b + Vec3{1.1f, 4.0f, 1.1f}), ctx.m.lifeguard, Mat4::identity(),
                    MeshBuilder::NoBottom);
    ctx.base.addBox(AABB(b + Vec3{-1.5f, 4.0f, -1.5f}, b + Vec3{1.5f, 4.25f, 1.5f}), ctx.m.awningTeal);
    // Ramp down towards the water (+X).
    ctx.base.addQuad(b + Vec3{1.4f, 2.0f, -0.5f}, b + Vec3{1.4f, 2.0f, 0.5f}, b + Vec3{3.8f, 0.0f, 0.5f},
                     b + Vec3{3.8f, 0.0f, -0.5f}, ctx.m.benchWood);
    for (const float sx : {-1.0f, 1.0f}) {
        for (const float sz : {-1.0f, 1.0f}) {
            const Vec3 p = b + Vec3{sx, 0, sz};
            ctx.collider(AABB(p + Vec3{-0.1f, 0, -0.1f}, p + Vec3{0.1f, 2.1f, 0.1f}));
        }
    }
    ctx.collider(AABB(b + Vec3{-1.4f, 2.0f, -1.4f}, b + Vec3{1.4f, 4.25f, 1.4f}));
}

void addPool(Context& ctx, const Rect& r)
{
    const float y = kGround;
    const float rim = 0.45f, rimH = 0.12f;
    ctx.detail.addBox(AABB({r.x0 - rim, y, r.z0 - rim}, {r.x1 + rim, y + rimH, r.z0}), ctx.m.trimWhite);
    ctx.detail.addBox(AABB({r.x0 - rim, y, r.z1}, {r.x1 + rim, y + rimH, r.z1 + rim}), ctx.m.trimWhite);
    ctx.detail.addBox(AABB({r.x0 - rim, y, r.z0}, {r.x0, y + rimH, r.z1}), ctx.m.trimWhite);
    ctx.detail.addBox(AABB({r.x1, y, r.z0}, {r.x1 + rim, y + rimH, r.z1}), ctx.m.trimWhite);
    ctx.detail.addQuad({r.x0, y + 0.06f, r.z1}, {r.x1, y + 0.06f, r.z1}, {r.x1, y + 0.06f, r.z0}, {r.x0, y + 0.06f, r.z0},
                       ctx.m.poolWater);
}

void addHedge(Context& ctx, const Rect& r, float height)
{
    ctx.base.addBox(AABB({r.x0, kGround, r.z0}, {r.x1, kGround + height, r.z1}), ctx.m.hedge, Mat4::identity(),
                    MeshBuilder::NoBottom);
    ctx.collider(AABB({r.x0, 0, r.z0}, {r.x1, kGround + height, r.z1}));
}

} // namespace av::gen
