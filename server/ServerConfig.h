#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "GameTypes.h"

namespace server {

struct ServerConfig {
    std::uint16_t port = 41986;     // TCP and UDP
    bool loopbackOnly = false;      // true: only accept local connections (single player)
    int tickRate = 30;              // simulation steps per second
    int snapshotRate = 20;          // snapshots per second per client
    int maxPlayers = 8;
    int pedestrians = 22;           // ambient pedestrians per player
    int trafficCars = 10;           // traffic vehicles per player
    int loadRadius = 2;             // chunks around each player
    float dayLengthMinutes = 20.0f; // 0 = clock stopped
    float startHour = 17.5f;
    bool overrideSavedHour = false; // true: startHour wins over the clock stored in the save
    std::uint32_t seed = 1986;
    std::string savePath = "saves/world.json"; // relative to the data root
    float autosaveSeconds = 60.0f;
    bool allowCheats = true;        // time skip etc.
    std::string motd = "Welcome to Grand Theft ASCII.";
    // Rooms
    bool roomServer = false;     // online room server: players create rooms with codes
    bool mainRoom = true;        // keep a permanent room for players who join without a code
    std::string roomName = "Main";
    av::proto::GameMode mode = av::proto::GameMode::FreeRoam;
    int maxRooms = 16;
    float emptyRoomSeconds = 120.0f; // close created rooms after being empty this long
    bool lanDiscovery = true;    // answer LAN browser queries (ignored when loopbackOnly)

    // Missing file -> defaults; malformed -> throws.
    static ServerConfig load(const std::filesystem::path& path);
};

} // namespace server
