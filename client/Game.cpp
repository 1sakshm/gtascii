#include "Game.h"

#include "GameHud.h"
#include "LocalPlayer.h"
#include "Missions.h"
#include "NetClient.h"
#include "Recorder.h"
#include "RemoteWorld.h"

#include "GameServer.h"

#include "Audio/Audio.h"
#include "Core/FileUtil.h"
#include "Core/FrameClock.h"
#include "Entities/VehicleVisual.h"
#include "Gameplay/ThirdPersonCamera.h"
#include "Graphics/AnsiPresenter.h"
#include "Graphics/ObjLoader.h"
#include "Graphics/Renderer.h"
#include "Physics/DynamicCollisionWorld.h"
#include "Platform/InputState.h"
#include "Platform/Terminal.h"
#include "World/ChunkManager.h"

#include <algorithm>
#include <functional>
#include <string>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace client {

namespace {

using namespace av;
using namespace av::proto;

std::int8_t toS8(float v) { return static_cast<std::int8_t>(std::lround(clampv(v, -1.0f, 1.0f) * 127.0f)); }

// Scripted input for --autopilot: walk, run, jump, crouch, wave, then grab
// a car when one is close and drive it around.
struct Autopilot {
    double t = 0.0;
    InputFrame frame(const ThirdPersonCamera& cam, float& cameraTurn, bool driving, float dt)
    {
        t += dt;
        InputFrame f;
        cameraTurn = 0.12f;
        const double phase = std::fmod(t, 40.0);
        auto move = [&](float turn) {
            const float a = static_cast<float>(t) * turn;
            const Vec3 d = normalize(cam.forwardXZ() * std::cos(a) + cam.rightXZ() * std::sin(a));
            f.moveX = toS8(d.x);
            f.moveZ = toS8(d.z);
        };
        if (driving) {
            f.throttle = toS8(phase < 34.0 ? 0.8f : -0.5f);
            f.steer = toS8(0.35f * std::sin(static_cast<float>(t) * 0.4f));
            if (phase > 38.0 && std::fmod(phase, 1.0) < 0.05) f.buttons |= Buttons::EnterExit;
            return f;
        }
        if (phase < 2.0) {
        } else if (phase < 6.0) {
            move(0.0f);
        } else if (phase < 12.0) {
            move(0.5f);
            f.buttons |= Buttons::Run;
            if (std::fmod(phase, 3.0) < 0.02) f.buttons |= Buttons::Jump;
        } else if (phase < 14.0) {
            f.buttons |= Buttons::Crouch;
        } else if (phase < 15.0) {
            if (phase < 14.02) f.buttons |= Buttons::Wave;
        } else {
            move(-0.3f);
            f.buttons |= Buttons::Run;
            if (std::fmod(phase, 1.0) < 0.02) f.buttons |= Buttons::EnterExit;
        }
        return f;
    }
};

// ---- the phone -------------------------------------------------------------
enum class PhoneScreen { Closed, Home, Jobs, Stats };

std::string distanceText(float metres)
{
    return metres < 1000.0f ? std::to_string(static_cast<int>(metres)) + " m"
                            : std::to_string(static_cast<int>(metres / 100.0f) / 10) + "." +
                                  std::to_string(static_cast<int>(metres / 100.0f) % 10) + " km";
}

} // namespace

