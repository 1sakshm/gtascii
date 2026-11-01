#pragma once

#include "Animation/Skeleton.h"

namespace av {

// What the character is doing, as reported to gameplay/HUD (and later
// replicated over the network as a compact animation state).
enum class AnimationState { Idle, Walk, Run, Jump, Fall, Land, Crouch, CrouchWalk, Wave, Sit, KnockedDown, Punch, Talk };
const char* toString(AnimationState s);

struct AnimationInput {
    float speed = 0.0f;         // horizontal speed, m/s
    float verticalSpeed = 0.0f; // m/s, positive up
    float turnRate = 0.0f;      // rad/s, positive = turning left
    bool grounded = true;
    bool crouching = false;
    float landingImpact = 0.0f; // > 0 on the step the character touched down (m/s)
    bool wave = false;          // start the wave gesture
    bool punch = false;         // start a punch
    bool talking = false;       // conversation gestures
    float sit = 0.0f;           // 1 = seated in a vehicle (blends in/out over ~0.5 s: entering/exiting)
    bool knockedDown = false;   // lying on the ground
};

// Procedural skeletal animation for the humanoid rig. Gait phase advances
// with distance travelled (so feet do not slide), and idle/walk/run/crouch/
// airborne/landing/wave layers are blended with smoothed weights into joint
// rotations every frame.
class HumanoidAnimator {
public:
    HumanoidAnimator();

    void update(const AnimationInput& input, float dt);
    const Pose& pose() const noexcept { return pose_; }
    AnimationState state() const noexcept { return state_; }
    float gaitPhase() const noexcept { return phase_; }

private:
    Pose pose_;
    AnimationState state_ = AnimationState::Idle;
    float time_ = 0.0f;
    float phase_ = 0.0f; // [0, 1) gait cycle
    float walkW_ = 0.0f, runW_ = 0.0f, crouchW_ = 0.0f, airW_ = 0.0f, fallW_ = 0.0f;
    float landTimer_ = 0.0f, landStrength_ = 0.0f;
    float waveTimer_ = 0.0f;
    float punchTimer_ = 0.0f;
    float sitW_ = 0.0f, knockW_ = 0.0f, talkW_ = 0.0f;
    float lean_ = 0.0f, headTurn_ = 0.0f;
};

} // namespace av
