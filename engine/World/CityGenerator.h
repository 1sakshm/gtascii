#pragma once

#include "Graphics/Material.h"
#include "Graphics/Mesh.h"
#include "World/Chunk.h"
#include "World/CityLayout.h"

#include <filesystem>
#include <memory>

namespace av {

// Material ids used by the generator, resolved once from the library so a
// missing definition fails at startup rather than mid-game.
struct CityMaterials {
    explicit CityMaterials(const MaterialLibrary& lib);

    MaterialId asphalt, laneYellow, laneWhite, sidewalk, curb, grass, sand, wetSand, plaza, water, poolWater,
        industrialGround;
    MaterialId concrete, concreteDark, glass[3], mullion, windowDark, windowLit, trimWhite, roofTile, roofGrey,
        doorWood, awningRed, awningTeal, neonPink, neonCyan, warehouse, warehouseRust, rollupDoor, containers[4],
        lighthouseRed, metal, metalDark, craneYellow, policeBlue, crossRed;
    MaterialId pastels[6], walls[4];
    MaterialId pole, lampGlow, beacon, benchWood, umbrellas[2], towel, lifeguard;
    MaterialId palmTrunk, palmLeaf, treeTrunk, treeLeaves, hedge;
};

// Procedurally builds chunk content from the city layout. Deterministic:
// the same coordinate always yields the same chunk.
class CityGenerator {
public:
    CityGenerator(const CityLayout& layout, const MaterialLibrary& materials, Mesh newsboxModel);

    std::unique_ptr<Chunk> generate(const ChunkCoord& coord) const;

    const CityLayout& layout() const noexcept { return layout_; }

private:
    const CityLayout& layout_;
    CityMaterials mat_;
    Mesh newsbox_;
};

} // namespace av
