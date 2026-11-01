#include "AI/PedestrianBrain.h"

#include <cmath>
#include <vector>

namespace av {

const char* toString(PedState s)
{
    switch (s) {
    case PedState::Idle: return "idle";
    case PedState::Walking: return "walking";
    case PedState::Running: return "running";
    case PedState::Talking: return "talking";
    case PedState::Reacting: return "reacting";
    case PedState::Fleeing: return "fleeing";
    case PedState::Interacting: return "interacting";
    }
    return "?";
}

void PedestrianBrain::start(const PedestrianNav& nav, const Vec3& position, Rng& rng)
{
    hasTarget_ = nav.nearest(position, target_);
    previous_ = target_;
    lastPosition_ = position;
    state_ = rng.chance(0.15f) ? PedState::Idle : PedState::Walking;
    timer_ = rng.range(1.0f, 4.0f);
}

void PedestrianBrain::threaten(const Vec3& from, float seconds)
{
    threat_ = from;
    if (state_ != PedState::Fleeing) {
        hasTarget_ = false; // re-plan away from the threat
    }
    state_ = PedState::Fleeing;
    timer_ = std::max(timer_, seconds);
}

void PedestrianBrain::bump(const Vec3& from)
{
    if (state_ == PedState::Fleeing) {
        return;
    }
    focus_ = from;
    state_ = PedState::Reacting;
    timer_ = 1.2f;
}

void PedestrianBrain::talkTo(const Vec3& partner, float seconds)
{
    focus_ = partner;
    state_ = PedState::Talking;
    timer_ = seconds;
}

void PedestrianBrain::pickNext(const PedestrianNav& nav, Rng& rng, bool away, const Vec3& position)
{
    std::vector<NavNode> options;
    if (!hasTarget_) {
        hasTarget_ = nav.nearest(position, target_);
        previous_ = target_;
        if (!away) {
            return;
        }
    }
    nav.neighbors(target_, options);
    if (options.empty()) {
        return;
    }
    NavNode chosen = options[static_cast<std::size_t>(rng.rangeInt(0, static_cast<int>(options.size()) - 1))];
    if (away) {
        // Prefer the neighbour furthest from the threat.
        float best = -1.0f;
        for (const NavNode& n : options) {
            const float d = lengthSq(nav.position(n) - threat_);
            if (d > best) {
                best = d;
                chosen = n;
            }
        }
    } else if (options.size() > 1 && chosen == previous_) {
        // Avoid immediately walking back the way we came.
        for (const NavNode& n : options) {
            if (n != previous_) {
                chosen = n;
                break;
            }
        }
    }
    previous_ = target_;
    target_ = chosen;
}

PedCommand PedestrianBrain::update(const PedestrianNav& nav, const Vec3& position, float dt, Rng& rng)
{
    PedCommand cmd;
    timer_ -= dt;

    auto faceTowards = [&](const Vec3& p) {
        const Vec3 v = p - position;
        if (lengthSq(Vec3{v.x, 0, v.z}) > 1e-4f) {
            cmd.face = true;
            cmd.faceYaw = std::atan2(v.x, v.z);
        }
    };
    auto walkToTarget = [&](bool run) {
        if (!hasTarget_) {
            pickNext(nav, rng, state_ == PedState::Fleeing, position);
            if (!hasTarget_) {
                return;
            }
        }
        Vec3 v = nav.position(target_) - position;
        v.y = 0.0f;
        if (length(v) < (run ? 1.6f : 1.0f)) {
            pickNext(nav, rng, state_ == PedState::Fleeing, position);
            v = nav.position(target_) - position;
            v.y = 0.0f;
        }
        cmd.moveDirection = normalize(v);
        cmd.run = run;
    };

    switch (state_) {
    case PedState::Idle:
    case PedState::Interacting:
        if (state_ == PedState::Interacting) {
            cmd.talk = true; // on the phone / looking at a shop window
        }
        if (timer_ <= 0.0f) {
            state_ = rng.chance(0.08f) ? PedState::Running : PedState::Walking;
            timer_ = rng.range(8.0f, 20.0f);
        }
        break;
    case PedState::Walking:
    case PedState::Running:
        walkToTarget(state_ == PedState::Running);
        if (timer_ <= 0.0f) {
            const float r = rng.uniform();
            state_ = r < 0.35f ? PedState::Idle : (r < 0.5f ? PedState::Interacting : PedState::Walking);
            timer_ = state_ == PedState::Walking ? rng.range(8.0f, 20.0f) : rng.range(2.0f, 6.0f);
        }
        break;
    case PedState::Talking:
        faceTowards(focus_);
        cmd.talk = true;
        if (timer_ <= 0.0f) {
            state_ = PedState::Walking;
            timer_ = rng.range(8.0f, 20.0f);
        }
        break;
    case PedState::Reacting:
        faceTowards(focus_);
        if (timer_ <= 0.0f) {
            state_ = rng.chance(0.4f) ? PedState::Fleeing : PedState::Walking;
            threat_ = focus_;
            timer_ = state_ == PedState::Fleeing ? 5.0f : rng.range(8.0f, 15.0f);
        }
        break;
    case PedState::Fleeing:
        walkToTarget(true);
        if (timer_ <= 0.0f) {
            state_ = PedState::Walking;
            timer_ = rng.range(8.0f, 20.0f);
        }
        break;
    }

    // Stuck detection: moving but not getting anywhere -> pick another route.
    if (length(cmd.moveDirection) > 0.0f) {
        if (lengthSq(position - lastPosition_) < (0.25f * dt) * (0.25f * dt)) { // slower than 0.25 m/s
            stuckTimer_ += dt;
        } else {
            stuckTimer_ = 0.0f;
        }
        if (stuckTimer_ > 2.5f) {
            stuckTimer_ = 0.0f;
            pickNext(nav, rng, false, position);
        }
    }
    lastPosition_ = position;
    return cmd;
}

} // namespace av
