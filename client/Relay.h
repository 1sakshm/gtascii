#pragma once

#include "Protocol.h"

#include "Networking/WebSocket.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace client {

// Online rooms through the relay (relay/ in the repository: a Cloudflare
// Worker). The host's game runs the room's server; the relay only forwards
// bytes, so nobody needs a public IP or port forwarding.
//
// Both bridges translate between the relay's WebSocket and ordinary loopback
// TCP/UDP sockets, so the game server and NetClient work unchanged.

// "wss://...", "https://..." (and ws:// / http:// for a local test relay).
bool isRelayUrl(const std::string& address);

// Public rooms listed by the relay.
std::optional<std::vector<av::proto::RoomInfo>> listRelayRooms(const std::string& relayUrl, std::string& error, int timeoutMs = 4000);

// A fresh 4-character room code (unambiguous characters).
std::string randomRoomCode();

// Runs next to the host's embedded server: one loopback TCP + UDP pair per guest.
class RelayHost {
public:
    ~RelayHost();
    // Registers the room. `httpStatus` is 409 when the code is taken.
    bool start(const std::string& relayUrl, const std::string& code, const av::proto::CreateRoomRequest& room,
               std::string& error, int* httpStatus = nullptr);
    // Starts forwarding to the local server (call once it listens).
    void serve(std::uint16_t serverPort);
    void stop();
    bool alive() const { return alive_; }
    std::string problem() const;
    int guests() const { return guests_; }

private:
    void loop();
    av::WebSocket ws_;
    std::uint16_t serverPort_ = 0;
    std::thread thread_;
    std::atomic<bool> stop_{false}, alive_{false};
    std::atomic<int> guests_{0};
    mutable std::mutex mutex_;
    std::string problem_;
};

// Runs next to a joining player's NetClient, which connects to localPort().
class RelayGuest {
public:
    ~RelayGuest();
    bool start(const std::string& relayUrl, const std::string& code, std::string& error, int* httpStatus = nullptr);
    std::uint16_t localPort() const { return port_; }
    void stop();
    bool alive() const { return alive_; }
    std::string problem() const;

private:
    void loop();
    av::WebSocket ws_;
    std::uint16_t port_ = 0;
    std::thread thread_;
    std::atomic<bool> stop_{false}, alive_{false};
    mutable std::mutex mutex_;
    std::string problem_;
    struct Sockets;
    Sockets* sockets_ = nullptr;
};

} // namespace client
