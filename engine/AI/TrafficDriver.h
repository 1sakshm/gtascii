#pragma once

#include "AI/Navigation.h"
#include "Core/Random.h"
#include "Physics/Vehicle.h"

namespace av {

// Lane-following AI for traffic cars. Drives a lane segment between two
// intersections, then a curved turn (quadratic Bezier) onto the next lane,
// choosing straight/left/right at random among existing roads. Steering is
// pure pursuit towards a look-ahead point; throttle tracks a target speed
// that drops for turns, cars ahead and blocked intersections.
class TrafficDriver {
public:
    void start(const RoadNetwork& roads, const Intersection& from, Heading heading, Rng& rng);

    struct Perception {
        float obstacleAhead = 1e9f;       // distance to the nearest car/person in our lane ahead
        bool intersectionBlocked = false; // another car is inside the next intersection
    };

    VehicleControls update(const RoadNetwork& roads, const VehicleState& car, const VehicleSpec& spec,
                           const Perception& perception, float dt, Rng& rng);

    // Where the current path is heading (for debugging / perception).
    Intersection nextIntersection() const { return RoadNetwork::step(from_, heading_); }
    bool turning() const { return turning_; }
    Heading heading() const { return heading_; }
    float cruiseSpeed = 11.0f;

private:
    void chooseNext(const RoadNetwork& roads, Rng& rng);
    Vec3 turnPoint(float t) const;

    Intersection from_;
    Heading heading_ = Heading::North;
    Heading next_ = Heading::North;
    bool turning_ = false;
    float turnT_ = 0.0f;
    float turnLength_ = 1.0f;
    Vec3 p0_, p1_, p2_; // turn curve
};

// Steering command (-1..1) that points the car at `target` (pure pursuit).
float steerTowards(const VehicleState& car, const VehicleSpec& spec, const Vec3& target);

} // namespace av
