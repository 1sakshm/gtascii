#pragma once

#include "Core/Json.h"
#include "World/WorldCoords.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace av {

enum class District { Downtown, Residential, HotelStrip, Beach, Ocean, Park, Industrial, Docks };
const char* toString(District d);

// A hand-placed landmark (loaded from assets/maps/landmarks.json). The
// generator owns how each type is built; the data file owns where.
struct Landmark {
    std::string name;
    std::string type;   // safehouse, tower, grand_hotel, lighthouse
    ChunkCoord chunk;
    JsonValue params;   // type-specific options
};

struct SpawnPoint {
    Vec3 position{20.0f, 0.2f, 40.0f};
    float yaw = 0.0f;
};

// Which edges of a chunk carry a (half-width) road.
struct ChunkRoads {
    bool west = false, east = false, north = false, south = false;
};

enum class Surface { Road, Sidewalk, Lot, Sand, Water };

// The city's large-scale rules, shared by generation and physics:
// district map, road network, surface classification and terrain height.
// Everything is a pure function of (seed, landmarks, position), so chunks
// can be generated in any order and regenerate identically after unloading.
//
// Map: the coast runs north-south. Chunk column x = 3 is the beach, x >= 4
// is ocean; x = 2 is the Ocean Drive hotel strip; downtown sits north of the
// origin; industry lies far west. Roads follow chunk borders.
class CityLayout {
public:
    static constexpr int kBeachColumn = 3;
    static constexpr float kRoadHalfWidth = 5.0f;  // road centred on chunk borders
    static constexpr float kSidewalkWidth = 3.0f;
    static constexpr float kCurbHeight = 0.15f;
    static constexpr float kWaterLevel = -0.25f;
    static constexpr float kSeabed = -1.0f;
    // A canal crosses the city along chunk row z = kCanalRow (bridged by the
    // north-south roads); the docks occupy the coast north of kDocksRow.
    static constexpr int kCanalRow = 4;
    static constexpr float kCanalZ0 = 16.0f, kCanalZ1 = 48.0f;
    static constexpr float kCanalWaterLevel = -0.4f;
    static constexpr float kCanalFloor = -2.4f;   // deep enough to swim
    static constexpr float kDeepSea = -7.0f;      // open ocean floor, past the shallows
    static constexpr int kDocksRow = -4; // beach column chunks with z <= this are docks
    static constexpr float kQuayEdge = 36.0f;

    explicit CityLayout(std::uint32_t seed = 1986, std::vector<Landmark> landmarks = {});

    // Loads {"spawn": {...}, "landmarks": [...]}. Throws on malformed data.
    static CityLayout fromFile(std::uint32_t seed, const std::filesystem::path& path);

    std::uint32_t seed() const noexcept { return seed_; }
    District district(const ChunkCoord& c) const;
    ChunkRoads roads(const ChunkCoord& c) const;
    Surface surface(const ChunkCoord& c, float localX, float localZ) const;
    float groundHeight(float worldX, float worldZ) const;
    // Water surface at a position, or -1e9 on dry land.
    float waterLevel(float worldX, float worldZ) const;
    const Landmark* landmark(const ChunkCoord& c) const;
    const Landmark* findLandmark(const std::string& type) const;
    bool isCanal(const ChunkCoord& c) const { return c.z == kCanalRow && c.x < kBeachColumn; }
    // Human-readable neighbourhood name for the HUD.
    std::string neighborhood(const ChunkCoord& c) const;
    const std::vector<Landmark>& landmarks() const noexcept { return landmarks_; }
    const SpawnPoint& spawn() const noexcept { return spawn_; }

    // Beach terrain profile (local x within a beach chunk).
    static float beachHeight(float localX);

private:
    std::uint32_t seed_;
    std::vector<Landmark> landmarks_;
    SpawnPoint spawn_;
};

} // namespace av
