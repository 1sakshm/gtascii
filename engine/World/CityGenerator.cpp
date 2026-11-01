#include "World/CityGenerator.h"

#include "World/GenContext.h"

#include <cmath>
#include <iterator>

namespace av {

CityMaterials::CityMaterials(const MaterialLibrary& lib)
{
    auto r = [&lib](const char* n) { return lib.require(n); };
    asphalt = r("asphalt");
    laneYellow = r("lane_yellow");
    laneWhite = r("lane_white");
    sidewalk = r("sidewalk");
    curb = r("curb");
    grass = r("grass");
    sand = r("sand");
    wetSand = r("wet_sand");
    plaza = r("plaza");
    water = r("water");
    poolWater = r("pool_water");
    industrialGround = r("industrial_ground");
    concrete = r("concrete");
    concreteDark = r("concrete_dark");
    glass[0] = r("glass_blue");
    glass[1] = r("glass_teal");
    glass[2] = r("glass_dark");
    mullion = r("mullion");
    windowDark = r("window_dark");
    windowLit = r("window_lit");
    trimWhite = r("trim_white");
    roofTile = r("roof_tile");
    roofGrey = r("roof_grey");
    doorWood = r("door_wood");
    awningRed = r("awning_red");
    awningTeal = r("awning_teal");
    neonPink = r("neon_pink");
    neonCyan = r("neon_cyan");
    warehouse = r("warehouse");
    warehouseRust = r("warehouse_rust");
    rollupDoor = r("rollup_door");
    containers[0] = r("container_red");
    containers[1] = r("container_blue");
    containers[2] = r("container_green");
    containers[3] = r("container_orange");
    lighthouseRed = r("lighthouse_red");
    craneYellow = r("crane_yellow");
    policeBlue = r("police_blue");
    crossRed = r("cross_red");
    metal = r("metal");
    metalDark = r("metal_dark");
    pastels[0] = r("pastel_pink");
    pastels[1] = r("pastel_mint");
    pastels[2] = r("pastel_lavender");
    pastels[3] = r("pastel_peach");
    pastels[4] = r("pastel_yellow");
    pastels[5] = r("pastel_aqua");
    walls[0] = r("wall_white");
    walls[1] = r("wall_beige");
    walls[2] = r("wall_grey");
    walls[3] = r("wall_blue");
    pole = r("pole");
    lampGlow = r("lamp_glow");
    beacon = r("beacon");
    benchWood = r("bench_wood");
    umbrellas[0] = r("umbrella_a");
    umbrellas[1] = r("umbrella_b");
    towel = r("towel");
    lifeguard = r("lifeguard");
    palmTrunk = r("palm_trunk");
    palmLeaf = r("palm_leaf");
    treeTrunk = r("tree_trunk");
    treeLeaves = r("tree_leaves");
    hedge = r("hedge");
}

CityGenerator::CityGenerator(const CityLayout& layout, const MaterialLibrary& materials, Mesh newsboxModel)
    : layout_(layout)
    , mat_(materials)
    , newsbox_(std::move(newsboxModel))
{
}

namespace {

using gen::Context;
using gen::Rect;
using gen::kGround;

constexpr float S = kChunkSize;
constexpr float R = CityLayout::kRoadHalfWidth;
constexpr float W = CityLayout::kRoadHalfWidth + CityLayout::kSidewalkWidth;

void flatQuad(MeshBuilder& b, float x0, float z0, float x1, float z1, float y, MaterialId mat)
{
    b.addQuad({x0, y, z1}, {x1, y, z1}, {x1, y, z0}, {x0, y, z0}, mat);
}

MaterialId lotMaterial(const CityMaterials& m, District d)
{
    switch (d) {
    case District::Downtown: return m.plaza;
    case District::HotelStrip: return m.plaza;
    case District::Industrial: return m.industrialGround;
    default: return m.grass;
    }
}

// Sand terrain as a height grid with smooth normals, plus the sea surface.
void buildBeachTerrain(Context& ctx)
{
    constexpr float step = 4.0f;
    const int nx = static_cast<int>((S - W) / step);
    const int nz = static_cast<int>(S / step);
    auto h = [](float x) { return CityLayout::beachHeight(x); };
    for (int i = 0; i < nx; ++i) {
        const float x0 = W + static_cast<float>(i) * step, x1 = x0 + step;
        const float h0 = h(x0), h1 = h(x1);
        if (h0 < CityLayout::kWaterLevel - 0.3f) {
            continue; // well under water: hidden by the sea surface
        }
        const MaterialId mat = (h0 + h1) * 0.5f < 0.0f ? ctx.m.wetSand : ctx.m.sand;
        for (int j = 0; j < nz; ++j) {
            const float z0 = static_cast<float>(j) * step, z1 = z0 + step;
            ctx.base.addQuad({x0, h0, z1}, {x1, h1, z1}, {x1, h1, z0}, {x0, h0, z0}, mat);
        }
    }
    // Sea surface from just before the shoreline to the chunk edge.
    float shore = W;
    while (shore < S && h(shore) > CityLayout::kWaterLevel + 0.05f) {
        shore += 1.0f;
    }
    flatQuad(ctx.base, shore - 1.0f, 0.0f, S, S, CityLayout::kWaterLevel, ctx.m.water);
}

void buildGround(Context& ctx, District district, const ChunkRoads& roads)
{
    if (district == District::Ocean) {
        flatQuad(ctx.base, 0, 0, S, S, CityLayout::kWaterLevel, ctx.m.water);
        return;
    }
    const bool canal = ctx.layout.isCanal(ctx.chunk.coord);
    const bool docks = district == District::Docks;
    std::vector<float> xb{0, R, W, S - W, S - R, S};
    std::vector<float> zb{0, R, W, S - W, S - R, S};
    if (canal) {
        zb = {0, R, W, CityLayout::kCanalZ0, CityLayout::kCanalZ1, S - W, S - R, S};
    }
    if (docks) {
        xb = {0, R, W, CityLayout::kQuayEdge, S};
    }
    // Ground in canal/dock chunks extends down to the water so quay walls
    // and bridge decks have visible sides.
    const float deckBottom = (canal || docks) ? -0.8f : 0.0f;
    for (std::size_t i = 0; i + 1 < xb.size(); ++i) {
        for (std::size_t j = 0; j + 1 < zb.size(); ++j) {
            const float x0 = xb[i], x1 = xb[i + 1], z0 = zb[j], z1 = zb[j + 1];
            if (district == District::Beach && x0 >= W) {
                continue; // sand handled separately
            }
            const Surface s = ctx.layout.surface(ctx.chunk.coord, (x0 + x1) * 0.5f, (z0 + z1) * 0.5f);
            if (s == Surface::Water) {
                continue;
            }
            if (s == Surface::Road) {
                if (deckBottom < 0.0f) {
                    ctx.base.addBox(AABB({x0, deckBottom, z0}, {x1, 0.0f, z1}), ctx.m.asphalt, Mat4::identity(),
                                    MeshBuilder::NoBottom);
                } else {
                    flatQuad(ctx.base, x0, z0, x1, z1, 0.0f, ctx.m.asphalt);
                }
            } else {
                const MaterialId mat = s == Surface::Sidewalk ? ctx.m.sidewalk : lotMaterial(ctx.m, district);
                ctx.base.addBox(AABB({x0, deckBottom, z0}, {x1, kGround, z1}), mat, Mat4::identity(), MeshBuilder::NoBottom);
            }
        }
    }
    if (canal) {
        constexpr float z0 = CityLayout::kCanalZ0, z1 = CityLayout::kCanalZ1;
        flatQuad(ctx.base, 0, z0, S, z1, CityLayout::kCanalWaterLevel, ctx.m.water);
        // Bridge railings along the inner sidewalk edges and quay railings.
        const float rh = 1.0f;
        for (const float x : {W - 0.15f, S - W}) {
            ctx.base.addBox(AABB({x, kGround, z0}, {x + 0.15f, kGround + rh, z1}), ctx.m.trimWhite);
            ctx.collider(AABB({x, -1.0f, z0}, {x + 0.15f, kGround + rh, z1}));
        }
        for (const float z : {z0 - 0.15f, z1}) {
            ctx.base.addBox(AABB({W, kGround, z}, {S - W, kGround + rh, z + 0.15f}), ctx.m.trimWhite);
            ctx.collider(AABB({W, -1.0f, z}, {S - W, kGround + rh, z + 0.15f}));
        }
    }
    if (docks) {
        flatQuad(ctx.base, CityLayout::kQuayEdge, 0, S, S, CityLayout::kWaterLevel, ctx.m.water);
    }
    if (district == District::Beach) {
        buildBeachTerrain(ctx);
    }

    // Road markings: dashed yellow centre lines (each chunk paints its half
    // of the line on its border) and crosswalks next to intersections.
    constexpr float y = 0.012f, half = 0.12f, dash = 3.0f;
    auto dashesZ = [&](float x0, float x1, float zStart, float zEnd) {
        for (float z = zStart + 1.0f; z < zEnd - 1.0f; z += dash * 2.0f) {
            flatQuad(ctx.detail, x0, z, x1, std::min(z + dash, zEnd - 1.0f), y, ctx.m.laneYellow);
        }
    };
    auto dashesX = [&](float z0, float z1, float xStart, float xEnd) {
        for (float x = xStart + 1.0f; x < xEnd - 1.0f; x += dash * 2.0f) {
            flatQuad(ctx.detail, x, z0, std::min(x + dash, xEnd - 1.0f), z1, y, ctx.m.laneYellow);
        }
    };
    const float zA = roads.north ? R : 0.0f, zB = roads.south ? S - R : S;
    const float xA = roads.west ? R : 0.0f, xB = roads.east ? S - R : S;
    if (roads.west) dashesZ(0.0f, half, zA, zB);
    if (roads.east) dashesZ(S - half, S, zA, zB);
    if (roads.north) dashesX(0.0f, half, xA, xB);
    if (roads.south) dashesX(S - half, S, xA, xB);

    auto crosswalkAcrossVertical = [&](float x0, float x1, float z0) { // stripes run along z
        for (float x = x0 + 0.3f; x + 0.5f <= x1; x += 1.0f) {
            flatQuad(ctx.detail, x, z0, x + 0.5f, z0 + 2.5f, y, ctx.m.laneWhite);
        }
    };
    auto crosswalkAcrossHorizontal = [&](float z0, float z1, float x0) {
        for (float z = z0 + 0.3f; z + 0.5f <= z1; z += 1.0f) {
            flatQuad(ctx.detail, x0, z, x0 + 2.5f, z + 0.5f, y, ctx.m.laneWhite);
        }
    };
    if (roads.west && roads.north) { crosswalkAcrossVertical(0, R, R + 0.4f); crosswalkAcrossHorizontal(0, R, R + 0.4f); }
    if (roads.west && roads.south) { crosswalkAcrossVertical(0, R, S - R - 2.9f); crosswalkAcrossHorizontal(S - R, S, R + 0.4f); }
    if (roads.east && roads.north) { crosswalkAcrossVertical(S - R, S, R + 0.4f); crosswalkAcrossHorizontal(0, R, S - R - 2.9f); }
    if (roads.east && roads.south) { crosswalkAcrossVertical(S - R, S, S - R - 2.9f); crosswalkAcrossHorizontal(S - R, S, S - R - 2.9f); }
}

// Street lights, palms and furniture along each sidewalk.
void buildSidewalkProps(Context& ctx, District district, const ChunkRoads& roads)
{
    struct Side {
        bool present;
        bool alongZ;   // sidewalk runs along z
        float offset;  // fixed coordinate near the curb
        Vec3 arm;      // towards the road
    };
    const Side sides[4] = {
        {roads.west, true, R + 0.5f, {-1, 0, 0}},
        {roads.east, true, S - R - 0.5f, {1, 0, 0}},
        {roads.north, false, R + 0.5f, {0, 0, -1}},
        {roads.south, false, S - R - 0.5f, {0, 0, 1}},
    };
    const bool palms = district == District::HotelStrip || district == District::Residential ||
                       district == District::Beach || district == District::Park;
    for (const Side& side : sides) {
        if (!side.present) {
            continue;
        }
        auto at = [&side](float t, float inset = 0.0f) {
            return side.alongZ ? Vec3{side.offset - side.arm.x * inset, kGround, t}
                               : Vec3{t, kGround, side.offset - side.arm.z * inset};
        };
        for (const float t : {14.0f, 32.0f, 50.0f}) {
            addStreetLight(ctx, at(t), side.arm);
        }
        if (palms) {
            for (const float t : {23.0f, 41.0f}) {
                addPalmTree(ctx, at(t, 0.6f), ctx.rng.range(6.5f, 9.0f));
            }
        } else if (district == District::Downtown) {
            if (ctx.rng.chance(0.6f)) {
                addNewsbox(ctx, at(20.0f, 1.6f), side.alongZ ? (side.arm.x < 0 ? -kHalfPi : kHalfPi) : (side.arm.z < 0 ? kPi : 0.0f));
            }
            addBench(ctx, at(38.0f, 1.6f), !side.alongZ);
        }
    }
}

void buildDowntown(Context& ctx, const Rect& lot)
{
    const float mid = (lot.x0 + lot.x1) * 0.5f;
    const float midZ = (lot.z0 + lot.z1) * 0.5f;
    const Rect subs[4] = {{lot.x0, lot.z0, mid - 2, midZ - 2}, {mid + 2, lot.z0, lot.x1, midZ - 2},
                          {lot.x0, midZ + 2, mid - 2, lot.z1}, {mid + 2, midZ + 2, lot.x1, lot.z1}};
    for (const Rect& sub : subs) {
        if (ctx.rng.chance(0.15f)) {
            addFountain(ctx, {sub.cx(), kGround, sub.cz()});
            addRoundTree(ctx, {sub.x0 + 3, kGround, sub.z0 + 3}, 0.9f);
            addRoundTree(ctx, {sub.x1 - 3, kGround, sub.z1 - 3}, 0.9f);
            continue;
        }
        const float inset = ctx.rng.range(1.0f, 3.5f);
        const Rect fp = sub.inset(inset);
        const float height = ctx.rng.range(24.0f, 88.0f);
        gen::BuildingStyle style;
        if (ctx.rng.chance(0.55f)) {
            style.wall = ctx.m.glass[ctx.rng.rangeInt(0, 2)];
            style.glassCurtain = true;
        } else {
            const MaterialId opts[3] = {ctx.m.concrete, ctx.m.walls[1], ctx.m.walls[2]};
            style.wall = opts[ctx.rng.rangeInt(0, 2)];
        }
        style.roof = ctx.m.roofGrey;
        style.litChance = 0.45f;
        if (height > 50.0f) {
            // Setback: a podium and a narrower tower above it.
            const float podium = height * ctx.rng.range(0.35f, 0.55f);
            addBuilding(ctx, fp, kGround, podium, style);
            const Rect upper = fp.inset(std::min(fp.width(), fp.depth()) * 0.18f);
            const float top = addBuilding(ctx, upper, kGround + podium, height - podium, style);
            addRooftopClutter(ctx, upper, top);
        } else {
            const float top = addBuilding(ctx, fp, kGround, height, style);
            addRooftopClutter(ctx, fp, top);
        }
    }
}

void buildResidential(Context& ctx, const Rect& lot)
{
    const float mid = (lot.x0 + lot.x1) * 0.5f;
    const float midZ = (lot.z0 + lot.z1) * 0.5f;
    const Rect plots[4] = {{lot.x0, lot.z0, mid, midZ}, {mid, lot.z0, lot.x1, midZ},
                           {lot.x0, midZ, mid, lot.z1}, {mid, midZ, lot.x1, lot.z1}};
    for (int i = 0; i < 4; ++i) {
        // Houses face the street on their side of the block.
        const int doorSide = (i % 2 == 0) ? 0 : 1; // 0 = -X (west road), 1 = +X (east road)
        addHouse(ctx, plots[i], doorSide);
    }
}

void buildHotelStrip(Context& ctx, const Rect& lot)
{
    const float segment = lot.depth() / 3.0f;
    for (int i = 0; i < 3; ++i) {
        const Rect hotel{lot.x1 - ctx.rng.range(24.0f, 30.0f), lot.z0 + segment * static_cast<float>(i) + 1.0f,
                         lot.x1 - 1.5f, lot.z0 + segment * static_cast<float>(i + 1) - 1.0f};
        addHotel(ctx, hotel, ctx.rng.rangeInt(3, 6), ctx.m.pastels[ctx.rng.rangeInt(0, 5)], false);
    }
    // Pool deck behind the hotels.
    addPool(ctx, {lot.x0 + 4.0f, lot.cz() - 5.0f, lot.x0 + 14.0f, lot.cz() + 5.0f});
    for (int i = 0; i < 4; ++i) {
        addPalmTree(ctx, {lot.x0 + ctx.rng.range(2.0f, 18.0f), kGround, lot.z0 + ctx.rng.range(2.0f, lot.depth() - 2.0f)},
                    ctx.rng.range(6.0f, 9.0f));
    }
}

void buildPark(Context& ctx, const Rect& lot)
{
    // Cross-shaped paths through the middle.
    const float cx = lot.cx(), cz = lot.cz();
    const float py = kGround + 0.01f;
    flatQuad(ctx.detail, cx - 1.5f, lot.z0, cx + 1.5f, lot.z1, py, ctx.m.plaza);
    flatQuad(ctx.detail, lot.x0, cz - 1.5f, lot.x1, cz + 1.5f, py, ctx.m.plaza);
    addFountain(ctx, {cx, kGround, cz});

    for (int i = 0; i < 14; ++i) {
        float x = ctx.rng.range(lot.x0 + 2.0f, lot.x1 - 2.0f);
        float z = ctx.rng.range(lot.z0 + 2.0f, lot.z1 - 2.0f);
        if (std::fabs(x - cx) < 4.0f || std::fabs(z - cz) < 4.0f) {
            continue; // keep paths clear
        }
        if (ctx.rng.chance(0.3f)) {
            addPalmTree(ctx, {x, kGround, z}, ctx.rng.range(6.0f, 8.5f));
        } else {
            addRoundTree(ctx, {x, kGround, z}, ctx.rng.range(0.8f, 1.3f));
        }
    }
    for (const float t : {-12.0f, 12.0f}) {
        addBench(ctx, {cx + 2.6f, kGround, cz + t}, false);
        addBench(ctx, {cx + t, kGround, cz + 2.6f}, true);
        addStreetLight(ctx, {cx - 2.2f, kGround, cz + t}, {1, 0, 0});
    }
}

void buildIndustrial(Context& ctx, const Rect& lot)
{
    addWarehouse(ctx, {lot.x0 + 2.0f, lot.z0 + 2.0f, lot.x0 + 2.0f + ctx.rng.range(24.0f, 32.0f), lot.z0 + ctx.rng.range(20.0f, 26.0f)});
    // Container yard.
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (ctx.rng.chance(0.25f)) {
                continue;
            }
            addContainerStack(ctx, lot.x0 + 6.0f + static_cast<float>(col) * 13.0f, lot.z1 - 4.0f - static_cast<float>(row) * 4.5f, true);
        }
    }
    if (ctx.rng.chance(0.5f)) {
        const Vec3 base{lot.x1 - 5.0f, kGround, lot.z0 + 6.0f};
        ctx.base.addCylinder(base, 26.0f, 1.6f, 1.1f, 12, ctx.m.concreteDark);
        ctx.collider(AABB({base.x - 1.6f, 0, base.z - 1.6f}, {base.x + 1.6f, 26.0f, base.z + 1.6f}));
        ctx.base.addCylinder(base + Vec3{0, 23.0f, 0}, 1.0f, 1.25f, 1.25f, 12, ctx.m.lighthouseRed);
    }
}

