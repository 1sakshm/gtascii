#include "TestHarness.h"

#include "Animation/Humanoid.h"
#include "Animation/HumanoidAnimator.h"
#include "Animation/Skeleton.h"
#include "Gameplay/ThirdPersonCamera.h"
#include "Physics/CharacterMotor.h"

#include <cmath>

using namespace av;

namespace {
Vec3 jointPos(const std::vector<Mat4>& g, int j) { return g[static_cast<std::size_t>(j)].translationPart(); }

std::vector<Mat4> bindGlobals()
{
    std::vector<Mat4> g;
    humanoidSkeleton().computeGlobal(humanoidSkeleton().bindPose(), g);
    return g;
}
} // namespace

TEST_CASE("Skeleton: hierarchy composes parent transforms")
{
    Skeleton s;
    const int root = s.addJoint("root", -1, {0, 1, 0});
    const int child = s.addJoint("child", root, {0, 0, 2});
    Pose p = s.bindPose();
    p.rotations[static_cast<std::size_t>(root)] = Quat::fromAxisAngle({0, 1, 0}, kHalfPi);
    std::vector<Mat4> g;
    s.computeGlobal(p, g);
    // Rotating the root swings the child's offset (0,0,2) to (2,0,0).
    CHECK(nearlyEqual(g[static_cast<std::size_t>(child)].translationPart(), Vec3{2, 1, 0}, 1e-4f));
    CHECK_EQ(s.find("child"), child);
    CHECK_EQ(s.find("nope"), -1);

    bool threw = false;
    try {
        s.addJoint("bad", 9, {});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("Humanoid: skeleton has every spec joint with human proportions")
{
    const Skeleton& s = humanoidSkeleton();
    CHECK_EQ(s.size(), std::size_t{JCount});
    for (const char* name : {"Pelvis", "Spine", "Chest", "Neck", "Head", "LeftShoulder", "LeftUpperArm", "LeftForearm",
                             "LeftHand", "RightHand", "LeftHip", "LeftThigh", "LeftShin", "LeftFoot", "RightFoot"}) {
        CHECK(s.find(name) >= 0);
    }
    const auto g = bindGlobals();
    CHECK(jointPos(g, JHead).y > 1.5f && jointPos(g, JHead).y < 1.7f);
    CHECK(jointPos(g, JLFoot).y > 0.0f && jointPos(g, JLFoot).y < 0.1f);
    CHECK(jointPos(g, JLHand).x > 0.1f);  // left is +X
    CHECK(jointPos(g, JRHand).x < -0.1f);
    CHECK(nearlyEqual(jointPos(g, JLHand).y, jointPos(g, JRHand).y)); // symmetric
}

TEST_CASE("HumanoidAnimator: walking swings legs in opposition and alternates")
{
    HumanoidAnimator anim;
    AnimationInput in;
    in.speed = 1.6f;
    std::vector<Mat4> g;
    float minDiff = 0.0f, maxDiff = 0.0f;
    for (int i = 0; i < 120; ++i) {
        anim.update(in, 1.0f / 60.0f);
        humanoidSkeleton().computeGlobal(anim.pose(), g);
        const float diff = jointPos(g, JLFoot).z - jointPos(g, JRFoot).z;
        minDiff = std::min(minDiff, diff);
        maxDiff = std::max(maxDiff, diff);
        // Opposite arm swings with each leg.
        if (std::fabs(diff) > 0.3f) {
            const float armDiff = jointPos(g, JLHand).z - jointPos(g, JRHand).z;
            CHECK(armDiff * diff < 0.0f);
        }
    }
    CHECK(maxDiff > 0.3f);  // left foot well ahead at some point
    CHECK(minDiff < -0.3f); // and right foot ahead at another
    CHECK(anim.state() == AnimationState::Walk);
}

TEST_CASE("HumanoidAnimator: states for idle, run, air, crouch, wave")
{
    HumanoidAnimator anim;
    AnimationInput in;
    for (int i = 0; i < 30; ++i) anim.update(in, 1.0f / 60.0f);
    CHECK(anim.state() == AnimationState::Idle);

    in.speed = 5.5f;
    for (int i = 0; i < 30; ++i) anim.update(in, 1.0f / 60.0f);
    CHECK(anim.state() == AnimationState::Run);

    in.grounded = false;
    in.verticalSpeed = 3.0f;
    anim.update(in, 1.0f / 60.0f);
    CHECK(anim.state() == AnimationState::Jump);
    in.verticalSpeed = -5.0f;
    anim.update(in, 1.0f / 60.0f);
    CHECK(anim.state() == AnimationState::Fall);

    in.grounded = true;
    in.speed = 0.0f;
    in.landingImpact = 8.0f;
    anim.update(in, 1.0f / 60.0f);
    CHECK(anim.state() == AnimationState::Land);
    in.landingImpact = 0.0f;

    in.crouching = true;
    std::vector<Mat4> standing, crouched;
    humanoidSkeleton().computeGlobal(humanoidSkeleton().bindPose(), standing);
    for (int i = 0; i < 60; ++i) anim.update(in, 1.0f / 60.0f);
    humanoidSkeleton().computeGlobal(anim.pose(), crouched);
    CHECK(anim.state() == AnimationState::Crouch);
    CHECK(jointPos(crouched, JHead).y < jointPos(standing, JHead).y - 0.3f);
    // Feet stay near the ground while crouched.
    CHECK(std::fabs(jointPos(crouched, JLFoot).y - jointPos(standing, JLFoot).y) < 0.12f);

    in.crouching = false;
    in.wave = true;
    for (int i = 0; i < 40; ++i) {
        anim.update(in, 1.0f / 60.0f);
        in.wave = false;
    }
    std::vector<Mat4> waving;
    humanoidSkeleton().computeGlobal(anim.pose(), waving);
    CHECK(anim.state() == AnimationState::Wave);
    CHECK(jointPos(waving, JRHand).y > jointPos(waving, JHead).y); // hand above head
}

TEST_CASE("CharacterMotor: gravity, landing, jumping")
{
    BoxCollisionWorld world;
    CharacterMotor m;
    m.position = {0, 3, 0};
    bool landed = false;
    float impact = 0.0f;
    for (int i = 0; i < 120 && !landed; ++i) {
        m.step({}, false, false, 1.0f / 60.0f, world);
        if (m.grounded()) {
            landed = true;
            impact = m.landingImpact();
        }
    }
    CHECK(landed);
    CHECK(nearlyEqual(m.position.y, 0.0f));
    CHECK(impact > 5.0f);

    float peak = 0.0f;
    m.step({}, true, false, 1.0f / 60.0f, world);
    CHECK(!m.grounded());
    for (int i = 0; i < 120; ++i) {
        m.step({}, false, false, 1.0f / 60.0f, world);
        peak = std::max(peak, m.position.y);
    }
    CHECK(peak > 0.9f && peak < 1.4f);
    CHECK(m.grounded());
}

TEST_CASE("CharacterMotor: walls block, curbs are stepped onto")
{
    BoxCollisionWorld world;
    world.boxes.push_back(AABB({2, 0, -5}, {3, 3, 5}));      // wall at x = 2..3
    world.boxes.push_back(AABB({-5, 0, 2}, {5, 0.15f, 8}));   // 15 cm curb at z >= 2
    CharacterMotor m;
    m.teleport({0, 0, 0}, world);

    for (int i = 0; i < 180; ++i) m.step({5, 0, 0}, false, false, 1.0f / 60.0f, world);
    CHECK(m.position.x <= 2.0f - m.settings().radius + 1e-3f); // stopped at the wall
    CHECK(m.position.x > 1.5f);

    for (int i = 0; i < 120; ++i) m.step({0, 0, 3}, false, false, 1.0f / 60.0f, world);
    CHECK(m.position.z > 3.0f);                     // walked onto the curb
    CHECK(nearlyEqual(m.position.y, 0.15f, 1e-3f)); // standing on top of it
    CHECK(m.grounded());

    // Fast movement does not tunnel through a thin pole.
    world.boxes.push_back(AABB({-0.05f, 0, 5.9f}, {0.05f, 3, 6.0f}));
    CharacterMotor fast;
    fast.teleport({0, 0.15f, 4}, world);
    for (int i = 0; i < 30; ++i) fast.step({0, 0, 30}, false, false, 1.0f / 60.0f, world);
    CHECK(fast.position.z < 6.0f);
}

TEST_CASE("ThirdPersonCamera: collision pulls the camera in front of walls")
{
    BoxCollisionWorld world;
    ThirdPersonCamera cam;
    cam.snapTo({0, 0, 0}, 0.0f); // character faces +Z, camera behind at -Z
    cam.update(0.016f, {0, 0, 0}, 0.0f, false, &world);
    CHECK(cam.currentDistance() > 4.0f);
    CHECK(cam.camera().position.z < -3.0f);

    world.boxes.push_back(AABB({-5, 0, -2.5f}, {5, 5, -2.0f})); // wall 2 m behind the player
    cam.update(0.016f, {0, 0, 0}, 0.0f, false, &world);
    CHECK(cam.currentDistance() < 2.5f);
    CHECK(cam.camera().position.z > -2.0f); // camera stays on the player's side

    // Camera-relative movement basis.
    CHECK(nearlyEqual(cam.forwardXZ(), Vec3{0, 0, 1}, 1e-3f));
    CHECK(nearlyEqual(cam.rightXZ(), Vec3{-1, 0, 0}, 1e-3f)); // facing +Z, right is -X
}
