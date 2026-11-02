// Grand Theft ASCII - game client.
#include "Game.h"
#include "Lobby.h"
#include "Menu.h"
#include "Relay.h"

#include "Audio/Audio.h"
#include "Core/FileUtil.h"
#include "Platform/Terminal.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

namespace {

const char* kUsage =
    "Grand Theft ASCII\n"
    "\n"
    "Usage: gtascii [options]          (no options: open the main menu)\n"
    "\n"
    "  --play                 Start single player right away\n"
    "  --host                 Host a LAN room right away\n"
    "  --connect HOST[:PORT]  Join a server / room server (default port 41986)\n"
    "  --room CODE            Room to join on that server\n"
    "  --create               Create an online room right away\n"
    "  --join CODE            Join an online room right away\n"
    "  --relay URL            Online relay to use (default: the Room server setting)\n"
    "  --name NAME            Player name (overrides settings)\n"
    "  --password PW          Protects your name/progress on a server\n"
    "  --hour H               Start time of day, 0-24\n"
    "  --seed N               World seed (single player / host)\n"
    "  --samples N            Sub-samples per cell axis (1 = fastest)\n"
    "  --no-mouse             Do not capture the mouse (keeps terminal text selection)\n"
    "  --no-audio             Silence\n"
    "  --data PATH            Game data folder (assets/ and config/)\n"
    "  --autopilot            Scripted movement demo\n"
    "  --frames N             Exit after N frames and print statistics\n"
    "  --size WxH             Render size when output is not a terminal\n"
    "  --version | --help\n"
    "\n"
    "Your settings file: ";

void usage(FILE* out)
{
    std::fprintf(out, "%s%s\n", kUsage, client::ClientConfig::userPath().string().c_str());
}

} // namespace

