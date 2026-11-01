#pragma once

#include "ClientConfig.h"
#include "Protocol.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace av {
class Audio;
class Terminal;
} // namespace av

namespace client {

enum class SessionMode {
    SinglePlayer, // private server on 127.0.0.1 in a background thread
    Host,         // same, but visible to (and joinable from) the local network
    Join,         // connect to a room server / dedicated server / LAN host
};

struct LaunchOptions {
    std::filesystem::path dataRoot;
    ClientConfig config;
    SessionMode mode = SessionMode::SinglePlayer;
    std::string serverAddress;               // Join: "host[:port]"
    std::string roomCode;                    // Join: room to enter ("" = main room)
    av::proto::CreateRoomRequest hostRoom;   // Host: name / mode / size
    std::uint64_t maxFrames = 0;
    bool autopilot = false;
    bool hourGiven = false; // --hour on the command line overrides the saved world clock
    bool mouse = true;
};

struct RunSummary {
    std::uint64_t frames = 0;
    double seconds = 0.0;
    double avgFrameMs = 0.0;
    double avgKBPerFrame = 0.0;
    std::size_t chunksGenerated = 0;
    std::size_t avgTriangles = 0;
    std::size_t remoteEntities = 0;
    double maxCorrection = 0.0;
    std::size_t netKBIn = 0, netKBOut = 0;
    std::string disconnectReason; // empty when the player chose to leave
};

// Plays one session (until the player quits to the menu or the connection
// drops). Throws std::runtime_error if the session cannot start.
RunSummary runGame(const LaunchOptions& options, av::Terminal& terminal, av::Audio& audio);

} // namespace client
