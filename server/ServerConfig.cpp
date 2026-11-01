#include "ServerConfig.h"

#include "Core/FileUtil.h"
#include "Core/Json.h"

#include <stdexcept>

namespace server {

ServerConfig ServerConfig::load(const std::filesystem::path& path)
{
    ServerConfig c;
    const auto text = av::readTextFile(path);
    if (!text) {
        return c;
    }
    av::JsonValue doc;
    try {
        doc = av::parseJson(*text);
    } catch (const std::exception& e) {
        throw std::runtime_error(path.string() + ": " + e.what());
    }
    c.port = static_cast<std::uint16_t>(doc.numberOr("port", c.port));
    c.loopbackOnly = doc.boolOr("loopbackOnly", c.loopbackOnly);
    c.tickRate = static_cast<int>(doc.numberOr("tickRate", c.tickRate));
    c.snapshotRate = static_cast<int>(doc.numberOr("snapshotRate", c.snapshotRate));
    c.maxPlayers = static_cast<int>(doc.numberOr("maxPlayers", c.maxPlayers));
    c.pedestrians = static_cast<int>(doc.numberOr("pedestrians", c.pedestrians));
    c.trafficCars = static_cast<int>(doc.numberOr("trafficCars", c.trafficCars));
    c.loadRadius = static_cast<int>(doc.numberOr("loadRadius", c.loadRadius));
    c.dayLengthMinutes = static_cast<float>(doc.numberOr("dayLengthMinutes", c.dayLengthMinutes));
    c.startHour = static_cast<float>(doc.numberOr("startHour", c.startHour));
    c.seed = static_cast<std::uint32_t>(doc.numberOr("seed", c.seed));
    c.savePath = doc.stringOr("savePath", c.savePath);
    c.autosaveSeconds = static_cast<float>(doc.numberOr("autosaveSeconds", c.autosaveSeconds));
    c.allowCheats = doc.boolOr("allowCheats", c.allowCheats);
    c.motd = doc.stringOr("motd", c.motd);
    c.roomServer = doc.boolOr("roomServer", c.roomServer);
    c.mainRoom = doc.boolOr("mainRoom", c.mainRoom);
    c.roomName = doc.stringOr("roomName", c.roomName);
    c.maxRooms = static_cast<int>(doc.numberOr("maxRooms", c.maxRooms));
    c.emptyRoomSeconds = static_cast<float>(doc.numberOr("emptyRoomSeconds", c.emptyRoomSeconds));
    c.lanDiscovery = doc.boolOr("lanDiscovery", c.lanDiscovery);
    c.tickRate = std::max(5, std::min(c.tickRate, 120));
    c.snapshotRate = std::max(1, std::min(c.snapshotRate, c.tickRate));
    return c;
}

} // namespace server
