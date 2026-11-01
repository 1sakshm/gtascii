#include "Graphics/Lighting.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace av {

namespace {

struct SkyKey {
    float hour;
    Vec3 zenith, horizon, sun, skyAmb, groundAmb, cloud;
    float sunStrength;
};

// A stylised 1980s Miami day: hot noon, magenta/orange sunset, violet night.
const SkyKey kKeys[] = {
    {0.0f, {0.02f, 0.02f, 0.08f}, {0.10f, 0.06f, 0.20f}, {0.35f, 0.40f, 0.65f}, {0.17f, 0.16f, 0.28f}, {0.08f, 0.07f, 0.10f}, {0.20f, 0.18f, 0.30f}, 0.25f},
    {5.0f, {0.05f, 0.04f, 0.14f}, {0.30f, 0.15f, 0.30f}, {0.40f, 0.35f, 0.55f}, {0.12f, 0.10f, 0.20f}, {0.06f, 0.05f, 0.08f}, {0.35f, 0.25f, 0.40f}, 0.25f},
    {6.5f, {0.25f, 0.30f, 0.60f}, {1.00f, 0.60f, 0.45f}, {1.00f, 0.65f, 0.40f}, {0.30f, 0.28f, 0.38f}, {0.16f, 0.12f, 0.10f}, {1.00f, 0.75f, 0.65f}, 0.80f},
    {9.0f, {0.18f, 0.45f, 0.90f}, {0.65f, 0.85f, 1.00f}, {1.00f, 0.95f, 0.85f}, {0.36f, 0.42f, 0.52f}, {0.20f, 0.18f, 0.15f}, {0.97f, 0.97f, 1.00f}, 1.00f},
    {13.0f, {0.10f, 0.40f, 0.95f}, {0.60f, 0.85f, 1.00f}, {1.00f, 0.98f, 0.92f}, {0.38f, 0.44f, 0.55f}, {0.22f, 0.20f, 0.16f}, {1.00f, 1.00f, 1.00f}, 1.05f},
    {16.5f, {0.20f, 0.40f, 0.85f}, {0.80f, 0.80f, 0.85f}, {1.00f, 0.88f, 0.70f}, {0.36f, 0.38f, 0.46f}, {0.22f, 0.18f, 0.14f}, {1.00f, 0.92f, 0.85f}, 0.95f},
    {18.6f, {0.30f, 0.15f, 0.50f}, {1.00f, 0.40f, 0.35f}, {1.00f, 0.50f, 0.30f}, {0.34f, 0.24f, 0.36f}, {0.20f, 0.12f, 0.10f}, {1.00f, 0.55f, 0.55f}, 0.75f},
    {19.6f, {0.12f, 0.05f, 0.30f}, {0.70f, 0.20f, 0.45f}, {0.80f, 0.35f, 0.40f}, {0.20f, 0.14f, 0.28f}, {0.10f, 0.07f, 0.10f}, {0.60f, 0.30f, 0.50f}, 0.35f},
    {21.0f, {0.03f, 0.02f, 0.10f}, {0.16f, 0.07f, 0.24f}, {0.35f, 0.40f, 0.65f}, {0.17f, 0.16f, 0.28f}, {0.08f, 0.07f, 0.10f}, {0.22f, 0.18f, 0.32f}, 0.25f},
    {24.0f, {0.02f, 0.02f, 0.08f}, {0.10f, 0.06f, 0.20f}, {0.35f, 0.40f, 0.65f}, {0.17f, 0.16f, 0.28f}, {0.08f, 0.07f, 0.10f}, {0.20f, 0.18f, 0.30f}, 0.25f},
};

} // namespace

SceneLighting SceneLighting::timeOfDay(float hours)
{
    hours = std::fmod(hours, 24.0f);
    if (hours < 0.0f) {
        hours += 24.0f;
    }

    std::size_t i = 0;
    while (i + 1 < std::size(kKeys) && kKeys[i + 1].hour < hours) {
        ++i;
    }
    const SkyKey& a = kKeys[i];
    const SkyKey& b = kKeys[std::min(i + 1, std::size(kKeys) - 1)];
    const float t = b.hour > a.hour ? smoothstep(0.0f, 1.0f, (hours - a.hour) / (b.hour - a.hour)) : 0.0f;

    SceneLighting s;
    s.skyZenith = lerp(a.zenith, b.zenith, t);
    s.skyHorizon = lerp(a.horizon, b.horizon, t);
    s.skyAmbient = lerp(a.skyAmb, b.skyAmb, t);
    s.groundAmbient = lerp(a.groundAmb, b.groundAmb, t);
    s.cloudColor = lerp(a.cloud, b.cloud, t);
    const float strength = lerpf(a.sunStrength, b.sunStrength, t);

    // Sun travels east (+X) -> overhead (tilted south, +Z) -> west.
    const float angle = (hours - 6.0f) / 12.0f * kPi;
    const Vec3 sunDir = normalize(Vec3{std::cos(angle), std::sin(angle), 0.35f});
    const float elevation = sunDir.y;
    s.sunDiscDirection = sunDir;
    s.sunVisible = elevation > -0.05f;
    s.sunDiscColor = lerp(a.sun, b.sun, t) * 1.2f;

    if (elevation > 0.0f) {
        s.toSun = sunDir;
        s.sunColor = lerp(a.sun, b.sun, t) * strength * smoothstep(0.0f, 0.12f, elevation);
    } else {
        // Moonlight from roughly opposite the sun: dim and blue.
        s.toSun = normalize(Vec3{-sunDir.x, std::max(0.35f, -sunDir.y), -sunDir.z + 0.3f});
        s.sunColor = Vec3{0.22f, 0.25f, 0.40f};
        s.moonVisible = elevation < -0.15f;
    }

    s.nightFactor = 1.0f - smoothstep(-0.12f, 0.10f, elevation);
    s.fogStart = 60.0f;
    s.fogEnd = 230.0f;
    return s;
}

} // namespace av
