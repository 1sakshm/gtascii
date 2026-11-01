#pragma once

#include "Graphics/Camera.h"
#include "Physics/CollisionWorld.h"

namespace av {

struct ThirdPersonCameraSettings {
    float distance = 4.8f;
    float minDistance = 1.8f;
    float maxDistance = 16.0f;
    float pivotHeight = 1.55f;      // look-at point above the feet (shoulders)
    float minPitch = -0.35f;        // radians; negative looks up
    float maxPitch = 1.30f;
    float followRate = 14.0f;       // pivot smoothing (1/s)
    float zoomOutRate = 4.0f;       // how fast the camera backs off after a collision
    float recenterRate = 3.5f;      // how fast recentering swings behind the player
    float autoRecenterDelay = 3.0f; // seconds without camera input before recentering while moving (0 = off)
    float collisionPadding = 0.3f;
    float fovY = radians(62.0f);
};

// Orbit camera behind a character: smoothed follow, zoom, pitch limits,
// collision avoidance (pulls in so walls never hide the player), manual and
// automatic recentering. Camera yaw is independent of character facing.
class ThirdPersonCamera {
public:
    explicit ThirdPersonCamera(ThirdPersonCameraSettings settings = {});

    void rotate(float deltaYaw, float deltaPitch); // manual input
    void zoom(float deltaDistance);
    void recenter() { recentering_ = true; }
    // Driving: pull back and up so the whole car and the road ahead are visible.
    void setVehicleMode(bool on) { vehicleMode_ = on; }
    void snapTo(const Vec3& feet, float characterYaw);

    void update(float dt, const Vec3& feet, float characterYaw, bool characterMoving, const CollisionWorld* world);

    Camera camera() const;
    float yaw() const noexcept { return yaw_; }
    float pitch() const noexcept { return pitch_; }
    float currentDistance() const noexcept { return currentDistance_; }
    // Horizontal camera basis for camera-relative movement.
    Vec3 forwardXZ() const;
    Vec3 rightXZ() const;

private:
    Vec3 viewDirection() const;

    ThirdPersonCameraSettings settings_;
    float yaw_ = 0.0f;
    float pitch_ = 0.32f;
    float targetDistance_;
    float currentDistance_;
    Vec3 pivot_;
    Vec3 position_;
    bool recentering_ = false;
    float idleTime_ = 0.0f;
    bool initialised_ = false;
    bool vehicleMode_ = false;
};

} // namespace av
