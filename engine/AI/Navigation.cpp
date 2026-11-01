#include "AI/Navigation.h"

#include <cmath>
#include <limits>

namespace av {

namespace {
// Corner offsets in chunk-local coordinates.
constexpr float kNear = PedestrianNav::kInset;
constexpr float kFar = kChunkSize - PedestrianNav::kInset;
const float kCornerX[4] = {kNear, kFar, kFar, kNear};
const float kCornerZ[4] = {kNear, kNear, kFar, kFar};

bool isCityBlock(const ChunkCoord& c)
{
    return c.x < CityLayout::kBeachColumn;
}
} // namespace

bool PedestrianNav::valid(const NavNode& n) const
{
    if (n.corner < 0 || n.corner > 3) {
        return false;
    }
    if (isCityBlock(n.chunk)) {
        return true;
    }
    // Beach and docks: only the west sidewalk along the coast road.
    if (n.chunk.x == CityLayout::kBeachColumn) {
        return n.corner == 0 || n.corner == 3;
    }
    return false;
}

Vec3 PedestrianNav::position(const NavNode& n) const
{
    const Vec3 o = chunkOrigin(n.chunk);
    return {o.x + kCornerX[n.corner], CityLayout::kCurbHeight, o.z + kCornerZ[n.corner]};
}

void PedestrianNav::neighbors(const NavNode& n, std::vector<NavNode>& out) const
{
    if (!valid(n)) {
        return;
    }
    auto push = [&](const NavNode& m) {
        if (valid(m)) {
            out.push_back(m);
        }
    };
    const ChunkCoord c = n.chunk;
    if (n.chunk.x == CityLayout::kBeachColumn) {
        // Continuous coast-road sidewalk, plus crossings to the hotel strip.
        push({c, n.corner == 0 ? 3 : 0});
        push(n.corner == 0 ? NavNode{{c.x, c.z - 1}, 3} : NavNode{{c.x, c.z + 1}, 0});
        push({{c.x - 1, c.z}, n.corner == 0 ? 1 : 2});
        return;
    }
    // Around the block.
    push({c, (n.corner + 1) % 4});
    push({c, (n.corner + 3) % 4});
    // Across the roads at the corner (crosswalks).
    const ChunkRoads r = layout_.roads(c);
    switch (n.corner) {
    case 0:
        if (r.west) push({{c.x - 1, c.z}, 1});
        if (r.north) push({{c.x, c.z - 1}, 3});
        break;
    case 1:
        if (r.east) push({{c.x + 1, c.z}, 0});
        if (r.north) push({{c.x, c.z - 1}, 2});
        break;
    case 2:
        if (r.east) push({{c.x + 1, c.z}, 3});
        if (r.south) push({{c.x, c.z + 1}, 1});
        break;
    default:
        if (r.west) push({{c.x - 1, c.z}, 2});
        if (r.south) push({{c.x, c.z + 1}, 0});
        break;
    }
}

bool PedestrianNav::nearest(const Vec3& p, NavNode& out) const
{
    const ChunkCoord centre = worldToChunk(p);
    float best = std::numeric_limits<float>::max();
    bool found = false;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int k = 0; k < 4; ++k) {
                const NavNode n{{centre.x + dx, centre.z + dz}, k};
                if (!valid(n)) {
                    continue;
                }
                const Vec3 q = position(n);
                const float d = (q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z);
                if (d < best) {
                    best = d;
                    out = n;
                    found = true;
                }
            }
        }
    }
    return found;
}

Vec3 headingVector(Heading h)
{
    switch (h) {
    case Heading::North: return {0, 0, -1};
    case Heading::East: return {1, 0, 0};
    case Heading::South: return {0, 0, 1};
    default: return {-1, 0, 0};
    }
}

Heading opposite(Heading h) { return static_cast<Heading>((static_cast<int>(h) + 2) % 4); }
Heading turnRight(Heading h) { return static_cast<Heading>((static_cast<int>(h) + 1) % 4); }
Heading turnLeft(Heading h) { return static_cast<Heading>((static_cast<int>(h) + 3) % 4); }

Vec3 laneOffset(Heading h)
{
    const Vec3 d = headingVector(h);
    return Vec3{-d.z, 0, d.x} * RoadNetwork::kLaneOffset;
}

bool RoadNetwork::exists(const Intersection& i) const
{
    return i.k <= CityLayout::kBeachColumn;
}

bool RoadNetwork::hasRoad(const Intersection& i, Heading h) const
{
    if (!exists(i)) {
        return false;
    }
    switch (h) {
    case Heading::North:
    case Heading::South: return true;                              // north-south lines up to the coast road
    case Heading::East: return i.k < CityLayout::kBeachColumn;     // cross streets stop at the coast
    default: return true;                                          // i.k <= coast, so there is a column to the west
    }
}

Vec3 RoadNetwork::position(const Intersection& i) const
{
    return {static_cast<float>(i.k) * kChunkSize, 0.0f, static_cast<float>(i.m) * kChunkSize};
}

Intersection RoadNetwork::step(const Intersection& i, Heading h)
{
    switch (h) {
    case Heading::North: return {i.k, i.m - 1};
    case Heading::East: return {i.k + 1, i.m};
    case Heading::South: return {i.k, i.m + 1};
    default: return {i.k - 1, i.m};
    }
}

Intersection RoadNetwork::nearest(const Vec3& p) const
{
    Intersection i{static_cast<int>(std::lround(p.x / kChunkSize)), static_cast<int>(std::lround(p.z / kChunkSize))};
    if (i.k > CityLayout::kBeachColumn) {
        i.k = CityLayout::kBeachColumn;
    }
    return i;
}

Vec3 RoadNetwork::laneStart(const Intersection& from, Heading h) const
{
    return position(from) + headingVector(h) * kStopDistance + laneOffset(h);
}

Vec3 RoadNetwork::laneEnd(const Intersection& from, Heading h) const
{
    return position(step(from, h)) - headingVector(h) * kStopDistance + laneOffset(h);
}

} // namespace av
