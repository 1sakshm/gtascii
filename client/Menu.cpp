#include "Menu.h"

// Menu item tables use brace-initialised aggregates with trailing defaults.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include "Lobby.h"
#include "Relay.h"
#include "RemoteWorld.h"

#include "Animation/Humanoid.h"
#include "Animation/HumanoidAnimator.h"
#include "Audio/Audio.h"
#include "Core/FrameClock.h"
#include "Graphics/AnsiPresenter.h"
#include "Graphics/Draw2D.h"
#include "Graphics/ObjLoader.h"
#include "Graphics/Renderer.h"
#include "Platform/Terminal.h"
#include "World/ChunkManager.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <iterator>
#include <optional>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace client {

using namespace av;
using namespace av::proto;

namespace {

const Color kPink{255, 70, 190};
const Color kCyan{60, 230, 245};
const Color kGold{255, 215, 80};
const Color kText{230, 228, 240};
const Color kDim{150, 140, 180};
const Color kPanel{14, 8, 30};
const Color kSelect{70, 20, 90};

// 5x5 block letters for the title.
const std::map<char, std::array<const char*, 5>>& titleFont()
{
    static const std::map<char, std::array<const char*, 5>> font = {
        {'A', {" ### ", "#   #", "#####", "#   #", "#   #"}}, {'C', {" ####", "#    ", "#    ", "#    ", " ####"}},
        {'D', {"#### ", "#   #", "#   #", "#   #", "#### "}}, {'E', {"#####", "#    ", "#### ", "#    ", "#####"}},
        {'F', {"#####", "#    ", "#### ", "#    ", "#    "}}, {'G', {" ####", "#    ", "#  ##", "#   #", " ####"}},
        {'H', {"#   #", "#   #", "#####", "#   #", "#   #"}}, {'I', {"#####", "  #  ", "  #  ", "  #  ", "#####"}},
        {'N', {"#   #", "##  #", "# # #", "#  ##", "#   #"}}, {'R', {"#### ", "#   #", "#### ", "#  # ", "#   #"}},
        {'S', {" ####", "#    ", " ### ", "    #", "#### "}}, {'T', {"#####", "  #  ", "  #  ", "  #  ", "  #  "}},
        {' ', {"   ", "   ", "   ", "   ", "   "}},
    };
    return font;
}

int titleWidth(const std::string& s)
{
    int w = 0;
    for (const char c : s) {
        const auto it = titleFont().find(c);
        w += (it != titleFont().end() ? static_cast<int>(std::string(it->second[0]).size()) : 3) + 1;
    }
    return w - 1;
}

void drawTitle(Framebuffer& fb, const std::string& s, int y, float time, Color a, Color b)
{
    int x = (fb.width() - titleWidth(s)) / 2;
    for (const char c : s) {
        const auto it = titleFont().find(c);
        if (it == titleFont().end()) continue;
        const int w = static_cast<int>(std::string(it->second[0]).size());
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < w; ++col) {
                if (it->second[static_cast<std::size_t>(row)][col] != '#') continue;
                // Neon gradient that slowly sweeps across the letters.
                const float t = 0.5f + 0.5f * std::sin(static_cast<float>(x + col) * 0.08f - time * 1.5f + static_cast<float>(row) * 0.2f);
                const Color c1 = lerp(a, b, t);
                fb.set(x + col, y + row, {U'█', c1, scale(c1, 0.35f)});
            }
        }
        x += w + 1;
    }
}

int textWidth(const std::string& s)
{
    int n = 0;
    for (const unsigned char c : s) n += (c & 0xC0) != 0x80 ? 1 : 0;
    return n;
}

struct Item {
    std::string label;
    std::function<std::string()> value; // shown right-aligned
    std::function<void(int)> adjust;    // left / right
    std::function<void()> activate;     // enter / click
    std::string* text = nullptr;        // editable text field
    std::size_t maxLength = 24;
    std::string hint;
};

enum class Screen { Main, Multiplayer, CreateRoom, JoinCode, Browse, Direct, Settings, Character, Controls };

} // namespace

struct MainMenu::Impl {
    Terminal& terminal;
    std::filesystem::path root;
    ClientConfig& cfg;
    Audio& audio;

    MaterialLibrary materials;
    std::unique_ptr<CityLayout> layout;
    std::unique_ptr<CityGenerator> generator;
    std::unique_ptr<ChunkManager> chunks;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<CharacterLooks> looks;
    HumanoidAnimator previewAnim;
    std::vector<Mat4> previewJoints;
    AnsiPresenter presenter;
    Framebuffer fb;

    Screen screen = Screen::Main;
    std::map<Screen, int> selection;
    bool editing = false;
    std::string notice;
    float noticeTimer = 0.0f;
    bool done = false;
    MenuOutcome outcome;
    bool dirty = false; // settings to save

    // Multiplayer state.
    bool createOnline = true;
    CreateRoomRequest roomRequest;
    std::string joinCode, directAddress;
    std::vector<RoomInfo> browse;
    bool browseStale = true;
    Appearance look;

