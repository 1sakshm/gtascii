// Phase 6/7: protocol encoding, and an end-to-end multiplayer session over
// real loopback sockets (server thread + two clients).
#include "TestHarness.h"

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers" // Hello{...} without the optional room
#endif

#include "GameServer.h"
#include "NetClient.h"
#include "Protocol.h"

#include "Core/FileUtil.h"

#include <chrono>
#include <filesystem>
#include <thread>

using namespace av;
using namespace av::proto;

TEST_CASE("Protocol: TCP messages round-trip")
{
    Hello h{kProtocolVersion, "Alice", "pw", 123u};
    const auto hd = decodeHello(encode(h));
    CHECK(hd && hd->name == "Alice" && hd->password == "pw" && hd->appearanceSeed == 123u);

    Welcome w;
    w.session = 0xDEADBEEF;
    w.netId = 9;
    w.worldSeed = 1986;
    w.udpPort = 41986;
    w.hour = 18.25f;
    w.motd = "hi";
    const auto wd = decodeWelcome(encode(w));
    CHECK(wd && wd->session == w.session && wd->netId == 9 && wd->hour == 18.25f && wd->motd == "hi");

    PlayerStatus s;
    s.money = 1234;
    s.wanted = 3;
    s.missionTitle = "Hot Package";
    s.missionTimer = 42.5f;
    s.marker = MarkerKind::Objective;
    s.markerPosition = {1, 2, 3};
    s.inventory = {{"Package", 1}, {"Flamingo statuette", 4}};
    const auto sd = decodeStatus(encode(s));
    CHECK(sd && *sd == s);

    CHECK(!decodeStatus({1, 2, 3})); // truncated
    CHECK(!decodeHello({}));
}

TEST_CASE("Protocol: snapshots round-trip and fit one datagram")
{
    Snapshot snap;
    snap.tick = 77;
    snap.serverTime = 12.5f;
    snap.ackInput = 1000;
    snap.self.mode = CharacterMode::InVehicle;
    snap.self.vehicle = 5;
    snap.self.seat = 0;
    snap.self.vehiclePosition = {10, 0, -4};
    snap.self.vehicleSpeed = 13.0f;
    for (int i = 0; i < 200; ++i) {
        EntityState e;
        e.netId = static_cast<std::uint16_t>(i + 1);
        e.kind = i % 2 ? EntityKind::Vehicle : EntityKind::Character;
        e.position = {static_cast<float>(i), 0.5f, -static_cast<float>(i)};
        e.yaw = 1.0f;
        e.model = 2;
        e.speed = 6.0f;
        e.mode = CharacterMode::KnockedDown;
        e.action = 0x15;
        snap.entities.push_back(e);
    }
    const auto bytes = encodeSnapshot(42u, snap);
    CHECK(bytes.size() <= kMaxUdpPayload);
    ByteReader r(bytes);
    const auto header = decodeUdpHeader(r);
    CHECK(header && header->session == 42u && header->type == Udp::Snapshot);
    const auto back = decodeSnapshot(r);
    CHECK(back.has_value());
    if (back) {
        CHECK_EQ(back->tick, 77u);
        CHECK(back->self.mode == CharacterMode::InVehicle && back->self.vehicleSpeed == 13.0f);
        CHECK(back->entities.size() > 30 && back->entities.size() < 200); // truncated to fit, nearest first
        const EntityState& c = back->entities[0];
        CHECK(c.kind == EntityKind::Character && c.mode == CharacterMode::KnockedDown && c.action == 0x15);
        CHECK(nearlyEqual(c.yaw, 1.0f, 1e-3f));
        CHECK(nearlyEqual(back->entities[1].speed, 6.0f, 0.4f));
    }

    std::vector<InputFrame> frames(3);
    for (int i = 0; i < 3; ++i) {
        frames[static_cast<std::size_t>(i)].seq = static_cast<std::uint32_t>(10 + i);
        frames[static_cast<std::size_t>(i)].moveX = -127;
        frames[static_cast<std::size_t>(i)].buttons = Buttons::Run | Buttons::Jump;
    }
    const auto ib = encodeInputs(7u, frames);
    ByteReader ir(ib);
    CHECK(decodeUdpHeader(ir).has_value());
    const auto inputs = decodeInputs(ir);
    CHECK(inputs && inputs->size() == 3 && (*inputs)[2].seq == 12u && (*inputs)[0].has(Buttons::Jump));
}

