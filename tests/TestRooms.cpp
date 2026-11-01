// Rooms: a room server creating rooms on demand, joining by code, LAN
// discovery, and the multiplayer activities.
#include "TestHarness.h"

#include "GameServer.h"
#include "Lobby.h"
#include "NetClient.h"
#include "ServerWorld.h"

#include "Animation/Humanoid.h"

#include <chrono>
#include <filesystem>
#include <thread>

using namespace av;
using namespace av::proto;

namespace {
server::ServerConfig roomServerConfig()
{
    server::ServerConfig sc;
    sc.port = 0;
    sc.loopbackOnly = true;
    sc.roomServer = true;
    sc.mainRoom = false;
    sc.pedestrians = 2;
    sc.trafficCars = 1;
    sc.savePath.clear();
    return sc;
}
} // namespace

TEST_CASE("Appearance: every look round-trips through its 28-bit code")
{
    Rng rng(99);
    for (int i = 0; i < 200; ++i) {
        const Appearance a = Appearance::random(rng);
        const Appearance b = Appearance::fromCode(a.code());
        CHECK_EQ(b.code(), a.code());
        CHECK(b.shirt == a.shirt && b.skin == a.skin && b.hair_style == a.hair_style && b.hat == a.hat);
        CHECK(a.code() < (1u << 28));
    }
    // Arbitrary numbers (e.g. NPC seeds) decode to valid looks.
    const Appearance any = Appearance::fromCode(0xFFFFFFFFu);
    CHECK(static_cast<int>(any.hair_style) < Appearance::kHairStyles);
}

TEST_CASE("Rooms: create a room, join it by code, reject bad codes")
{
    server::EmbeddedServer srv(roomServerConfig(), AV_SOURCE_DIR);
    std::string error;
    CHECK(srv.start(error));
    NetAddress addr;
    addr.port = srv.port();

    CreateRoomRequest req;
    req.name = "Test Room";
    req.mode = GameMode::Races;
    req.maxPlayers = 4;
    const auto code = client::createRoom(addr, req, error);
    CHECK(code.has_value());
    if (!code) return;
    CHECK_EQ(code->size(), std::size_t{4});

    const auto rooms = client::listRooms(addr, error);
    CHECK(rooms && rooms->size() == 1);
    if (rooms && !rooms->empty()) {
        CHECK_EQ((*rooms)[0].code, *code);
        CHECK_EQ((*rooms)[0].name, std::string("Test Room"));
        CHECK((*rooms)[0].mode == GameMode::Races);
    }

    client::NetClient a;
    Hello h{kProtocolVersion, "Ana", "", 1u, client::normalizeRoomCode(" gta-" + *code + " ")};
    CHECK(a.connect(addr, h, error));
    CHECK_EQ(a.welcome().roomCode, *code);
    CHECK(a.welcome().mode == GameMode::Races);

    client::NetClient lost;
    CHECK(!lost.connect(addr, Hello{kProtocolVersion, "Ben", "", 2u, "ZZZZ"}, error));
    CHECK(error.find("ZZZZ") != std::string::npos);

    client::NetClient noRoom;
    CHECK(!noRoom.connect(addr, Hello{kProtocolVersion, "Cy", "", 3u, ""}, error)); // room server has no main room
}

TEST_CASE("Rooms: a LAN host answers discovery broadcasts")
{
    server::ServerConfig sc;
    sc.port = 0;
    sc.loopbackOnly = false; // discovery is off for loopback-only (single player) servers
    sc.lanDiscovery = true;
    sc.roomName = "Discovery Test";
    sc.pedestrians = 0;
    sc.trafficCars = 0;
    sc.savePath.clear();
    server::EmbeddedServer srv(sc, AV_SOURCE_DIR);
    std::string error;
    CHECK(srv.start(error));
    bool found = false;
    for (int attempt = 0; attempt < 3 && !found; ++attempt) {
        for (const RoomInfo& r : client::discoverLanRooms(500)) {
            found = found || (r.name == "Discovery Test" && r.port == srv.port());
        }
    }
    CHECK(found);
}

TEST_CASE("Activities: a street race grids players in cars and finishes")
{
    server::ServerConfig sc;
    sc.savePath.clear();
    sc.pedestrians = 0;
    sc.trafficCars = 0;
    server::ServerWorld world(sc, AV_SOURCE_DIR);
    std::string error;
    const Entity p = world.joinPlayer("Racer", "", 1u, 1u, error);
    world.command(p, CommandKind::StartRace, 0);
    PlayerStatus st = world.buildStatus(p);
    CHECK(st.missionTitle.rfind("Race: ", 0) == 0);
    for (int i = 0; i < 30 * 9; ++i) world.tick(1.0f / 30.0f); // gathering -> grid
    auto* c = world.registry().get<server::CharacterComponent>(p);
    CHECK(c->mode == CharacterMode::InVehicle);
    CHECK_EQ(world.buildStatus(p).frozen, std::uint8_t{1});
    for (int i = 0; i < 30 * 5; ++i) world.tick(1.0f / 30.0f); // countdown -> GO
    st = world.buildStatus(p);
    CHECK_EQ(st.frozen, std::uint8_t{0});
    CHECK(st.marker == MarkerKind::Checkpoint);

    // Drive through every checkpoint by placing the car on it.
    const int before = world.buildStatus(p).money;
    for (int k = 0; k < 20 && !world.buildStatus(p).missionTitle.empty(); ++k) {
        const PlayerStatus now = world.buildStatus(p);
        if (now.marker != MarkerKind::Checkpoint) break;
        auto* v = world.registry().get<server::VehicleComponent>(c->vehicle);
        v->state.position = now.markerPosition;
        world.tick(1.0f / 30.0f);
    }
    st = world.buildStatus(p);
    CHECK(st.missionTitle.empty());
    CHECK_EQ(st.money, before + 1000); // first place prize
    CHECK(!world.scoreboard().empty() && world.scoreboard()[0].points == 3);
}

TEST_CASE("Activities: cops catch a robber")
{
    server::ServerConfig sc;
    sc.savePath.clear();
    sc.pedestrians = 0;
    sc.trafficCars = 0;
    server::ServerWorld world(sc, AV_SOURCE_DIR);
    std::string error;
    const Entity a = world.joinPlayer("Alpha", "", 1u, 1u, error);
    const Entity b = world.joinPlayer("Bravo", "", 2u, 2u, error);
    world.command(a, CommandKind::StartCopsAndRobbers, 0);
    for (int i = 0; i < 30 * 9; ++i) world.tick(1.0f / 30.0f);
    auto* pa = world.registry().get<server::PlayerComponent>(a);
    auto* pb = world.registry().get<server::PlayerComponent>(b);
    CHECK((pa->role == "cop" && pb->role == "robber") || (pa->role == "robber" && pb->role == "cop"));
    const Entity cop = pa->role == "cop" ? a : b, robber = cop == a ? b : a;
    // Stand the cop right next to the robber for two seconds.
    for (int i = 0; i < 60; ++i) {
        auto* rc = world.registry().get<server::CharacterComponent>(robber);
        auto* cc = world.registry().get<server::CharacterComponent>(cop);
        cc->body.motor.setState(rc->body.motor.position + Vec3{0.8f, 0, 0}, {}, true);
        world.tick(1.0f / 30.0f);
    }
    CHECK(world.registry().get<server::PlayerComponent>(robber)->role.empty()); // round over: cops won
    CHECK(world.registry().get<server::PlayerComponent>(cop)->points >= 4);
}
