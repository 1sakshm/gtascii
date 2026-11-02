#pragma once

#include "Interiors.h"

#include "Math/Vec.h"
#include "World/CityLayout.h"

#include <string>
#include <vector>

// Secrets. Golden eggs are hidden around the map (each player collects each
// one once; all of them pays out), and two set pieces are there just to be
// found: a UFO hovering over the sea and a giant rubber duck in the canal.
namespace av::proto {

inline constexpr int kGoldenEggCount = 8;
inline constexpr int kGoldenEggIdBase = 1000; // stored with the flamingo pickups in the profile

inline const Vec3 kUfoPosition{405.0f, 78.0f, -230.0f};
inline const Vec3 kDuckPosition{-100.0f, CityLayout::kCanalWaterLevel, 288.0f};
inline constexpr int kGoldPaint = 8; // the reward car's paint

// A dead-end alley tucked into the corner of Bayfront Park, with the
// creator's name sprayed on the wall. It opens towards -z; the walls run
// 8 m along +z.
inline const Vec3 kAlleyPosition{-14.0f, CityLayout::kCurbHeight, 104.0f};
inline constexpr float kAlleyHalfWidth = 1.3f, kAlleyLength = 8.0f, kAlleyHeight = 4.5f;
inline constexpr const char* kAlleyMessage = "Someone sprayed this on the wall: creator: saksham";

// Solid walls of the alley (server and client must agree for prediction).
inline std::vector<AABB> alleyColliders()
{
    const Vec3 a = kAlleyPosition;
    const float w = kAlleyHalfWidth, t = 0.35f;
    return {
        AABB({a.x - w - t, a.y, a.z}, {a.x - w, a.y + kAlleyHeight, a.z + kAlleyLength}),                    // left wall
        AABB({a.x + w, a.y, a.z}, {a.x + w + t, a.y + kAlleyHeight, a.z + kAlleyLength}),                    // right wall
        AABB({a.x - w - t, a.y, a.z + kAlleyLength}, {a.x + w + t, a.y + kAlleyHeight, a.z + kAlleyLength + t}), // dead end
        AABB({a.x + 0.2f, a.y, a.z + 6.4f}, {a.x + w - 0.05f, a.y + 1.2f, a.z + 7.9f}),                         // dumpster
    };
}

struct EggSpot {
    enum class Where { HighestRoof, Fixed } where;
    int chunkX = 0, chunkZ = 0; // HighestRoof: the top of the tallest thing in this chunk
    Vec3 position;              // Fixed
    const char* hint;
};

// Interior-relative spots are resolved by the server (it knows the rooms).
inline std::vector<EggSpot> eggSpots(const std::vector<InteriorDef>& interiors)
{
    std::vector<EggSpot> spots = {
        {EggSpot::Where::HighestRoof, 0, -2, {}, "on top of Vice Tower"},
        {EggSpot::Where::HighestRoof, 3, -3, {}, "up the Point Lighthouse"},
        {EggSpot::Where::HighestRoof, 1, 2, {}, "on the hospital roof"},
        {EggSpot::Where::HighestRoof, 3, -5, {}, "high above the docks"},
        {EggSpot::Where::Fixed, 0, 0, kUfoPosition + Vec3{0.0f, -9.0f, 0.0f}, "under the thing in the sky"},
        {EggSpot::Where::Fixed, 0, 0, kDuckPosition + Vec3{0.0f, 4.6f, 1.2f}, "on a very big duck"},
        {EggSpot::Where::Fixed, 0, 0, {kWorldEdgeX - 2.5f, CityLayout::kWaterLevel + 0.2f, 60.0f}, "at the edge of the world"},
    };
    for (const InteriorDef& d : interiors) {
        if (d.kind == InteriorKind::Club) {
            spots.push_back({EggSpot::Where::Fixed, 0, 0, d.origin + Vec3{0.0f, 1.25f, 8.45f}, "with the DJ"});
        }
    }
    return spots;
}

} // namespace av::proto