TEST_CASE("Multiplayer: two clients see each other, chat, and persist")
{
    const std::filesystem::path root = AV_SOURCE_DIR;
    server::ServerConfig sc;
    sc.port = 0;
    sc.loopbackOnly = true;
    sc.savePath = (std::filesystem::temp_directory_path() / "gtascii_test_multiplayer.json").string();
    sc.pedestrians = 4;
    sc.trafficCars = 2;
    std::filesystem::remove(std::filesystem::path(sc.savePath));

    {
        server::EmbeddedServer srv(sc, root);
        std::string error;
        CHECK(srv.start(error));
        NetAddress addr;
        addr.port = srv.port();

        client::NetClient a, b;
        CHECK(a.connect(addr, Hello{kProtocolVersion, "Alice", "", 1u}, error));
        CHECK(b.connect(addr, Hello{kProtocolVersion, "Bob", "secret", 2u}, error));

        client::NetClient dup;
        CHECK(!dup.connect(addr, Hello{kProtocolVersion, "Alice", "", 3u}, error)); // name in use
        CHECK(!error.empty());

        const std::uint16_t bobId = b.welcome().netId;
        bool aliceSeesBob = false, bobMoved = false, chatArrived = false;
        Vec3 firstBob, lastBob;
        std::uint32_t seq = 0;
        bool sentChat = false;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (std::chrono::steady_clock::now() < until && !(aliceSeesBob && bobMoved && chatArrived)) {
            // Bob runs east; Alice stands still (but keeps her UDP endpoint alive).
            InputFrame fb;
            fb.seq = ++seq;
            fb.moveX = 127;
            fb.buttons = Buttons::Run;
            b.sendInputs({fb});
            InputFrame fa;
            fa.seq = seq;
            a.sendInputs({fa});
            a.poll();
            b.poll();
            for (const Snapshot& s : a.takeSnapshots()) {
                for (const EntityState& e : s.entities) {
                    if (e.netId == bobId && (e.flags & EntityFlags::Player)) {
                        if (!aliceSeesBob) firstBob = e.position;
                        aliceSeesBob = true;
                        lastBob = e.position;
                    }
                }
            }
            bobMoved = aliceSeesBob && lastBob.x - firstBob.x > 2.0f;
            if (aliceSeesBob && !sentChat) {
                a.sendChat("hello bob");
                sentChat = true;
            }
            for (const ChatLine& line : b.takeChat()) {
                chatArrived = chatArrived || (line.from == "Alice" && line.text == "hello bob");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        CHECK(aliceSeesBob);
        CHECK(bobMoved);
        CHECK(chatArrived);
        CHECK(a.roster().size() == 2 && b.roster().size() == 2);
        CHECK(a.status().money == 500); // starting cash, delivered over TCP
    } // server stops and saves

    const auto saved = readTextFile(std::filesystem::path(sc.savePath));
    CHECK(saved.has_value());
    if (saved) {
        const JsonValue doc = parseJson(*saved);
        CHECK(doc.find("players") && doc.find("players")->find("Alice") && doc.find("players")->find("Bob"));
        CHECK(doc.find("players")->find("Bob")->numberOr("passwordHash", 0) != 0);
    }

    // Bob's name is now protected by his password.
    {
        server::EmbeddedServer srv(sc, root);
        std::string error;
        CHECK(srv.start(error));
        NetAddress addr;
        addr.port = srv.port();
        client::NetClient wrong, right;
        CHECK(!wrong.connect(addr, Hello{kProtocolVersion, "Bob", "guess", 2u}, error));
        CHECK(right.connect(addr, Hello{kProtocolVersion, "Bob", "secret", 2u}, error));
    }
    std::filesystem::remove(std::filesystem::path(sc.savePath));
}
