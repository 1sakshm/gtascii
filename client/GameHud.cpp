#include "GameHud.h"

#include "Graphics/Draw2D.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace client {

namespace {
const av::Color kBarBg{12, 8, 24};
const av::Color kPanelBg{18, 12, 34};
const av::Color kPink{255, 80, 190};
const av::Color kCyan{70, 230, 240};
const av::Color kText{225, 225, 235};
const av::Color kDim{150, 145, 175};
const av::Color kGold{255, 215, 80};

int textWidth(const std::string& s)
{
    int n = 0;
    for (const unsigned char c : s) {
        n += (c & 0xC0) != 0x80 ? 1 : 0; // count UTF-8 code points
    }
    return n;
}

void panel(av::Framebuffer& fb, int x, int y, int w, int h)
{
    av::Cell c;
    c.bg = kPanelBg;
    av::draw::fillRect(fb, x, y, w, h, c);
}

std::string moneyText(int amount)
{
    std::string digits = std::to_string(std::abs(amount));
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) {
        digits.insert(static_cast<std::size_t>(i), ",");
    }
    return (amount < 0 ? "-$" : "$") + digits;
}
} // namespace

void drawStatusBar(av::Framebuffer& fb, const HudInfo& h)
{
    if (fb.height() < 3) {
        return;
    }
    av::Cell bar;
    bar.bg = kBarBg;
    av::draw::fillRect(fb, 0, 0, fb.width(), 1, bar);
    int x = 1;
    x += av::draw::text(fb, x, 0, "GRAND THEFT ASCII", kPink, kBarBg) + 1;
    std::string where = h.neighborhood;
    if (!h.landmark.empty()) {
        where += " - " + h.landmark;
    }
    x += av::draw::text(fb, x, 0, where, kCyan, kBarBg);
    const int hh = static_cast<int>(h.hour) % 24;
    const int mm = static_cast<int>((h.hour - std::floor(h.hour)) * 60.0f);
    char buf[256];
    std::snprintf(buf, sizeof buf, "  %02d:%02d  %s%s  %4.1f fps %4.1f ms  %zu tris  %d player%s  %s", hh, mm,
                  h.activity.c_str(), h.running ? "" : " [walk]", h.fps, h.frameMs, h.triangles, h.players,
                  h.players == 1 ? "" : "s", h.connection.c_str());
    av::draw::text(fb, x, 0, buf, kText, kBarBg);
}

void drawHelpBar(av::Framebuffer& fb, bool driving)
{
    if (fb.height() < 3) {
        return;
    }
    av::Cell bar;
    bar.bg = kBarBg;
    av::draw::fillRect(fb, 0, fb.height() - 1, fb.width(), 1, bar);
    const char* help = driving
        ? "W/S throttle/brake  A/D steer  Space handbrake  F get out  Arrows/drag camera  Enter chat  M map  I items  Tab players  Q quit"
        : "WASD move  Shift walk  Space jump  C crouch  E use  F car  X punch  G wave  Arrows/drag camera  T time  Enter chat  M map  I items  Tab players  Q quit";
    av::draw::text(fb, 1, fb.height() - 1, help, kDim, kBarBg);
}

