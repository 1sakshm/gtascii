// Saving and loading persistent world data (player profiles, world clock).
#include "ServerWorld.h"

#include "Core/FileUtil.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace server {

using namespace av;

namespace {

std::string quote(const std::string& s)
{
    std::string out = "\"";
    for (const char ch : s) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", ch);
                out += buf;
            } else {
                out += ch;
            }
        }
    }
    return out + "\"";
}

} // namespace

void ServerWorld::save()
{
    if (savePath_.empty()) {
        return;
    }
    std::ostringstream o;
    o << "{\n  \"version\": 1,\n  \"seed\": " << config_.seed << ",\n  \"hour\": " << hour_ << ",\n  \"players\": {";
    bool firstPlayer = true;
    for (const auto& [name, p] : profiles_) {
        o << (firstPlayer ? "\n" : ",\n") << "    " << quote(name) << ": {";
        firstPlayer = false;
        o << "\"passwordHash\": " << p.passwordHash << ", \"money\": " << p.money << ", \"savePoint\": "
          << (p.hasSavePoint ? "true" : "false") << ", \"playSeconds\": " << static_cast<long long>(p.playSeconds);
        o << ",\n      \"missions\": [";
        bool first = true;
        for (const std::string& m : p.missionsDone) {
            o << (first ? "" : ", ") << quote(m);
            first = false;
        }
        o << "], \"pickups\": [";
        first = true;
        for (const int id : p.pickups) {
            o << (first ? "" : ", ") << id;
            first = false;
        }
        o << "],\n      \"vehicles\": [";
        first = true;
        for (const auto& [model, paint] : p.ownedVehicles) {
            o << (first ? "" : ", ") << "[" << model << ", " << paint << "]";
            first = false;
        }
        o << "], \"inventory\": {";
        first = true;
        for (const auto& [item, count] : p.inventory) {
            if (item == "Package") {
                continue; // mission items are not persistent
            }
            o << (first ? "" : ", ") << quote(item) << ": " << count;
            first = false;
        }
        o << "}}";
    }
    o << "\n  }\n}\n";

    const std::filesystem::path path = savePath_;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            return;
        }
        f << o.str();
    }
    std::filesystem::rename(tmp, path, ec); // atomic replace: a crash never leaves a half-written save
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
}

void ServerWorld::loadSave()
{
    if (savePath_.empty()) {
        return;
    }
    const auto text = readTextFile(savePath_);
    if (!text) {
        return; // first run
    }
    JsonValue doc;
    try {
        doc = parseJson(*text);
    } catch (const std::exception&) {
        // Keep the unreadable file for inspection and start fresh.
        std::error_code ec;
        std::filesystem::rename(savePath_, savePath_.string() + ".corrupt", ec);
        return;
    }
    if (!config_.overrideSavedHour && static_cast<std::uint32_t>(doc.numberOr("seed", config_.seed)) == config_.seed) {
        hour_ = static_cast<float>(doc.numberOr("hour", hour_));
    }
    const JsonValue* players = doc.find("players");
    if (!players || !players->isObject()) {
        return;
    }
    try {
    for (const auto& [name, pj] : players->asObject()) {
        PlayerProfile p;
        p.name = name;
        p.passwordHash = static_cast<std::uint32_t>(pj.numberOr("passwordHash", 0));
        p.money = static_cast<int>(pj.numberOr("money", 500));
        p.hasSavePoint = pj.boolOr("savePoint", false);
        p.playSeconds = pj.numberOr("playSeconds", 0);
        if (const JsonValue* m = pj.find("missions")) {
            for (const JsonValue& id : m->asArray()) p.missionsDone.insert(id.asString());
        }
        if (const JsonValue* pk = pj.find("pickups")) {
            for (const JsonValue& id : pk->asArray()) p.pickups.insert(static_cast<int>(id.asNumber()));
        }
        if (const JsonValue* vs = pj.find("vehicles")) {
            for (const JsonValue& v : vs->asArray()) {
                if (v.size() == 2) {
                    p.ownedVehicles.push_back({static_cast<int>(v[0].asNumber()), static_cast<int>(v[1].asNumber())});
                }
            }
        }
        if (const JsonValue* inv = pj.find("inventory"); inv && inv->isObject()) {
            for (const auto& [item, count] : inv->asObject()) p.inventory[item] = static_cast<int>(count.asNumber());
        }
        profiles_[name] = std::move(p);
    }
    } catch (const std::exception&) {
        profiles_.clear(); // malformed entries: start fresh rather than refuse to run
    }
}

} // namespace server
