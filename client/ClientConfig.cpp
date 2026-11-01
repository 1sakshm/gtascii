#include "ClientConfig.h"

#include "Core/FileUtil.h"
#include "Core/Json.h"

#include <cstdio>
#include <stdexcept>

namespace client {

ClientConfig ClientConfig::load(const std::filesystem::path& path)
{
    ClientConfig c;
    c.apply(path);
    return c;
}

ClientConfig ClientConfig::loadLayered(const std::filesystem::path& defaults, const std::filesystem::path& user)
{
    ClientConfig c = load(defaults);
    c.apply(user);
    return c;
}

std::filesystem::path ClientConfig::userPath() { return av::userDataDir() / "settings.json"; }

void ClientConfig::apply(const std::filesystem::path& path)
{
    ClientConfig& c = *this;
    const auto text = av::readTextFile(path);
    if (!text) {
        return;
    }
    av::JsonValue doc;
    try {
        doc = av::parseJson(*text);
    } catch (const std::exception& e) {
        throw std::runtime_error(path.string() + ": " + e.what());
    }
    auto f = [](const av::JsonValue* o, const char* k, float d) { return o ? static_cast<float>(o->numberOr(k, d)) : d; };
    auto i = [](const av::JsonValue* o, const char* k, int d) { return o ? static_cast<int>(o->numberOr(k, d)) : d; };
    auto b = [](const av::JsonValue* o, const char* k, bool d) { return o ? o->boolOr(k, d) : d; };
    auto s = [](const av::JsonValue* o, const char* k, const std::string& d) { return o ? o->stringOr(k, d) : d; };
    auto o_num = [](const av::JsonValue* o, const char* k, double d) { return o ? o->numberOr(k, d) : d; };

    const av::JsonValue* r = doc.find("render");
    c.samplesX = i(r, "samplesX", c.samplesX);
    c.samplesY = i(r, "samplesY", c.samplesY);
    c.cellAspect = f(r, "cellAspect", c.cellAspect);
    c.outlines = b(r, "outlines", c.outlines);
    c.ramp = s(r, "ramp", c.ramp);
    c.maxPointLights = i(r, "maxPointLights", c.maxPointLights);
    c.targetFps = f(r, "targetFps", static_cast<float>(c.targetFps));
    c.colorBits = i(r, "colorBits", c.colorBits);
    c.fogStart = f(r, "fogStart", c.fogStart);
    c.fogEnd = f(r, "fogEnd", c.fogEnd);

    const av::JsonValue* w = doc.find("world");
    c.seed = static_cast<std::uint32_t>(i(w, "seed", static_cast<int>(c.seed)));
    c.loadRadius = i(w, "loadRadius", c.loadRadius);
    c.detailDistance = f(w, "detailDistance", c.detailDistance);
    c.startHour = f(w, "startHour", c.startHour);
    c.dayLengthMinutes = f(w, "dayLengthMinutes", c.dayLengthMinutes);

    const av::JsonValue* cam = doc.find("camera");
    c.cameraDistance = f(cam, "distance", c.cameraDistance);
    c.keyRotateSpeed = f(cam, "keyRotateSpeed", c.keyRotateSpeed);
    c.mouseSensitivity = f(cam, "mouseSensitivity", c.mouseSensitivity);
    c.autoRecenterDelay = f(cam, "autoRecenterDelay", c.autoRecenterDelay);
    c.invertY = b(cam, "invertY", c.invertY);

    const av::JsonValue* p = doc.find("player");
    c.playerName = s(p, "name", c.playerName);
    c.appearanceSeed = static_cast<std::uint32_t>(o_num(p, "appearance", c.appearanceSeed));
    c.password = s(p, "password", c.password);

    const av::JsonValue* n = doc.find("network");
    c.server = s(n, "server", c.server);
    c.hostPort = static_cast<std::uint16_t>(i(n, "hostPort", c.hostPort));
    c.lobby = s(n, "lobby", c.lobby);
    const av::JsonValue* au = doc.find("audio");
    c.volume = f(au, "volume", c.volume);
    c.fovDegrees = i(cam, "fov", c.fovDegrees);
}


namespace {
std::string quoted(const std::string& s)
{
    std::string out = "\"";
    for (const char ch : s) {
        if (ch == '"' || ch == '\\') {
            out += '\\';
        }
        if (static_cast<unsigned char>(ch) >= 0x20) {
            out += ch;
        }
    }
    return out + "\"";
}
} // namespace

bool ClientConfig::save(const std::filesystem::path& path) const
{
    char buf[4096];
    std::snprintf(buf, sizeof buf,
                  "// Grand Theft ASCII - your settings (written by the in-game Settings menu).\n"
                  "{\n"
                  "    \"render\": {\"samplesX\": %d, \"samplesY\": %d, \"cellAspect\": %.2f, \"outlines\": %s, \"ramp\": %s,\n"
                  "               \"maxPointLights\": %d, \"targetFps\": %.0f, \"colorBits\": %d, \"fogStart\": %.0f, \"fogEnd\": %.0f},\n"
                  "    \"world\": {\"seed\": %u, \"loadRadius\": %d, \"detailDistance\": %.0f, \"startHour\": %.1f, \"dayLengthMinutes\": %.0f},\n"
                  "    \"camera\": {\"distance\": %.1f, \"keyRotateSpeed\": %.2f, \"mouseSensitivity\": %.3f, \"autoRecenterDelay\": %.1f,\n"
                  "               \"invertY\": %s, \"fov\": %d},\n"
                  "    \"player\": {\"name\": %s, \"password\": %s, \"appearance\": %u},\n"
                  "    \"network\": {\"server\": %s, \"hostPort\": %u, \"lobby\": %s},\n"
                  "    \"audio\": {\"volume\": %.2f}\n"
                  "}\n",
                  samplesX, samplesY, static_cast<double>(cellAspect), outlines ? "true" : "false", quoted(ramp).c_str(), maxPointLights,
                  targetFps, colorBits, static_cast<double>(fogStart), static_cast<double>(fogEnd), seed, loadRadius,
                  static_cast<double>(detailDistance), static_cast<double>(startHour), static_cast<double>(dayLengthMinutes),
                  static_cast<double>(cameraDistance), static_cast<double>(keyRotateSpeed), static_cast<double>(mouseSensitivity),
                  static_cast<double>(autoRecenterDelay), invertY ? "true" : "false", fovDegrees, quoted(playerName).c_str(),
                  quoted(password).c_str(), appearanceSeed, quoted(server).c_str(), static_cast<unsigned>(hostPort),
                  quoted(lobby).c_str(), static_cast<double>(volume));
    return av::writeTextFileAtomic(path, buf);
}

} // namespace client
