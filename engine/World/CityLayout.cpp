#include "World/CityLayout.h"

#include "Core/FileUtil.h"

#include <cmath>
#include <stdexcept>

namespace av {

const char* toString(District d)
{
    switch (d) {
    case District::Downtown: return "Downtown";
    case District::Residential: return "Residential";
    case District::HotelStrip: return "Ocean Drive";
    case District::Beach: return "Beach";
    case District::Ocean: return "Ocean";
    case District::Park: return "Park";
    case District::Industrial: return "Industrial";
    case District::Docks: return "Docks";
    }
    return "?";
}

CityLayout::CityLayout(std::uint32_t seed, std::vector<Landmark> landmarks)
    : seed_(seed)
    , landmarks_(std::move(landmarks))
{
}

CityLayout CityLayout::fromFile(std::uint32_t seed, const std::filesystem::path& path)
{
    const auto text = readTextFile(path);
    if (!text) {
        throw std::runtime_error("cannot read map file: " + path.string());
    }
    try {
        const JsonValue doc = parseJson(*text);
        std::vector<Landmark> landmarks;
        if (const JsonValue* list = doc.find("landmarks")) {
            for (const JsonValue& item : list->asArray()) {
                Landmark lm;
                lm.name = item.stringOr("name", "landmark");
                const JsonValue* type = item.find("type");
                if (!type) {
                    throw std::runtime_error("landmark '" + lm.name + "' needs a \"type\"");
                }
                lm.type = type->asString();
                const JsonValue* chunk = item.find("chunk");
                if (!chunk || chunk->size() != 2) {
                    throw std::runtime_error("landmark '" + lm.name + "' needs \"chunk\": [x, z]");
                }
                lm.chunk = {static_cast<int>((*chunk)[0].asNumber()), static_cast<int>((*chunk)[1].asNumber())};
                lm.params = item;
                landmarks.push_back(std::move(lm));
            }
        }
        CityLayout layout(seed, std::move(landmarks));
        if (const JsonValue* spawn = doc.find("spawn")) {
            const JsonValue* pos = spawn->find("position");
            if (pos && pos->size() == 3) {
                layout.spawn_.position = {static_cast<float>((*pos)[0].asNumber()), static_cast<float>((*pos)[1].asNumber()),
                                          static_cast<float>((*pos)[2].asNumber())};
            }
            layout.spawn_.yaw = radians(static_cast<float>(spawn->numberOr("yawDegrees", 0.0)));
        }
        return layout;
    } catch (const std::exception& e) {
        throw std::runtime_error(path.string() + ": " + e.what());
    }
}

const Landmark* CityLayout::findLandmark(const std::string& type) const
{
    for (const Landmark& lm : landmarks_) {
        if (lm.type == type) {
            return &lm;
        }
    }
    return nullptr;
}

std::string CityLayout::neighborhood(const ChunkCoord& c) const
{
    if (isCanal(c)) {
        return "Canal Walk";
    }
    switch (district(c)) {
    case District::Downtown: return "Downtown";
    case District::HotelStrip: return "Neon Mile";
    case District::Beach: return "Seabreeze Beach";
    case District::Ocean: return "Open Water";
    case District::Docks: return "Harbor Docks";
    case District::Industrial: return "Foundry Yards";
    case District::Park: return "Park";
    case District::Residential: break;
    }
    if (c.z > kCanalRow) return "Coral Shores";
    if (c.z <= -4) return "Palmetto Heights";
    if (c.x <= -2) return "Mango Grove";
    return "Sunset Flats";
}

const Landmark* CityLayout::landmark(const ChunkCoord& c) const
{
    for (const Landmark& lm : landmarks_) {
        if (lm.chunk == c) {
            return &lm;
        }
    }
    return nullptr;
}

District CityLayout::district(const ChunkCoord& c) const
{
    if (c.x >= kBeachColumn + 1) {
        return District::Ocean;
    }
    if (c.x == kBeachColumn) {
        return c.z <= kDocksRow ? District::Docks : District::Beach;
    }
    if (const Landmark* lm = landmark(c)) {
        const std::string d = lm->params.stringOr("district", "");
        if (d == "park") return District::Park;
        if (d == "downtown") return District::Downtown;
        if (d == "residential") return District::Residential;
    }
    if (c.x == kBeachColumn - 1) {
        return District::HotelStrip;
    }
    if (c.x <= -4) {
        return District::Industrial;
    }
    if (c.x >= -1 && c.x <= 1 && c.z >= -3 && c.z <= -1) {
        return District::Downtown;
    }
    if (hash2i(seed_ ^ 0x9a4cu, c.x, c.z) % 7 == 0) {
        return District::Park;
    }
    return District::Residential;
}

ChunkRoads CityLayout::roads(const ChunkCoord& c) const
{
    // North-south roads run on every border line up to the coast road at the
    // beach's west edge; east-west roads stop at the coast road.
    ChunkRoads r;
    r.west = c.x <= kBeachColumn;
    r.east = c.x + 1 <= kBeachColumn && c.x < kBeachColumn;
    const bool city = c.x < kBeachColumn;
    r.north = city;
    r.south = city;
    return r;
}

Surface CityLayout::surface(const ChunkCoord& c, float lx, float lz) const
{
    const District d = district(c);
    if (d == District::Ocean) {
        return Surface::Water;
    }
    const ChunkRoads r = roads(c);
    const float S = kChunkSize, R = kRoadHalfWidth, W = R + kSidewalkWidth;
    if ((r.west && lx < R) || (r.east && lx >= S - R) || (r.north && lz < R) || (r.south && lz >= S - R)) {
        return Surface::Road;
    }
    if ((r.west && lx < W) || (r.east && lx >= S - W) || (r.north && lz < W) || (r.south && lz >= S - W)) {
        return Surface::Sidewalk;
    }
    if (d == District::Beach) {
        return beachHeight(lx) < kWaterLevel ? Surface::Water : Surface::Sand;
    }
    if (d == District::Docks) {
        return lx < kQuayEdge ? Surface::Lot : Surface::Water;
    }
    if (isCanal(c) && lz >= kCanalZ0 && lz < kCanalZ1) {
        return Surface::Water;
    }
    return Surface::Lot;
}

float CityLayout::beachHeight(float lx)
{
    const float start = kRoadHalfWidth + kSidewalkWidth;
    const float t = clampv((lx - start) / (kChunkSize - start), 0.0f, 1.0f);
    return kCurbHeight - (kCurbHeight - kSeabed) * std::pow(t, 2.2f);
}

float CityLayout::groundHeight(float wx, float wz) const
{
    const ChunkCoord c = worldToChunk(wx, wz);
    const float lx = wx - static_cast<float>(c.x) * kChunkSize;
    const float lz = wz - static_cast<float>(c.z) * kChunkSize;
    switch (surface(c, lx, lz)) {
    case Surface::Road: return 0.0f;
    case Surface::Water:
        if (isCanal(c)) return kCanalFloor;
        if (district(c) == District::Beach) return beachHeight(lx);
        return kSeabed;
    case Surface::Sand: return beachHeight(lx);
    default: return kCurbHeight;
    }
}

} // namespace av
