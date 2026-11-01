#pragma once

#include "Math/Vec.h"

#include <vector>

namespace av {

struct PointLight {
    Vec3 position;
    Vec3 color{1.0f, 0.8f, 0.5f};
    float range = 12.0f;     // light reaches zero at this distance
    float intensity = 1.0f;
};

// Everything the shading stage needs to know about the environment.
struct SceneLighting {
    Vec3 toSun = normalize(Vec3{0.4f, 0.8f, 0.3f}); // unit vector towards the sun (or moon at night)
    Vec3 sunColor{1.0f, 0.95f, 0.85f};
    Vec3 skyAmbient{0.35f, 0.40f, 0.50f};    // ambient from above
    Vec3 groundAmbient{0.18f, 0.16f, 0.14f}; // ambient bounced from below

    Vec3 skyZenith{0.20f, 0.40f, 0.80f};
    Vec3 skyHorizon{0.70f, 0.80f, 0.95f};
    Vec3 sunDiscDirection = normalize(Vec3{0.4f, 0.8f, 0.3f}); // real sun position, even at night
    Vec3 sunDiscColor{1.0f, 0.95f, 0.8f};
    bool sunVisible = true;  // sun disc above the horizon
    bool moonVisible = false; // moon disc (night)
    Vec3 cloudColor{0.95f, 0.95f, 1.0f};
    float cloudCover = 0.45f;

    float fogStart = 60.0f;
    float fogEnd = 220.0f;
    float nightFactor = 0.0f; // 0 = day, 1 = full night (stars, lit windows, street lights)
    float time = 0.0f;        // seconds, drives water and clouds

    std::vector<PointLight> pointLights;

    // Sun position, sky palette and ambient for a time of day in hours [0, 24).
    // Sunrise is over the ocean (+X, east), sunset to the west.
    static SceneLighting timeOfDay(float hours);
};

} // namespace av
