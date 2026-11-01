#pragma once

#include "Physics/Vehicle.h"
#include "World/CityLayout.h"

#include <filesystem>
#include <string>
#include <vector>

namespace av::proto {

enum class MissionType { Courier, Checkpoints, Delivery, Escape };

// One mission from assets/missions/missions.json. Places are given as a
// chunk plus a chunk-local position and resolved to world coordinates.
struct MissionDef {
    std::string id;
    std::string title;
    std::string briefing;
    MissionType type = MissionType::Courier;
    Vec3 start;
    std::vector<Vec3> points; // courier: pickup, dropoff; checkpoints: in order; delivery: dropoff
    float timeLimit = 0.0f;   // seconds, 0 = none
    int reward = 0;
    bool requiresVehicle = false;
    VehicleModel vehicle = VehicleModel::Coupe; // delivery
    Vec3 vehicleSpawn;                         // delivery
    float vehicleYaw = 0.0f;
    float damageLimit = 0.7f;                  // delivery
    int wanted = 0;                            // escape
};

struct RaceDef {
    std::string id, name;
    Vec3 start;
    float startYaw = 0.0f;
    std::vector<Vec3> checkpoints;
};

std::vector<RaceDef> loadRaces(const std::filesystem::path& path, const CityLayout& layout);

// Throws std::runtime_error on malformed data.
std::vector<MissionDef> loadMissions(const std::filesystem::path& path, const CityLayout& layout);

// Resolves {"chunk": [x, z], "at": [lx, lz]} to a world position on the ground.
Vec3 resolvePlace(const JsonValue& place, const CityLayout& layout);

} // namespace av::proto
