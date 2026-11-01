#pragma once

#include "Protocol.h"

#include "Graphics/Framebuffer.h"
#include "Math/Mat.h"
#include "World/ChunkManager.h"

#include <map>
#include <string>
#include <vector>

namespace client {

struct HudInfo {
    std::string neighborhood;
    std::string landmark;
    float hour = 12.0f;
    av::Vec3 position;
    std::string activity;
    bool running = true;
    double fps = 0.0;
    double frameMs = 0.0;
    std::size_t triangles = 0;
    int players = 1;
    std::string connection; // "local", "host :41986", "192.168.1.5:41986"
};

struct Blip {
    av::Vec3 position;
    char32_t glyph = U'*';
    av::Color color{255, 255, 255};
    bool pinToEdge = false; // objectives stay visible at the map edge
};

struct Toast {
    std::string text;
    av::Color color{230, 230, 240};
    float timeLeft = 3.0f;
};

struct ChatEntry {
    std::string text;
    av::Color color{220, 220, 230};
    float age = 0.0f;
};

void drawStatusBar(av::Framebuffer& fb, const HudInfo& info);
void drawHelpBar(av::Framebuffer& fb, bool driving);
void drawMinimap(av::Framebuffer& fb, const av::ChunkManager& world, const av::CityLayout& layout, const av::Vec3& player,
                 float yaw, const std::vector<Blip>& blips);
void drawPlayerPanel(av::Framebuffer& fb, const av::proto::PlayerStatus& status, float time);
void drawMissionPanel(av::Framebuffer& fb, const av::proto::PlayerStatus& status);
void drawPrompt(av::Framebuffer& fb, const std::string& text);
void drawToasts(av::Framebuffer& fb, const std::vector<Toast>& toasts);
void drawBanner(av::Framebuffer& fb, const std::string& text, av::Color color, float alpha);
void drawChat(av::Framebuffer& fb, const std::vector<ChatEntry>& lines, bool typing, const std::string& input);
void drawInventory(av::Framebuffer& fb, const av::proto::PlayerStatus& status);
void drawRoster(av::Framebuffer& fb, const std::map<std::uint16_t, std::string>& roster, const std::string& self);
// Room scoreboard (Tab): points, roles, room code to share.
void drawScoreboard(av::Framebuffer& fb, const std::vector<av::proto::ScoreEntry>& scores, const std::string& self,
                    const std::string& roomCode, const std::string& roomName, av::proto::GameMode mode);

// The in-game phone: a title, a list of entries and a footer hint.
struct PhoneView {
    std::string title;
    std::vector<std::string> lines;
    int selected = 0;
    std::string footer;
};
void drawPhone(av::Framebuffer& fb, const PhoneView& view, float time);
// Name above a character; hidden when behind the camera or occluded.
void drawNameTag(av::Framebuffer& fb, const av::Mat4& viewProjection, const av::Vec3& cameraPos, const av::Vec3& head,
                 const std::string& name, av::Color color);

} // namespace client
