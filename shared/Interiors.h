#pragma once

#include "Graphics/Lighting.h"
#include "Math/Geometry.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Enterable buildings. Each interior is a furnished room floating high above
// its building (out of sight of the street); a door marker on the sidewalk
// teleports you in, the exit marker inside brings you back. The server uses
// the boxes for collision, the client draws them. Also: the invisible wall
// at the edge of the map.
namespace av::proto {

inline constexpr float kInteriorFloorY = 300.0f;
inline constexpr float kWorldEdgeX = 448.0f;  // out at sea: you cannot swim, drive or fly past this
inline constexpr float kFlightCeiling = 165.0f; // above Vice Tower (140 m)

enum class InteriorKind : std::uint8_t { Home, Office, Club, Casino };

struct InteriorBox {
    AABB box;
    std::string material;
    bool solid = true;
};

struct InteriorDef {
    InteriorKind kind = InteriorKind::Home;
    std::string name;
    Vec3 door;            // outside, feet on the sidewalk
    float doorYaw = 0.0f; // facing away from the building (how you leave)
    Vec3 origin;          // centre of the floor inside
    Vec3 entry;           // where you appear inside
    float entryYaw = 0.0f;
    Vec3 exit;            // exit marker inside
    float halfX = 8.0f, halfZ = 6.0f, height = 3.2f;
    std::vector<InteriorBox> boxes;
    std::vector<PointLight> lights;
    std::vector<Vec3> slots;     // casino: slot machines (stand here, press E)
    std::vector<Vec3> dancers;   // club: where the crowd dances
    std::vector<Vec3> computers; // office: trading terminals
    Vec3 bed;                    // home: save / sleep spot

    bool contains(const Vec3& p) const
    {
        return std::fabs(p.x - origin.x) < halfX + 1.0f && std::fabs(p.z - origin.z) < halfZ + 1.0f &&
               p.y > origin.y - 2.0f && p.y < origin.y + height + 2.0f;
    }
};

// Reads assets/maps/interiors.json (door positions) and builds the rooms.
std::vector<InteriorDef> loadInteriors(const std::filesystem::path& file);
// The interior a position is in, or nullptr.
const InteriorDef* findInterior(const std::vector<InteriorDef>& interiors, const Vec3& p);

// Every solid box of every interior plus the map edge wall.
std::vector<AABB> staticExtraColliders(const std::vector<InteriorDef>& interiors);

const char* toString(InteriorKind k);

} // namespace av::proto
