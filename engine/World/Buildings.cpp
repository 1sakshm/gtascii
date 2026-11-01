// Building generators: towers, houses, art deco hotels, warehouses and the
// data-placed landmark structures.
#include "World/GenContext.h"

#include <algorithm>
#include <cmath>

namespace av::gen {

namespace {

constexpr unsigned kSides = MeshBuilder::FaceNegX | MeshBuilder::FacePosX | MeshBuilder::FaceNegZ | MeshBuilder::FacePosZ;

void flatQuad(MeshBuilder& b, float x0, float z0, float x1, float z1, float y, MaterialId mat)
{
    b.addQuad({x0, y, z1}, {x1, y, z1}, {x1, y, z0}, {x0, y, z0}, mat);
}

// A window-sized quad on one facade. side: 0 -X, 1 +X, 2 -Z, 3 +Z. `u` is
// the centre along the facade, y0/y1 its vertical extent.
void facadeQuad(MeshBuilder& b, const Rect& fp, int side, float u, float halfWidth, float y0, float y1, MaterialId mat,
                float offset = 0.04f)
{
    const float a = u - halfWidth, c = u + halfWidth;
    switch (side) {
    case 0: { const float x = fp.x0 - offset; b.addQuad({x, y0, a}, {x, y0, c}, {x, y1, c}, {x, y1, a}, mat); break; }
    case 1: { const float x = fp.x1 + offset; b.addQuad({x, y0, c}, {x, y0, a}, {x, y1, a}, {x, y1, c}, mat); break; }
    case 2: { const float z = fp.z0 - offset; b.addQuad({c, y0, z}, {a, y0, z}, {a, y1, z}, {c, y1, z}, mat); break; }
    default: { const float z = fp.z1 + offset; b.addQuad({a, y0, z}, {c, y0, z}, {c, y1, z}, {a, y1, z}, mat); break; }
    }
}

float facadeLength(const Rect& fp, int side) { return side < 2 ? fp.depth() : fp.width(); }
float facadeStart(const Rect& fp, int side) { return side < 2 ? fp.z0 : fp.x0; }

void pyramidRoof(Context& ctx, const Rect& fp, float y, float rise, float overhang, MaterialId mat)
{
    const Rect e = fp.inset(-overhang);
    const Vec3 apex{fp.cx(), y + rise, fp.cz()};
    ctx.base.addTriangle({e.x0, y, e.z1}, {e.x1, y, e.z1}, apex, mat);
    ctx.base.addTriangle({e.x1, y, e.z0}, {e.x0, y, e.z0}, apex, mat);
    ctx.base.addTriangle({e.x1, y, e.z1}, {e.x1, y, e.z0}, apex, mat);
    ctx.base.addTriangle({e.x0, y, e.z0}, {e.x0, y, e.z1}, apex, mat);
    // Underside of the overhang.
    ctx.base.addQuad({e.x0, y, e.z0}, {e.x1, y, e.z0}, {e.x1, y, e.z1}, {e.x0, y, e.z1}, mat);
}

} // namespace

float addBuilding(Context& ctx, const Rect& fp, float baseY, float height, const BuildingStyle& style)
{
    const float top = baseY + height;
    ctx.base.addBox(AABB({fp.x0, baseY, fp.z0}, {fp.x1, top, fp.z1}), style.wall, Mat4::identity(), kSides);
    flatQuad(ctx.base, fp.x0, fp.z0, fp.x1, fp.z1, top, style.roof);
    const float p = 0.3f, ph = style.parapet ? 0.8f : 0.0f;
    if (style.parapet) {
    const MaterialId parapet = style.glassCurtain ? ctx.m.mullion : style.wall;
    ctx.base.addBox(AABB({fp.x0, top, fp.z0}, {fp.x1, top + ph, fp.z0 + p}), parapet, Mat4::identity(), MeshBuilder::NoBottom);
    ctx.base.addBox(AABB({fp.x0, top, fp.z1 - p}, {fp.x1, top + ph, fp.z1}), parapet, Mat4::identity(), MeshBuilder::NoBottom);
    ctx.base.addBox(AABB({fp.x0, top, fp.z0 + p}, {fp.x0 + p, top + ph, fp.z1 - p}), parapet, Mat4::identity(), MeshBuilder::NoBottom);
    ctx.base.addBox(AABB({fp.x1 - p, top, fp.z0 + p}, {fp.x1, top + ph, fp.z1 - p}), parapet, Mat4::identity(), MeshBuilder::NoBottom);
    }
    ctx.collider(AABB({fp.x0, 0.0f, fp.z0}, {fp.x1, top + ph, fp.z1}));

    const int floors = std::max(1, static_cast<int>(height / style.floorHeight));
    if (style.glassCurtain || style.floorBands) {
        // Horizontal bands at each floor line.
        const float protrude = style.glassCurtain ? 0.08f : 0.12f;
        const float bandH = style.glassCurtain ? 0.22f : 0.32f;
        const MaterialId bandMat = style.glassCurtain ? ctx.m.mullion : ctx.m.trimWhite;
        for (int f = 1; f < floors; ++f) {
            const float y = baseY + static_cast<float>(f) * style.floorHeight;
            ctx.detail.addBox(AABB({fp.x0 - protrude, y, fp.z0 - protrude}, {fp.x1 + protrude, y + bandH, fp.z1 + protrude}),
                              bandMat, Mat4::identity(), kSides);
        }
    }
    if (!style.windows) {
        return top;
    }
    for (int side = 0; side < 4; ++side) {
        const float len = facadeLength(fp, side);
        const int count = std::max(1, static_cast<int>((len - 1.5f) / 3.2f));
        const float spacing = len / static_cast<float>(count);
        for (int f = 0; f < floors; ++f) {
            const float y0 = baseY + static_cast<float>(f) * style.floorHeight + 1.0f;
            const float y1 = y0 + std::min(1.7f, style.floorHeight - 1.3f);
            for (int k = 0; k < count; ++k) {
                const bool lit = ctx.rng.chance(style.litChance);
                if (style.glassCurtain && !lit) {
                    continue; // dark glass already reads as windows
                }
                const float u = facadeStart(fp, side) + spacing * (static_cast<float>(k) + 0.5f);
                facadeQuad(ctx.detail, fp, side, u, style.glassCurtain ? spacing * 0.45f : 0.7f, y0, y1,
                           lit ? ctx.m.windowLit : ctx.m.windowDark);
            }
        }
    }
    return top;
}

void addRooftopClutter(Context& ctx, const Rect& roof, float y)
{
    const int units = ctx.rng.rangeInt(1, 3);
    for (int i = 0; i < units; ++i) {
        const float w = ctx.rng.range(1.5f, 3.0f), d = ctx.rng.range(1.5f, 3.0f);
        const float x = ctx.rng.range(roof.x0 + 1.0f, std::max(roof.x0 + 1.0f, roof.x1 - w - 1.0f));
        const float z = ctx.rng.range(roof.z0 + 1.0f, std::max(roof.z0 + 1.0f, roof.z1 - d - 1.0f));
        ctx.detail.addBox(AABB({x, y, z}, {x + w, y + ctx.rng.range(1.0f, 2.0f), z + d}), ctx.m.metal, Mat4::identity(),
                          MeshBuilder::NoBottom);
    }
    if (ctx.rng.chance(0.4f)) {
        const Vec3 c{roof.x1 - 3.0f, y, roof.z0 + 3.0f};
        ctx.base.addCylinder(c + Vec3{0, 2.0f, 0}, 3.0f, 1.4f, 1.4f, 10, ctx.m.roofTile);
        ctx.base.addCylinder(c, 2.0f, 0.1f, 0.1f, 4, ctx.m.metalDark, false);
    }
    if (ctx.rng.chance(0.5f)) {
        const Vec3 c{roof.cx(), y, roof.cz()};
        const float h = ctx.rng.range(5.0f, 12.0f);
        ctx.base.addCylinder(c, h, 0.12f, 0.06f, 5, ctx.m.metalDark, false);
        ctx.base.addBox(AABB(c + Vec3{-0.25f, h, -0.25f}, c + Vec3{0.25f, h + 0.5f, 0.25f}), ctx.m.beacon);
    }
}

void addHouse(Context& ctx, const Rect& plot, int doorSide)
{
    Rng& rng = ctx.rng;
    const float w = rng.range(9.0f, 12.5f), d = rng.range(8.0f, 11.0f);
    const float cx = plot.cx() + rng.range(-1.5f, 1.5f), cz = plot.cz() + rng.range(-1.5f, 1.5f);
    const Rect fp{cx - w * 0.5f, cz - d * 0.5f, cx + w * 0.5f, cz + d * 0.5f};
    const int floors = rng.chance(0.4f) ? 2 : 1;

    BuildingStyle style;
    style.wall = rng.chance(0.75f) ? ctx.m.pastels[rng.rangeInt(0, 5)] : ctx.m.walls[0];
    style.roof = ctx.m.roofGrey;
    style.floorHeight = 3.0f;
    style.litChance = 0.4f;
    const bool pitched = rng.chance(0.55f);
    style.parapet = !pitched;
    const float top = addBuilding(ctx, fp, kGround, 3.0f * static_cast<float>(floors), style);
    if (pitched) {
        pyramidRoof(ctx, fp, top, rng.range(1.6f, 2.4f), 0.5f, ctx.m.roofTile);
    }

    // Front door and awning on the street side.
    const int side = doorSide == 0 ? 0 : 1;
    facadeQuad(ctx.detail, fp, side, cz, 0.6f, kGround, kGround + 2.2f, ctx.m.doorWood, 0.06f);
    const float fx = side == 0 ? fp.x0 : fp.x1;
    const float out = side == 0 ? -1.6f : 1.6f;
    const MaterialId awning = rng.chance(0.5f) ? ctx.m.awningRed : ctx.m.awningTeal;
    ctx.detail.addQuad({fx, kGround + 2.7f, cz - 1.3f}, {fx, kGround + 2.7f, cz + 1.3f},
                       {fx + out, kGround + 2.3f, cz + 1.3f}, {fx + out, kGround + 2.3f, cz - 1.3f}, awning);

    // Back yard: pool, palms, hedge along the plot edge.
    const float backX = side == 0 ? fp.x1 + 1.5f : fp.x0 - 1.5f;
    if (rng.chance(0.35f)) {
        const float px0 = side == 0 ? backX : backX - 4.5f;
        const float px1 = std::min(px0 + 4.5f, plot.x1 - 0.5f);
        if (px1 - px0 > 2.5f && px0 > plot.x0 + 0.5f) {
            addPool(ctx, {px0, cz - 3.0f, px1, cz + 3.0f});
        }
    }
    const int palms = rng.rangeInt(0, 2);
    for (int i = 0; i < palms; ++i) {
        const float px = side == 0 ? plot.x0 + 1.5f : plot.x1 - 1.5f;
        const float pz = rng.chance(0.5f) ? plot.z0 + 2.0f : plot.z1 - 2.0f;
        addPalmTree(ctx, {px, kGround, pz}, rng.range(5.5f, 8.0f));
    }
    if (rng.chance(0.6f)) {
        const float hz = rng.chance(0.5f) ? plot.z0 + 0.4f : plot.z1 - 1.0f;
        addHedge(ctx, {plot.x0 + 1.0f, hz, plot.x1 - 1.0f, hz + 0.6f}, 1.1f);
    }
}

void addHotel(Context& ctx, const Rect& fp, int floors, MaterialId wall, bool grand)
{
    Rng& rng = ctx.rng;
    BuildingStyle style;
    style.wall = wall;
    style.roof = ctx.m.roofGrey;
    style.floorHeight = 3.3f;
    style.floorBands = true;
    style.litChance = 0.5f;
    const float height = style.floorHeight * static_cast<float>(floors);
    const float top = addBuilding(ctx, fp, kGround, height, style);

    // Streamline-moderne rounded front corners.
    const float r = std::min(1.8f, fp.depth() * 0.15f);
    for (const float z : {fp.z0, fp.z1}) {
        ctx.base.addCylinder({fp.x1, kGround, z}, height, r, r, 12, wall);
    }
    ctx.collider(AABB({fp.x1 - r, 0, fp.z0 - r}, {fp.x1 + r, top, fp.z0 + r}));
    ctx.collider(AABB({fp.x1 - r, 0, fp.z1 - r}, {fp.x1 + r, top, fp.z1 + r}));

    // Stepped crown above the entrance.
    const float cz = fp.cz();
    const float crownW = grand ? 10.0f : 6.0f;
    ctx.base.addBox(AABB({fp.x1 - 4.0f, top, cz - crownW * 0.5f}, {fp.x1, top + (grand ? 5.0f : 2.5f), cz + crownW * 0.5f}), wall,
                    Mat4::identity(), MeshBuilder::NoBottom);
    ctx.detail.addBox(AABB({fp.x1 - 0.1f, top + 0.4f, cz - crownW * 0.5f}, {fp.x1 + 0.15f, top + 0.7f, cz + crownW * 0.5f}),
                      ctx.m.neonCyan);

    // Vertical neon fin on the facade, rising above the roof.
    const MaterialId neon = rng.chance(0.5f) ? ctx.m.neonPink : ctx.m.neonCyan;
    const float finZ = grand ? cz : cz + rng.range(-fp.depth() * 0.25f, fp.depth() * 0.25f);
    ctx.detail.addBox(AABB({fp.x1 + 0.1f, kGround + 3.5f, finZ - 0.3f}, {fp.x1 + 0.8f, top + (grand ? 7.0f : 3.0f), finZ + 0.3f}), neon);
    ctx.light({fp.x1 + 1.5f, kGround + 5.0f, finZ}, neon == ctx.m.neonPink ? Vec3{1.0f, 0.3f, 0.8f} : Vec3{0.3f, 0.9f, 1.0f}, 12.0f, 1.2f);
    if (grand) {
        // Neon outline along the top of the facade.
        ctx.detail.addBox(AABB({fp.x1 + 0.05f, top - 0.5f, fp.z0}, {fp.x1 + 0.25f, top - 0.25f, fp.z1}), ctx.m.neonPink);
    }

    // Entrance awning and door.
    facadeQuad(ctx.detail, fp, 1, cz, 1.5f, kGround, kGround + 2.6f, ctx.m.windowDark, 0.06f);
    const MaterialId awning = rng.chance(0.5f) ? ctx.m.awningTeal : ctx.m.awningRed;
    ctx.detail.addQuad({fp.x1, kGround + 3.1f, cz + 3.0f}, {fp.x1, kGround + 3.1f, cz - 3.0f},
                       {fp.x1 + 2.4f, kGround + 2.6f, cz - 3.0f}, {fp.x1 + 2.4f, kGround + 2.6f, cz + 3.0f}, awning);
}

void addWarehouse(Context& ctx, const Rect& fp)
{
    const float h = ctx.rng.range(7.5f, 11.0f);
    const MaterialId wall = ctx.rng.chance(0.5f) ? ctx.m.warehouse : ctx.m.warehouseRust;
    ctx.base.addBox(AABB({fp.x0, kGround, fp.z0}, {fp.x1, kGround + h, fp.z1}), wall, Mat4::identity(), kSides);
    ctx.collider(AABB({fp.x0, 0, fp.z0}, {fp.x1, kGround + h + 2.0f, fp.z1}));
    // Pitched roof, ridge along X, with gable ends.
    const float y = kGround + h, rise = 2.0f, cz = fp.cz();
    ctx.base.addQuad({fp.x0, y, fp.z1}, {fp.x1, y, fp.z1}, {fp.x1, y + rise, cz}, {fp.x0, y + rise, cz}, ctx.m.roofGrey);
    ctx.base.addQuad({fp.x1, y, fp.z0}, {fp.x0, y, fp.z0}, {fp.x0, y + rise, cz}, {fp.x1, y + rise, cz}, ctx.m.roofGrey);
    ctx.base.addTriangle({fp.x1, y, fp.z1}, {fp.x1, y, fp.z0}, {fp.x1, y + rise, cz}, wall);
    ctx.base.addTriangle({fp.x0, y, fp.z0}, {fp.x0, y, fp.z1}, {fp.x0, y + rise, cz}, wall);
    // Roll-up doors and a row of high windows on the long sides.
    const int doors = std::max(1, static_cast<int>(fp.width() / 9.0f));
    for (int i = 0; i < doors; ++i) {
        const float u = fp.x0 + fp.width() * (static_cast<float>(i) + 0.5f) / static_cast<float>(doors);
        facadeQuad(ctx.detail, fp, 3, u, 2.0f, kGround, kGround + 4.5f, ctx.m.rollupDoor);
        facadeQuad(ctx.detail, fp, 2, u, 1.5f, kGround + h - 2.2f, kGround + h - 1.0f,
                   ctx.rng.chance(0.3f) ? ctx.m.windowLit : ctx.m.windowDark);
    }
    ctx.light({fp.cx(), kGround + 5.0f, fp.z1 + 1.5f}, {1.0f, 0.85f, 0.6f}, 14.0f, 1.0f);
}

void addContainerStack(Context& ctx, float x, float z, bool alongX)
{
    const int levels = ctx.rng.rangeInt(1, 3);
    const float L = 6.1f, Wd = 2.44f, H = 2.6f;
    for (int i = 0; i < levels; ++i) {
        const float y = kGround + static_cast<float>(i) * H;
        const AABB box = alongX ? AABB({x, y, z}, {x + L, y + H, z + Wd}) : AABB({x, y, z}, {x + Wd, y + H, z + L});
        ctx.base.addBox(box, ctx.m.containers[ctx.rng.rangeInt(0, 3)], Mat4::identity(), MeshBuilder::NoBottom);
    }
    const float top = kGround + static_cast<float>(levels) * H;
    ctx.collider(alongX ? AABB({x, 0, z}, {x + L, top, z + Wd}) : AABB({x, 0, z}, {x + Wd, top, z + L}));
}

void addSafehouse(Context& ctx, const Rect& lot, const Landmark& lm)
{
    (void)lm;
    const float cx = lot.cx();
    const Rect house{cx - 9.0f, 20.0f, cx + 9.0f, 34.0f};
    BuildingStyle style;
    style.wall = ctx.m.pastels[0];
    style.roof = ctx.m.roofGrey;
    style.floorHeight = 3.4f;
    style.floorBands = true;
    style.litChance = 0.7f;
    const float top = addBuilding(ctx, house, kGround, 6.8f, style);

    // Garage wing with roll-up door facing the street (north, -Z).
    const Rect garage{cx + 9.0f, 22.0f, cx + 15.0f, 32.0f};
    BuildingStyle gs = style;
    gs.windows = false;
    addBuilding(ctx, garage, kGround, 3.4f, gs);
    facadeQuad(ctx.detail, garage, 2, garage.cx(), 2.2f, kGround, kGround + 2.8f, ctx.m.rollupDoor, 0.05f);

    // Front door, awning and a neon sign above it.
    facadeQuad(ctx.detail, house, 2, cx, 0.8f, kGround, kGround + 2.4f, ctx.m.doorWood, 0.06f);
    ctx.detail.addQuad({cx + 2.0f, kGround + 3.0f, house.z0}, {cx - 2.0f, kGround + 3.0f, house.z0},
                       {cx - 2.0f, kGround + 2.5f, house.z0 - 2.2f}, {cx + 2.0f, kGround + 2.5f, house.z0 - 2.2f},
                       ctx.m.awningTeal);
    ctx.detail.addBox(AABB({cx - 2.5f, kGround + 3.6f, house.z0 - 0.25f}, {cx + 2.5f, kGround + 4.1f, house.z0 - 0.05f}),
                      ctx.m.neonCyan);
    ctx.light({cx, kGround + 3.0f, house.z0 - 2.0f}, {0.4f, 0.9f, 1.0f}, 10.0f, 1.2f);

    // Roof terrace rail and a palm-lined path from the street.
    ctx.detail.addBox(AABB({house.x0, top + 0.8f, house.z0}, {house.x1, top + 0.9f, house.z0 + 0.1f}), ctx.m.trimWhite);
    flatQuad(ctx.detail, cx - 1.5f, 8.0f, cx + 1.5f, house.z0, kGround + 0.01f, ctx.m.plaza);
    for (const float px : {cx - 4.0f, cx + 4.0f}) {
        addPalmTree(ctx, {px, kGround, 12.0f}, 7.5f);
        addPalmTree(ctx, {px, kGround, 17.0f}, 8.5f);
    }
    addHedge(ctx, {lot.x0 + 1.0f, 9.0f, cx - 2.5f, 9.6f}, 1.0f);
    // (East side left open: the driveway from the garage to the street.)
    flatQuad(ctx.detail, cx + 9.0f, 8.0f, cx + 15.0f, 22.0f, kGround + 0.01f, ctx.m.plaza);
    addPool(ctx, {cx - 6.0f, 39.0f, cx + 6.0f, 49.0f});
    addPalmTree(ctx, {cx - 9.0f, kGround, 44.0f}, 8.0f);
    addPalmTree(ctx, {cx + 9.0f, kGround, 44.0f}, 7.0f);
}

void addLandmarkTower(Context& ctx, const Rect& lot, const Landmark& lm)
{
    const float height = static_cast<float>(lm.params.numberOr("height", 140.0));
    BuildingStyle podium;
    podium.wall = ctx.m.concrete;
    podium.roof = ctx.m.roofGrey;
    podium.litChance = 0.5f;
    const Rect base = lot.inset(4.0f);
    const float podiumTop = addBuilding(ctx, base, kGround, 14.0f, podium);

    BuildingStyle glass;
    glass.wall = ctx.m.glass[1];
    glass.roof = ctx.m.roofGrey;
    glass.glassCurtain = true;
    glass.litChance = 0.4f;
    const Rect t1 = lot.inset(11.0f), t2 = lot.inset(14.0f), t3 = lot.inset(17.0f);
    const float h1 = height * 0.55f, h2 = height * 0.3f, h3 = height * 0.15f;
    const float y1 = addBuilding(ctx, t1, podiumTop, h1, glass);
    const float y2 = addBuilding(ctx, t2, y1, h2, glass);
    const float y3 = addBuilding(ctx, t3, y2, h3, glass);
    for (const auto& [r, y] : {std::pair{t1, y1}, std::pair{t2, y2}}) {
        ctx.detail.addBox(AABB({r.x0 - 0.1f, y - 0.6f, r.z0 - 0.1f}, {r.x1 + 0.1f, y - 0.3f, r.z1 + 0.1f}), ctx.m.neonPink,
                          Mat4::identity(), kSides);
    }
    // Spire with a beacon.
    const Vec3 c{lot.cx(), y3, lot.cz()};
    ctx.base.addCylinder(c, 24.0f, 0.6f, 0.08f, 8, ctx.m.metal);
    ctx.base.addBox(AABB(c + Vec3{-0.5f, 24.0f, -0.5f}, c + Vec3{0.5f, 25.0f, 0.5f}), ctx.m.beacon);
    ctx.light(c + Vec3{0, 24.5f, 0}, {1.0f, 0.3f, 0.3f}, 30.0f, 2.0f);
    // Plaza trees at the base.
    addRoundTree(ctx, {lot.x0 + 1.5f, kGround, lot.z0 + 1.5f}, 0.8f);
    addRoundTree(ctx, {lot.x1 - 1.5f, kGround, lot.z1 - 1.5f}, 0.8f);
}

void addLighthouse(Context& ctx, float x, float z)
{
    const float baseY = CityLayout::beachHeight(x);
    const Vec3 b{x, baseY, z};
    ctx.base.addCylinder(b, 1.0f, 3.2f, 3.0f, 14, ctx.m.concrete);
    constexpr int segments = 8;
    constexpr float segH = 3.0f;
    for (int i = 0; i < segments; ++i) {
        const float r0 = lerpf(2.4f, 1.6f, static_cast<float>(i) / segments);
        const float r1 = lerpf(2.4f, 1.6f, static_cast<float>(i + 1) / segments);
        ctx.base.addCylinder(b + Vec3{0, 1.0f + static_cast<float>(i) * segH, 0}, segH, r0, r1, 14,
                             i % 2 == 0 ? ctx.m.lighthouseRed : ctx.m.trimWhite, false);
    }
    const float topY = 1.0f + segments * segH;
    ctx.base.addCylinder(b + Vec3{0, topY, 0}, 0.3f, 2.5f, 2.5f, 14, ctx.m.metalDark);
    ctx.base.addCylinder(b + Vec3{0, topY + 0.3f, 0}, 2.2f, 1.3f, 1.3f, 12, ctx.m.beacon);
    ctx.base.addCylinder(b + Vec3{0, topY + 2.5f, 0}, 1.6f, 1.6f, 0.05f, 12, ctx.m.lighthouseRed);
    ctx.collider(AABB({x - 3.2f, baseY - 1.0f, z - 3.2f}, {x + 3.2f, baseY + topY + 4.0f, z + 3.2f}));
    ctx.light({x, baseY + topY + 1.4f, z}, {1.0f, 0.95f, 0.75f}, 45.0f, 2.2f);
}

void addCivicBuilding(Context& ctx, const Rect& lot, const Landmark& lm, CivicKind kind)
{
    (void)lm;
    BuildingStyle style;
    style.roof = ctx.m.roofGrey;
    style.litChance = 0.6f;
    switch (kind) {
    case CivicKind::Police: {
        // Station set back from the west road, with a forecourt and blue sign.
        const Rect fp{lot.x0 + 14.0f, lot.z0 + 8.0f, lot.x1 - 4.0f, lot.z1 - 8.0f};
        style.wall = ctx.m.walls[2];
        style.floorBands = true;
        const float top = addBuilding(ctx, fp, kGround, 10.2f, style);
        ctx.detail.addBox(AABB({fp.x0 - 0.3f, kGround + 3.4f, fp.cz() - 4.0f}, {fp.x0 - 0.05f, kGround + 4.4f, fp.cz() + 4.0f}),
                          ctx.m.policeBlue);
        facadeQuad(ctx.detail, fp, 0, fp.cz(), 1.6f, kGround, kGround + 2.6f, ctx.m.windowDark, 0.06f);
        ctx.light({fp.x0 - 2.0f, kGround + 4.0f, fp.cz()}, {0.3f, 0.5f, 1.0f}, 12.0f, 1.2f);
        ctx.base.addCylinder({fp.x0 - 6.0f, kGround, fp.z0}, 12.0f, 0.08f, 0.06f, 5, ctx.m.metal, false);
        ctx.base.addBox(AABB({fp.x0 - 6.0f, kGround + 10.0f, fp.z0}, {fp.x0 - 6.0f + 0.05f, kGround + 11.5f, fp.z0 + 2.2f}),
                        ctx.m.policeBlue);
        addRooftopClutter(ctx, fp, top);
        break;
    }
    case CivicKind::Hospital: {
        const Rect fp{lot.x0 + 8.0f, lot.z0 + 6.0f, lot.x1 - 8.0f, lot.z1 - 12.0f};
        style.wall = ctx.m.walls[0];
        style.floorBands = true;
        const float top = addBuilding(ctx, fp, kGround, 17.0f, style);
        // Red cross on the street (south) facade and a rooftop helipad.
        const float cx = fp.cx(), y = top - 4.0f, z = fp.z1 + 0.08f;
        ctx.detail.addBox(AABB({cx - 0.6f, y - 1.8f, z}, {cx + 0.6f, y + 1.8f, z + 0.15f}), ctx.m.crossRed);
        ctx.detail.addBox(AABB({cx - 1.8f, y - 0.6f, z}, {cx + 1.8f, y + 0.6f, z + 0.15f}), ctx.m.crossRed);
        ctx.light({cx, y, z + 2.0f}, {1.0f, 0.3f, 0.3f}, 14.0f, 1.0f);
        ctx.base.addCylinder({cx, top, fp.cz()}, 0.2f, 5.0f, 5.0f, 16, ctx.m.concreteDark);
        ctx.detail.addBox(AABB({cx - 1.5f, top + 0.2f, fp.cz() - 0.3f}, {cx + 1.5f, top + 0.25f, fp.cz() + 0.3f}), ctx.m.trimWhite);
        // Ambulance bay canopy.
        ctx.detail.addQuad({cx - 5.0f, kGround + 3.2f, fp.z1}, {cx + 5.0f, kGround + 3.2f, fp.z1},
                           {cx + 5.0f, kGround + 3.0f, fp.z1 + 4.0f}, {cx - 5.0f, kGround + 3.0f, fp.z1 + 4.0f}, ctx.m.awningRed);
        break;
    }
    case CivicKind::Dealership: {
        // Glass showroom with a forecourt facing the north road.
        const Rect fp{lot.x0 + 6.0f, lot.z0 + 18.0f, lot.x1 - 6.0f, lot.z1 - 4.0f};
        style.wall = ctx.m.glass[0];
        style.glassCurtain = true;
        style.floorHeight = 6.0f;
        addBuilding(ctx, fp, kGround, 6.0f, style);
        ctx.detail.addBox(AABB({fp.x0, kGround + 6.2f, fp.z0 - 0.3f}, {fp.x1, kGround + 7.0f, fp.z0 - 0.05f}), ctx.m.neonPink);
        ctx.light({fp.cx(), kGround + 5.0f, fp.z0 - 3.0f}, {1.0f, 0.4f, 0.8f}, 16.0f, 1.2f);
        // Bunting poles around the forecourt.
        for (float x = lot.x0 + 2.0f; x < lot.x1; x += 8.0f) {
            ctx.base.addCylinder({x, kGround, lot.z0 + 1.0f}, 5.0f, 0.06f, 0.05f, 4, ctx.m.metal, false);
            ctx.detail.addBox(AABB({x - 0.02f, kGround + 4.0f, lot.z0 + 1.0f}, {x + 0.02f, kGround + 4.8f, lot.z0 + 2.0f}),
                              ctx.m.pastels[ctx.rng.rangeInt(0, 5)]);
        }
        break;
    }
    }
}

} // namespace av::gen