    Impl(Terminal& t, const std::filesystem::path& r, ClientConfig& c, Audio& a)
        : terminal(t), root(r), cfg(c), audio(a)
    {
        materials.loadFile(root / "assets/materials/materials.json");
        layout = std::make_unique<CityLayout>(CityLayout::fromFile(cfg.seed, root / "assets/maps/landmarks.json"));
        generator = std::make_unique<CityGenerator>(*layout, materials,
                                                    loadObjFile(root / "assets/models/newsbox.obj", materials, materials.require("metal")));
        chunks = std::make_unique<ChunkManager>(*generator, 2);
        renderer = std::make_unique<Renderer>(materials, *findRamp("standard"));
        RendererSettings rs;
        rs.samplesX = rs.samplesY = std::clamp(cfg.samplesX, 1, 3);
        rs.cellAspect = cfg.cellAspect;
        renderer->setSettings(rs);
        looks = std::make_unique<CharacterLooks>(materials);
        look = Appearance::fromCode(cfg.appearanceSeed);
        roomRequest.name = cfg.playerName + "'s room";
        directAddress = cfg.server;
    }

    void say(const std::string& text)
    {
        notice = text;
        noticeTimer = 5.0f;
    }

    void go(Screen s)
    {
        screen = s;
        editing = false;
        audio.play(Sound::MenuSelect, 0.6f);
        if (s == Screen::Browse) browseStale = true;
    }

    void back()
    {
        if (dirty) {
            cfg.save(ClientConfig::userPath());
            dirty = false;
        }
        switch (screen) {
        case Screen::Main: break;
        case Screen::CreateRoom:
        case Screen::JoinCode:
        case Screen::Browse:
        case Screen::Direct: go(Screen::Multiplayer); break;
        default: go(Screen::Main); break;
        }
    }

    std::optional<NetAddress> lobbyAddress()
    {
        if (cfg.lobby.empty()) {
            say("No room server set: Settings > Room server (or use LAN rooms)");
            return std::nullopt;
        }
        const auto addr = NetAddress::parse(cfg.lobby, kDefaultPort);
        if (!addr) say("Cannot resolve room server '" + cfg.lobby + "'");
        return addr;
    }

    void finish(MenuChoice choice)
    {
        if (dirty) {
            cfg.save(ClientConfig::userPath());
            dirty = false;
        }
        outcome.choice = choice;
        done = true;
        audio.play(Sound::MenuSelect);
    }

