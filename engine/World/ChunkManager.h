#pragma once

#include "Physics/CollisionWorld.h"
#include "World/Chunk.h"
#include "World/CityGenerator.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace av {

struct ChunkStats {
    std::size_t loaded = 0;
    std::size_t generatedTotal = 0;
    std::size_t unloadedTotal = 0;
    double lastGenerateMs = 0.0;
};

// Streams chunks around a focus point: loads missing chunks nearest-first
// (with a per-update budget so streaming never stalls a frame for long) and
// unloads chunks beyond loadRadius + 1 (hysteresis avoids thrashing at
// borders). Also exposes the loaded colliders and the layout's terrain to
// physics through the CollisionWorld interface.
class ChunkManager : public CollisionWorld {
public:
    using ChunkMap = std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash>;

    ChunkManager(const CityGenerator& generator, int loadRadius);

    // Streams around `focus`. Returns the number of chunks generated.
    int update(const Vec3& focus, int maxNewChunks = 2);
    // Streams around several points (one per player on a server).
    int update(const std::vector<Vec3>& foci, int maxNewChunks);
    // Synchronously loads everything in range (startup, teleports).
    void loadAll(const Vec3& focus);
    void loadAll(const std::vector<Vec3>& foci);

    void setLoadRadius(int radius) { loadRadius_ = std::max(radius, 0); }
    int loadRadius() const noexcept { return loadRadius_; }
    const ChunkMap& chunks() const noexcept { return chunks_; }
    const Chunk* find(const ChunkCoord& c) const;
    const ChunkStats& stats() const noexcept { return stats_; }

    // CollisionWorld
    float groundHeight(float x, float z) const override;
    void gatherColliders(const AABB& region, std::vector<AABB>& out) const override;

private:
    const CityGenerator& generator_;
    int loadRadius_;
    ChunkMap chunks_;
    ChunkStats stats_;
};

} // namespace av
