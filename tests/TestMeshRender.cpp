#include "TestHarness.h"

#include "Core/Json.h"
#include "Graphics/Material.h"
#include "Graphics/Mesh.h"
#include "Graphics/ObjLoader.h"
#include "Graphics/Renderer.h"

#include <cmath>
#include <limits>

using namespace av;

namespace {

// Every triangle's winding must agree with its vertex normals (outward).
bool windingMatchesNormals(const Mesh& m)
{
    for (std::size_t t = 0; t < m.triangleCount(); ++t) {
        const Vertex& a = m.vertices[m.indices[t * 3]];
        const Vertex& b = m.vertices[m.indices[t * 3 + 1]];
        const Vertex& c = m.vertices[m.indices[t * 3 + 2]];
        const Vec3 face = cross(b.position - a.position, c.position - a.position);
        if (lengthSq(face) < 1e-14f) {
            continue; // degenerate (pole) triangles
        }
        if (dot(face, a.normal + b.normal + c.normal) <= 0.0f) {
            return false;
        }
    }
    return true;
}

struct TestScene {
    MaterialLibrary materials;
    MaterialId red, blue;
    TestScene()
    {
        Material r;
        r.name = "red";
        r.albedo = {1, 0, 0};
        red = materials.add(r);
        Material b;
        b.name = "blue";
        b.albedo = {0, 0, 1};
        blue = materials.add(b);
    }
};

Camera frontCamera()
{
    Camera cam;
    cam.position = {0, 0, 5};
    cam.target = {0, 0, 0};
    cam.fovY = radians(60.0f);
    return cam;
}

int countMaterial(const Renderer& r, MaterialId id)
{
    int n = 0;
    for (int y = 0; y < r.sampleHeight(); ++y) {
        for (int x = 0; x < r.sampleWidth(); ++x) {
            n += r.sampleMaterial(x, y) == id ? 1 : 0;
        }
    }
    return n;
}

} // namespace

TEST_CASE("Mesh: primitives have outward winding and bounds")
{
    Mesh m;
    MeshBuilder b(m);
    b.addBox(AABB({-1, -1, -1}, {1, 1, 1}), 1);
    CHECK_EQ(m.triangleCount(), std::size_t{12});
    CHECK(windingMatchesNormals(m));
    CHECK(m.bounds.min == Vec3(-1, -1, -1));

    const auto checkShape = [](auto addFn) {
        Mesh mesh;
        MeshBuilder mb(mesh);
        addFn(mb);
        return !mesh.empty() && windingMatchesNormals(mesh);
    };
    CHECK(checkShape([](MeshBuilder& mb) { mb.addEllipsoid({0, 0, 0}, {1, 2, 0.5f}, 8, 12, 1); }));
    CHECK(checkShape([](MeshBuilder& mb) { mb.addCylinder({0, 0, 0}, 2.0f, 1.0f, 0.5f, 10, 1); }));
    CHECK(checkShape([](MeshBuilder& mb) { mb.addCapsule(1.0f, 0.3f, 0.2f, 10, 1); }));
    CHECK(checkShape([](MeshBuilder& mb) { mb.addTorus({0, 0, 0}, 2.0f, 0.5f, 16, 8, 1); }));
    CHECK(checkShape([](MeshBuilder& mb) {
        mb.addBox(AABB({0, 0, 0}, {1, 1, 1}), 1, Mat4::rotationAxis({1, 1, 0}, 0.7f) * Mat4::scale({3, 1, 2}));
    }));
}

TEST_CASE("Mesh: box face mask and append with override")
{
    Mesh m;
    MeshBuilder b(m);
    b.addBox(AABB({0, 0, 0}, {1, 1, 1}), 3, Mat4::identity(), MeshBuilder::NoBottom);
    CHECK_EQ(m.triangleCount(), std::size_t{10});

    Mesh combined;
    MeshBuilder cb(combined);
    cb.append(m, Mat4::translation({5, 0, 0}), 9);
    CHECK_EQ(combined.triangleCount(), std::size_t{10});
    CHECK_EQ(combined.triangleMaterials[0], MaterialId{9});
    CHECK(nearlyEqual(combined.bounds.min.x, 5.0f));
}

