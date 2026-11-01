#pragma once

#include "World/CityLayout.h"

#include <vector>

namespace av {

// ---- Pedestrian navigation ----------------------------------------------
//
// Pedestrians walk a graph whose nodes are the four sidewalk corners of each
// city block (0 NW, 1 NE, 2 SE, 3 SW, at 7 m from the chunk edges, i.e. the
// middle of the sidewalk). Edges follow the sidewalk around the block and
// cross roads at the crosswalks. The graph is a pure function of the city
// layout, so it never needs to be stored and always matches the streets.
struct NavNode {
    ChunkCoord chunk;
    int corner = 0;
    bool operator==(const NavNode& o) const { return chunk == o.chunk && corner == o.corner; }
    bool operator!=(const NavNode& o) const { return !(*this == o); }
};

class PedestrianNav {
public:
    static constexpr float kInset = 7.0f;

    explicit PedestrianNav(const CityLayout& layout) : layout_(layout) {}

    bool valid(const NavNode& n) const;
    Vec3 position(const NavNode& n) const;
    void neighbors(const NavNode& n, std::vector<NavNode>& out) const;
    // Nearest valid node to a world position (searches the surrounding chunks).
    bool nearest(const Vec3& p, NavNode& out) const;

private:
    const CityLayout& layout_;
};

// ---- Road network -------------------------------------------------------
//
// Roads run along chunk borders; intersections sit on the chunk grid. Cars
// drive on the right: each direction has one lane, 2.5 m from the centre line.
enum class Heading : int { North, East, South, West }; // North = -Z, East = +X

Vec3 headingVector(Heading h);
Heading opposite(Heading h);
Heading turnLeft(Heading h);
Heading turnRight(Heading h);
Vec3 laneOffset(Heading h); // towards the right-hand side of travel

struct Intersection {
    int k = 0, m = 0; // world position (k * 64, m * 64)
    bool operator==(const Intersection& o) const { return k == o.k && m == o.m; }
};

class RoadNetwork {
public:
    static constexpr float kLaneOffset = 2.5f;
    static constexpr float kStopDistance = 7.0f; // stop line distance from the intersection centre

    explicit RoadNetwork(const CityLayout& layout) : layout_(layout) {}

    bool exists(const Intersection& i) const;
    bool hasRoad(const Intersection& i, Heading h) const;
    Vec3 position(const Intersection& i) const;
    static Intersection step(const Intersection& i, Heading h);
    Intersection nearest(const Vec3& p) const;
    // Where a car heading `h` from intersection `from` starts its lane.
    Vec3 laneStart(const Intersection& from, Heading h) const;
    Vec3 laneEnd(const Intersection& from, Heading h) const;

private:
    const CityLayout& layout_;
};

} // namespace av