// Canal chunks: promenades with palms, benches and lamps on both quays.
void buildCanalQuays(Context& ctx)
{
    for (const float z : {12.0f, 52.0f}) {
        for (float x = W + 4.0f; x < S - W - 2.0f; x += 9.0f) {
            addPalmTree(ctx, {x, kGround, z}, ctx.rng.range(6.0f, 8.0f));
            addBench(ctx, {x + 4.5f, kGround, z}, true);
        }
        addStreetLight(ctx, {S * 0.5f, kGround, z < 32.0f ? CityLayout::kCanalZ0 - 1.0f : CityLayout::kCanalZ1 + 1.0f},
                       {0, 0, z < 32.0f ? 1.0f : -1.0f});
    }
}

void buildDocks(Context& ctx)
{
    constexpr float q = CityLayout::kQuayEdge;
    // Wooden piers out over the water.
    for (const float z : {9.0f, 31.0f, 53.0f}) {
        const AABB deck({q - 0.5f, 0.2f, z - 1.6f}, {S - 1.0f, 0.5f, z + 1.6f});
        ctx.base.addBox(deck, ctx.m.benchWood);
        ctx.collider(deck);
        for (float x = q + 3.0f; x < S - 1.0f; x += 6.0f) {
            for (const float side : {-1.4f, 1.4f}) {
                ctx.base.addBox(AABB({x - 0.15f, -1.0f, z + side - 0.15f}, {x + 0.15f, 0.2f, z + side + 0.15f}), ctx.m.treeTrunk);
            }
        }
        addStreetLight(ctx, {S - 2.0f, 0.5f, z + 1.2f}, {0, 0, -1});
    }
    // Bollards along the quay edge.
    for (float z = 3.0f; z < S; z += 6.0f) {
        ctx.detail.addCylinder({q - 0.8f, kGround, z}, 0.6f, 0.2f, 0.25f, 6, ctx.m.metalDark);
    }
    // Container stacks and a gantry crane on the quay.
    for (int i = 0; i < 4; ++i) {
        addContainerStack(ctx, W + 3.0f + static_cast<float>(i % 2) * 8.0f, 6.0f + static_cast<float>(i) * 13.0f, true);
    }
    const float cx = q - 6.0f, cz = 31.0f;
    for (const float dx : {-3.0f, 3.0f}) {
        for (const float dz : {-5.0f, 5.0f}) {
            ctx.base.addBox(AABB({cx + dx - 0.4f, kGround, cz + dz - 0.4f}, {cx + dx + 0.4f, kGround + 18.0f, cz + dz + 0.4f}),
                            ctx.m.craneYellow);
            ctx.collider(AABB({cx + dx - 0.4f, 0, cz + dz - 0.4f}, {cx + dx + 0.4f, kGround + 18.0f, cz + dz + 0.4f}));
        }
    }
    ctx.base.addBox(AABB({cx - 3.5f, kGround + 18.0f, cz - 5.5f}, {cx + 3.5f, kGround + 19.5f, cz + 5.5f}), ctx.m.craneYellow);
    ctx.base.addBox(AABB({cx - 1.0f, kGround + 18.5f, cz - 1.0f}, {S + 8.0f, kGround + 19.2f, cz + 1.0f}), ctx.m.craneYellow);
    ctx.base.addBox(AABB({cx - 1.5f, kGround + 19.5f, cz - 1.5f}, {cx + 1.5f, kGround + 21.5f, cz + 1.5f}), ctx.m.concreteDark);
}