TEST_CASE("ObjLoader: faces, normals, materials, negative indices")
{
    MaterialLibrary lib;
    Material red;
    red.name = "red";
    const MaterialId redId = lib.add(red);
    const char* obj = "# quad\n"
                      "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
                      "vn 0 0 1\n"
                      "usemtl red\n"
                      "f 1//1 2//1 3//1 4//1\n"
                      "usemtl unknown\n"
                      "f -4 -2 -1\n";
    const Mesh m = parseObj(obj, lib, 77);
    CHECK_EQ(m.triangleCount(), std::size_t{3});
    CHECK_EQ(m.triangleMaterials[0], redId);
    CHECK_EQ(m.triangleMaterials[1], redId);
    CHECK_EQ(m.triangleMaterials[2], MaterialId{77});
    CHECK(windingMatchesNormals(m));

    bool threw = false;
    try {
        parseObj("v 0 0 0\nf 1 2 3\n", lib, 1);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("MaterialLibrary: JSON loading, ramps, lookups")
{
    MaterialLibrary lib;
    lib.loadJson(parseJson(R"({
        "ramps": {"wavy": " -~"},
        "materials": {
            "water": {"color": [0, 128, 255], "water": true, "ramp": "wavy", "reflectivity": 0.5},
            "lamp": {"color": [255, 255, 255], "emissive": [255, 200, 100], "emissiveStrength": 2, "nightEmissive": true}
        }})"));
    const MaterialId water = lib.require("water");
    CHECK(lib.get(water).water);
    CHECK(lib.get(water).ramp != nullptr && lib.get(water).ramp->size() == 3);
    CHECK(nearlyEqual(lib.get(water).albedo.z, 1.0f));
    CHECK(nearlyEqual(lib.get(lib.require("lamp")).emissive.x, 2.0f));
    CHECK(lib.find("nope") == kNoMaterial);
    bool threw = false;
    try {
        lib.require("nope");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("Renderer: draws a facing quad, culls its back")
{
    TestScene s;
    Renderer r(s.materials, *findRamp("standard"));
    Mesh quad;
    MeshBuilder(quad).addQuad({-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}, s.red); // faces +Z

    r.beginFrame(40, 20, frontCamera(), SceneLighting{});
    r.drawMesh(quad, Mat4::identity());
    const int front = countMaterial(r, s.red);
    CHECK(front > 0);
    CHECK_EQ(r.stats().trianglesRasterized, std::size_t{2});

    r.beginFrame(40, 20, frontCamera(), SceneLighting{});
    r.drawMesh(quad, Mat4::rotationY(kPi)); // now faces away
    CHECK_EQ(countMaterial(r, s.red), 0);
    CHECK_EQ(r.stats().trianglesBackfaced, std::size_t{2});
}

TEST_CASE("Renderer: z-buffer keeps the nearer surface regardless of order")
{
    TestScene s;
    Renderer r(s.materials, *findRamp("standard"));
    Mesh nearQuad, farQuad;
    MeshBuilder(nearQuad).addQuad({-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}, s.red);
    MeshBuilder(farQuad).addQuad({-2, -2, -1}, {2, -2, -1}, {2, 2, -1}, {-2, 2, -1}, s.blue);

    for (int order = 0; order < 2; ++order) {
        r.beginFrame(40, 20, frontCamera(), SceneLighting{});
        if (order == 0) {
            r.drawMesh(nearQuad, Mat4::identity());
            r.drawMesh(farQuad, Mat4::identity());
        } else {
            r.drawMesh(farQuad, Mat4::identity());
            r.drawMesh(nearQuad, Mat4::identity());
        }
        const int cx = r.sampleWidth() / 2, cy = r.sampleHeight() / 2;
        CHECK_EQ(r.sampleMaterial(cx, cy), s.red);
        CHECK(nearlyEqual(r.sampleDepth(cx, cy), 4.0f, 0.05f)); // camera at z=5, quad at z=1
        CHECK(countMaterial(r, s.blue) > 0);                    // far quad visible around it
    }
}

TEST_CASE("Renderer: near-plane clipping of geometry through the camera")
{
    TestScene s;
    Renderer r(s.materials, *findRamp("standard"));
    // Floor quad extending from behind the camera to far in front.
    Mesh floor;
    MeshBuilder(floor).addQuad({-10, -1, 20}, {10, -1, 20}, {10, -1, -50}, {-10, -1, -50}, s.blue);
    r.beginFrame(40, 20, frontCamera(), SceneLighting{});
    r.drawMesh(floor, Mat4::identity());
    CHECK(r.stats().trianglesClipped > 0);
    // Lower half of the screen sees floor, upper half does not.
    CHECK_EQ(r.sampleMaterial(r.sampleWidth() / 2, r.sampleHeight() - 1), s.blue);
    CHECK_EQ(r.sampleMaterial(r.sampleWidth() / 2, 0), kNoMaterial);
    for (int y = 0; y < r.sampleHeight(); ++y) {
        for (int x = 0; x < r.sampleWidth(); ++x) {
            const float d = r.sampleDepth(x, y);
            CHECK(std::isinf(d) || (d > 0.0f && std::isfinite(d)));
        }
    }
}

TEST_CASE("Renderer: frustum culls off-screen meshes")
{
    TestScene s;
    Renderer r(s.materials, *findRamp("standard"));
    Mesh box;
    MeshBuilder(box).addBox(AABB({-1, -1, -1}, {1, 1, 1}), s.red);
    r.beginFrame(40, 20, frontCamera(), SceneLighting{});
    r.drawMesh(box, Mat4::translation({0, 0, 20})); // behind the camera
    r.drawMesh(box, Mat4::translation({100, 0, 0}));
    CHECK_EQ(r.stats().meshesCulled, std::size_t{2});
    CHECK_EQ(r.stats().trianglesSubmitted, std::size_t{0});
}

TEST_CASE("Renderer: resolve shades lit faces brighter and writes depth")
{
    TestScene s;
    Renderer r(s.materials, *findRamp("standard"));
    RendererSettings settings;
    settings.outlines = false;
    r.setSettings(settings);
    Mesh box;
    MeshBuilder(box).addBox(AABB({-1, -1, -1}, {1, 1, 1}), s.red);

    SceneLighting light;
    light.toSun = normalize(Vec3{0, 0, 1}); // from the camera side
    light.fogStart = 1000;
    light.fogEnd = 2000;
    Framebuffer fb;
    r.beginFrame(40, 20, frontCamera(), light);
    r.drawMesh(box, Mat4::identity());
    r.resolve(fb);
    CHECK_EQ(fb.width(), 40);
    const Cell& centre = fb.at(20, 10);
    CHECK(centre.material == s.red);
    CHECK(nearlyEqual(centre.depth, 4.0f, 0.1f));
    CHECK(centre.fg.r > 150 && centre.fg.g < 60);
    CHECK(centre.ch != U' ');

    // Same box lit from behind: front face only gets ambient light.
    light.toSun = normalize(Vec3{0, 0, -1});
    Framebuffer dark;
    r.beginFrame(40, 20, frontCamera(), light);
    r.drawMesh(box, Mat4::identity());
    r.resolve(dark);
    CHECK(dark.at(20, 10).fg.r < centre.fg.r);

    CHECK(fb.at(0, 0).material == kNoMaterial); // sky
    CHECK(std::isinf(fb.at(0, 0).depth));
}
