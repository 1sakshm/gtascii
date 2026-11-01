#include "Missions.h"

#include "Core/FileUtil.h"

#include <stdexcept>

namespace av::proto {

Vec3 resolvePlace(const JsonValue& place, const CityLayout& layout)
{
    const JsonValue* chunk = place.find("chunk");
    const JsonValue* at = place.find("at");
    if (!chunk || !at || chunk->size() != 2 || at->size() != 2) {
        throw std::runtime_error("place needs \"chunk\": [x, z] and \"at\": [x, z]");
    }
    const float x = static_cast<float>((*chunk)[0].asNumber() * kChunkSize + (*at)[0].asNumber());
    const float z = static_cast<float>((*chunk)[1].asNumber() * kChunkSize + (*at)[1].asNumber());
    return {x, layout.groundHeight(x, z), z};
}

std::vector<RaceDef> loadRaces(const std::filesystem::path& path, const CityLayout& layout)
{
    std::vector<RaceDef> out;
    const auto text = readTextFile(path);
    if (!text) {
        return out; // races are optional
    }
    try {
        const JsonValue doc = parseJson(*text);
        const JsonValue* list = doc.find("races");
        if (!list) {
            return out;
        }
        for (const JsonValue& r : list->asArray()) {
            RaceDef d;
            d.id = r.stringOr("id", "race");
            d.name = r.stringOr("name", d.id);
            const JsonValue* start = r.find("start");
            const JsonValue* cps = r.find("checkpoints");
            if (!start || !cps || cps->size() == 0) {
                throw std::runtime_error("race '" + d.id + "' needs a start and checkpoints");
            }
            d.start = resolvePlace(*start, layout);
            d.startYaw = radians(static_cast<float>(start->numberOr("yawDegrees", 0.0)));
            for (const JsonValue& c : cps->asArray()) {
                d.checkpoints.push_back(resolvePlace(c, layout));
            }
            out.push_back(std::move(d));
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(path.string() + ": " + e.what());
    }
    return out;
}

std::vector<MissionDef> loadMissions(const std::filesystem::path& path, const CityLayout& layout)
{
    const auto text = readTextFile(path);
    if (!text) {
        throw std::runtime_error("cannot read missions: " + path.string());
    }
    std::vector<MissionDef> out;
    try {
        const JsonValue doc = parseJson(*text);
        const JsonValue* list = doc.find("missions");
        if (!list) {
            throw std::runtime_error("missing \"missions\" array");
        }
        for (const JsonValue& m : list->asArray()) {
            MissionDef d;
            d.id = m.stringOr("id", "");
            if (d.id.empty() || !m.find("start")) {
                throw std::runtime_error("every mission needs an \"id\" and a \"start\"");
            }
            d.title = m.stringOr("title", d.id);
            d.briefing = m.stringOr("briefing", "");
            const std::string type = m.stringOr("type", "courier");
            if (type == "courier") d.type = MissionType::Courier;
            else if (type == "checkpoints") d.type = MissionType::Checkpoints;
            else if (type == "delivery") d.type = MissionType::Delivery;
            else if (type == "escape") d.type = MissionType::Escape;
            else throw std::runtime_error("mission '" + d.id + "': unknown type '" + type + "'");
            d.start = resolvePlace(*m.find("start"), layout);
            if (const JsonValue* pts = m.find("points")) {
                for (const JsonValue& p : pts->asArray()) {
                    d.points.push_back(resolvePlace(p, layout));
                }
            }
            d.timeLimit = static_cast<float>(m.numberOr("time", 0.0));
            d.reward = static_cast<int>(m.numberOr("reward", 100));
            d.requiresVehicle = m.boolOr("requiresVehicle", false);
            d.damageLimit = static_cast<float>(m.numberOr("damageLimit", 0.7));
            d.wanted = static_cast<int>(m.numberOr("wanted", 0));
            const std::string vehicle = m.stringOr("vehicle", "coupe");
            d.vehicle = vehicle == "sedan" ? VehicleModel::Sedan : vehicle == "van" ? VehicleModel::Van : VehicleModel::Coupe;
            if (const JsonValue* spawn = m.find("vehicleSpawn")) {
                d.vehicleSpawn = resolvePlace(*spawn, layout);
                d.vehicleYaw = radians(static_cast<float>(spawn->numberOr("yawDegrees", 0.0)));
            } else {
                d.vehicleSpawn = d.start;
            }
            const bool needsPoints = d.type != MissionType::Escape;
            if (needsPoints && d.points.empty()) {
                throw std::runtime_error("mission '" + d.id + "' has no points");
            }
            if (d.type == MissionType::Courier && d.points.size() != 2) {
                throw std::runtime_error("courier mission '" + d.id + "' needs exactly 2 points (pickup, dropoff)");
            }
            out.push_back(std::move(d));
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(path.string() + ": " + e.what());
    }
    return out;
}

} // namespace av::proto
