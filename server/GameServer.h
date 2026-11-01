#pragma once

#include "ServerConfig.h"
#include "ServerWorld.h"

#include "Networking/Net.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace server {

// Network front end of the authoritative server. One process, one port,
// many rooms: each room is an independent ServerWorld with its own players.
// TCP: lobby (list/create rooms), handshake, sessions, chat, events, status.
// UDP: input frames in, snapshots out (routed to rooms by session id), plus
// LAN discovery replies on a well-known port.
class GameServer {
public:
    GameServer(const ServerConfig& config, const std::filesystem::path& dataRoot);
    ~GameServer();

    // Binds sockets. Port 0 picks a free port.
    bool start(std::string& error);
    void step(float dt);
    void run(const std::atomic<bool>& stop);

    std::uint16_t port() const;
    std::uint16_t udpPort() const;
    std::size_t clientCount() const { return clients_.size(); }
    std::size_t roomCount() const { return rooms_.size(); }
    // The permanent main room's world (tests, embedded single player).
    ServerWorld* mainWorld();

    std::function<void(const std::string&)> log;

private:
    struct Room {
        std::string code, name;
        av::proto::GameMode mode = av::proto::GameMode::FreeRoam;
        bool isPublic = true, permanent = false;
        int maxPlayers = 8;
        std::unique_ptr<ServerWorld> world;
        double emptySince = 0.0;
        float snapshotAccumulator = 0.0f, statusAccumulator = 0.0f, scoreAccumulator = 0.0f;
    };
    struct Client {
        std::unique_ptr<av::TcpStream> tcp;
        std::uint32_t session = 0;
        Room* room = nullptr;
        Entity player = av::kNullEntity;
        std::string name;
        bool hasUdp = false;
        av::NetAddress udp;
        double lastHeard = 0.0;
        av::proto::PlayerStatus lastStatus;
        bool statusSent = false;
        std::vector<av::proto::ScoreEntry> lastScores;
    };

    Room* createRoom(const std::string& name, av::proto::GameMode mode, bool isPublic, int maxPlayers, bool permanent);
    Room* findRoom(const std::string& code);
    std::vector<av::proto::RoomInfo> roomList(bool publicOnly) const;
    std::string newRoomCode();

    void acceptClients();
    void pollTcp();
    void pollUdp();
    void pollDiscovery();
    void handleHello(Client& c, const std::vector<std::uint8_t>& payload);
    void handleChat(Client& c, const std::string& text);
    void sendToRoom(Room* room, av::proto::Tcp type, const std::vector<std::uint8_t>& payload);
    void dropClient(std::size_t index, const std::string& reason);
    void serviceRooms(float dt);
    void emit(const std::string& line);

    ServerConfig config_;
    std::filesystem::path dataRoot_;
    av::NetInit net_;
    std::map<std::string, std::unique_ptr<Room>> rooms_;
    av::TcpListener listener_;
    av::UdpSocket udp_;
    av::UdpSocket discovery_;
    std::vector<std::unique_ptr<Client>> clients_;
    av::Rng rng_;
    double clock_ = 0.0;
};

// Runs a GameServer on a background thread (single player and LAN hosting).
class EmbeddedServer {
public:
    EmbeddedServer(const ServerConfig& config, const std::filesystem::path& dataRoot);
    ~EmbeddedServer();

    bool start(std::string& error);
    std::uint16_t port() const { return port_; }

private:
    std::unique_ptr<GameServer> server_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::uint16_t port_ = 0;
};

} // namespace server