void buildBeach(Context& ctx)
{
    for (int i = 0; i < 7; ++i) {
        const float x = ctx.rng.range(W + 3.0f, 36.0f), z = ctx.rng.range(3.0f, S - 3.0f);
        addPalmTree(ctx, {x, CityLayout::beachHeight(x), z}, ctx.rng.range(6.0f, 9.5f));
    }
    for (int i = 0; i < 6; ++i) {
        const float x = ctx.rng.range(22.0f, 40.0f), z = ctx.rng.range(4.0f, S - 4.0f);
        addBeachUmbrella(ctx, {x, CityLayout::beachHeight(x), z});
        const float tx = x + 1.2f;
        const float ty = CityLayout::beachHeight(tx) + 0.02f;
        flatQuad(ctx.detail, tx, z - 0.9f, tx + 0.9f, z + 0.9f, ty, ctx.m.towel);
    }
    if (ctx.rng.chance(0.6f)) {
        const float x = 38.0f;
        addLifeguardTower(ctx, {x, CityLayout::beachHeight(x), ctx.rng.range(12.0f, 52.0f)});
    }
}

} // namespace

std::unique_ptr<Chunk> CityGenerator::generate(const ChunkCoord& coord) const
{
    auto chunk = std::make_unique<Chunk>();
    chunk->coord = coord;
    chunk->district = layout_.district(coord);
    Context ctx(layout_, mat_, newsbox_, *chunk, hash2i(layout_.seed(), coord.x, coord.z));

    const ChunkRoads roads = layout_.roads(coord);
    buildGround(ctx, chunk->district, roads);
    if (chunk->district != District::Ocean) {
        buildSidewalkProps(ctx, chunk->district, roads);
    }
    // Canal chunks keep only the quay strips as building land.
    const bool canal = layout_.isCanal(coord);

    const Rect fullLot{W, W, S - W, S - W};
    const Rect lot = canal ? Rect{W, CityLayout::kCanalZ1 + 1.0f, S - W, S - W} : fullLot;
    const Landmark* lm = layout_.landmark(coord);
    if (lm) {
        chunk->landmarkName = lm->name;
    }
    const std::string type = lm ? lm->type : "";

    if (type == "safehouse") {
        gen::addSafehouse(ctx, lot, *lm);
    } else if (type == "tower") {
        gen::addLandmarkTower(ctx, lot, *lm);
    } else if (type == "police_station") {
        gen::addCivicBuilding(ctx, lot, *lm, gen::CivicKind::Police);
    } else if (type == "hospital") {
        gen::addCivicBuilding(ctx, lot, *lm, gen::CivicKind::Hospital);
    } else if (type == "dealership") {
        gen::addCivicBuilding(ctx, lot, *lm, gen::CivicKind::Dealership);
    } else if (type == "grand_hotel") {
        gen::addHotel(ctx, lot.inset(1.0f), static_cast<int>(lm->params.numberOr("floors", 8)),
                      mat_.pastels[5], true);
    } else if (canal) {
        buildCanalQuays(ctx);
    } else {
        switch (chunk->district) {
        case District::Downtown: buildDowntown(ctx, lot); break;
        case District::Residential: buildResidential(ctx, lot); break;
        case District::HotelStrip: buildHotelStrip(ctx, lot); break;
        case District::Park: buildPark(ctx, lot); break;
        case District::Industrial: buildIndustrial(ctx, lot); break;
        case District::Beach: buildBeach(ctx); break;
        case District::Docks: buildDocks(ctx); break;
        case District::Ocean: break;
        }
        if (type == "lighthouse") {
            gen::addLighthouse(ctx, 30.0f, 32.0f);
        }
    }

    chunk->baseMesh.computeBounds();
    chunk->detailMesh.computeBounds();
    AABB local = chunk->baseMesh.bounds;
    local.expand(chunk->detailMesh.bounds);
    chunk->worldBounds = local.valid() ? AABB(local.min + chunk->origin(), local.max + chunk->origin())
                                       : AABB(chunk->origin(), chunk->origin() + Vec3{S, 1, S});
    return chunk;
}

} // namespace av
