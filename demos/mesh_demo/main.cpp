// GRAND THEFT ASCII - Phase 2 3D renderer demo.
//
// Scene 1: a rotating cube with a different material per face.
// Scene 2: several intersecting meshes (torus, ellipsoid, box, cylinder,
//          capsule) on a floor; the z-buffer resolves their intersections.
// Scene 3: a model loaded from assets/models/newsbox.obj.
// Scene 4: the procedural vehicle models (wheels, steering, police lights).
#include "Core/FileUtil.h"
#include "Core/FrameClock.h"
#include "Graphics/AnsiPresenter.h"
#include "Entities/VehicleVisual.h"
#include "Graphics/Draw2D.h"
#include "Graphics/Mesh.h"
#include "Graphics/ObjLoader.h"
#include "Graphics/Renderer.h"
#include "Math/Quat.h"
#include "Platform/Terminal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

using namespace av;

namespace {

struct Scene {
    struct Item {
        const Mesh* mesh;
        Vec3 position;
        Vec3 spinAxis;
        float spinSpeed;
    };
    std::vector<Item> items;
    const char* name;
    Vec3 target{0, 1.2f, 0};
    float distance = 6.0f;
};

int run(int argc, char** argv)
{
    std::uint64_t maxFrames = 0;
    int startScene = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) {
            maxFrames = std::strtoull(argv[++i], nullptr, 10);
        } else if (a == "--scene" && i + 1 < argc) {
            startScene = std::atoi(argv[++i]) - 1;
        } else {
            std::fprintf(stderr, "usage: mesh_demo [--scene 1-3] [--frames N]\n");
            return 2;
        }
    }

    const auto root = findDataRoot(argv[0], "assets/materials/materials.json");
    if (!root) {
        std::fprintf(stderr, "error: cannot find assets/ (run from the repository or build directory)\n");
        return 1;
    }
    MaterialLibrary materials;
    materials.loadFile(*root / "assets/materials/materials.json");

    // Meshes.
    Mesh cube;
    {
        MeshBuilder b(cube);
        const char* faces[6] = {"demo_red", "demo_green", "demo_blue", "demo_yellow", "demo_magenta", "demo_cyan"};
        const unsigned bits[6] = {MeshBuilder::FacePosX, MeshBuilder::FaceNegX, MeshBuilder::FacePosY,
                                  MeshBuilder::FaceNegY, MeshBuilder::FacePosZ, MeshBuilder::FaceNegZ};
        for (int f = 0; f < 6; ++f) {
            b.addBox(AABB({-1, -1, -1}, {1, 1, 1}), materials.require(faces[f]), Mat4::identity(), bits[f]);
        }
    }
    Mesh floor;
    MeshBuilder(floor).addBox(AABB({-8, -0.2f, -8}, {8, 0, 8}), materials.require("demo_floor"));
    Mesh torus, ellipsoid, box, cylinder, capsule;
    MeshBuilder(torus).addTorus({0, 0, 0}, 1.3f, 0.35f, 28, 12, materials.require("demo_magenta"));
    MeshBuilder(ellipsoid).addEllipsoid({0, 0, 0}, {1.0f, 1.0f, 1.0f}, 14, 20, materials.require("demo_cyan"));
    MeshBuilder(box).addBox(AABB({-0.8f, -0.8f, -0.8f}, {0.8f, 0.8f, 0.8f}), materials.require("demo_yellow"));
    MeshBuilder(cylinder).addCylinder({0, -1.2f, 0}, 2.4f, 0.45f, 0.45f, 18, materials.require("demo_green"));
    MeshBuilder(capsule).addCapsule(1.6f, 0.4f, 0.25f, 16, materials.require("demo_red"),
                                    Mat4::translation({0, 0.8f, 0}));
    const Mesh newsbox = loadObjFile(*root / "assets/models/newsbox.obj", materials, materials.require("demo_white"));

    std::vector<Scene> scenes(4);
    const VehicleVisuals vehicles(materials);
    scenes[0] = {{{&cube, {0, 1.4f, 0}, {0.3f, 1.0f, 0.2f}, 0.8f}}, "Rotating cube", {0, 1.4f, 0}, 5.5f};
    scenes[1] = {{{&torus, {0, 1.5f, 0}, {1, 0.3f, 0}, 0.7f},
                  {&ellipsoid, {0.6f, 1.5f, 0}, {0, 1, 0}, 0.0f},
                  {&box, {-0.8f, 1.2f, 0.3f}, {0.5f, 1, 0.2f}, 0.9f},
                  {&cylinder, {0.2f, 1.4f, -0.5f}, {0, 0, 1}, 0.5f},
                  {&capsule, {-0.3f, 1.4f, 0.9f}, {1, 0, 0.4f}, 1.1f},
                  {&floor, {0, 0, 0}, {0, 1, 0}, 0.0f}},
                 "Intersecting meshes", {0, 1.3f, 0}, 6.5f};
    scenes[2] = {{{&newsbox, {0, 0, 0}, {0, 1, 0}, 0.6f}, {&floor, {0, 0, 0}, {0, 1, 0}, 0.0f}}, "OBJ model", {0, 0.55f, 0}, 2.4f};
    scenes[3] = {{{&floor, {0, 0, 0}, {0, 1, 0}, 0.0f}}, "Vehicles", {0, 0.8f, 0}, 13.0f};
    int current = std::clamp(startScene, 0, 3);

    Terminal terminal;
    AnsiPresenter presenter;
    Renderer renderer(materials, *findRamp("standard"));
    FrameClock clock(60.0);
    Framebuffer fb;
    std::vector<InputEvent> events;

    float yaw = 0.6f, pitch = 0.35f, zoom = 1.0f;
    float hour = 16.5f;
    double t = 0.0;
    bool running = true;
    while (running) {
        const double dt = clock.beginFrame();
        t += dt;
        events.clear();
        terminal.pollInput(events);
        for (const InputEvent& ev : events) {
            if (ev.type == InputEvent::Type::Interrupt) {
                running = false;
            }
            if (ev.type != InputEvent::Type::Key || !ev.key.down) {
                continue;
            }
            const KeyEvent& k = ev.key;
            if (k.key == Key::Escape || k.ch == U'q') running = false;
            else if (k.key == Key::Left) yaw -= 0.15f;
            else if (k.key == Key::Right) yaw += 0.15f;
            else if (k.key == Key::Up) pitch = std::min(1.4f, pitch + 0.1f);
            else if (k.key == Key::Down) pitch = std::max(-0.2f, pitch - 0.1f);
            else if (k.ch == U'+' || k.ch == U'=') zoom = std::max(0.4f, zoom - 0.1f);
            else if (k.ch == U'-') zoom = std::min(3.0f, zoom + 0.1f);
            else if (k.ch == U't') hour = std::fmod(hour + 1.0f, 24.0f);
            else if (k.ch >= U'1' && k.ch <= U'4') current = static_cast<int>(k.ch - U'1');
        }

        const TerminalSize ts = terminal.size();
        Camera cam;
        const Scene& scene = scenes[static_cast<std::size_t>(current)];
        const float dist = scene.distance * zoom;
        cam.target = scene.target;
        cam.position = cam.target + Vec3{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)} * dist;
        SceneLighting light = SceneLighting::timeOfDay(hour);
        light.time = static_cast<float>(t);
        light.fogStart = 40.0f;
        light.fogEnd = 120.0f;
        light.pointLights.push_back({{2.5f, 2.5f, 2.5f}, {1.0f, 0.4f, 0.8f}, 8.0f, 1.5f});

        renderer.beginFrame(ts.cols, ts.rows, cam, light);
        for (const Scene::Item& item : scenes[static_cast<std::size_t>(current)].items) {
            const Mat4 model = Mat4::translation(item.position) *
                               Mat4::rotationAxis(item.spinAxis, static_cast<float>(t) * item.spinSpeed);
            renderer.drawMesh(*item.mesh, model);
        }
        if (current == 3) {
            // All car models side by side, wheels turning, police lights on.
            for (int m = 0; m < static_cast<int>(VehicleModel::Count); ++m) {
                VehicleState vs;
                vs.position = {0.0f, 0.0f, static_cast<float>(m - 2) * 3.0f};
                vs.yaw = kHalfPi; // a row of cars, side-on
                vs.wheelSpin = static_cast<float>(t) * 4.0f;
                vs.steer = 0.4f * std::sin(static_cast<float>(t));
                vehicles.draw(renderer, static_cast<VehicleModel>(m), m * 2, vs, {true, static_cast<float>(t)});
            }
        }
        renderer.resolve(fb);

        char status[200];
        const RenderStats& rs = renderer.stats();
        std::snprintf(status, sizeof status,
                      " [%d/4] %s | %.0f fps | tris %zu in, %zu drawn, %zu backface, %zu clipped | %02d:00 ",
                      current + 1, scenes[static_cast<std::size_t>(current)].name, clock.smoothedFps(),
                      rs.trianglesSubmitted, rs.trianglesRasterized, rs.trianglesBackfaced, rs.trianglesClipped,
                      static_cast<int>(hour));
        draw::text(fb, 0, 0, status, {240, 240, 240}, Color{15, 15, 25});
        draw::text(fb, 0, fb.height() - 1, " 1-4 scene  arrows orbit  +/- zoom  T time of day  Q quit ",
                   {170, 170, 190}, Color{15, 15, 25});

        const std::string& out = presenter.encode(fb);
        if (!out.empty()) {
            terminal.write(out);
        }
        clock.endFrame();
        if (maxFrames && clock.frameCount() >= maxFrames) {
            running = false;
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
}
