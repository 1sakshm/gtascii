#include "World/ChunkManager.h"

#include <algorithm>
#include <chrono>

namespace av {

ChunkManager::ChunkManager(const CityGenerator& generator, int loadRadius)
    : generator_(generator)
    , loadRadius_(std::max(loadRadius, 0))
{
}

const Chunk* ChunkManager::find(const ChunkCoord& c) const
{
    const auto it = chunks_.find(c);
    return it == chunks_.end() ? nullptr : it->second.get();
}

int ChunkManager::update(const Vec3& focus, int maxNewChunks)
{
    return update(std::vector<Vec3>{focus}, maxNewChunks);
}

int ChunkManager::update(const std::vector<Vec3>& foci, int maxNewChunks)
{
    std::vector<ChunkCoord> centres;
    for (const Vec3& f : foci) {
        centres.push_back(worldToChunk(f));
    }
    auto nearestCentre = [&centres](const ChunkCoord& c) {
        int best = 1 << 30;
        for (const ChunkCoord& k : centres) {
            best = std::min(best, chunkDistance(c, k));
        }
        return best;
    };

    // Unload chunks beyond radius + 1 of every focus.
    for (auto it = chunks_.begin(); it != chunks_.end();) {
        if (nearestCentre(it->first) > loadRadius_ + 1) {
            it = chunks_.erase(it);
            ++stats_.unloadedTotal;
        } else {
            ++it;
        }
    }

    // Missing chunks in range of any focus, nearest first.
    struct Candidate {
        ChunkCoord c;
        float d;
    };
    std::vector<Candidate> missing;
    for (std::size_t fi = 0; fi < foci.size(); ++fi) {
        const ChunkCoord centre = centres[fi];
        const float fx = foci[fi].x / kChunkSize - 0.5f, fz = foci[fi].z / kChunkSize - 0.5f;
        for (int dz = -loadRadius_; dz <= loadRadius_; ++dz) {
            for (int dx = -loadRadius_; dx <= loadRadius_; ++dx) {
                const ChunkCoord c{centre.x + dx, centre.z + dz};
                if (chunks_.count(c) != 0) {
                    continue;
                }
                const float d = (static_cast<float>(c.x) - fx) * (static_cast<float>(c.x) - fx) +
                                (static_cast<float>(c.z) - fz) * (static_cast<float>(c.z) - fz);
                bool merged = false;
                for (Candidate& m : missing) {
                    if (m.c == c) {
                        m.d = std::min(m.d, d);
                        merged = true;
                        break;
                    }
                }
                if (!merged) {
                    missing.push_back({c, d});
                }
            }
        }
    }
    std::sort(missing.begin(), missing.end(), [](const Candidate& a, const Candidate& b) { return a.d < b.d; });

    int generated = 0;
    for (const Candidate& m : missing) {
        // Chunks under a focus are always loaded immediately, budget or not.
        const bool underFocus = std::find(centres.begin(), centres.end(), m.c) != centres.end();
        if (generated >= maxNewChunks && !underFocus) {
            continue;
        }
        const auto t0 = std::chrono::steady_clock::now();
        chunks_[m.c] = generator_.generate(m.c);
        stats_.lastGenerateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        ++stats_.generatedTotal;
        ++generated;
    }
    stats_.loaded = chunks_.size();
    return generated;
}

void ChunkManager::loadAll(const Vec3& focus)
{
    update(focus, (2 * loadRadius_ + 1) * (2 * loadRadius_ + 1));
}

void ChunkManager::loadAll(const std::vector<Vec3>& foci)
{
    update(foci, static_cast<int>(foci.size()) * (2 * loadRadius_ + 1) * (2 * loadRadius_ + 1));
}

float ChunkManager::groundHeight(float x, float z) const
{
    return generator_.layout().groundHeight(x, z);
}

void ChunkManager::gatherColliders(const AABB& region, std::vector<AABB>& out) const
{
    const ChunkCoord lo = worldToChunk(region.min.x, region.min.z);
    const ChunkCoord hi = worldToChunk(region.max.x, region.max.z);
    for (int cz = lo.z; cz <= hi.z; ++cz) {
        for (int cx = lo.x; cx <= hi.x; ++cx) {
            const Chunk* chunk = find({cx, cz});
            if (chunk == nullptr) {
                continue;
            }
            for (const AABB& b : chunk->colliders) {
                if (b.intersects(region)) {
                    out.push_back(b);
                }
            }
        }
    }
}

} // namespace av