    // ---- screens ----------------------------------------------------------
    std::vector<Item> items()
    {
        std::vector<Item> v;
        switch (screen) {
        case Screen::Main:
            v.push_back({"Single Player", nullptr, nullptr, [this] { finish(MenuChoice::SinglePlayer); }, nullptr, 0,
                         "Your own city. Progress is saved."});
            v.push_back({"Multiplayer", nullptr, nullptr, [this] { go(Screen::Multiplayer); }, nullptr, 0,
                         "Rooms with codes, LAN games, direct connect"});
            v.push_back({"Character", nullptr, nullptr, [this] { go(Screen::Character); }, nullptr, 0, "Change your name and look"});
            v.push_back({"Settings", nullptr, nullptr, [this] { go(Screen::Settings); }, nullptr, 0, "Graphics, controls, audio, network"});
            v.push_back({"Controls", nullptr, nullptr, [this] { go(Screen::Controls); }, nullptr, 0, "How to play"});
            v.push_back({"Quit", nullptr, nullptr, [this] { finish(MenuChoice::Quit); }, nullptr, 0, ""});
            break;
        case Screen::Multiplayer:
            v.push_back({"Browse rooms", nullptr, nullptr, [this] { go(Screen::Browse); }, nullptr, 0,
                         "Public rooms on the room server and on your network"});
            v.push_back({"Join with a room code", nullptr, nullptr, [this] { go(Screen::JoinCode); }, nullptr, 0,
                         "A friend gives you a 4-letter code"});
            v.push_back({"Create online room", nullptr, nullptr, [this] { createOnline = true; go(Screen::CreateRoom); }, nullptr, 0,
                         "Hosted by your game - anyone online with the code can join"});
            v.push_back({"Host a LAN room", nullptr, nullptr, [this] { createOnline = false; go(Screen::CreateRoom); }, nullptr, 0,
                         "Runs on this PC; players on your network see it in Browse"});
            v.push_back({"Direct connect", nullptr, nullptr, [this] { go(Screen::Direct); }, nullptr, 0, "Join a server by address"});
            v.push_back({"Room server", [this] { return cfg.lobby.empty() ? std::string("(not set)") : cfg.lobby; }, nullptr,
                         [this] { editing = true; }, &cfg.lobby, 60, "Online relay (wss://...) or a room server address"});
            v.push_back({"Back", nullptr, nullptr, [this] { back(); }, nullptr, 0, ""});
            break;
        case Screen::CreateRoom: {
            v.push_back({"Room name", [this] { return roomRequest.name; }, nullptr, [this] { editing = true; }, &roomRequest.name, 24, ""});
            v.push_back({"Game mode", [this] { return std::string("< ") + toString(roomRequest.mode) + " >"; },
                         [this](int d) { roomRequest.mode = static_cast<GameMode>((static_cast<int>(roomRequest.mode) + d + 3) % 3); },
                         nullptr, nullptr, 0, "Free roam; or races / cops & robbers rounds that start on their own"});
            v.push_back({"Max players", [this] { return "< " + std::to_string(roomRequest.maxPlayers) + " >"; },
                         [this](int d) { roomRequest.maxPlayers = static_cast<std::uint8_t>(std::clamp(roomRequest.maxPlayers + d, 2, 16)); },
                         nullptr, nullptr, 0, ""});
            if (createOnline) {
                v.push_back({"Visibility", [this] { return roomRequest.isPublic ? std::string("< Public >") : std::string("< Private (code only) >"); },
                             [this](int) { roomRequest.isPublic = !roomRequest.isPublic; }, nullptr, nullptr, 0, ""});
            }
            v.push_back({createOnline ? "Create & play" : "Host & play", nullptr, nullptr,
                         [this] {
                             if (!createOnline) {
                                 outcome.hostRoom = roomRequest;
                                 finish(MenuChoice::HostLan);
                                 return;
                             }
                             if (isRelayUrl(cfg.lobby)) {
                                 outcome.hostRoom = roomRequest;
                                 finish(MenuChoice::HostOnline);
                                 return;
                             }
                             const auto addr = lobbyAddress();
                             if (!addr) return;
                             std::string error;
                             const auto code = createRoom(*addr, roomRequest, error);
                             if (!code) {
                                 say(error);
                                 return;
                             }
                             outcome.address = cfg.lobby;
                             outcome.roomCode = *code;
                             finish(MenuChoice::Join);
                         },
                         nullptr, 0, ""});
            v.push_back({"Back", nullptr, nullptr, [this] { back(); }, nullptr, 0, ""});
            break;
        }
        case Screen::JoinCode:
            v.push_back({"Room code", [this] { return joinCode.empty() ? std::string("(type the code)") : joinCode; }, nullptr,
                         [this] { editing = true; }, &joinCode, 8, "Press Enter, type the code, press Enter"});
            v.push_back({"Join", nullptr, nullptr,
                         [this] {
                             const std::string code = normalizeRoomCode(joinCode);
                             if (code.size() != 4) {
                                 say("Room codes are 4 letters/digits, like K7PX");
                                 return;
                             }
                             if (isRelayUrl(cfg.lobby)) {
                                 outcome.roomCode = code;
                                 finish(MenuChoice::JoinOnline);
                                 return;
                             }
                             if (!lobbyAddress()) return;
                             outcome.address = cfg.lobby;
                             outcome.roomCode = code;
                             finish(MenuChoice::Join);
                         },
                         nullptr, 0, ""});
            v.push_back({"Back", nullptr, nullptr, [this] { back(); }, nullptr, 0, ""});
            break;
        case Screen::Browse:
            v.push_back({"Refresh", nullptr, nullptr, [this] { browseStale = true; }, nullptr, 0, "Searches the room server and your network"});
            for (const RoomInfo& r : browse) {
                const bool lan = !r.relay && !r.address.empty() && (cfg.lobby.empty() || r.address != cfg.lobby);
                const std::string label = r.name.substr(0, 22);
                const std::string detail = std::string(toString(r.mode)) + "  " + std::to_string(r.players) + "/" + std::to_string(r.maxPlayers) + (lan ? "  LAN" : "");
                v.push_back({label, [detail] { return detail; }, nullptr,
                             [this, r] {
                                 outcome.roomCode = r.code;
                                 if (r.relay) {
                                     finish(MenuChoice::JoinOnline);
                                     return;
                                 }
                                 outcome.address = r.address + ":" + std::to_string(r.port);
                                 finish(MenuChoice::Join);
                             },
                             nullptr, 0, "Room " + r.code});
            }
            if (browse.empty()) {
                v.push_back({"(no rooms found)", nullptr, nullptr, nullptr, nullptr, 0, "Create one, or check the room server address"});
            }
            v.push_back({"Back", nullptr, nullptr, [this] { back(); }, nullptr, 0, ""});
            break;
        case Screen::Direct:
            v.push_back({"Address", [this] { return directAddress.empty() ? std::string("(e.g. 192.168.1.5)") : directAddress; }, nullptr,
                         [this] { editing = true; }, &directAddress, 60, ""});
            v.push_back({"Connect", nullptr, nullptr,
                         [this] {
                             if (directAddress.empty()) {
                                 say("Type an address first");
                                 return;
                             }
                             outcome.address = directAddress;
                             outcome.roomCode.clear();
                             finish(MenuChoice::Join);
                         },
                         nullptr, 0, ""});
            v.push_back({"Back", nullptr, nullptr, [this] { back(); }, nullptr, 0, ""});
            break;
        case Screen::Settings: settingsItems(v); break;
        case Screen::Character: characterItems(v); break;
        case Screen::Controls:
            v.push_back({"Back", nullptr, nullptr, [this] { back(); }, nullptr, 0, ""});
            break;
        }
        return v;
    }

