#pragma once

// Internal to the city generator: shared state and builders for one chunk.

#include "Core/Random.h"
#include "World/CityGenerator.h"

namespace av::gen {

struct Context {
    const CityLayout& layout;
    const CityMaterials& m;
    const Mesh& newsbox;
    Chunk& chunk;
    Rng rng;
    MeshBuilder base;
    MeshBuilder detail;

    Context(const CityLayout& l, const CityMaterials& mats, const Mesh& news, Chunk& c, std::uint32_t seed)
        : layout(l), m(mats), newsbox(news), chunk(c), rng(seed), base(c.baseMesh), detail(c.detailMesh)
    {
    }

    Vec3 toWorld(const Vec3& local) const { return chunk.origin() + local; }
    // Registers a solid box given in chunk-local coordinates.
    void collider(const AABB& local)
    {
        chunk.colliders.push_back(AABB(toWorld(local.min), toWorld(local.max)));
    }
    void light(const Vec3& local, const Vec3& color, float range, float intensity = 1.0f)
    {
        chunk.lights.push_back({toWorld(local), color, range, intensity});
    }
};

// Lot (block interior) bounds in chunk-local coordinates.
struct Rect {
    float x0, z0, x1, z1;
    float width() const { return x1 - x0; }
    float depth() const { return z1 - z0; }
    float cx() const { return (x0 + x1) * 0.5f; }
    float cz() const { return (z0 + z1) * 0.5f; }
    Rect inset(float d) const { return {x0 + d, z0 + d, x1 - d, z1 - d}; }
};

inline constexpr float kGround = CityLayout::kCurbHeight; // lot surface height

// ---- Buildings.cpp ----
struct BuildingStyle {
    MaterialId wall;
    MaterialId roof;
    float floorHeight = 3.4f;
    bool windows = true;
    bool glassCurtain = false; // whole facade glass with mullion bands
    bool floorBands = false;   // protruding trim at each floor (art deco)
    bool parapet = true;
    float litChance = 0.35f;
};
// Box building with windows on all four sides; returns the roof height.
float addBuilding(Context& ctx, const Rect& footprint, float baseY, float height, const BuildingStyle& style);
void addRooftopClutter(Context& ctx, const Rect& roof, float roofY);
void addHouse(Context& ctx, const Rect& plot, int doorSide);
void addHotel(Context& ctx, const Rect& lot, int floors, MaterialId wall, bool grand);
void addWarehouse(Context& ctx, const Rect& lot);
void addContainerStack(Context& ctx, float x, float z, bool alongX);
void addSafehouse(Context& ctx, const Rect& lot, const Landmark& lm);
void addLandmarkTower(Context& ctx, const Rect& lot, const Landmark& lm);
void addLighthouse(Context& ctx, float x, float z);
enum class CivicKind { Police, Hospital, Dealership };
void addCivicBuilding(Context& ctx, const Rect& lot, const Landmark& lm, CivicKind kind);

// ---- Props.cpp ----
void addPalmTree(Context& ctx, const Vec3& base, float height);
void addRoundTree(Context& ctx, const Vec3& base, float scale);
void addStreetLight(Context& ctx, const Vec3& base, const Vec3& armDirection);
void addBench(Context& ctx, const Vec3& base, bool alongX);
void addNewsbox(Context& ctx, const Vec3& base, float yaw);
void addFountain(Context& ctx, const Vec3& centre);
void addBeachUmbrella(Context& ctx, const Vec3& base);
void addLifeguardTower(Context& ctx, const Vec3& base);
void addPool(Context& ctx, const Rect& r);
void addHedge(Context& ctx, const Rect& r, float height);

} // namespace av::gen