int main(int argc, char** argv)
{
    try {
        // Data folder: --data, or found next to the executable / current directory.
        std::filesystem::path dataRoot;
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::string(argv[i]) == "--data") dataRoot = argv[i + 1];
        }
        if (dataRoot.empty()) {
            const auto found = av::findDataRoot(argc > 0 ? argv[0] : nullptr, "assets/materials/materials.json");
            if (!found) {
                std::fprintf(stderr, "error: cannot find the game data (assets/). Use --data PATH.\n");
                return 1;
            }
            dataRoot = *found;
        }

        client::LaunchOptions opts;
        opts.dataRoot = dataRoot;
        opts.config = client::ClientConfig::loadLayered(dataRoot / "config/client.json", client::ClientConfig::userPath());
        bool direct = false, audioOn = true, listOnline = false;
        std::string menuDemo;
        int cols = 120, rows = 40;

        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            const char* next = i + 1 < argc ? argv[i + 1] : nullptr;
            auto need = [&](const char* what) {
                if (!next) {
                    std::fprintf(stderr, "error: %s needs a value\n\n", what);
                    usage(stderr);
                    std::exit(2);
                }
                ++i;
                return next;
            };
            if (a == "--help" || a == "-h") {
                usage(stdout);
                return 0;
            } else if (a == "--version") {
                std::printf("gtascii %s (protocol %d)\n", GTASCII_VERSION, av::proto::kProtocolVersion);
                return 0;
            } else if (a == "--data") {
                need("--data");
            } else if (a == "--play") {
                opts.mode = client::SessionMode::SinglePlayer;
                direct = true;
            } else if (a == "--host") {
                opts.mode = client::SessionMode::Host;
                direct = true;
            } else if (a == "--connect") {
                opts.mode = client::SessionMode::Join;
                opts.serverAddress = need("--connect");
                direct = true;
            } else if (a == "--create") {
                opts.mode = client::SessionMode::OnlineHost;
                direct = true;
            } else if (a == "--join") {
                opts.mode = client::SessionMode::OnlineJoin;
                opts.roomCode = client::normalizeRoomCode(need("--join"));
                direct = true;
            } else if (a == "--relay") {
                opts.config.lobby = need("--relay");
            } else if (a == "--exec") { // hidden: chat lines (cheats) to send, separated by ';'
                std::string list = need("--exec");
                for (std::size_t at = 0; at <= list.size();) {
                    const std::size_t semi = std::min(list.find(';', at), list.size());
                    if (semi > at) opts.execLines.push_back(list.substr(at, semi - at));
                    at = semi + 1;
                }
            } else if (a == "--weapon") { // hidden
                opts.startWeapon = std::atoi(need("--weapon"));
            } else if (a == "--fire") { // hidden
                opts.holdFire = true;
            } else if (a == "--list-rooms") { // hidden: relay check for CI / debugging
                listOnline = true;
            } else if (a == "--room") {
                opts.roomCode = client::normalizeRoomCode(need("--room"));
            } else if (a == "--name") {
                opts.config.playerName = need("--name");
            } else if (a == "--password") {
                opts.config.password = need("--password");
            } else if (a == "--hour") {
                opts.config.startHour = static_cast<float>(std::atof(need("--hour")));
                opts.hourGiven = true;
            } else if (a == "--seed") {
                opts.config.seed = static_cast<std::uint32_t>(std::strtoul(need("--seed"), nullptr, 10));
            } else if (a == "--radius") {
                opts.config.loadRadius = std::atoi(need("--radius"));
            } else if (a == "--samples") {
                opts.config.samplesX = opts.config.samplesY = std::atoi(need("--samples"));
            } else if (a == "--no-mouse") {
                opts.mouse = false;
            } else if (a == "--menu-demo") { // hidden: headless menu capture
                menuDemo = need("--menu-demo");
            } else if (a == "--no-audio") {
                audioOn = false;
            } else if (a == "--autopilot") {
                opts.autopilot = true;
                direct = true;
            } else if (a == "--frames") {
                opts.maxFrames = std::strtoull(need("--frames"), nullptr, 10);
                direct = true;
            } else if (a == "--size") {
                if (std::sscanf(need("--size"), "%dx%d", &cols, &rows) != 2) {
                    std::fprintf(stderr, "error: --size needs WIDTHxHEIGHT\n");
                    return 2;
                }
            } else {
                std::fprintf(stderr, "error: unknown option %s\n\n", a.c_str());
                usage(stderr);
                return 2;
            }
        }
        if (opts.hostRoom.name.empty()) {
            opts.hostRoom.name = opts.config.playerName + "'s room";
        }
        opts.relayUrl = opts.config.lobby;
        const bool online = opts.mode == client::SessionMode::OnlineHost || opts.mode == client::SessionMode::OnlineJoin;
        if ((online || listOnline) && !client::isRelayUrl(opts.relayUrl)) {
            std::fprintf(stderr, "error: online rooms need a relay URL (--relay wss://... or the Room server setting)\n");
            return 2;
        }
        if (listOnline) {
            std::string error;
            const auto rooms = client::listRelayRooms(opts.relayUrl, error);
            if (!rooms) {
                std::fprintf(stderr, "error: %s\n", error.c_str());
                return 1;
            }
            std::printf("%zu online room(s) on %s\n", rooms->size(), opts.relayUrl.c_str());
            for (const auto& r : *rooms) {
                std::printf("  %s  %-24s %s  %d/%d\n", r.code.c_str(), r.name.c_str(), av::proto::toString(r.mode), r.players,
                            r.maxPlayers);
            }
            return 0;
        }

        av::Audio audio;
        if (audioOn && opts.config.volume > 0.0f && opts.maxFrames == 0) {
            audio.start(opts.config.volume);
        }

        client::RunSummary last;
        {
            av::TerminalOptions termOpts;
            termOpts.enableMouse = opts.mouse;
            termOpts.fallbackSize = {cols, rows};
            av::Terminal terminal(termOpts);

            if (direct) {
                last = client::runGame(opts, terminal, audio);
            } else {
                client::MainMenu menu(terminal, dataRoot, opts.config, audio);
                if (!menuDemo.empty()) {
                    menu.demo(menuDemo, 45);
                    menu.run("");
                    return 0;
                }
                std::string notice;
                for (;;) {
                    const client::MenuOutcome choice = menu.run(notice);
                    notice.clear();
                    if (choice.choice == client::MenuChoice::Quit) {
                        break;
                    }
                    client::LaunchOptions session = opts; // includes settings changed in the menu
                    if (choice.choice == client::MenuChoice::SinglePlayer) {
                        session.mode = client::SessionMode::SinglePlayer;
                    } else if (choice.choice == client::MenuChoice::HostOnline) {
                        session.mode = client::SessionMode::OnlineHost;
                        session.hostRoom = choice.hostRoom;
                        session.relayUrl = session.config.lobby;
                    } else if (choice.choice == client::MenuChoice::JoinOnline) {
                        session.mode = client::SessionMode::OnlineJoin;
                        session.roomCode = choice.roomCode;
                        session.relayUrl = session.config.lobby;
                    } else if (choice.choice == client::MenuChoice::HostLan) {
                        session.mode = client::SessionMode::Host;
                        session.hostRoom = choice.hostRoom;
                    } else {
                        session.mode = client::SessionMode::Join;
                        session.serverAddress = choice.address;
                        session.roomCode = choice.roomCode;
                    }
                    try {
                        last = client::runGame(session, terminal, audio);
                        notice = last.disconnectReason;
                    } catch (const std::exception& e) {
                        notice = e.what();
                    }
                }
            }
        } // terminal restored here

        if (direct) {
            std::printf("Grand Theft ASCII session: %llu frames in %.1f s (%.1f fps), %.2f ms/frame work, %.1f KB/frame, "
                        "%zu triangles/frame, %zu chunks generated, %zu entities in view, net %zu KB in / %zu KB out, "
                        "max prediction correction %.2f m\n",
                        static_cast<unsigned long long>(last.frames), last.seconds,
                        last.seconds > 0 ? static_cast<double>(last.frames) / last.seconds : 0.0, last.avgFrameMs,
                        last.avgKBPerFrame, last.avgTriangles, last.chunksGenerated, last.remoteEntities, last.netKBIn,
                        last.netKBOut, last.maxCorrection);
            if (!last.disconnectReason.empty()) {
                std::printf("Disconnected: %s\n", last.disconnectReason.c_str());
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