    void settingsItems(std::vector<Item>& v)
    {
        auto changed = [this] { dirty = true; };
        v.push_back({"Graphics quality", [this] { return std::string("< ") + (cfg.samplesX <= 1 ? "Fast" : cfg.samplesX == 2 ? "Balanced" : "Sharp") + " >"; },
                     [this, changed](int d) { cfg.samplesX = cfg.samplesY = std::clamp(cfg.samplesX + d, 1, 3); changed(); }, nullptr, nullptr, 0,
                     "Sub-samples per character cell (Fast = 1x1, Sharp = 3x3)"});
        static const char* ramps[] = {"standard", "detailed", "blocks", "minimal"};
        v.push_back({"Character style", [this] { return "< " + cfg.ramp + " >"; },
                     [this, changed](int d) {
                         int i = 0;
                         for (int k = 0; k < 4; ++k) if (cfg.ramp == ramps[k]) i = k;
                         cfg.ramp = ramps[(i + d + 4) % 4];
                         changed();
                     },
                     nullptr, nullptr, 0, "Glyph set used for shading"});
        v.push_back({"Frame rate cap", [this] { return cfg.targetFps <= 0 ? std::string("< Unlimited >") : "< " + std::to_string(static_cast<int>(cfg.targetFps)) + " >"; },
                     [this, changed](int d) {
                         static const double caps[] = {30, 60, 120, 0};
                         int i = 1;
                         for (int k = 0; k < 4; ++k) if (cfg.targetFps == caps[k]) i = k;
                         cfg.targetFps = caps[(i + d + 4) % 4];
                         changed();
                     },
                     nullptr, nullptr, 0, ""});
        v.push_back({"Colour depth", [this] { return "< " + std::to_string(cfg.colorBits) + "-bit >"; },
                     [this, changed](int d) { cfg.colorBits = std::clamp(cfg.colorBits + d, 4, 8); changed(); }, nullptr, nullptr, 0,
                     "Lower = less data for slow terminals or SSH"});
        v.push_back({"Outlines", [this] { return cfg.outlines ? std::string("< On >") : std::string("< Off >"); },
                     [this, changed](int) { cfg.outlines = !cfg.outlines; changed(); }, nullptr, nullptr, 0, ""});
        v.push_back({"View distance", [this] { return "< " + std::to_string(cfg.loadRadius * 64 + 32) + " m >"; },
                     [this, changed](int d) { cfg.loadRadius = std::clamp(cfg.loadRadius + d, 1, 3); changed(); }, nullptr, nullptr, 0, ""});
        v.push_back({"Field of view", [this] { return "< " + std::to_string(cfg.fovDegrees) + " >"; },
                     [this, changed](int d) { cfg.fovDegrees = std::clamp(cfg.fovDegrees + d * 5, 45, 95); changed(); }, nullptr, nullptr, 0, ""});
        v.push_back({"Camera distance", [this] { char b[16]; std::snprintf(b, sizeof b, "< %.1f m >", cfg.cameraDistance); return std::string(b); },
                     [this, changed](int d) { cfg.cameraDistance = std::clamp(cfg.cameraDistance + 0.5f * static_cast<float>(d), 2.5f, 10.0f); changed(); },
                     nullptr, nullptr, 0, ""});
        v.push_back({"Mouse sensitivity", [this] { return "< " + std::to_string(static_cast<int>(std::lround(cfg.mouseSensitivity * 1000.0f))) + " >"; },
                     [this, changed](int d) { cfg.mouseSensitivity = std::clamp(cfg.mouseSensitivity + 0.005f * static_cast<float>(d), 0.005f, 0.1f); changed(); },
                     nullptr, nullptr, 0, ""});
        v.push_back({"Mouse look", [this] { return cfg.mouseLook ? std::string("< On: move the mouse to look >") : std::string("< Off: drag to look >"); },
                     [this, changed](int) { cfg.mouseLook = !cfg.mouseLook; changed(); }, nullptr, nullptr, 0,
                     "Move the mouse or trackpad to turn; rest at the screen edge to keep turning. Click to shoot."});
        v.push_back({"Invert camera Y", [this] { return cfg.invertY ? std::string("< Yes >") : std::string("< No >"); },
                     [this, changed](int) { cfg.invertY = !cfg.invertY; changed(); }, nullptr, nullptr, 0, ""});
        v.push_back({"Volume", [this] { return "< " + std::to_string(static_cast<int>(std::lround(cfg.volume * 100.0f))) + "% >"; },
                     [this, changed](int d) {
                         cfg.volume = std::clamp(cfg.volume + 0.1f * static_cast<float>(d), 0.0f, 1.0f);
                         audio.setVolume(cfg.volume);
                         audio.play(Sound::MenuMove);
                         changed();
                     },
                     nullptr, nullptr, 0, audio.available() ? "" : "No audio device on this system"});
        v.push_back({"Day length", [this] { return cfg.dayLengthMinutes <= 0 ? std::string("< Frozen >") : "< " + std::to_string(static_cast<int>(cfg.dayLengthMinutes)) + " min >"; },
                     [this, changed](int d) {
                         static const float lens[] = {0, 10, 20, 40, 60};
                         int i = 2;
                         for (int k = 0; k < 5; ++k) if (cfg.dayLengthMinutes == lens[k]) i = k;
                         cfg.dayLengthMinutes = lens[(i + d + 5) % 5];
                         changed();
                     },
                     nullptr, nullptr, 0, "Single player / hosted rooms"});
        v.push_back({"Back", nullptr, nullptr, [this] { back(); }, nullptr, 0, "Settings are saved automatically"});
    }

