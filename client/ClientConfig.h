#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace client {

struct ClientConfig {
    // render
    int samplesX = 2, samplesY = 2;
    float cellAspect = 2.0f;
    bool outlines = true;
    std::string ramp = "standard";
    int maxPointLights = 16;
    double targetFps = 60.0;
    int colorBits = 8;
    float fogStart = 50.0f, fogEnd = 170.0f;
    // world
    std::uint32_t seed = 1986;
    int loadRadius = 2;
    float detailDistance = 110.0f;
    float startHour = 17.5f;
    float dayLengthMinutes = 20.0f;
    // camera
    float cameraDistance = 4.8f;
    float keyRotateSpeed = 2.2f;
    float mouseSensitivity = 0.035f;
    float autoRecenterDelay = 3.0f;
    bool invertY = false;
    bool mouseLook = true; // the camera follows the mouse / trackpad (no dragging needed)
    // player
    std::string playerName = "Player";
    std::string password;
    std::uint32_t appearanceSeed = 7;
    // network
    std::string server;          // "" = single player (embedded local server)
    std::uint16_t hostPort = 41986;

    // network (continued)
    std::string lobby;           // room server for online rooms ("" = LAN rooms only)
    // audio
    float volume = 0.7f;         // 0 = muted
    // gameplay
    int fovDegrees = 62;

    // Missing file -> defaults; malformed file -> throws.
    static ClientConfig load(const std::filesystem::path& path);
    // Shipped defaults overlaid with the player's own settings file.
    static ClientConfig loadLayered(const std::filesystem::path& defaults, const std::filesystem::path& user);
    // Applies the keys present in a config file on top of this config.
    void apply(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;
    static std::filesystem::path userPath();
};

} // namespace client
