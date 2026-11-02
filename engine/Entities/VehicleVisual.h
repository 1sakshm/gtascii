#pragma once

#include "Graphics/Material.h"
#include "Graphics/Mesh.h"
#include "Physics/Vehicle.h"

#include <array>

namespace av {

class Renderer;

inline constexpr int kCarPaintCount = 8;

// Procedural car models (one set of meshes per model, shared by all cars)
// and their drawing: painted body, glass, bumpers, lights, four wheels that
// spin with distance travelled and steer, and police light bars.
class VehicleVisuals {
public:
    explicit VehicleVisuals(const MaterialLibrary& materials);

    struct DrawFlags {
        bool sirens = false; // flashing light bar (police)
        float time = 0.0f;
    };

    void draw(Renderer& renderer, VehicleModel model, int paint, const VehicleState& state, const DrawFlags& flags) const;

private:
    struct ModelMeshes {
        Mesh paint;  // drawn with the car's paint material
        Mesh fixed;  // glass, trim, lights (own materials)
        Mesh sirenLeft, sirenRight;
        Mesh rotor, tailRotor; // helicopter
        bool hasSirens = false;
    };
    void build(VehicleModel model, ModelMeshes& out) const;
    void buildHelicopter(ModelMeshes& out) const;

    std::array<ModelMeshes, static_cast<std::size_t>(VehicleModel::Count)> models_;
    Mesh wheel_;
    MaterialId paints_[kCarPaintCount];
    MaterialId policeBlack_, sirenRed_, sirenBlue_, sirenOff_;
    MaterialId glass_, trim_, headlight_, taillight_, tire_, hub_, policeWhite_, cabSign_, cabYellow_;
    MaterialId heliBody_, rotorMat_, gold_;
};

} // namespace av
