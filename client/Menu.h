#pragma once

#include "ClientConfig.h"
#include "Protocol.h"

#include <filesystem>
#include <string>

namespace av {
class Audio;
class Terminal;
} // namespace av

namespace client {

enum class MenuChoice { SinglePlayer, HostLan, Join, Quit };

struct MenuOutcome {
    MenuChoice choice = MenuChoice::Quit;
    std::string address;  // Join: "host[:port]"
    std::string roomCode; // Join: room to enter ("" = server's main room)
    av::proto::CreateRoomRequest hostRoom; // HostLan: room name / mode / size
};

// The title screen: a slow 3D flyover of the city behind the menus, the
// character creator (with a live 3D preview) and the settings screens.
// Settings are written to the player's settings file when changed.
class MainMenu {
public:
    MainMenu(av::Terminal& terminal, const std::filesystem::path& dataRoot, ClientConfig& config, av::Audio& audio);
    ~MainMenu();

    // Shows the menu until the player picks something. `notice` is shown in
    // a banner (e.g. why the last session ended).
    MenuOutcome run(const std::string& notice);
    // Headless capture: open `screen` ("main", "multiplayer", "character",
    // "settings", "controls", "create") and quit after `frames` frames.
    void demo(const std::string& screen, int frames);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace client
