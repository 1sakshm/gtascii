#include "TestHarness.h"

#include "Graphics/Material.h"
#include "World/ChunkManager.h"
#include "World/CityGenerator.h"
#include "World/CityLayout.h"
#include "World/WorldCoords.h"

#include <cmath>
#include <filesystem>

using namespace av;

namespace {

const std::filesystem::path kRoot = AV_SOURCE_DIR;

MaterialLibrary& realMaterials()
{
    static MaterialLibrary lib = [] {
        MaterialLibrary l;
        l.loadFile(kRoot / "assets/materials/materials.json");
        return l;
    }();
    return lib;
}

bool sameMesh(const Mesh& a, const Mesh& b)
{
    if (a.vertices.size() != b.vertices.size() || a.indices != b.indices || a.triangleMaterials != b.triangleMaterials) {
        return false;
    }
    for (std::size_t i = 0; i < a.vertices.size(); ++i) {
        if (a.vertices[i].position != b.vertices[i].position) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("WorldCoords: floor division, origins, local coordinates")
{
    CHECK(worldToChunk(0.0f, 0.0f) == (ChunkCoord{0, 0}));
    CHECK(worldToChunk(63.9f, 64.0f) == (ChunkCoord{0, 1}));
    CHECK(worldToChunk(-0.1f, -64.0f) == (ChunkCoord{-1, -1}));
    CHECK(worldToChunk(-64.1f, 0.0f) == (ChunkCoord{-2, 0}));
    CHECK(chunkOrigin({-2, 3}) == Vec3(-128.0f, 0.0f, 192.0f));
    const Vec3 local = worldToChunkLocal({-10.0f, 5.0f, 70.0f});
    CHECK(nearlyEqual(local, Vec3{54.0f, 5.0f, 6.0f}));
    CHECK_EQ(chunkDistance({0, 0}, {3, -2}), 3);
    CHECK(ChunkCoordHash{}({1, 2}) != ChunkCoordHash{}({2, 1}));
}

TEST_CASE("CityLayout: districts and the road network are consistent")
{
    const CityLayout layout(1986);
    CHECK(layout.district({4, 0}) == District::Ocean);
    CHECK(layout.district({3, 7}) == District::Beach);
    CHECK(layout.district({2, -5}) == District::HotelStrip);
    CHECK(layout.district({0, -2}) == District::Downtown);
    CHECK(layout.district({-6, 3}) == District::Industrial);

    // A road on a chunk border is present from both sides.
    for (int cx = -6; cx <= 4; ++cx) {
        for (int cz = -3; cz <= 3; ++cz) {
            const ChunkRoads a = layout.roads({cx, cz});
            CHECK_EQ(a.east, layout.roads({cx + 1, cz}).west);
            CHECK_EQ(a.south, layout.roads({cx, cz + 1}).north);
        }
    }
    CHECK(layout.roads({3, 0}).west);   // coast road
    CHECK(!layout.roads({3, 0}).north); // no cross streets on the beach
    CHECK(!layout.roads({4, 0}).west);  // nothing in the sea
}

TEST_CASE("CityLayout: terrain heights for roads, curbs, beach and sea")
{
    const CityLayout layout(1986);
    CHECK_EQ(layout.groundHeight(2.0f, 30.0f), 0.0f);                       // road (west edge of chunk 0)
    CHECK_EQ(layout.groundHeight(6.0f, 30.0f), CityLayout::kCurbHeight);    // sidewalk
    CHECK_EQ(layout.groundHeight(30.0f, 30.0f), CityLayout::kCurbHeight);   // lot
    const float beachX = 3.0f * kChunkSize;
    CHECK(layout.groundHeight(beachX + 10.0f, 30.0f) > layout.groundHeight(beachX + 50.0f, 30.0f)); // slopes to sea
    CHECK_EQ(layout.groundHeight(5.0f * kChunkSize, 0.0f), CityLayout::kDeepSea); // open water is deep enough to swim
    // Beach meets seabed without a step at the chunk border.
    CHECK(nearlyEqual(layout.groundHeight(4.0f * kChunkSize - 0.01f, 10.0f), CityLayout::kSeabed, 0.01f));
    CHECK(layout.surface({3, 0}, 60.0f, 10.0f) == Surface::Water);
    CHECK(layout.surface({3, 0}, 15.0f, 10.0f) == Surface::Sand);
}

TEST_CASE("CityLayout: landmarks and spawn load from the map file")
{
    const CityLayout layout = CityLayout::fromFile(1986, kRoot / "assets/maps/landmarks.json");
    CHECK(layout.landmarks().size() >= 4);
    const Landmark* safehouse = layout.landmark({0, 0});
    CHECK(safehouse != nullptr && safehouse->type == "safehouse");
    CHECK(layout.landmark({50, 50}) == nullptr);
    CHECK(layout.district({-1, 1}) == District::Park); // overridden by the map
    CHECK(worldToChunk(layout.spawn().position) == (ChunkCoord{0, 0}));
}

TEST_CASE("CityGenerator: deterministic, non-empty chunks for every district")
{
    const CityLayout layout = CityLayout::fromFile(1986, kRoot / "assets/maps/landmarks.json");
    const CityGenerator gen(layout, realMaterials(), Mesh{});
    const ChunkCoord samples[] = {{0, 0}, {0, -2}, {1, -1}, {-2, 2}, {2, 0}, {2, 1}, {3, 0}, {3, -3}, {4, 0}, {-5, 0}, {-1, 1}};
    for (const ChunkCoord& c : samples) {
        const auto a = gen.generate(c);
        const auto b = gen.generate(c);
        CHECK(!a->baseMesh.empty());
        CHECK(sameMesh(a->baseMesh, b->baseMesh));
        CHECK(sameMesh(a->detailMesh, b->detailMesh));
        CHECK_EQ(a->colliders.size(), b->colliders.size());
        // Everything stays near its chunk (rounded hotel corners may poke
        // slightly into the sidewalk, never into neighbouring chunks).
        const Vec3 o = a->origin();
        CHECK(a->worldBounds.min.x >= o.x - 1.0f && a->worldBounds.max.x <= o.x + kChunkSize + 1.0f);
        CHECK(a->worldBounds.min.z >= o.z - 1.0f && a->worldBounds.max.z <= o.z + kChunkSize + 1.0f);
        if (a->district != District::Ocean) {
            CHECK(!a->colliders.empty());
            CHECK(!a->lights.empty());
        }
    }
    // Downtown has tall buildings; the landmark tower is the tallest.
    CHECK(gen.generate({0, -2})->worldBounds.max.y > 130.0f);
    CHECK(gen.generate({0, -2})->landmarkName == "Vice Tower");
}

TEST_CASE("ChunkManager: streams nearest-first, unloads with hysteresis")
{
    const CityLayout layout(1986);
    const CityGenerator gen(layout, realMaterials(), Mesh{});
    ChunkManager mgr(gen, 1);

    const int first = mgr.update({32, 0, 32}, 1);
    CHECK_EQ(first, 1);
    CHECK(mgr.find({0, 0}) != nullptr); // the focus chunk comes first
    mgr.loadAll({32, 0, 32});
    CHECK_EQ(mgr.chunks().size(), std::size_t{9});

    // Moving one chunk east keeps the old column (radius + 1 hysteresis).
    mgr.loadAll({96, 0, 32});
    CHECK(mgr.find({-1, 0}) != nullptr);
    CHECK_EQ(mgr.chunks().size(), std::size_t{12});
    // Moving further drops it.
    mgr.loadAll({160, 0, 32});
    CHECK(mgr.find({-1, 0}) == nullptr);
    CHECK(mgr.stats().unloadedTotal > 0);

    // Collision queries only see loaded chunks and match their colliders.
    std::vector<AABB> boxes;
    mgr.gatherColliders(AABB({128, 0, 0}, {192, 300, 64}), boxes);
    CHECK_EQ(boxes.size(), mgr.find({2, 0})->colliders.size());
    boxes.clear();
    mgr.gatherColliders(AABB({-128, 0, 0}, {-64, 300, 64}), boxes);
    CHECK(boxes.empty());
    CHECK_EQ(mgr.groundHeight(130.0f, 30.0f), 0.0f);
}
