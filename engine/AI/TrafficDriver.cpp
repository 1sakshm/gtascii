#include "AI/TrafficDriver.h"

#include <cmath>

namespace av {

float steerTowards(const VehicleState& car, const VehicleSpec& spec, const Vec3& target)
{
    const Vec3 v = target - car.position;
    const float desired = std::atan2(v.x, v.z);
    float angle = wrapAngle(desired - car.yaw);
    if (car.speed < -0.5f) {
        angle = -angle; // reversing: steering is mirrored
    }
    return clampv(angle / spec.maxSteer * 1.6f, -1.0f, 1.0f);
}

void TrafficDriver::start(const RoadNetwork& roads, const Intersection& from, Heading heading, Rng& rng)
{
    from_ = from;
    heading_ = heading;
    turning_ = false;
    turnT_ = 0.0f;
    chooseNext(roads, rng);
}

void TrafficDriver::chooseNext(const RoadNetwork& roads, Rng& rng)
{
    const Intersection to = RoadNetwork::step(from_, heading_);
    Heading options[3];
    float weights[3];
    int n = 0;
    const Heading candidates[3] = {heading_, turnLeft(heading_), turnRight(heading_)};
    const float baseWeights[3] = {0.5f, 0.25f, 0.25f};
    for (int i = 0; i < 3; ++i) {
        if (roads.hasRoad(to, candidates[i])) {
            options[n] = candidates[i];
            weights[n] = baseWeights[i];
            ++n;
        }
    }
    if (n == 0) {
        next_ = opposite(heading_); // dead end: U-turn
        return;
    }
    float total = 0.0f;
    for (int i = 0; i < n; ++i) total += weights[i];
    float pick = rng.uniform() * total;
    for (int i = 0; i < n; ++i) {
        if (pick < weights[i] || i == n - 1) {
            next_ = options[i];
            return;
        }
        pick -= weights[i];
    }
}

Vec3 TrafficDriver::turnPoint(float t) const
{
    const float u = 1.0f - t;
    return p0_ * (u * u) + p1_ * (2.0f * u * t) + p2_ * (t * t);
}

VehicleControls TrafficDriver::update(const RoadNetwork& roads, const VehicleState& car, const VehicleSpec& spec,
                                      const Perception& perception, float dt, Rng& rng)
{
    const Vec3 d = headingVector(heading_);
    const Intersection to = RoadNetwork::step(from_, heading_);
    float targetSpeed = cruiseSpeed;
    Vec3 target;

    if (!turning_) {
        const Vec3 start = roads.laneStart(from_, heading_);
        const Vec3 end = roads.laneEnd(from_, heading_);
        const float segLen = dot(end - start, d);
        const float s = dot(car.position - start, d);
        target = start + d * std::min(s + 7.0f, segLen + 3.0f);
        const float toEnd = segLen - s;
        if (toEnd < 12.0f) {
            targetSpeed = std::min(targetSpeed, 6.5f);
        }
        if (perception.intersectionBlocked && toEnd < 10.0f) {
            targetSpeed = std::min(targetSpeed, std::max(0.0f, (toEnd - 0.5f) * 1.2f));
        }
        if (toEnd < 0.5f && !perception.intersectionBlocked) {
            // Begin the turn: curve from this lane's end onto the next lane.
            turning_ = true;
            turnT_ = 0.0f;
            p0_ = end;
            p2_ = roads.laneStart(to, next_);
            if (next_ == heading_) {
                p1_ = (p0_ + p2_) * 0.5f;
            } else if (next_ == opposite(heading_)) {
                p1_ = roads.position(to) + d * 2.0f;
            } else {
                p1_ = roads.position(to) + laneOffset(heading_) + laneOffset(next_);
            }
            turnLength_ = std::max(1.0f, length(p1_ - p0_) + length(p2_ - p1_));
        }
    }
    if (turning_) {
        // Advance along the curve by the distance travelled.
        turnT_ = std::min(1.0f, turnT_ + std::max(car.speed, 0.0f) * dt / turnLength_);
        target = turnPoint(std::min(1.0f, turnT_ + 6.0f / turnLength_));
        if (next_ != heading_) {
            targetSpeed = std::min(targetSpeed, 6.0f);
        }
        if (turnT_ >= 0.98f) {
            from_ = to;
            heading_ = next_;
            turning_ = false;
            chooseNext(roads, rng);
        }
    }

    if (perception.obstacleAhead < 16.0f) {
        targetSpeed = std::min(targetSpeed, std::max(0.0f, (perception.obstacleAhead - 6.0f) * 1.1f));
    }

    VehicleControls c;
    c.steer = steerTowards(car, spec, target);
    const float err = targetSpeed - car.speed;
    c.throttle = clampv(err * 0.45f, -1.0f, 1.0f);
    if (targetSpeed < 0.3f && car.speed < 0.5f) {
        c.throttle = 0.0f;
        c.handbrake = true;
    }
    return c;
}

} // namespace av
