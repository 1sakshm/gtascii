#pragma once

#include "GameTypes.h"

#include "Math/Vec.h"
#include "Networking/ByteStream.h"

#include <optional>
#include <string>
#include <vector>

// Wire protocol. TCP carries sessions and reliable events as frames
// ([u16 len][u8 type][payload]); UDP carries inputs and snapshots, each
// datagram starting with {u16 magic, u8 version, u8 type, u32 session}.
namespace av::proto {

// ---- TCP frame types ----------------------------------------------------
enum class Tcp : std::uint8_t {
    Hello = 1,     // C->S
    Chat = 2,      // C->S
    Command = 3,   // C->S
    ListRooms = 4, // C->S (lobby)
    CreateRoom = 5,// C->S (lobby)
    Welcome = 10,  // S->C
    Reject = 11,   // S->C
    Roster = 12,   // S->C player joined / left
    ChatOut = 13,  // S->C
    Event = 14,    // S->C
    Status = 15,   // S->C
    RoomList = 16, // S->C
    RoomCreated = 17, // S->C
    Scoreboard = 18,  // S->C
};

enum class CommandKind : std::uint8_t { SkipHour = 1, StartRace = 2, StartCopsAndRobbers = 3 };

struct Hello {
    std::uint8_t version = kProtocolVersion;
    std::string name;
    std::string password;
    std::uint32_t appearanceSeed = 0; // packed Appearance code
    std::string room;                 // room code; "" = the server's main room
};

struct RoomInfo {
    std::string code;
    std::string name;
    GameMode mode = GameMode::FreeRoam;
    std::uint8_t players = 0, maxPlayers = 8;
    bool isPublic = true;
    std::string address; // filled in by the client (LAN discovery / lobby host)
    std::uint16_t port = 0;
    bool relay = false;  // client-side: listed by the online relay (join by code)
};

struct CreateRoomRequest {
    std::string name;
    GameMode mode = GameMode::FreeRoam;
    bool isPublic = true;
    std::uint8_t maxPlayers = 8;
};

struct ScoreEntry {
    std::string name;
    std::int32_t points = 0;
    std::string role; // "", "cop", "robber", "racer"
};

struct Welcome {
    std::uint32_t session = 0;
    std::uint16_t netId = 0;
    std::uint32_t worldSeed = 0;
    std::uint16_t udpPort = 0;
    std::uint8_t tickRate = 30;
    float hour = 12.0f;
    std::string motd;
    std::string roomCode, roomName;
    GameMode mode = GameMode::FreeRoam;
};

struct RosterEntry {
    std::uint16_t netId = 0;
    std::string name;
    bool joined = true;
};

struct ChatLine {
    std::string from;
    std::string text;
    bool system = false;
};

struct Event {
    EventKind kind = EventKind::Info;
    std::int32_t value = 0;
    std::string text;
};

struct InventoryItem {
    std::string name;
    std::uint16_t count = 0;
};

struct PlayerStatus {
    std::int32_t money = 0;
    std::uint8_t health = 100;
    std::uint8_t wanted = 0;
    std::string missionTitle;
    std::string objective;
    float missionTimer = -1.0f; // < 0: no timer
    MarkerKind marker = MarkerKind::None;
    Vec3 markerPosition;
    std::string prompt;         // context action ("Press E to ...")
    std::uint16_t pickupsFound = 0, pickupsTotal = 0;
    std::vector<InventoryItem> inventory;
    std::uint16_t missionsDone = 0, missionsTotal = 0;
    std::uint32_t playSeconds = 0;
    std::uint8_t frozen = 0;    // race countdown: inputs ignored
    bool operator==(const PlayerStatus& o) const;
};

// ---- UDP ------------------------------------------------------------------
enum class Udp : std::uint8_t { Input = 1, Hello = 2, Snapshot = 10 };

struct InputFrame {
    std::uint32_t seq = 0;
    std::int8_t moveX = 0, moveZ = 0; // world-space move direction * 127
    std::uint16_t buttons = 0;
    std::int8_t throttle = 0, steer = 0; // * 127 (driving)