RunSummary runGame(const LaunchOptions& opts, Terminal& terminal, Audio& audio)
{
    const ClientConfig& cfg = opts.config;
    const auto& root = opts.dataRoot;

    // ---- server: embedded (single player / LAN host) or remote (join) ----
    std::unique_ptr<server::EmbeddedServer> embedded;
    NetAddress address;
    std::string connectionLabel;
    if (opts.mode == SessionMode::Join) {
        const auto parsed = NetAddress::parse(opts.serverAddress, kDefaultPort);
        if (!parsed) {
            throw std::runtime_error("cannot resolve server address '" + opts.serverAddress + "'");
        }
        address = *parsed;
        connectionLabel = opts.serverAddress;
    } else {
        server::ServerConfig sc = server::ServerConfig::load(root / "config/server.json");
        const bool host = opts.mode == SessionMode::Host;
        sc.loopbackOnly = !host;
        sc.lanDiscovery = host;
        sc.port = host ? cfg.hostPort : 0;
        sc.startHour = cfg.startHour;
        sc.overrideSavedHour = opts.hourGiven;
        sc.dayLengthMinutes = cfg.dayLengthMinutes;
        sc.seed = cfg.seed;
        sc.roomServer = false;
        sc.mainRoom = true;
        if (host) {
            sc.roomName = opts.hostRoom.name.empty() ? cfg.playerName + "'s room" : opts.hostRoom.name;
            sc.mode = opts.hostRoom.mode;
            sc.maxPlayers = opts.hostRoom.maxPlayers;
            sc.savePath = "saves/lan.json";
        } else {
            sc.maxPlayers = 1;
            sc.savePath = "saves/singleplayer.json";
        }
        embedded = std::make_unique<server::EmbeddedServer>(sc, root);
        std::string error;
        if (!embedded->start(error) && host) {
            // The usual port is busy (another game on this PC?): use any free one.
            sc.port = 0;
            embedded = std::make_unique<server::EmbeddedServer>(sc, root);
            error.clear();
            if (!embedded->start(error)) throw std::runtime_error("could not start the server: " + error);
        } else if (!error.empty()) {
            throw std::runtime_error("could not start the local server: " + error);
        }
        address.ip = 0x7F000001;
        address.port = embedded->port();
        connectionLabel = host ? "LAN host :" + std::to_string(address.port) : "single player";
    }

    NetClient net;
    Hello hello;
    hello.name = cfg.playerName;
    hello.password = cfg.password;
    hello.appearanceSeed = cfg.appearanceSeed;
    hello.room = opts.roomCode;
    std::string error;
    if (!net.connect(address, hello, error)) {
        throw std::runtime_error(error);
    }
    const Welcome welcome = net.welcome();
    if (!welcome.roomCode.empty() && welcome.roomCode != "MAIN") {
        connectionLabel = "room " + welcome.roomCode;
    }

    // ---- client world: same seed as the server, generated locally ----
    MaterialLibrary materials;
    materials.loadFile(root / "assets/materials/materials.json");
    const CityLayout layout = CityLayout::fromFile(welcome.worldSeed, root / "assets/maps/landmarks.json");
    const CityGenerator generator(layout, materials,
                                  loadObjFile(root / "assets/models/newsbox.obj", materials, materials.require("metal")));
    const std::vector<MissionDef> missions = loadMissions(root / "assets/missions/missions.json", layout);
    ChunkManager world(generator, cfg.loadRadius);
    DynamicCollisionWorld predictionWorld(world);
    const VehicleVisuals vehicleVisuals(materials);
    CharacterLooks looks(materials);
    RemoteWorld remote(materials, looks);
    CharacterAnimation myAnim;

    LocalPlayer me;
    world.loadAll(layout.spawn().position);
    me.reset(layout.spawn().position, layout.spawn().yaw, world);

    ThirdPersonCameraSettings camSettings;
    camSettings.distance = cfg.cameraDistance;
    camSettings.autoRecenterDelay = cfg.autoRecenterDelay;
    camSettings.fovY = radians(static_cast<float>(cfg.fovDegrees));
    ThirdPersonCamera camera(camSettings);
    camera.snapTo(me.position(), me.yaw());

    const CharRamp* ramp = materials.findRamp(cfg.ramp);
    Renderer renderer(materials, ramp ? *ramp : *findRamp("standard"));
    RendererSettings rs;
    rs.samplesX = cfg.samplesX;
    rs.samplesY = cfg.samplesY;
    rs.cellAspect = cfg.cellAspect;
    rs.outlines = cfg.outlines;
    rs.maxPointLights = cfg.maxPointLights;
    renderer.setSettings(rs);

    PresenterOptions po;
    po.colorBits = cfg.colorBits;
    AnsiPresenter presenter(po);
    FrameClock clock(cfg.targetFps);
    InputState input;
    std::vector<InputEvent> events;
    Framebuffer fb;
    Recorder recorder;

    // ---- session state ----
    std::uint32_t seq = 0;
    std::vector<InputFrame> recent;
    std::vector<AABB> obstacles;
    bool showHud = true, showMap = true, showInventory = false, showScores = false;
    bool crouch = false, alwaysRun = true, radioOn = false;
    bool chatting = false;
    std::string chatInput;
    std::vector<ChatEntry> chatLog;
    std::vector<Toast> toasts;
    std::string bannerText;
    Color bannerColor{255, 80, 190};
    float bannerTimer = 0.0f;
    std::uint16_t pendingEdges = 0;
    std::uint8_t localAction = 0;
    double physicsAccumulator = 0.0;
    float hour = welcome.hour;
    std::string currentLandmark;
    Autopilot autopilot;
    PhoneScreen phone = PhoneScreen::Closed;
    int phoneSel = 0;
    bool hasWaypoint = false;
    Vec3 waypoint;
    float lastGait = 0.0f, lastDamage = 0.0f;
    std::uint8_t lastFrozen = 0;

    chatLog.push_back({welcome.motd, {255, 215, 80}, 0.0f});
    if (opts.mode == SessionMode::Host) {
        chatLog.push_back({"Hosting \"" + welcome.roomName + "\" on your network (port " + std::to_string(address.port) +
                               ") - friends find it under Multiplayer > Browse rooms",
                           {70, 230, 240}, 0.0f});
    } else if (!welcome.roomCode.empty() && welcome.roomCode != "MAIN") {
        chatLog.push_back({"Room code " + welcome.roomCode + " - share it so friends can join (Tab shows it again)", {70, 230, 240}, 0.0f});
    }
    chatLog.push_back({"P opens your phone: jobs with GPS, street races, cops & robbers, radio", {150, 145, 175}, 0.0f});
    audio.play(Sound::MissionStart, 0.6f);

    RunSummary summary;
    double totalMs = 0.0, totalBytes = 0.0;
    std::size_t totalTris = 0;
    bool running = true;
    presenter.invalidate();

    while (running) {
        const float dt = static_cast<float>(clock.beginFrame());
        const double now = clock.elapsedSeconds();
        const PlayerStatus& status = net.status();

        // ---- input ----
        input.beginFrame(now);
        events.clear();
        terminal.pollInput(events);
        for (const InputEvent& ev : events) {
            if (ev.type == InputEvent::Type::Interrupt) {
                running = false;
            } else if (ev.type == InputEvent::Type::Resize) {
                presenter.invalidate();
            } else if (chatting && ev.type == InputEvent::Type::Key && ev.key.down) {
                const KeyEvent& k = ev.key;
                if (k.key == Key::Enter) {
                    if (!chatInput.empty()) net.sendChat(chatInput);
                    chatInput.clear();
                    chatting = false;
                } else if (k.key == Key::Escape) {
                    chatInput.clear();
                    chatting = false;
                } else if (k.key == Key::Backspace) {
                    if (!chatInput.empty()) chatInput.pop_back();
                } else if (k.key == Key::Character && k.ch >= 0x20 && k.ch < 0x7F && chatInput.size() < 100) {
                    char c = static_cast<char>(k.ch);
                    if (k.shift && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                    chatInput.push_back(c);
                }
                continue;
            }
            input.handle(ev, now);
        }

        // Phone navigation takes the arrows / Enter while it is open.
        std::vector<std::pair<std::string, std::function<void()>>> phoneItems;
        std::string phoneTitle, phoneFooter = "Up/Down  Enter  Backspace back  P close";
        if (phone == PhoneScreen::Home) {
            phoneTitle = "PHONE";
            phoneItems.push_back({"Jobs (GPS to a mission)", [&] { phone = PhoneScreen::Jobs; phoneSel = 0; }});
            phoneItems.push_back({"Start a street race", [&] { net.sendCommand(CommandKind::StartRace, 0); phone = PhoneScreen::Closed; }});
            phoneItems.push_back({"Start cops & robbers", [&] { net.sendCommand(CommandKind::StartCopsAndRobbers, 0); phone = PhoneScreen::Closed; }});
            phoneItems.push_back({std::string("Radio: ") + (radioOn ? "ON (Vice FM)" : "off"), [&] { radioOn = !radioOn; }});
            phoneItems.push_back({std::string("Record session: ") + (recorder.active() ? "STOP" : "start"), [&] {
                                      if (recorder.active()) {
                                          recorder.stop();
                                          toasts.push_back({"Recording saved", {70, 230, 240}, 3.0f});
                                      } else {
                                          const TerminalSize ts = terminal.size();
                                          const std::string file = recorder.start(userDataDir() / "recordings", ts.cols, ts.rows);
                                          presenter.invalidate();
                                          toasts.push_back({file.empty() ? "Could not start recording" : "Recording to " + file, {70, 230, 240}, 4.0f});
                                      }
                                  }});
            phoneItems.push_back({"Stats", [&] { phone = PhoneScreen::Stats; phoneSel = 0; }});
            if (hasWaypoint) phoneItems.push_back({"Clear GPS waypoint", [&] { hasWaypoint = false; }});
        } else if (phone == PhoneScreen::Jobs) {
            phoneTitle = "JOBS";
            std::vector<std::pair<float, const MissionDef*>> sorted;
            for (const MissionDef& m : missions) sorted.push_back({distance(m.start, me.position()), &m});
            std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& [d, m] : sorted) {
                const MissionDef* def = m;
                phoneItems.push_back({def->title + "  $" + std::to_string(def->reward) + "  " + distanceText(d), [&, def] {
                                          hasWaypoint = true;
                                          waypoint = def->start;
                                          phone = PhoneScreen::Closed;
                                          toasts.push_back({"GPS set: " + def->title, {255, 220, 60}, 3.0f});
                                      }});
            }
        } else if (phone == PhoneScreen::Stats) {
            phoneTitle = "STATS";
            const std::uint32_t mins = status.playSeconds / 60;
            phoneItems.push_back({"Cash: $" + std::to_string(status.money), nullptr});
            phoneItems.push_back({"Missions: " + std::to_string(status.missionsDone) + "/" + std::to_string(status.missionsTotal), nullptr});
            phoneItems.push_back({"Flamingos: " + std::to_string(status.pickupsFound) + "/" + std::to_string(status.pickupsTotal), nullptr});
            phoneItems.push_back({"Play time: " + std::to_string(mins / 60) + "h " + std::to_string(mins % 60) + "m", nullptr});
            phoneItems.push_back({"Room: " + welcome.roomName + " (" + welcome.roomCode + ")", nullptr});
        }

        if (!chatting) {
            if (input.pressed(U'p')) {
                phone = phone == PhoneScreen::Closed ? PhoneScreen::Home : PhoneScreen::Closed;
                phoneSel = 0;
                audio.play(Sound::MenuSelect, 0.5f);
            }
            if (phone != PhoneScreen::Closed && !phoneItems.empty()) {
                const int n = static_cast<int>(phoneItems.size());
                if (input.pressed(Key::Up)) { phoneSel = (phoneSel + n - 1) % n; audio.play(Sound::MenuMove, 0.4f); }
                if (input.pressed(Key::Down)) { phoneSel = (phoneSel + 1) % n; audio.play(Sound::MenuMove, 0.4f); }
                phoneSel = std::clamp(phoneSel, 0, n - 1);
                if (input.pressed(Key::Enter) && phoneItems[static_cast<std::size_t>(phoneSel)].second) {
                    audio.play(Sound::MenuSelect, 0.5f);
                    phoneItems[static_cast<std::size_t>(phoneSel)].second();
                }
                if (input.pressed(Key::Backspace) || input.pressed(Key::Escape)) {
                    phone = phone == PhoneScreen::Home ? PhoneScreen::Closed : PhoneScreen::Home;
                    phoneSel = 0;
                }
            } else {
                if (input.pressed(Key::Escape) || input.pressed(U'q')) running = false;
                if (input.pressed(Key::Enter)) {
                    chatting = true;
                    input.releaseAll();
                }
            }
            if (input.pressed(U'h')) showHud = !showHud;
            if (input.pressed(U'm')) showMap = !showMap;
            if (input.pressed(U'i')) showInventory = !showInventory;
            if (input.pressed(Key::Tab)) showScores = !showScores;
            if (input.pressed(U't')) net.sendCommand(CommandKind::SkipHour, 1);
            if (input.pressed(U'c')) crouch = !crouch;
            if (input.pressed(U'r')) alwaysRun = !alwaysRun;
            if (input.pressed(U'v')) camera.recenter();
            if (input.pressed(U' ')) pendingEdges |= Buttons::Jump;
            if (input.pressed(U'e')) pendingEdges |= Buttons::Interact;
            if (input.pressed(U'f')) { pendingEdges |= Buttons::EnterExit; audio.play(Sound::Door, 0.6f); }
            if (input.pressed(U'x')) { pendingEdges |= Buttons::Punch; audio.play(Sound::Punch, 0.7f); }
            if (input.pressed(U'g')) pendingEdges |= Buttons::Wave;
            if (input.pressed(U'o')) {
                rs.outlines = !rs.outlines;
                renderer.setSettings(rs);
            }
            if (input.pressed(Key::F9)) {
                if (recorder.active()) {
                    recorder.stop();
                    toasts.push_back({"Recording saved", {70, 230, 240}, 3.0f});
                } else {
                    const TerminalSize ts = terminal.size();
                    const std::string file = recorder.start(userDataDir() / "recordings", ts.cols, ts.rows);
                    presenter.invalidate();
                    toasts.push_back({file.empty() ? "Could not start recording" : "Recording to " + file, {70, 230, 240}, 4.0f});
                }
            }
        }

        // Camera control (arrows belong to the phone while it is open).
        float yawIn = 0.0f, pitchIn = 0.0f;
        if (!chatting && phone == PhoneScreen::Closed) {
            if (input.held(Key::Left)) yawIn += cfg.keyRotateSpeed * dt;
            if (input.held(Key::Right)) yawIn -= cfg.keyRotateSpeed * dt;
            if (input.held(Key::Up)) pitchIn -= cfg.keyRotateSpeed * 0.6f * dt;
            if (input.held(Key::Down)) pitchIn += cfg.keyRotateSpeed * 0.6f * dt;
            if (input.held(U'+') || input.held(U'=')) camera.zoom(-6.0f * dt);
            if (input.held(U'-')) camera.zoom(6.0f * dt);
        }
        yawIn -= static_cast<float>(input.dragDx()) * cfg.mouseSensitivity;
        pitchIn += static_cast<float>(input.dragDy()) * cfg.mouseSensitivity * (cfg.invertY ? -1.0f : 1.0f);
        camera.zoom(static_cast<float>(-input.wheel()) * 0.8f);

        // This frame's input.
        InputFrame base;
        const bool driving = me.driving();
        if (!chatting) {
            if (driving) {
                float throttle = 0.0f, steer = 0.0f;
                if (input.held(U'w')) throttle += 1.0f;
                if (input.held(U's')) throttle -= 1.0f;
                if (input.held(U'a')) steer += 1.0f;
                if (input.held(U'd')) steer -= 1.0f;
                base.throttle = toS8(throttle);
                base.steer = toS8(steer);
                if (input.held(U' ')) base.buttons |= Buttons::Handbrake;
            } else {
                Vec3 move;
                if (input.held(U'w')) move += camera.forwardXZ();
                if (input.held(U's')) move -= camera.forwardXZ();
                if (input.held(U'd')) move += camera.rightXZ();
                if (input.held(U'a')) move -= camera.rightXZ();
                move = normalize(move);
                base.moveX = toS8(move.x);
                base.moveZ = toS8(move.z);
            }
            if (alwaysRun != input.shiftHeld()) base.buttons |= Buttons::Run;
            if (crouch) base.buttons |= Buttons::Crouch;
        }
        if (opts.autopilot) {
            float turn = 0.0f;
            base = autopilot.frame(camera, turn, driving, dt);
            yawIn += turn * dt;
        }
        if (yawIn != 0.0f || pitchIn != 0.0f) {
            camera.rotate(yawIn, pitchIn);
        }
        const std::uint16_t autoEdges = base.buttons & (Buttons::Jump | Buttons::EnterExit | Buttons::Wave | Buttons::Punch | Buttons::Interact);
        base.buttons = static_cast<std::uint16_t>(base.buttons & ~autoEdges);
        pendingEdges |= autoEdges;
        if (status.frozen) {
            base = InputFrame{}; // race countdown: the server ignores input anyway
        }

        // ---- prediction + send (fixed 60 Hz input steps) ----
        obstacles.clear();
        remote.vehicleObstacles(obstacles, me.vehicleNetId());
        std::vector<AABB> walkers = obstacles; // on foot, other people are solid too
        for (const auto& [id, e] : remote.entities()) {
            if (e.current.kind == EntityKind::Character && e.current.mode == CharacterMode::OnFoot) {
                const Vec3 p = e.current.position;
                walkers.push_back(AABB({p.x - 0.22f, p.y, p.z - 0.22f}, {p.x + 0.22f, p.y + 1.8f, p.z + 0.22f}));
            }
        }
        predictionWorld.dynamicBoxes = driving ? obstacles : walkers;
        physicsAccumulator += dt;
        int steps = 0;
        while (physicsAccumulator >= kInputStep && steps < 6) {
            InputFrame f = base;
            f.seq = ++seq;
            f.buttons |= pendingEdges;
            if (pendingEdges & (Buttons::Wave | Buttons::Punch)) {
                localAction = static_cast<std::uint8_t>((((localAction >> 2) + 1) << 2) | ((pendingEdges & Buttons::Punch) ? 2 : 1));
            }
            pendingEdges = 0;
            me.predict(f, predictionWorld, &obstacles);
            recent.push_back(f);
            if (recent.size() > 4) recent.erase(recent.begin());
            net.sendInputs(recent);
            physicsAccumulator -= kInputStep;
            ++steps;
        }
        if (steps == 6) physicsAccumulator = 0.0;

        // ---- network in ----
        net.poll();
        if (!net.connected()) {
            summary.disconnectReason = net.disconnectReason().empty() ? "Lost connection to the server" : net.disconnectReason();
            running = false;
        }
        const std::vector<Snapshot> snaps = net.takeSnapshots();
        for (const Snapshot& s : snaps) {
            remote.ingest(s, now);
        }
        if (!snaps.empty()) {
            const Snapshot& latest = snaps.back();
            hour = latest.hour;
            VehicleModel model = VehicleModel::Sedan;
            if (const auto* v = remote.find(latest.self.vehicle)) model = static_cast<VehicleModel>(v->current.model);
            me.reconcile(latest, model, predictionWorld, &obstacles);
            summary.maxCorrection = std::max(summary.maxCorrection, static_cast<double>(me.lastCorrection()));
        }
        for (const Event& ev : net.takeEvents()) {
            switch (ev.kind) {
            case EventKind::MissionPassed: bannerText = ev.text; bannerColor = {120, 255, 140}; bannerTimer = 4.0f; audio.play(Sound::MissionPass); break;
            case EventKind::MissionFailed: bannerText = ev.text; bannerColor = {255, 80, 80}; bannerTimer = 4.0f; audio.play(Sound::MissionFail); break;
            case EventKind::Arrested: bannerText = ev.text; bannerColor = {80, 140, 255}; bannerTimer = 4.0f; audio.play(Sound::Arrested); break;
            case EventKind::Hospitalized: bannerText = ev.text; bannerColor = {255, 80, 80}; bannerTimer = 4.0f; audio.play(Sound::MissionFail); break;
            case EventKind::MissionStarted:
                bannerText = ev.text;
                bannerColor = {255, 80, 190};
                bannerTimer = 4.0f;
                audio.play(ev.text.rfind("GO!", 0) == 0 ? Sound::Go : Sound::MissionStart);
                break;
            case EventKind::Wanted:
                toasts.push_back({ev.text + (ev.value > 0 ? " - wanted " + std::string(static_cast<std::size_t>(ev.value), '*') : ""),
                                  {255, 90, 90}, 3.5f});
                break;
            case EventKind::Pickup: toasts.push_back({ev.text, {120, 255, 140}, 3.5f}); audio.play(Sound::Pickup); break;
            case EventKind::Money:
            case EventKind::Purchase: toasts.push_back({ev.text, {120, 255, 140}, 3.5f}); audio.play(Sound::Money); break;
            case EventKind::Saved: toasts.push_back({ev.text, {70, 230, 240}, 3.0f}); audio.play(Sound::MenuSelect); break;
            default:
                if (ev.text == "On your marks...") audio.play(Sound::Countdown);
                chatLog.push_back({ev.text, {255, 215, 80}, 0.0f});
                break;
            }
        }
        for (const ChatLine& line : net.takeChat()) {
            chatLog.push_back({line.system ? line.text : "<" + line.from + "> " + line.text,
                               line.system ? Color{255, 215, 80} : Color{220, 220, 235}, 0.0f});
            audio.play(Sound::Chat, 0.5f);
        }
        if (chatLog.size() > 50) chatLog.erase(chatLog.begin(), chatLog.begin() + 10);
        for (ChatEntry& c : chatLog) c.age += dt;
        for (Toast& t : toasts) t.timeLeft -= dt;
        toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [](const Toast& t) { return t.timeLeft <= 0.0f; }), toasts.end());
        if (toasts.size() > 4) toasts.erase(toasts.begin(), toasts.end() - 4);
        bannerTimer = std::max(0.0f, bannerTimer - dt);
        if (status.frozen && !lastFrozen) audio.play(Sound::Countdown);
        lastFrozen = status.frozen;

        // ---- local presentation state ----
        remote.update(now, dt);
        me.updateVisual(dt);
        world.update(me.position(), 1);
        const bool inVehicle = me.mode() == CharacterMode::InVehicle;
        const bool movingNow = driving ? std::fabs(me.vehicle().speed) > 1.0f : (base.moveX != 0 || base.moveZ != 0);
        camera.setVehicleMode(inVehicle);
        if (driving && std::fabs(me.vehicle().speed) > 3.0f) camera.recenter();
        camera.update(dt, me.position(), me.yaw(), movingNow || inVehicle, &world);
        if (hasWaypoint && length(Vec3{waypoint.x - me.position().x, 0, waypoint.z - me.position().z}) < 4.0f) {
            hasWaypoint = false;
            toasts.push_back({"You have arrived", {255, 220, 60}, 2.5f});
        }

        // Local character, animated from the predicted state.
        EntityState meState;
        meState.position = me.position();
        meState.yaw = me.yaw();
        meState.mode = me.mode();
        const Vec3 v = me.velocity();
        meState.speed = std::sqrt(v.x * v.x + v.z * v.z);
        meState.verticalSpeed = v.y;
        meState.grounded = me.grounded();
        meState.crouch = crouch && me.mode() == CharacterMode::OnFoot;
        meState.action = localAction;
        if (driving) {
            meState.position = vehicleSeatPosition(me.vehicle(), vehicleSpec(me.vehicleModel()), 0);
        } else if (inVehicle) {
            if (const auto* car = remote.find(me.vehicleNetId())) meState.yaw = car->current.yaw;
        }
        myAnim.update(meState, dt);

        // ---- audio ----
        if (!driving && me.mode() == CharacterMode::OnFoot && me.grounded() && meState.speed > 0.5f) {
            const float gait = myAnim.animator.gaitPhase();
            if ((lastGait < 0.5f && gait >= 0.5f) || gait < lastGait) audio.play(Sound::Footstep, meState.speed > 3.0f ? 0.6f : 0.35f);
        }
        lastGait = myAnim.animator.gaitPhase();
        if (driving) {
            const float speed = std::fabs(me.vehicle().speed);
            audio.setLoop(Loop::Engine, 0.55f, 1.0f + speed * 0.09f + (std::fabs(base.throttle) > 0 ? 0.3f : 0.0f));
            if (me.vehicle().damage > lastDamage + 0.02f) audio.play(Sound::Crash, std::min(1.0f, (me.vehicle().damage - lastDamage) * 12.0f));
            lastDamage = me.vehicle().damage;
        } else {
            audio.setLoop(Loop::Engine, 0.0f);
            lastDamage = 0.0f;
        }
        float sirenVolume = 0.0f;
        for (const auto& [id, e] : remote.entities()) {
            if (e.current.kind == EntityKind::Vehicle && (e.current.flags & EntityFlags::Sirens)) {
                sirenVolume = std::max(sirenVolume, 1.0f - distance(e.current.position, me.position()) / 70.0f);
            }
        }
        audio.setLoop(Loop::Siren, std::max(0.0f, sirenVolume) * 0.7f);
        audio.setLoop(Loop::Radio, radioOn ? (inVehicle ? 0.6f : 0.25f) : 0.0f);

        // Landmark title card.
        const Chunk* here = world.find(worldToChunk(me.position()));
        const std::string landmark = here ? here->landmarkName : "";
        if (landmark != currentLandmark) {
            currentLandmark = landmark;
            if (!landmark.empty() && bannerTimer <= 0.0f) {
                bannerText = landmark;
                bannerColor = {255, 80, 190};
                bannerTimer = 3.0f;
            }
        }

        // ---- render ----
        const Camera cam = camera.camera();
        SceneLighting light = SceneLighting::timeOfDay(hour);
        light.time = static_cast<float>(now);
        light.fogStart = cfg.fogStart;
        light.fogEnd = cfg.fogEnd;
        for (const auto& [coord, chunk] : world.chunks()) {
            for (const PointLight& l : chunk->lights) {
                if (distance(l.position, cam.position) < 90.0f) light.pointLights.push_back(l);
            }
        }
        for (const auto& [id, e] : remote.entities()) {
            if (e.current.kind != EntityKind::Vehicle || distance(e.current.position, cam.position) > 70.0f) continue;
            const Vec3 fwd{std::sin(e.current.yaw), 0, std::cos(e.current.yaw)};
            light.pointLights.push_back({e.current.position + fwd * 4.5f + Vec3{0, 0.8f, 0}, {1.0f, 0.95f, 0.8f}, 10.0f, 1.0f});
            if (e.current.flags & EntityFlags::Sirens) {
                const bool red = std::fmod(now * 3.0, 1.0) < 0.5;
                light.pointLights.push_back({e.current.position + Vec3{0, 2.0f, 0}, red ? Vec3{1, 0.1f, 0.1f} : Vec3{0.2f, 0.3f, 1}, 12.0f, 2.0f});
            }
        }

        const TerminalSize ts = terminal.size();
        renderer.beginFrame(ts.cols, ts.rows, cam, light);
        for (const auto& [coord, chunk] : world.chunks()) {
            if (!renderer.isVisible(chunk->worldBounds)) continue;
            const Mat4 model = Mat4::translation(chunk->origin());
            renderer.drawMesh(chunk->baseMesh, model);
            const Vec3 centre = chunk->origin() + Vec3{kChunkSize * 0.5f, 0, kChunkSize * 0.5f};
            const float d = length(Vec3{centre.x - cam.position.x, 0, centre.z - cam.position.z}) - kChunkSize * 0.5f;
            if (d < cfg.detailDistance) renderer.drawMesh(chunk->detailMesh, model);
        }
        remote.draw(renderer, vehicleVisuals, static_cast<float>(now), driving ? me.vehicleNetId() : 0);
        if (driving) {
            const auto* mine = remote.find(me.vehicleNetId());
            vehicleVisuals.draw(renderer, me.vehicleModel(), mine ? mine->current.paint : 0, me.vehicle(),
                                {me.vehicleModel() == VehicleModel::Cruiser, static_cast<float>(now)});
        }
        if (!myAnim.joints.empty()) {
            // In cops & robbers, cops wear the uniform.
            const bool copLook = std::any_of(net.scores().begin(), net.scores().end(),
                                             [&](const ScoreEntry& s) { return s.name == cfg.playerName && s.role == "cop"; });
            looks.get(cfg.appearanceSeed, copLook).draw(renderer, Mat4::translation(meState.position) * Mat4::rotationY(meState.yaw), myAnim.joints);
        }
        if (status.marker != MarkerKind::None) {
            remote.drawMarker(renderer, status.markerPosition, status.marker, static_cast<float>(now));
        }
        if (hasWaypoint) {
            remote.drawMarker(renderer, waypoint, MarkerKind::Objective, static_cast<float>(now));
        }
        renderer.resolve(fb);

        // Name tags: friends cyan, cops blue, robbers red.
        for (const auto& [id, e] : remote.entities()) {
            if (e.current.kind == EntityKind::Character && (e.current.flags & EntityFlags::Player)) {
                const auto it = net.roster().find(id);
                const Color tag = (e.current.flags & EntityFlags::Robber) ? Color{255, 90, 90}
                                  : (e.current.flags & EntityFlags::Police) ? Color{90, 140, 255} : Color{70, 230, 240};
                drawNameTag(fb, renderer.viewProjection(), cam.position, e.current.position + Vec3{0, 2.2f, 0},
                            it != net.roster().end() ? it->second : "player", tag);
            }
        }

        if (showHud) {
            HudInfo info;
            const ChunkCoord cc = worldToChunk(me.position());
            info.neighborhood = layout.neighborhood(cc);
            info.landmark = landmark;
            info.hour = hour;
            info.position = me.position();
            info.activity = driving ? std::string("driving ") + vehicleSpec(me.vehicleModel()).name
                                    : inVehicle ? std::string("passenger") : toString(myAnim.animator.state());
            if (recorder.active()) info.activity += "  [REC]";
            info.running = (base.buttons & Buttons::Run) != 0;
            info.fps = clock.smoothedFps();
            info.frameMs = clock.workTimeMs();
            info.triangles = renderer.stats().trianglesRasterized;
            info.players = static_cast<int>(std::max<std::size_t>(1, net.roster().size()));
            info.connection = connectionLabel;
            drawStatusBar(fb, info);
            drawHelpBar(fb, driving);
            drawMissionPanel(fb, status);
            if (showMap) {
                const bool blink = std::fmod(now * 4.0, 1.0) < 0.5;
                const Color siren = blink ? Color{255, 60, 60} : Color{80, 120, 255};
                std::vector<Blip> blips;
                for (const auto& [id, e] : remote.entities()) {
                    const EntityState& s = e.current;
                    if (s.kind == EntityKind::Character && (s.flags & EntityFlags::Robber)) {
                        blips.push_back({s.position, U'R', {255, 80, 80}, false});
                    } else if (s.kind == EntityKind::Character && (s.flags & EntityFlags::Player)) {
                        blips.push_back({s.position, U'@', (s.flags & EntityFlags::Police) ? Color{90, 140, 255} : Color{70, 230, 240}, false});
                    } else if (s.kind == EntityKind::Character && (s.flags & EntityFlags::Police)) {
                        blips.push_back({s.position, U'p', siren, false});
                    } else if (s.kind == EntityKind::Vehicle && (s.flags & EntityFlags::Police)) {
                        blips.push_back({s.position, U'P', siren, false});
                    } else if (s.kind == EntityKind::Vehicle && (s.flags & EntityFlags::Owned)) {
                        blips.push_back({s.position, U'c', {120, 255, 140}, false});
                    } else if (s.kind == EntityKind::Marker) {
                        const auto mk = static_cast<MarkerKind>(s.subtype);
                        const bool save = mk == MarkerKind::SavePoint, shop = mk == MarkerKind::Dealership;
                        blips.push_back({s.position, save ? U'S' : shop ? U'$' : U'M',
                                         save ? Color{70, 230, 240} : shop ? Color{120, 255, 140} : Color{255, 80, 190}, false});
                    } else if (s.kind == EntityKind::Pickup) {
                        blips.push_back({s.position, U'*', {255, 110, 180}, false});
                    }
                }
                if (status.marker != MarkerKind::None) blips.push_back({status.markerPosition, U'X', {255, 220, 60}, true});
                if (hasWaypoint) blips.push_back({waypoint, U'W', {255, 220, 60}, true});
                drawMinimap(fb, world, layout, me.position(), me.yaw(), blips);
                drawPlayerPanel(fb, status, static_cast<float>(now));
            }
            drawPrompt(fb, status.prompt);
            drawToasts(fb, toasts);
            drawChat(fb, chatLog, chatting, chatInput);
            if (showInventory) drawInventory(fb, status);
            if (showScores) drawScoreboard(fb, net.scores(), cfg.playerName, welcome.roomCode, welcome.roomName, welcome.mode);
        }
        if (phone != PhoneScreen::Closed) {
            PhoneView view;
            view.title = phoneTitle;
            for (const auto& [label, action] : phoneItems) view.lines.push_back(label);
            view.selected = std::clamp(phoneSel, 0, std::max(0, static_cast<int>(view.lines.size()) - 1));
            view.footer = phoneFooter;
            drawPhone(fb, view, static_cast<float>(now));
        }
        drawBanner(fb, bannerText, bannerColor, std::min(1.0f, bannerTimer));

        const std::string& out = presenter.encode(fb);
        if (!out.empty()) {
            terminal.write(out);
            recorder.write(out);
        }
        totalBytes += static_cast<double>(out.size());
        totalTris += renderer.stats().trianglesRasterized;

        clock.endFrame();
        totalMs += clock.workTimeMs();
        if (opts.maxFrames > 0 && clock.frameCount() >= opts.maxFrames) running = false;
    }
    audio.setLoop(Loop::Engine, 0.0f);
    audio.setLoop(Loop::Siren, 0.0f);
    audio.setLoop(Loop::Radio, 0.0f);

    summary.frames = clock.frameCount();
    summary.seconds = clock.elapsedSeconds();
    const double frames = static_cast<double>(std::max<std::uint64_t>(summary.frames, 1));
    summary.avgFrameMs = totalMs / frames;
    summary.avgKBPerFrame = totalBytes / frames / 1024.0;
    summary.chunksGenerated = world.stats().generatedTotal;
    summary.avgTriangles = static_cast<std::size_t>(static_cast<double>(totalTris) / frames);
    summary.remoteEntities = remote.entities().size();
    summary.netKBIn = net.bytesIn() / 1024;
    summary.netKBOut = net.bytesOut() / 1024;
    return summary;
}

} // namespace client