void drawMinimap(av::Framebuffer& fb, const av::ChunkManager& world, const av::CityLayout& layout, const av::Vec3& player,
                 float yaw, const std::vector<Blip>& blips)
{
    constexpr int kW = 27, kH = 13;
    constexpr float kCell = 8.0f; // metres per cell horizontally, 16 vertically (cell aspect)
    const int x0 = fb.width() - kW - 2, y0 = 2;
    if (x0 < 0 || y0 + kH + 2 > fb.height()) {
        return;
    }
    av::Cell frame;
    frame.bg = kBarBg;
    av::draw::fillRect(fb, x0 - 1, y0 - 1, kW + 2, kH + 2, frame);
    av::draw::text(fb, x0 + kW / 2, y0 - 1, "N", kCyan, kBarBg);

    std::vector<av::AABB> boxes;
    for (int my = 0; my < kH; ++my) {
        for (int mx = 0; mx < kW; ++mx) {
            const float wx = player.x + static_cast<float>(mx - kW / 2) * kCell;
            const float wz = player.z + static_cast<float>(my - kH / 2) * kCell * 2.0f;
            const av::ChunkCoord cc = av::worldToChunk(wx, wz);
            const av::Vec3 local = av::Vec3{wx, 0, wz} - av::chunkOrigin(cc);
            av::Color c;
            char32_t ch = U' ';
            switch (layout.surface(cc, local.x, local.z)) {
            case av::Surface::Road: c = {70, 70, 80}; break;
            case av::Surface::Sidewalk: c = {130, 125, 120}; break;
            case av::Surface::Sand: c = {200, 180, 120}; break;
            case av::Surface::Water: c = {30, 80, 150}; ch = U'~'; break;
            case av::Surface::Lot:
                switch (layout.district(cc)) {
                case av::District::Downtown: c = {110, 100, 120}; break;
                case av::District::Industrial:
                case av::District::Docks: c = {100, 90, 80}; break;
                case av::District::HotelStrip: c = {170, 120, 150}; break;
                default: c = {50, 110, 50}; break;
                }
                break;
            }
            boxes.clear();
            world.gatherColliders(av::AABB({wx - 2, 0, wz - 2}, {wx + 2, 200, wz + 2}), boxes);
            for (const av::AABB& b : boxes) {
                if (b.max.y > 4.0f && b.size().x > 3.0f && b.size().z > 3.0f) {
                    c = {225, 215, 230};
                    ch = U'#';
                    break;
                }
            }
            av::Cell& cell = fb.at(x0 + mx, y0 + my);
            cell.ch = ch;
            cell.bg = c;
            cell.fg = av::scale(c, 0.7f);
        }
    }
    for (const Blip& b : blips) {
        int mx = static_cast<int>(std::lround((b.position.x - player.x) / kCell)) + kW / 2;
        int my = static_cast<int>(std::lround((b.position.z - player.z) / (kCell * 2.0f))) + kH / 2;
        if (mx < 0 || my < 0 || mx >= kW || my >= kH) {
            if (!b.pinToEdge) {
                continue;
            }
            mx = std::clamp(mx, 0, kW - 1);
            my = std::clamp(my, 0, kH - 1);
        }
        av::Cell& cell = fb.at(x0 + mx, y0 + my);
        cell.ch = b.glyph;
        cell.fg = b.color;
        cell.bg = av::scale(b.color, 0.25f);
    }
    // Player arrow: yaw 0 faces +Z (south, down on the map).
    const float a = av::wrapAngle(yaw);
    char32_t arrow = U'v';
    if (std::fabs(a) > 2.36f) arrow = U'^';
    else if (a > 0.79f) arrow = U'>';
    else if (a < -0.79f) arrow = U'<';
    av::Cell& me = fb.at(x0 + kW / 2, y0 + kH / 2);
    me.ch = arrow;
    me.fg = {255, 255, 80};
    me.bg = kPink;
}

void drawPlayerPanel(av::Framebuffer& fb, const av::proto::PlayerStatus& s, float time)
{
    const int w = 27, x = fb.width() - w - 2, y = 17;
    if (x < 0 || y + 3 >= fb.height()) {
        return;
    }
    panel(fb, x - 1, y, w + 2, 3);
    av::draw::text(fb, x, y, moneyText(s.money), {120, 255, 140}, kPanelBg);
    // Wanted stars flash while the police are after you.
    std::string stars;
    for (int i = 0; i < 5; ++i) stars += i < s.wanted ? '*' : '.';
    const bool flash = s.wanted > 0 && std::fmod(time, 0.8f) < 0.4f;
    av::draw::text(fb, x + w - 5, y, stars, s.wanted > 0 ? (flash ? av::Color{255, 60, 60} : av::Color{80, 140, 255}) : kDim, kPanelBg);
    // Health bar.
    const int barW = w - 4;
    const int filled = static_cast<int>(std::lround(barW * s.health / 100.0f));
    av::draw::text(fb, x, y + 1, "HP", kDim, kPanelBg);
    for (int i = 0; i < barW; ++i) {
        fb.set(x + 3 + i, y + 1, {i < filled ? U'|' : U'.', i < filled ? (s.health > 30 ? av::Color{90, 230, 120} : av::Color{255, 70, 70}) : kDim, kPanelBg});
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "Flamingos %u/%u", s.pickupsFound, s.pickupsTotal);
    av::draw::text(fb, x, y + 2, buf, kPink, kPanelBg);
}

void drawMissionPanel(av::Framebuffer& fb, const av::proto::PlayerStatus& s)
{
    if (s.missionTitle.empty() || fb.height() < 8) {
        return;
    }
    std::string timer;
    if (s.missionTimer >= 0.0f) {
        const int t = static_cast<int>(std::ceil(s.missionTimer));
        char buf[16];
        std::snprintf(buf, sizeof buf, "  %d:%02d", t / 60, t % 60);
        timer = buf;
    }
    const std::string title = s.missionTitle + timer;
    const int w = std::max(textWidth(title), textWidth(s.objective)) + 2;
    panel(fb, 0, 1, w + 1, 2);
    av::draw::text(fb, 1, 1, s.missionTitle, kPink, kPanelBg);
    if (!timer.empty()) {
        av::draw::text(fb, 1 + textWidth(s.missionTitle), 1, timer, s.missionTimer < 15.0f ? av::Color{255, 80, 80} : kGold, kPanelBg);
    }
    av::draw::text(fb, 1, 2, s.objective, kText, kPanelBg);
}

