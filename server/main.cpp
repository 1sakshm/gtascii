// GRAND THEFT ASCII dedicated server.
#include "GameServer.h"

#include "Core/FileUtil.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <optional>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};

extern "C" void onSignal(int) { g_stop = true; }

const char* kUsage =
    "Grand Theft ASCII dedicated server\n"
    "\n"
    "Usage: gtascii-server [options]\n"
    "  --rooms         Online room server: players create rooms with codes\n"
    "                  (uses config/server-online.json unless --config is given)\n"
    "  --config PATH   Server config (default: config/server.json)\n"
    "  --port N        TCP/UDP port (default 41986)\n"
    "  --local         Accept connections from this machine only\n"
    "  --data PATH     Game data folder (assets/ and config/)\n"
    "  --seconds N     Stop after N seconds (testing)\n"
    "  --help\n";

void logLine(const std::string& line)
{
    const std::time_t t = std::time(nullptr);
    char stamp[16];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S", std::localtime(&t));
    std::printf("[%s] %s\n", stamp, line.c_str());
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv)
{
    try {
        std::optional<std::filesystem::path> root;
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::string(argv[i]) == "--data") root = std::filesystem::path(argv[i + 1]);
        }
        if (!root) root = av::findDataRoot(argc > 0 ? argv[0] : nullptr, "assets/materials/materials.json");
        if (!root) {
            std::fprintf(stderr, "error: cannot find the assets/ directory\n");
            return 1;
        }
        std::filesystem::path configPath = *root / "config/server.json";
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--rooms") configPath = *root / "config/server-online.json";
        }
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::string(argv[i]) == "--config") configPath = argv[i + 1];
        }
        server::ServerConfig config = server::ServerConfig::load(configPath);
        double seconds = 0.0;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--help" || a == "-h") {
                std::printf("%s", kUsage);
                return 0;
            } else if (a == "--config" && i + 1 < argc) {
                ++i;
            } else if (a == "--port" && i + 1 < argc) {
                config.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
            } else if (a == "--local") {
                config.loopbackOnly = true;
            } else if (a == "--rooms") {
                config.roomServer = true;
            } else if (a == "--data" && i + 1 < argc) {
                ++i; // handled above
            } else if (a == "--seconds" && i + 1 < argc) {
                seconds = std::atof(argv[++i]);
            } else {
                std::fprintf(stderr, "error: unknown option %s\n\n%s", a.c_str(), kUsage);
                return 2;
            }
        }

        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);

        logLine("loading world (seed " + std::to_string(config.seed) + ")...");
        server::GameServer srv(config, *root);
        srv.log = logLine;
        std::string error;
        if (!srv.start(error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        logLine("tick rate " + std::to_string(config.tickRate) + " Hz, snapshots " + std::to_string(config.snapshotRate) +
                " Hz. Press Ctrl+C to stop.");

        if (seconds > 0.0) {
            std::thread timer([seconds] {
                std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
                g_stop = true;
            });
            timer.detach();
        }
        srv.run(g_stop);
        logLine("saved and stopped.");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