    void characterItems(std::vector<Item>& v)
    {
        auto apply = [this] {
            look.resolveColors();
            cfg.appearanceSeed = look.code();
            dirty = true;
        };
        auto cycle = [apply](int& field, int count) {
            return [&field, count, apply](int d) {
                field = (field + d + count) % count;
                apply();
            };
        };
        auto swatch = [](int i, int n) { return "< " + std::to_string(i + 1) + "/" + std::to_string(n) + " >"; };
        v.push_back({"Name", [this] { return cfg.playerName; }, nullptr, [this] { editing = true; }, &cfg.playerName, 16,
                     "Letters, digits, _ and - (shown to other players)"});
        v.push_back({"Skin tone", [this, swatch] { return swatch(look.skinIndex, Appearance::kSkins); }, cycle(look.skinIndex, Appearance::kSkins), nullptr});
        static const char* hairNames[] = {"Bald", "Short", "Long", "Afro", "Ponytail"};
        v.push_back({"Hair", [this] { return std::string("< ") + hairNames[static_cast<int>(look.hair_style)] + " >"; },
                     [this, apply](int d) { look.hair_style = static_cast<HairStyle>((static_cast<int>(look.hair_style) + d + 5) % 5); apply(); }, nullptr});
        v.push_back({"Hair colour", [this, swatch] { return swatch(look.hairIndex, Appearance::kHairColors); }, cycle(look.hairIndex, Appearance::kHairColors), nullptr});
        v.push_back({"Shirt", [this, swatch] { return swatch(look.shirtIndex, Appearance::kShirts); }, cycle(look.shirtIndex, Appearance::kShirts), nullptr});
        v.push_back({"Sleeves", [this] { return look.longSleeves ? std::string("< Long >") : std::string("< Short >"); },
                     [this, apply](int) { look.longSleeves = !look.longSleeves; apply(); }, nullptr});
        v.push_back({"Legs", [this] { return look.shorts ? std::string("< Shorts >") : std::string("< Trousers >"); },
                     [this, apply](int) { look.shorts = !look.shorts; apply(); }, nullptr});
        v.push_back({"Trousers colour", [this, swatch] { return swatch(look.trousersIndex, Appearance::kTrousers); }, cycle(look.trousersIndex, Appearance::kTrousers), nullptr});
        v.push_back({"Shoes", [this, swatch] { return swatch(look.shoesIndex, Appearance::kShoes); }, cycle(look.shoesIndex, Appearance::kShoes), nullptr});
        static const char* hatNames[] = {"None", "Cap", "Fedora"};
        v.push_back({"Hat", [this] { return std::string("< ") + hatNames[static_cast<int>(look.hat)] + " >"; },
                     [this, apply](int d) { look.hat = static_cast<HatStyle>((static_cast<int>(look.hat) + d + 3) % 3); apply(); }, nullptr});
        v.push_back({"Hat colour", [this, swatch] { return swatch(look.hatIndex, Appearance::kHatColors); }, cycle(look.hatIndex, Appearance::kHatColors), nullptr});
        v.push_back({"Backpack", [this] { return look.backpack ? "< Colour " + std::to_string(look.packIndex + 1) + " >" : std::string("< None >"); },
                     [this, apply](int d) {
                         // None -> colour 1..4 -> None
                         int state = look.backpack ? look.packIndex + 1 : 0;
                         state = (state + d + 5) % 5;
                         look.backpack = state > 0;
                         if (state > 0) look.packIndex = state - 1;
                         apply();
                     },
                     nullptr});
        v.push_back({"Sunglasses", [this] { return look.sunglasses ? std::string("< On >") : std::string("< Off >"); },
                     [this, apply](int) { look.sunglasses = !look.sunglasses; apply(); }, nullptr});
        v.push_back({"Randomize", nullptr, nullptr,
                     [this, apply] {
                         Rng rng(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
                         look = Appearance::random(rng);
                         apply();
                     },
                     nullptr});
        v.push_back({"Done", nullptr, nullptr, [this] { back(); }, nullptr, 0, "Saved automatically"});
    }

    // ---- frame ------------------------------------------------------------
    void renderBackground(float time)
    {
        const TerminalSize ts = terminal.size();
        Camera cam;
        SceneLighting light = SceneLighting::timeOfDay(18.7f);
        light.time = time;
        light.fogStart = 80.0f;
        light.fogEnd = 260.0f;
        Vec3 focus;
        if (screen == Screen::Character) {
            // Close-up of your character outside the safehouse.
            Vec3 spot = layout->spawn().position + Vec3{0, 0, 4.0f};
            spot.y = layout->groundHeight(spot.x, spot.z);
            focus = spot;
            cam.position = spot + Vec3{-1.2f, 1.3f, -4.6f};
            cam.target = spot + Vec3{-1.2f, 0.95f, 0.0f};
            cam.fovY = radians(50.0f);
        } else {
            // Slow orbit around downtown and the Vice Tower.
            const Vec3 centre{32.0f, 0.0f, -110.0f};
            const float a = time * 0.05f;
            focus = centre;
            cam.position = centre + Vec3{std::sin(a) * 150.0f, 55.0f, std::cos(a) * 150.0f};
            cam.target = centre + Vec3{0, 45.0f, 0};
            cam.fovY = radians(60.0f);
        }
        cam.zFar = 500.0f;
        chunks->update(focus, 2);
        for (const auto& [coord, chunk] : chunks->chunks()) {
            for (const PointLight& l : chunk->lights) light.pointLights.push_back(l);
        }
        renderer->beginFrame(ts.cols, ts.rows, cam, light);
        for (const auto& [coord, chunk] : chunks->chunks()) {
            if (!renderer->isVisible(chunk->worldBounds)) continue;
            const Mat4 model = Mat4::translation(chunk->origin());
            renderer->drawMesh(chunk->baseMesh, model);
            renderer->drawMesh(chunk->detailMesh, model);
        }
        if (screen == Screen::Character) {
            AnimationInput in;
            in.wave = std::fmod(time, 8.0f) < 0.05f;
            previewAnim.update(in, 1.0f / 30.0f);
            humanoidSkeleton().computeGlobal(previewAnim.pose(), previewJoints);
            const Mat4 world = Mat4::translation(focus) * Mat4::rotationY(kPi + std::sin(time * 0.6f) * 0.9f);
            looks->get(cfg.appearanceSeed, false).draw(*renderer, world, previewJoints);
        }
        renderer->resolve(fb);
    }

    void drawPanel(const std::vector<Item>& list, int sel, float time)
    {
        const bool wide = fb.width() >= 100;
        int y = 1;
        if (fb.height() >= 26) {
            drawTitle(fb, "GRAND THEFT", y, time, kPink, kCyan);
            drawTitle(fb, "ASCII", y + 6, time + 1.0f, kCyan, kPink);
            y += 12;
        } else {
            draw::text(fb, (fb.width() - 17) / 2, y, "GRAND THEFT ASCII", kPink, kPanel);
            y += 2;
        }
        static const std::map<Screen, std::string> headings = {
            {Screen::Main, ""}, {Screen::Multiplayer, "MULTIPLAYER"}, {Screen::CreateRoom, "NEW ROOM"},
            {Screen::JoinCode, "JOIN A ROOM"}, {Screen::Browse, "ROOMS"}, {Screen::Direct, "DIRECT CONNECT"},
            {Screen::Settings, "SETTINGS"}, {Screen::Character, "CHARACTER"}, {Screen::Controls, "CONTROLS"}};

        const int w = wide ? 58 : std::min(fb.width() - 2, 50);
        const int x = screen == Screen::Character ? std::max(1, fb.width() / 2 + 2) : (fb.width() - w) / 2;
        const int panelW = screen == Screen::Character ? std::min(w, fb.width() - x - 1) : w;
        const std::string& heading = headings.at(screen);

        int maxRows = fb.height() - y - 4;
        if (screen == Screen::Controls) {
            static const char* lines[] = {
                "ON FOOT",  "  W A S D move   Shift walk   Space jump   C crouch",
                "  F get in / out of cars (or ride with a friend)   E use",
                "  X attack / shoot (aims where the camera looks)   G wave",
                "  Q next weapon   1-9, 0 pick a weapon   ` fists   J jetpack",
                "  P phone (jobs, races, cops & robbers, cheat list)",
                "DRIVING",  "  W/S throttle / brake   A/D steer   Space handbrake",
                "  Helicopter: Space up, Z down, W/S forward / back",
                "CAMERA",   "  arrows or drag the mouse   +/- or wheel zoom   V recenter",
                "OTHER",    "  Enter chat   M map   I items   Tab scoreboard   T skip an hour",
                "  F9 record the session (.cast file)   H hide HUD   Esc quit to menu",
                "CHEATS",   "  type them in chat: FULLCLIP  FLYBOY  ROCKETMAN  IAMGOD ..."};
            const int rows = static_cast<int>(std::size(lines)) + 3;
            const int cw = std::min(fb.width() - 2, 66);
            const int cx = (fb.width() - cw) / 2;
            Cell bg;
            bg.bg = kPanel;
            draw::fillRect(fb, cx, y, cw, rows, bg);
            draw::text(fb, cx + 2, y, heading, kGold, kPanel);
            int ry = y + 1;
            for (const char* l : lines) {
                draw::text(fb, cx + 2, ry++, l, l[0] == ' ' ? kText : kCyan, kPanel);
            }
        }

        const int rows = std::min(static_cast<int>(list.size()), std::max(1, maxRows - (heading.empty() ? 1 : 2)));
        const int top = std::clamp(sel - rows / 2, 0, std::max(0, static_cast<int>(list.size()) - rows));
        const int boxY = screen == Screen::Controls ? fb.height() - 4 : y;
        const int boxH = rows + (heading.empty() ? 1 : 2) + 1;
        Cell bg;
        bg.bg = kPanel;
        draw::fillRect(fb, x, boxY, panelW, screen == Screen::Controls ? 2 : boxH, bg);
        int ry = boxY;
        if (!heading.empty() && screen != Screen::Controls) {
            draw::text(fb, x + 2, ry++, heading, kGold, kPanel);
        }
        itemRows.clear();
        for (int i = top; i < top + rows && i < static_cast<int>(list.size()); ++i) {
            const Item& it = list[static_cast<std::size_t>(i)];
            const bool selected = i == sel;
            const Color rowBg = selected ? kSelect : kPanel;
            Cell row;
            row.bg = rowBg;
            draw::fillRect(fb, x + 1, ry, panelW - 2, 1, row);
            draw::text(fb, x + 2, ry, (selected ? "> " : "  ") + it.label, selected ? kPink : kText, rowBg);
            std::string value = it.value ? it.value() : "";
            if (selected && editing && it.text) value = *it.text + (std::fmod(time, 1.0f) < 0.5f ? "_" : " ");
            if (!value.empty()) {
                const int vx = x + panelW - 2 - textWidth(value);
                draw::text(fb, std::max(x + 4 + textWidth(it.label), vx), ry, value, selected ? kCyan : kDim, rowBg);
            }
            itemRows.push_back({ry, i});
            ++ry;
        }
        // Hint and notices.
        const Item* cur = sel < static_cast<int>(list.size()) ? &list[static_cast<std::size_t>(sel)] : nullptr;
        std::string hint = cur ? cur->hint : "";
        if (editing) hint = "Typing - Enter to finish";
        Cell foot;
        foot.bg = kPanel;
        draw::fillRect(fb, 0, fb.height() - 1, fb.width(), 1, foot);
        draw::text(fb, 1, fb.height() - 1,
                   editing ? "Enter done   Backspace delete" : "Up/Down choose   Left/Right change   Enter select   Esc back   mouse works too",
                   kDim, kPanel);
        if (!hint.empty() && fb.height() > 3) {
            draw::text(fb, (fb.width() - textWidth(hint)) / 2, fb.height() - 2, hint, kDim, Color{8, 4, 16});
        }
        if (noticeTimer > 0.0f) {
            const int nw = textWidth(notice) + 4;
            const int nx = (fb.width() - nw) / 2, ny = std::max(0, fb.height() - 4);
            Cell n;
            n.bg = Color{60, 10, 40};
            draw::fillRect(fb, nx, ny, nw, 1, n);
            draw::text(fb, nx + 2, ny, notice, kGold, Color{60, 10, 40});
        }
        draw::text(fb, fb.width() - 22, fb.height() - 1, "v1.0  gtascii", kDim, kPanel);
    }

    std::vector<std::pair<int, int>> itemRows; // screen row -> item index (mouse)
    int demoFrames = 0;

    MenuOutcome run(const std::string& firstNotice)
    {
        done = false;
        outcome = {};
        if (!firstNotice.empty()) say(firstNotice);
        FrameClock clock(30.0);
        std::vector<InputEvent> events;
        bool mouseWasDown = false;
        presenter.invalidate();
        while (!done) {
            if (demoFrames > 0 && clock.frameCount() >= static_cast<std::uint64_t>(demoFrames)) {
                outcome.choice = MenuChoice::Quit;
                break;
            }
            const float dt = static_cast<float>(clock.beginFrame());
            const float time = static_cast<float>(clock.elapsedSeconds());
            noticeTimer = std::max(0.0f, noticeTimer - dt);

            if (screen == Screen::Browse && browseStale) {
                browseStale = false;
                say("Searching for rooms...");
                fb.clear();
                browse = discoverLanRooms(600);
                if (isRelayUrl(cfg.lobby)) {
                    std::string error;
                    if (auto online = listRelayRooms(cfg.lobby, error)) {
                        for (const RoomInfo& r : *online) browse.push_back(r);
                    } else {
                        say(error);
                    }
                } else if (!cfg.lobby.empty()) {
                    if (const auto addr = NetAddress::parse(cfg.lobby, kDefaultPort)) {
                        std::string error;
                        if (auto online = listRooms(*addr, error, 2500)) {
                            for (RoomInfo& r : *online) {
                                r.address = cfg.lobby.substr(0, cfg.lobby.rfind(':') == std::string::npos ? cfg.lobby.size() : cfg.lobby.rfind(':'));
                                browse.push_back(r);
                            }
                        } else {
                            say(error);
                        }
                    }
                }
                if (noticeTimer > 0.0f && notice == "Searching for rooms...") {
                    say(std::to_string(browse.size()) + " room" + (browse.size() == 1 ? "" : "s") + " found");
                }
            }

            std::vector<Item> list = items();
            int& sel = selection[screen];
            sel = std::clamp(sel, 0, std::max(0, static_cast<int>(list.size()) - 1));

            events.clear();
            terminal.pollInput(events);
            for (const InputEvent& ev : events) {
                if (ev.type == InputEvent::Type::Interrupt) {
                    outcome.choice = MenuChoice::Quit;
                    done = true;
                }
                if (ev.type == InputEvent::Type::Resize) presenter.invalidate();
                if (ev.type == InputEvent::Type::Mouse) {
                    // Hover selects, click activates.
                    for (const auto& [row, index] : itemRows) {
                        if (ev.mouse.y == row && index != sel) {
                            sel = index;
                            audio.play(Sound::MenuMove, 0.4f);
                        }
                    }
                    const bool click = ev.mouse.left && !mouseWasDown;
                    mouseWasDown = ev.mouse.left;
                    if (click) {
                        for (const auto& [row, index] : itemRows) {
                            if (ev.mouse.y == row && list[static_cast<std::size_t>(index)].activate) {
                                list[static_cast<std::size_t>(index)].activate();
                                break;
                            }
                        }
                    }
                    if (ev.mouse.wheel != 0 && !list.empty()) {
                        sel = std::clamp(sel - ev.mouse.wheel, 0, static_cast<int>(list.size()) - 1);
                    }
                    continue;
                }
                if (ev.type != InputEvent::Type::Key || !ev.key.down || list.empty()) continue;
                const KeyEvent& k = ev.key;
                Item& it = list[static_cast<std::size_t>(sel)];
                if (editing && it.text) {
                    if (k.key == Key::Enter || k.key == Key::Escape) {
                        editing = false;
                        dirty = true;
                    } else if (k.key == Key::Backspace) {
                        if (!it.text->empty()) it.text->pop_back();
                    } else if (k.key == Key::Character && k.ch >= 0x20 && k.ch < 0x7F && it.text->size() < it.maxLength) {
                        char c = static_cast<char>(k.ch);
                        if (k.shift && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                        it.text->push_back(c);
                    }
                    continue;
                }
                const char32_t ch = k.key == Key::Character ? k.ch : 0;
                if (k.key == Key::Up || ch == U'w') { sel = (sel + static_cast<int>(list.size()) - 1) % static_cast<int>(list.size()); audio.play(Sound::MenuMove, 0.4f); }
                else if (k.key == Key::Down || ch == U's' || k.key == Key::Tab) { sel = (sel + 1) % static_cast<int>(list.size()); audio.play(Sound::MenuMove, 0.4f); }
                else if (k.key == Key::Left || ch == U'a') { if (it.adjust) { it.adjust(-1); audio.play(Sound::MenuMove, 0.5f); } }
                else if (k.key == Key::Right || ch == U'd') { if (it.adjust) { it.adjust(1); audio.play(Sound::MenuMove, 0.5f); } }
                else if (k.key == Key::Enter || ch == U' ') {
                    if (it.activate) it.activate();
                    else if (it.adjust) it.adjust(1);
                } else if (k.key == Key::Escape || k.key == Key::Backspace || ch == U'q') {
                    if (screen == Screen::Main) {
                        if (ch != U'q' && k.key != Key::Escape) continue;
                        finish(MenuChoice::Quit);
                    } else {
                        back();
                    }
                }
                if (done) break;
            }
            if (done) break;

            renderBackground(time);
            list = items(); // values may have changed
            drawPanel(list, std::clamp(sel, 0, std::max(0, static_cast<int>(list.size()) - 1)), time);
            const std::string& out = presenter.encode(fb);
            if (!out.empty()) terminal.write(out);
            clock.endFrame();
        }
        cfg.server = directAddress;
        return outcome;
    }
};

MainMenu::MainMenu(Terminal& terminal, const std::filesystem::path& dataRoot, ClientConfig& config, Audio& audio)
    : impl_(new Impl(terminal, dataRoot, config, audio))
{
}

MainMenu::~MainMenu() { delete impl_; }

MenuOutcome MainMenu::run(const std::string& notice) { return impl_->run(notice); }

void MainMenu::demo(const std::string& screen, int frames)
{
    static const std::map<std::string, Screen> names = {{"main", Screen::Main}, {"multiplayer", Screen::Multiplayer},
                                                        {"character", Screen::Character}, {"settings", Screen::Settings},
                                                        {"controls", Screen::Controls}, {"create", Screen::CreateRoom}};
    const auto it = names.find(screen);
    impl_->screen = it != names.end() ? it->second : Screen::Main;
    impl_->demoFrames = frames;
}

} // namespace client