void drawPrompt(av::Framebuffer& fb, const std::string& text)
{
    if (text.empty() || fb.height() < 6) {
        return;
    }
    const int w = textWidth(text) + 4;
    const int x = (fb.width() - w) / 2, y = fb.height() - 4;
    panel(fb, x, y, w, 1);
    av::draw::text(fb, x + 2, y, text, kGold, kPanelBg);
}

void drawToasts(av::Framebuffer& fb, const std::vector<Toast>& toasts)
{
    int y = 4;
    for (const Toast& t : toasts) {
        if (y >= fb.height() - 6) {
            break;
        }
        const int w = textWidth(t.text) + 2;
        const int x = (fb.width() - w) / 2;
        const float a = std::min(1.0f, t.timeLeft);
        panel(fb, x, y, w, 1);
        av::draw::text(fb, x + 1, y, t.text, av::lerp(kPanelBg, t.color, a), kPanelBg);
        ++y;
    }
}

void drawBanner(av::Framebuffer& fb, const std::string& text, av::Color color, float alpha)
{
    if (alpha <= 0.0f || text.empty() || fb.height() < 8) {
        return;
    }
    const int w = textWidth(text) + 6;
    const int x = (fb.width() - w) / 2, y = fb.height() / 3;
    av::Cell box;
    box.bg = kBarBg;
    av::draw::fillRect(fb, x, y, w, 3, box);
    av::draw::text(fb, x + 3, y + 1, text, av::lerp(kBarBg, color, alpha), kBarBg);
    for (int i = 0; i < w; ++i) {
        fb.set(x + i, y, {U'=', av::lerp(kBarBg, kCyan, alpha), kBarBg});
        fb.set(x + i, y + 2, {U'=', av::lerp(kBarBg, kCyan, alpha), kBarBg});
    }
}

void drawChat(av::Framebuffer& fb, const std::vector<ChatEntry>& lines, bool typing, const std::string& input)
{
    const int maxLines = 6;
    const int bottom = fb.height() - (typing ? 3 : 2);
    int y = bottom;
    int shown = 0;
    for (auto it = lines.rbegin(); it != lines.rend() && shown < maxLines; ++it) {
        if (!typing && it->age > 12.0f) {
            break;
        }
        if (y <= 1) {
            break;
        }
        const float fade = typing ? 1.0f : std::min(1.0f, (12.0f - it->age) / 2.0f);
        av::draw::text(fb, 1, y, it->text, av::lerp(kPanelBg, it->color, fade), kPanelBg);
        --y;
        ++shown;
    }
    if (typing) {
        const std::string line = "Say: " + input + "_";
        panel(fb, 0, fb.height() - 2, std::max(40, textWidth(line) + 2), 1);
        av::draw::text(fb, 1, fb.height() - 2, line, kCyan, kPanelBg);
    }
}

void drawInventory(av::Framebuffer& fb, const av::proto::PlayerStatus& s)
{
    const int w = 34, h = static_cast<int>(s.inventory.size()) + 4;
    const int x = (fb.width() - w) / 2, y = std::max(2, (fb.height() - h) / 2);
    panel(fb, x, y, w, h);
    av::draw::text(fb, x + 2, y, "INVENTORY", kPink, kPanelBg);
    av::draw::text(fb, x + 2, y + 1, "Cash " + moneyText(s.money), {120, 255, 140}, kPanelBg);
    int row = y + 2;
    if (s.inventory.empty()) {
        av::draw::text(fb, x + 2, row++, "(nothing yet)", kDim, kPanelBg);
    }
    for (const auto& item : s.inventory) {
        std::string line = item.name;
        if (item.count > 1) line += "  x" + std::to_string(item.count);
        av::draw::text(fb, x + 2, row++, line, kText, kPanelBg);
    }
}

void drawRoster(av::Framebuffer& fb, const std::map<std::uint16_t, std::string>& roster, const std::string& self)
{
    const int w = 30, h = static_cast<int>(roster.size()) + 3;
    const int x = (fb.width() - w) / 2, y = 3;
    panel(fb, x, y, w, h);
    av::draw::text(fb, x + 2, y, "PLAYERS ONLINE", kPink, kPanelBg);
    av::draw::text(fb, x + 2, y + 1, self + " (you)", kGold, kPanelBg);
    int row = y + 2;
    for (const auto& [id, name] : roster) {
        if (name != self) {
            av::draw::text(fb, x + 2, row++, name, kCyan, kPanelBg);
        }
    }
}

