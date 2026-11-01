#pragma once

#include "Core/Random.h"
#include "Math/Vec.h"

#include <cmath>
#include <cstddef>
#include <functional>

namespace av {

// World units are metres. The world is divided into square chunks on the
// XZ plane; chunk (cx, cz) covers [cx*S, (cx+1)*S) x [cz*S, (cz+1)*S).
inline constexpr float kChunkSize = 64.0f;

struct ChunkCoord {
    int x = 0;
    int z = 0;

    constexpr bool operator==(const ChunkCoord& o) const { return x == o.x && z == o.z; }
    constexpr bool operator!=(const ChunkCoord& o) const { return !(*this == o); }
};

struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& c) const { return hash2i(0x9e37u, c.x, c.z); }
};

// Floor division, so negative coordinates map to negative chunks.
inline int worldToChunkAxis(float w) { return static_cast<int>(std::floor(w / kChunkSize)); }

inline ChunkCoord worldToChunk(float x, float z) { return {worldToChunkAxis(x), worldToChunkAxis(z)}; }
inline ChunkCoord worldToChunk(const Vec3& p) { return worldToChunk(p.x, p.z); }

// Minimum corner of the chunk in world space (y = 0).
inline Vec3 chunkOrigin(const ChunkCoord& c)
{
    return {static_cast<float>(c.x) * kChunkSize, 0.0f, static_cast<float>(c.z) * kChunkSize};
}

// Position relative to the chunk's minimum corner, in [0, S).
inline Vec3 worldToChunkLocal(const Vec3& p)
{
    const ChunkCoord c = worldToChunk(p);
    return p - chunkOrigin(c);
}

// Chebyshev ("ring") distance between chunks.
inline int chunkDistance(const ChunkCoord& a, const ChunkCoord& b)
{
    const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
    const int dz = a.z > b.z ? a.z - b.z : b.z - a.z;
    return dx > dz ? dx : dz;
}

} // namespace av