    Vec3 move() const { return {moveX / 127.0f, 0.0f, moveZ / 127.0f}; }
    bool has(std::uint16_t b) const { return (buttons & b) != 0; }
};

struct SelfState {
    CharacterMode mode = CharacterMode::OnFoot;
    Vec3 position, velocity;
    float yaw = 0.0f;
    bool grounded = true;
    std::uint16_t vehicle = 0; // net id, 0 = none
    std::uint8_t seat = 0;
    // Driver only: the authoritative vehicle state for prediction.
    Vec3 vehiclePosition;
    float vehicleYaw = 0.0f, vehicleSpeed = 0.0f, vehicleSteer = 0.0f, vehicleDamage = 0.0f;
};

struct EntityState {
    std::uint16_t netId = 0;
    EntityKind kind = EntityKind::Character;
    std::uint8_t flags = 0;
    Vec3 position;
    float yaw = 0.0f;
    // Character
    std::uint32_t appearance = 0;
    CharacterMode mode = CharacterMode::OnFoot;
    bool grounded = true, crouch = false;
    std::uint8_t action = 0; // (counter << 2) | type: 1 wave, 2 punch
    float speed = 0.0f, verticalSpeed = 0.0f;
    std::uint16_t vehicle = 0;
    // Vehicle
    std::uint8_t model = 0, paint = 0;
    float steer = 0.0f, wheelSpin = 0.0f, pitch = 0.0f, roll = 0.0f, damage = 0.0f;
    // Pickup / marker
    std::uint8_t subtype = 0;
};

struct Snapshot {
    std::uint32_t tick = 0;
    float serverTime = 0.0f;
    float hour = 12.0f;
    std::uint32_t ackInput = 0;
    SelfState self;
    std::vector<EntityState> entities;
};

// Encoding. Decoders return nullopt for malformed input.
std::vector<std::uint8_t> encode(const Hello& m);
std::vector<std::uint8_t> encode(const Welcome& m);
std::vector<std::uint8_t> encode(const RosterEntry& m);
std::vector<std::uint8_t> encode(const ChatLine& m);
std::vector<std::uint8_t> encode(const Event& m);
std::vector<std::uint8_t> encode(const PlayerStatus& m);
std::vector<std::uint8_t> encode(const CreateRoomRequest& m);
std::vector<std::uint8_t> encodeRoomList(const std::vector<RoomInfo>& rooms);
std::vector<std::uint8_t> encodeScoreboard(const std::vector<ScoreEntry>& scores);
std::optional<CreateRoomRequest> decodeCreateRoom(const std::vector<std::uint8_t>& p);
std::optional<std::vector<RoomInfo>> decodeRoomList(const std::vector<std::uint8_t>& p);
std::optional<std::vector<ScoreEntry>> decodeScoreboard(const std::vector<std::uint8_t>& p);
// LAN discovery datagrams.
std::vector<std::uint8_t> encodeDiscoveryQuery();
std::vector<std::uint8_t> encodeDiscoveryReply(std::uint16_t gamePort, const std::vector<RoomInfo>& rooms);
bool isDiscoveryQuery(const std::uint8_t* data, std::size_t size);
std::optional<std::pair<std::uint16_t, std::vector<RoomInfo>>> decodeDiscoveryReply(const std::uint8_t* data, std::size_t size);
std::optional<Hello> decodeHello(const std::vector<std::uint8_t>& p);
std::optional<Welcome> decodeWelcome(const std::vector<std::uint8_t>& p);
std::optional<RosterEntry> decodeRoster(const std::vector<std::uint8_t>& p);
std::optional<ChatLine> decodeChat(const std::vector<std::uint8_t>& p);
std::optional<Event> decodeEvent(const std::vector<std::uint8_t>& p);
std::optional<PlayerStatus> decodeStatus(const std::vector<std::uint8_t>& p);

// UDP datagrams (header included).
std::vector<std::uint8_t> encodeUdpHello(std::uint32_t session);
std::vector<std::uint8_t> encodeInputs(std::uint32_t session, const std::vector<InputFrame>& frames);
std::vector<std::uint8_t> encodeSnapshot(std::uint32_t session, const Snapshot& s);

struct UdpHeader {
    Udp type;
    std::uint32_t session;
};
std::optional<UdpHeader> decodeUdpHeader(ByteReader& r);
std::optional<std::vector<InputFrame>> decodeInputs(ByteReader& r);
std::optional<Snapshot> decodeSnapshot(ByteReader& r);

} // namespace av::proto