void drawScoreboard(av::Framebuffer& fb, const std::vector<av::proto::ScoreEntry>& scores, const std::string& self,
                    const std::string& roomCode, const std::string& roomName, av::proto::GameMode mode)
{
    const int w = 44, h = static_cast<int>(scores.size()) + 5;
    const int x = (fb.width() - w) / 2, y = 3;
    panel(fb, x, y, w, h);
    av::draw::text(fb, x + 2, y, "SCOREBOARD", kPink, kPanelBg);
    std::string room = roomName + "  (" + av::proto::toString(mode) + ")";
    av::draw::text(fb, x + 2, y + 1, room.substr(0, static_cast<std::size_t>(w - 4)), kDim, kPanelBg);
    if (!roomCode.empty() && roomCode != "MAIN") {
        av::draw::text(fb, x + 2, y + 2, "Room code: " + roomCode + "  - share it with friends", kGold, kPanelBg);
    }
    int row = y + 3;
    int rank = 1;
    for (const auto& s : scores) {
        char line[96];
        std::snprintf(line, sizeof line, "%2d. %-16s %5d pts  %s", rank++, s.name.c_str(), s.points, s.role.c_str());
        const av::Color c = s.name == self ? kGold : (s.role == "robber" ? av::Color{255, 90, 90} : s.role == "cop" ? av::Color{90, 140, 255} : kText);
        av::draw::text(fb, x + 2, row++, line, c, kPanelBg);
    }
}

void drawPhone(av::Framebuffer& fb, const PhoneView& view, float time)
{
    const int w = 36, h = std::max(12, static_cast<int>(view.lines.size()) + 6);
    const int x = fb.width() - w - 3, y = std::max(1, (fb.height() - h) / 2);
    if (x < 0 || y + h > fb.height()) {
        return;
    }
    const av::Color body{30, 24, 46}, screen{10, 26, 40}, edge{120, 110, 150};
    av::Cell c;
    c.bg = body;
    av::draw::fillRect(fb, x, y, w, h, c);
    c.bg = screen;
    av::draw::fillRect(fb, x + 2, y + 1, w - 4, h - 3, c);
    for (int i = 0; i < w; ++i) {
        fb.set(x + i, y, {U'-', edge, body});
        fb.set(x + i, y + h - 1, {U'-', edge, body});
    }
    // Status line: time-ish dots and battery.
    av::draw::text(fb, x + 3, y + 1, view.title, kPink, screen);
    av::draw::text(fb, x + w - 9, y + 1, std::fmod(time, 2.0f) < 1.8f ? "[####]" : "[### ]", kCyan, screen);
    int row = y + 3;
    const int visible = h - 7;
    const int top = std::clamp(view.selected - visible / 2, 0, std::max(0, static_cast<int>(view.lines.size()) - visible));
    for (int i = top; i < static_cast<int>(view.lines.size()) && row < y + h - 3; ++i) {
        const bool sel = i == view.selected;
        const av::Color bg = sel ? av::Color{40, 70, 100} : screen;
        av::Cell r;
        r.bg = bg;
        av::draw::fillRect(fb, x + 2, row, w - 4, 1, r);
        av::draw::text(fb, x + 3, row, (sel ? "> " : "  ") + view.lines[static_cast<std::size_t>(i)].substr(0, static_cast<std::size_t>(w - 8)),
                       sel ? kGold : kText, bg);
        ++row;
    }
    av::draw::text(fb, x + 3, y + h - 2, view.footer, kDim, body);
}

void drawNameTag(av::Framebuffer& fb, const av::Mat4& vp, const av::Vec3& cameraPos, const av::Vec3& head,
                 const std::string& name, av::Color color)
{
    const av::Vec4 clip = vp * av::Vec4(head, 1.0f);
    if (clip.w <= 0.1f) {
        return;
    }
    const float nx = clip.x / clip.w, ny = clip.y / clip.w;
    if (nx < -1.0f || nx > 1.0f || ny < -1.0f || ny > 1.0f) {
        return;
    }
    const int cx = static_cast<int>((nx * 0.5f + 0.5f) * static_cast<float>(fb.width()));
    const int cy = static_cast<int>((0.5f - ny * 0.5f) * static_cast<float>(fb.height()));
    if (!fb.inBounds(cx, cy)) {
        return;
    }
    const float dist = av::distance(cameraPos, head);
    if (fb.at(cx, cy).depth < dist - 1.5f || dist > 80.0f) {
        return; // behind a wall, or too far to read
    }
    const int w = textWidth(name);
    av::draw::text(fb, cx - w / 2, cy, name, color, av::Color{10, 10, 20});
}

} // namespace client
