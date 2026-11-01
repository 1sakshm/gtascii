#include "Entities/VehicleVisual.h"

#include "Graphics/Renderer.h"

#include <cmath>
#include <string>

namespace av {

VehicleVisuals::VehicleVisuals(const MaterialLibrary& lib)
{
    for (int i = 0; i < kCarPaintCount; ++i) {
        paints_[i] = lib.require("car_paint_" + std::to_string(i));
    }
    policeBlack_ = lib.require("police_black");
    policeWhite_ = lib.require("police_white");
    sirenRed_ = lib.require("siren_red");
    sirenBlue_ = lib.require("siren_blue");
    sirenOff_ = lib.require("siren_off");
    glass_ = lib.require("car_glass");
    trim_ = lib.require("metal_dark");
    headlight_ = lib.require("headlight");
    taillight_ = lib.require("taillight");
    tire_ = lib.require("tire");
    hub_ = lib.require("hubcap");
    cabSign_ = lib.require("cab_sign");
    cabYellow_ = lib.require("car_paint_7");

    // Wheel: tyre with its axle along X, and a hub cross that shows rotation.
    MeshBuilder wb(wheel_);
    const VehicleSpec& ref = vehicleSpec(VehicleModel::Sedan);
    const float r = ref.wheelRadius;
    wb.addCylinder({0, -0.13f, 0}, 0.26f, r, r, 10, tire_, true, Mat4::rotationZ(kHalfPi));
    wb.addBox(AABB({-0.14f, -r * 0.75f, -0.05f}, {0.14f, r * 0.75f, 0.05f}), hub_);
    wb.addBox(AABB({-0.14f, -0.05f, -r * 0.75f}, {0.14f, 0.05f, r * 0.75f}), hub_);

    for (std::size_t m = 0; m < models_.size(); ++m) {
        build(static_cast<VehicleModel>(m), models_[m]);
    }
}

void VehicleVisuals::build(VehicleModel model, ModelMeshes& out) const
{
    const VehicleSpec& s = vehicleSpec(model);
    const float hw = s.width * 0.5f, hl = s.length * 0.5f, h = s.height;
    const float floor = s.wheelRadius * 0.55f;
    MeshBuilder paint(out.paint), fixed(out.fixed);

    const bool van = model == VehicleModel::Van;
    const bool coupe = model == VehicleModel::Coupe;
    const float beltline = van ? h * 0.45f : h * (coupe ? 0.52f : 0.55f);

    // Lower body with a slightly lower hood and trunk.
    paint.addBox(AABB({-hw, floor, -hl + 0.9f}, {hw, beltline, hl - 1.0f}), kNoMaterial);
    paint.addBox(AABB({-hw, floor, hl - 1.0f}, {hw, beltline - 0.08f, hl}), kNoMaterial);
    paint.addBox(AABB({-hw, floor, -hl}, {hw, beltline - 0.05f, -hl + 0.9f}), kNoMaterial);

    // Cabin: glass greenhouse with a painted roof.
    const float cabFront = van ? hl - 0.6f : (coupe ? 0.45f : 0.55f);
    const float cabBack = van ? -hl + 0.2f : (coupe ? -1.0f : -1.25f);
    const float roof = van ? h : h - 0.07f;
    fixed.addBox(AABB({-hw + 0.08f, beltline, cabBack}, {hw - 0.08f, roof - 0.06f, cabFront}), glass_);
    paint.addBox(AABB({-hw + 0.06f, roof - 0.06f, cabBack - 0.05f}, {hw - 0.06f, roof, cabFront + 0.05f}), kNoMaterial);
    if (van) {
        // Solid panel van: paint over most of the greenhouse behind the cab.
        paint.addBox(AABB({-hw + 0.04f, beltline, cabBack}, {hw - 0.04f, roof - 0.06f, cabFront - 1.4f}), kNoMaterial);
    }

    // Bumpers, lights.
    fixed.addBox(AABB({-hw - 0.02f, floor - 0.05f, hl - 0.05f}, {hw + 0.02f, floor + 0.2f, hl + 0.12f}), trim_);
    fixed.addBox(AABB({-hw - 0.02f, floor - 0.05f, -hl - 0.12f}, {hw + 0.02f, floor + 0.2f, -hl + 0.05f}), trim_);
    for (const float x : {-hw + 0.3f, hw - 0.3f}) {
        fixed.addBox(AABB({x - 0.22f, beltline - 0.28f, hl - 0.02f}, {x + 0.22f, beltline - 0.12f, hl + 0.04f}), headlight_);
        fixed.addBox(AABB({x - 0.22f, beltline - 0.25f, -hl - 0.04f}, {x + 0.22f, beltline - 0.1f, -hl + 0.02f}), taillight_);
    }

    if (model == VehicleModel::Cruiser) {
        // White doors on a black body, and a light bar.
        for (const float side : {-1.0f, 1.0f}) {
            const float x = side * (hw + 0.01f);
            fixed.addBox(AABB({std::min(x, x - side * 0.02f), floor + 0.1f, -1.2f}, {std::max(x, x - side * 0.02f), beltline - 0.02f, 0.9f}),
                         policeWhite_);
        }
        MeshBuilder(out.sirenLeft).addBox(AABB({0.05f, roof, -0.45f}, {0.65f, roof + 0.16f, -0.15f}), kNoMaterial);
        MeshBuilder(out.sirenRight).addBox(AABB({-0.65f, roof, -0.45f}, {-0.05f, roof + 0.16f, -0.15f}), kNoMaterial);
        out.hasSirens = true;
    }
    if (model == VehicleModel::Cab) {
        fixed.addBox(AABB({-0.35f, roof, -0.5f}, {0.35f, roof + 0.22f, -0.2f}), cabSign_);
    }
    out.paint.computeBounds();
    out.fixed.computeBounds();
}

void VehicleVisuals::draw(Renderer& renderer, VehicleModel model, int paint, const VehicleState& state,
                          const DrawFlags& flags) const
{
    const auto mi = static_cast<std::size_t>(model);
    if (mi >= models_.size()) {
        return;
    }
    const ModelMeshes& m = models_[mi];
    const VehicleSpec& s = vehicleSpec(model);
    const Mat4 world = vehicleMatrix(state);
    if (!renderer.isVisible(AABB({-s.width, -0.5f, -s.length}, {s.width, s.height + 0.5f, s.length}).transformed(world))) {
        return;
    }

    MaterialId paintMat = paints_[((paint % kCarPaintCount) + kCarPaintCount) % kCarPaintCount];
    if (model == VehicleModel::Cruiser) {
        paintMat = policeBlack_;
    } else if (model == VehicleModel::Cab) {
        paintMat = cabYellow_;
    }
    renderer.drawMesh(m.paint, world, paintMat);
    renderer.drawMesh(m.fixed, world);
    if (m.hasSirens) {
        const bool phase = std::fmod(flags.time * 3.0f, 1.0f) < 0.5f;
        renderer.drawMesh(m.sirenLeft, world, flags.sirens ? (phase ? sirenRed_ : sirenOff_) : sirenOff_);
        renderer.drawMesh(m.sirenRight, world, flags.sirens ? (phase ? sirenOff_ : sirenBlue_) : sirenOff_);
    }

    const float hw = s.width * 0.5f - 0.14f, hb = s.wheelBase * 0.5f;
    const float spin = state.wheelSpin;
    const Vec3 wheels[4] = {{hw, s.wheelRadius, hb}, {-hw, s.wheelRadius, hb}, {hw, s.wheelRadius, -hb}, {-hw, s.wheelRadius, -hb}};
    for (int i = 0; i < 4; ++i) {
        const float steer = i < 2 ? state.steer : 0.0f;
        renderer.drawMesh(wheel_, world * Mat4::translation(wheels[i]) * Mat4::rotationY(steer) * Mat4::rotationX(spin));
    }
}

} // namespace av
