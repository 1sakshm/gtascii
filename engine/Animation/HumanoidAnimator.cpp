#include "Animation/HumanoidAnimator.h"

#include "Animation/Humanoid.h"

#include <cmath>

namespace av {

const char* toString(AnimationState s)
{
    switch (s) {
    case AnimationState::Idle: return "idle";
    case AnimationState::Walk: return "walk";
    case AnimationState::Run: return "run";
    case AnimationState::Jump: return "jump";
    case AnimationState::Fall: return "fall";
    case AnimationState::Land: return "land";
    case AnimationState::Crouch: return "crouch";
    case AnimationState::CrouchWalk: return "crouch-walk";
    case AnimationState::Wave: return "wave";
    case AnimationState::Sit: return "sit";
    case AnimationState::KnockedDown: return "knocked down";
    case AnimationState::Punch: return "punch";
    case AnimationState::Talk: return "talk";
    }
    return "?";
}

namespace {
constexpr float kWalkSpeed = 1.6f;
constexpr float kRunSpeed = 5.0f;
constexpr float kLandDuration = 0.32f;
constexpr float kWaveDuration = 2.2f;
constexpr float kPunchDuration = 0.4f;

void approach(float& value, float target, float rate, float dt) { value += (target - value) * damp(rate, dt); }
} // namespace

HumanoidAnimator::HumanoidAnimator()
{
    pose_ = humanoidSkeleton().bindPose();
    pose_.offsetJoint = JPelvis;
}

void HumanoidAnimator::update(const AnimationInput& in, float dt)
{
    time_ += dt;

    // ---- layer weights ---------------------------------------------------
    const float speed = in.grounded ? in.speed : 0.0f;
    approach(walkW_, saturate(speed / kWalkSpeed), 10.0f, dt);
    approach(runW_, saturate((speed - 2.2f) / (kRunSpeed - 2.2f)), 6.0f, dt);
    approach(crouchW_, in.crouching && in.grounded ? 1.0f : 0.0f, 9.0f, dt);
    approach(airW_, in.grounded ? 0.0f : 1.0f, in.grounded ? 18.0f : 9.0f, dt);
    approach(fallW_, !in.grounded && in.verticalSpeed < -2.5f ? 1.0f : 0.0f, 4.0f, dt);

    if (in.landingImpact > 2.0f) {
        landTimer_ = kLandDuration;
        landStrength_ = saturate(in.landingImpact / 12.0f) * 0.7f + 0.3f;
    }
    landTimer_ = std::max(0.0f, landTimer_ - dt);
    const float landW = landTimer_ > 0.0f ? std::sin(landTimer_ / kLandDuration * kPi) * landStrength_ : 0.0f;

    if (in.wave && waveTimer_ <= 0.0f) {
        waveTimer_ = kWaveDuration;
    }
    waveTimer_ = std::max(0.0f, waveTimer_ - dt);
    const float waveW = waveTimer_ > 0.0f ? smoothstep(0.0f, 0.3f, waveTimer_) * smoothstep(0.0f, 0.3f, kWaveDuration - waveTimer_) : 0.0f;

    approach(sitW_, saturate(in.sit), 7.0f, dt);
    approach(knockW_, in.knockedDown ? 1.0f : 0.0f, in.knockedDown ? 10.0f : 3.0f, dt);
    approach(talkW_, in.talking ? 1.0f : 0.0f, 5.0f, dt);
    if (in.punch && punchTimer_ <= 0.0f) {
        punchTimer_ = kPunchDuration;
    }
    punchTimer_ = std::max(0.0f, punchTimer_ - dt);
    const float punchW = punchTimer_ > 0.0f ? std::sin((1.0f - punchTimer_ / kPunchDuration) * kPi) : 0.0f;

    // Gait phase tied to distance travelled; stride grows with speed.
    const float stride = lerpf(1.35f, 2.6f, runW_) * lerpf(1.0f, 0.6f, crouchW_);
    phase_ = std::fmod(phase_ + speed * dt / stride, 1.0f);
    const float ph = phase_ * kTwoPi;
    const float s = std::sin(ph), c = std::cos(ph);

    approach(lean_, clampv(in.turnRate * 0.06f * saturate(in.speed / kRunSpeed), -0.3f, 0.3f), 6.0f, dt);
    approach(headTurn_, clampv(in.turnRate * 0.12f, -0.5f, 0.5f), 8.0f, dt);

    // ---- accumulate Euler angles per joint (pitch X, yaw Y, roll Z) -------
    Vec3 e[JCount];
    Vec3 pelvisOffset;
    const float idleW = 1.0f - walkW_;

    // Rest pose: arms slightly away from the body, elbows soft.
    e[JLUpperArm].z += 0.10f;
    e[JRUpperArm].z -= 0.10f;
    e[JLForearm].x -= 0.12f;
    e[JRForearm].x -= 0.12f;

    // Idle: breathing, gentle arm sway, occasional glances around.
    e[JChest].x += std::sin(time_ * 1.7f) * 0.025f * idleW;
    e[JLUpperArm].x += std::sin(time_ * 1.1f) * 0.03f * idleW;
    e[JRUpperArm].x += std::sin(time_ * 1.1f + 0.5f) * 0.03f * idleW;
    e[JHead].y += (std::sin(time_ * 0.37f) * 0.25f + std::sin(time_ * 0.13f) * 0.15f) * idleW * (1.0f - crouchW_);

    // Walk / run cycle: opposing arms and legs.
    const float legAmp = lerpf(0.45f, 0.80f, runW_) * walkW_;
    const float armAmp = lerpf(0.38f, 0.85f, runW_) * walkW_;
    const float kneeAmp = lerpf(0.65f, 1.45f, runW_) * walkW_;
    e[JLThigh].x += -s * legAmp;
    e[JRThigh].x += s * legAmp;
    e[JLShin].x += std::max(0.0f, c) * kneeAmp + 0.06f * walkW_;
    e[JRShin].x += std::max(0.0f, -c) * kneeAmp + 0.06f * walkW_;
    e[JLFoot].x += (s * legAmp - std::max(0.0f, c) * kneeAmp) * 0.4f;
    e[JRFoot].x += (-s * legAmp - std::max(0.0f, -c) * kneeAmp) * 0.4f;
    e[JLUpperArm].x += s * armAmp;
    e[JRUpperArm].x += -s * armAmp;
    const float elbow = (0.25f + 1.0f * runW_) * walkW_;
    e[JLForearm].x -= elbow + std::max(0.0f, -s) * 0.3f * walkW_;
    e[JRForearm].x -= elbow + std::max(0.0f, s) * 0.3f * walkW_;
    e[JPelvis].y += s * 0.12f * walkW_;
    e[JChest].y -= s * 0.18f * walkW_;
    e[JSpine].x += 0.28f * runW_ + 0.04f * walkW_; // lean into the run
    e[JHead].x -= 0.18f * runW_;
    pelvisOffset.y += (std::cos(2.0f * ph) * 0.5f - 0.5f) * lerpf(0.035f, 0.07f, runW_) * walkW_;

    // Turning: lean into the turn and lead with the head.
    e[JSpine].z -= lean_;
    e[JHead].y += headTurn_;

    // Crouch.
    pelvisOffset.y -= 0.42f * crouchW_;
    e[JLThigh].x -= 1.25f * crouchW_;
    e[JRThigh].x -= 1.25f * crouchW_;
    e[JLShin].x += 2.0f * crouchW_;
    e[JRShin].x += 2.0f * crouchW_;
    e[JLFoot].x -= 0.75f * crouchW_;
    e[JRFoot].x -= 0.75f * crouchW_;
    e[JSpine].x += 0.35f * crouchW_;
    e[JHead].x -= 0.25f * crouchW_;
    e[JLUpperArm].x -= 0.35f * crouchW_;
    e[JRUpperArm].x -= 0.35f * crouchW_;
    e[JLForearm].x -= 0.5f * crouchW_;
    e[JRForearm].x -= 0.5f * crouchW_;

    // Airborne: tucked jump pose, morphing into a flailing fall.
    const float jumpW = airW_ * (1.0f - fallW_);
    e[JLThigh].x -= 0.75f * jumpW + 0.25f * fallW_;
    e[JRThigh].x -= 0.15f * jumpW - 0.20f * fallW_;
    e[JLShin].x += 1.05f * jumpW + 0.45f * fallW_;
    e[JRShin].x += 0.45f * jumpW + 0.30f * fallW_;
    e[JLUpperArm].x -= 0.55f * jumpW;
    e[JRUpperArm].x -= 0.35f * jumpW;
    e[JLUpperArm].z += 0.35f * jumpW + 1.25f * fallW_;
    e[JRUpperArm].z -= 0.35f * jumpW + 1.25f * fallW_;
    e[JLForearm].x -= 0.4f * airW_;
    e[JRForearm].x -= 0.4f * airW_;
    e[JLForearm].z += std::sin(time_ * 9.0f) * 0.3f * fallW_;
    e[JRForearm].z -= std::sin(time_ * 9.0f + 1.0f) * 0.3f * fallW_;

    // Landing: knees absorb the impact.
    pelvisOffset.y -= 0.20f * landW;
    e[JLThigh].x -= 0.65f * landW;
    e[JRThigh].x -= 0.65f * landW;
    e[JLShin].x += 1.15f * landW;
    e[JRShin].x += 1.15f * landW;
    e[JLFoot].x -= 0.5f * landW;
    e[JRFoot].x -= 0.5f * landW;
    e[JSpine].x += 0.3f * landW;
    e[JLUpperArm].z += 0.3f * landW;
    e[JRUpperArm].z -= 0.3f * landW;

    // Wave (right arm), layered over everything else.
    if (waveW > 0.0f) {
        e[JRUpperArm] = lerp(e[JRUpperArm], Vec3{-0.2f, 0.0f, -2.5f}, waveW);
        e[JRForearm] = lerp(e[JRForearm], Vec3{0.0f, 0.0f, std::sin(time_ * 11.0f) * 0.45f - 0.2f}, waveW);
        e[JHead].y = lerpf(e[JHead].y, -0.25f, waveW);
    }

    // Talking: hand gestures and nodding.
    if (talkW_ > 0.01f) {
        e[JRUpperArm].x -= (0.35f + std::sin(time_ * 3.1f) * 0.15f) * talkW_;
        e[JRForearm].x -= (0.9f + std::sin(time_ * 4.3f) * 0.35f) * talkW_;
        e[JLForearm].x -= (0.5f + std::sin(time_ * 3.7f + 1.0f) * 0.25f) * talkW_;
        e[JHead].x += std::sin(time_ * 2.9f) * 0.08f * talkW_;
    }

    // Punch: right jab with a torso twist.
    if (punchW > 0.0f) {
        e[JRUpperArm] = lerp(e[JRUpperArm], Vec3{-1.5f, 0.0f, 0.15f}, punchW);
        e[JRForearm] = lerp(e[JRForearm], Vec3{-0.1f, 0.0f, 0.0f}, punchW);
        e[JLUpperArm] = lerp(e[JLUpperArm], Vec3{-0.8f, 0.0f, 0.2f}, punchW * 0.7f);
        e[JLForearm] = lerp(e[JLForearm], Vec3{-1.6f, 0.0f, 0.0f}, punchW * 0.7f);
        e[JChest].y -= 0.35f * punchW;
    }

    // Seated (driving): hips and knees at right angles, hands on the wheel.
    if (sitW_ > 0.01f) {
        auto toward = [&](int j, Vec3 target) { e[j] = lerp(e[j], target, sitW_); };
        toward(JLThigh, {-1.5f, 0.0f, 0.06f});
        toward(JRThigh, {-1.5f, 0.0f, -0.06f});
        toward(JLShin, {1.45f, 0.0f, 0.0f});
        toward(JRShin, {1.45f, 0.0f, 0.0f});
        toward(JLFoot, {-0.1f, 0.0f, 0.0f});
        toward(JRFoot, {-0.1f, 0.0f, 0.0f});
        toward(JLUpperArm, {-0.95f, 0.0f, -0.15f});
        toward(JRUpperArm, {-0.95f, 0.0f, 0.15f});
        toward(JLForearm, {-0.55f, 0.0f, 0.0f});
        toward(JRForearm, {-0.55f, 0.0f, 0.0f});
        toward(JSpine, {-0.08f, 0.0f, 0.0f});
        pelvisOffset = lerp(pelvisOffset, Vec3{0.0f, -0.45f, 0.0f}, sitW_);
    }

    // Knocked down: the whole body pivots back around the feet and lies flat.
    float rootPitch = 0.0f;
    if (knockW_ > 0.01f) {
        rootPitch = -1.45f * knockW_;
        e[JLUpperArm].z += 1.1f * knockW_;
        e[JRUpperArm].z -= 1.1f * knockW_;
        e[JLThigh].z += 0.15f * knockW_;
        e[JRThigh].z -= 0.15f * knockW_;
        e[JHead].x += 0.3f * knockW_;
        pelvisOffset.y += 0.12f * knockW_;
    }
    e[JRoot].x += rootPitch;

    for (int j = 0; j < JCount; ++j) {
        pose_.rotations[static_cast<std::size_t>(j)] = Quat::fromEuler(e[j].x, e[j].y, e[j].z);
    }
    pose_.offset = pelvisOffset;

    // ---- discrete state --------------------------------------------------
    if (knockW_ > 0.5f) {
        state_ = AnimationState::KnockedDown;
    } else if (sitW_ > 0.5f) {
        state_ = AnimationState::Sit;
    } else if (punchW > 0.0f) {
        state_ = AnimationState::Punch;
    } else if (waveW > 0.5f) {
        state_ = AnimationState::Wave;
    } else if (talkW_ > 0.5f && in.speed < 0.3f) {
        state_ = AnimationState::Talk;
    } else if (!in.grounded) {
        state_ = in.verticalSpeed > 0.0f ? AnimationState::Jump : AnimationState::Fall;
    } else if (landTimer_ > 0.0f) {
        state_ = AnimationState::Land;
    } else if (in.crouching) {
        state_ = in.speed > 0.3f ? AnimationState::CrouchWalk : AnimationState::Crouch;
    } else if (in.speed > 3.2f) {
        state_ = AnimationState::Run;
    } else if (in.speed > 0.3f) {
        state_ = AnimationState::Walk;
    } else {
        state_ = AnimationState::Idle;
    }
}

} // namespace av
