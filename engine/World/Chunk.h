#pragma once

#include "Graphics/Lighting.h"
#include "Graphics/Mesh.h"
#include "World/CityLayout.h"

#include <string>
#include <vector>

namespace av {

// One generated, loaded piece of the world. Geometry is stored relative to
// the chunk origin (keeps float precision high far from the world origin);
// colliders and lights are in world space.
struct Chunk {
    ChunkCoord coord;
    District district = District::Residential;
    std::string landmarkName;

    Mesh baseMesh;    // ground, roads, building shells: always drawn
    Mesh detailMesh;  // windows, markings, props, foliage: skipped at distance
    std::vector<AABB> colliders;
    std::vector<PointLight> lights;
    AABB worldBounds; // for frustum culling

    Vec3 origin() const { return chunkOrigin(coord); }
};

} // namespace av
