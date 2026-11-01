#pragma once

#include "AI/Navigation.h"
#include "Core/Random.h"

namespace av {

enum class PedState : std::uint8_t { Idle, Walking, Running, Talking, Reacting, Fleeing, Interacting };
const char* toString(PedState s);

// What the pedestrian wants this step, consumed by the character body.
struct PedCommand {
    Vec3 moveDirection; // unit or zero
    bool run = false;
    bool wave = false;  // gesture (greeting / talking)
    bool talk = false;  // talking animation
    float faceYaw = 0.0f;
    bool face = false;  // turn on the spot towards faceYaw
};

// Decision-making for one pedestrian: wanders the sidewalk graph, pauses,
// chats with others nearby, reacts to and flees from threats, and recovers
// when stuck. Pure logic: inputs are its position and events, output a command.
class PedestrianBrain {
public:
    void start(const PedestrianNav& nav, const Vec3& position, Rng& rng);

    // External stimuli.
    void threaten(const Vec3& from, float seconds = 8.0f); // flee from a point
    void bump(const Vec3& from);                           // brief reaction
    void talkTo(const Vec3& partner, float seconds);       // stop and chat

    PedCommand update(const PedestrianNav& nav, const Vec3& position, float dt, Rng& rng);

    PedState state() const noexcept { return state_; }
    bool available() const { return state_ == PedState::Idle || state_ == PedState::Walking; }

private:
    void pickNext(const PedestrianNav& nav, Rng& rng, bool awayFromThreat, const Vec3& position);

    PedState state_ = PedState::Walking;
    NavNode target_, previous_;
    float timer_ = 0.0f;
    Vec3 threat_;
    Vec3 focus_;
    Vec3 lastPosition_;
    float stuckTimer_ = 0.0f;
    bool hasTarget_ = false;
};

} // namespace av
