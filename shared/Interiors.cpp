#include "Interiors.h"

#include "Core/FileUtil.h"
#include "Core/Json.h"
#include "World/WorldCoords.h"

#include <cmath>
#include <stdexcept>

namespace av::proto {

namespace {

struct Builder {
    InteriorDef& d;

    void box(float x0, float y0, float z0, float x1, float y1, float z1, const char* material, bool solid = true)
    {
        const Vec3 o = d.origin;
        d.boxes.push_back({AABB{{o.x + std::min(x0, x1), o.y + std::min(y0, y1), o.z + std::min(z0, z1)},
                                {o.x + std::max(x0, x1), o.y + std::max(y0, y1), o.z + std::max(z0, z1)}},
                           material, solid});
    }
    Vec3 at(float x, float z) const { return d.origin + Vec3{x, 0.0f, z}; }
    void light(float x, float y, float z, Vec3 color, float range, float intensity = 1.0f)
    {
        d.lights.push_back({d.origin + Vec3{x, y, z}, color, range, intensity});
    }

    // Floor, ceiling and four walls (with the entrance in the -z wall).
    void shell(const char* floor, const char* walls, const char* ceiling)
    {
        const float hx = d.halfX, hz = d.halfZ, h = d.height, t = 0.3f;
        box(-hx - t, -0.5f, -hz - t, hx + t, 0.0f, hz + t, floor);
        box(-hx - t, h, -hz - t, hx + t, h + 0.4f, hz + t, ceiling);
        box(-hx - t, 0.0f, -hz - t, -hx, h, hz + t, walls);
        box(hx, 0.0f, -hz - t, hx + t, h, hz + t, walls);
        box(-hx, 0.0f, hz, hx, h, hz + t, walls);
        box(-hx, 0.0f, -hz - t, -1.0f, h, -hz, walls);
        box(1.0f, 0.0f, -hz - t, hx, h, -hz, walls);
        box(-1.0f, 2.2f, -hz - t, 1.0f, h, -hz, walls);
        box(-1.0f, 0.0f, -hz - 0.05f, 1.0f, 2.2f, -hz + 0.05f, "door_wood"); // the closed door you came through
        d.entry = at(0.0f, -hz + 2.8f); // clear of the exit marker
        d.entryYaw = 0.0f;
        d.exit = at(0.0f, -hz + 0.9f);
    }
};

void buildHome(InteriorDef& d)
{
    d.halfX = 7.0f;
    d.halfZ = 5.5f;
    d.height = 3.0f;
    Builder b{d};
    b.shell("wood_floor", "wall_beige", "wall_white");
    b.box(-6.8f, 0.0f, 2.5f, -3.6f, 0.55f, 5.4f, "bed_sheet");         // bed
    b.box(-6.8f, 0.55f, 4.7f, -3.6f, 1.2f, 5.4f, "door_wood");         // headboard
    b.box(2.0f, 0.0f, 4.3f, 6.0f, 0.5f, 5.3f, "sofa");                // sofa
    b.box(2.0f, 0.5f, 5.0f, 6.0f, 1.1f, 5.4f, "sofa");
    b.box(2.6f, 0.0f, 1.8f, 5.4f, 0.45f, 3.2f, "door_wood");          // coffee table
    b.box(6.6f, 0.6f, 0.5f, 6.9f, 1.9f, 3.9f, "screen_blue");         // TV on the wall
    b.box(-6.9f, 0.0f, -5.4f, -4.6f, 0.95f, -1.5f, "wall_white");     // kitchen counter
    b.box(-6.9f, 1.6f, -5.4f, -6.4f, 2.4f, -1.5f, "door_wood");       // cupboards
    b.box(-0.6f, 0.0f, 0.0f, 0.6f, 0.8f, 1.2f, "door_wood");          // dining table
    b.light(0.0f, 2.6f, 0.0f, {1.0f, 0.85f, 0.6f}, 14.0f, 1.2f);
    b.light(4.0f, 2.4f, 3.5f, {0.6f, 0.75f, 1.0f}, 8.0f, 0.6f);
    d.bed = b.at(-3.0f, 4.0f);
}

void buildOffice(InteriorDef& d)
{
    d.halfX = 11.0f;
    d.halfZ = 8.0f;
    d.height = 3.4f;
    Builder b{d};
    b.shell("office_carpet", "wall_grey", "wall_white");
    // Floor-to-ceiling windows along the back wall.
    b.box(-10.8f, 0.6f, 7.9f, 10.8f, 3.2f, 8.0f, "glass_blue", false);
    // Rows of desks with terminals.
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 4; ++col) {
            const float x = -8.0f + col * 4.5f, z = -3.0f + row * 4.5f;
            b.box(x - 1.2f, 0.0f, z - 0.6f, x + 1.2f, 0.75f, z + 0.6f, "metal");
            b.box(x - 0.45f, 0.75f, z + 0.1f, x + 0.45f, 1.35f, z + 0.18f, "screen_blue");
            d.computers.push_back(b.at(x, z - 1.2f));
        }
    }
    // The boss's desk.
    b.box(-2.2f, 0.0f, 5.6f, 2.2f, 0.8f, 6.8f, "door_wood");
    b.box(-0.5f, 0.8f, 6.0f, 0.5f, 1.4f, 6.1f, "screen_blue");
    b.box(-10.9f, 0.0f, -7.9f, -8.0f, 2.2f, -6.5f, "wall_blue"); // filing cabinets
    b.light(-5.0f, 3.0f, 0.0f, {0.95f, 0.97f, 1.0f}, 14.0f, 1.0f);
    b.light(5.0f, 3.0f, 0.0f, {0.95f, 0.97f, 1.0f}, 14.0f, 1.0f);
}

void buildClub(InteriorDef& d)
{
    d.halfX = 11.0f;
    d.halfZ = 9.0f;
    d.height = 5.0f;
    Builder b{d};
    b.shell("concrete_dark", "wall_blue", "concrete_dark");
    // Checkerboard dance floor, slightly raised.
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            const float x = -6.0f + i * 2.0f, z = -2.0f + j * 2.0f;
            b.box(x, 0.0f, z, x + 2.0f, 0.08f, z + 2.0f, (i + j) % 2 ? "dance_floor_a" : "dance_floor_b", false);
        }
    }
    for (int k = 0; k < 8; ++k) d.dancers.push_back(b.at(-4.5f + (k % 4) * 3.0f, 0.0f + (k / 4) * 4.0f + (k % 2) * 0.8f));
    b.box(-4.0f, 0.0f, 8.0f, 4.0f, 1.2f, 8.9f, "metal_dark");         // DJ booth
    b.box(-1.0f, 1.2f, 8.4f, 1.0f, 1.5f, 8.7f, "neon_cyan");
    b.box(-10.9f, 0.0f, -6.0f, -9.6f, 1.1f, 4.0f, "door_wood");       // bar
    b.box(-10.9f, 1.8f, -6.0f, -10.7f, 2.0f, 4.0f, "neon_pink", false);
    b.box(-10.95f, 3.6f, -8.8f, -10.85f, 3.8f, 8.8f, "neon_pink", false); // neon strips
    b.box(10.85f, 3.6f, -8.8f, 10.95f, 3.8f, 8.8f, "neon_cyan", false);
    b.box(-6.0f, 4.8f, -2.0f, 6.0f, 4.95f, 10.0f, "metal_dark", false); // lighting rig
    b.light(-3.0f, 4.0f, 2.0f, {1.0f, 0.2f, 0.8f}, 12.0f, 2.0f);
    b.light(3.0f, 4.0f, 6.0f, {0.2f, 0.8f, 1.0f}, 12.0f, 2.0f);
    b.light(-8.0f, 3.0f, -2.0f, {1.0f, 0.6f, 0.3f}, 8.0f, 1.0f);
}

void buildCasino(InteriorDef& d)
{
    d.halfX = 12.0f;
    d.halfZ = 10.0f;
    d.height = 5.0f;
    Builder b{d};
    b.shell("carpet_red", "wall_beige", "wall_white");
    // Two rows of slot machines along the side walls.
    for (int side = -1; side <= 1; side += 2) {
        for (int k = 0; k < 5; ++k) {
            const float z = -6.0f + k * 3.0f;
            const float x = side * 11.2f;
            b.box(x - 0.6f, 0.0f, z - 0.6f, x + 0.6f, 1.6f, z + 0.6f, "gold");
            b.box(x - side * 0.62f, 0.9f, z - 0.4f, x - side * 0.6f, 1.4f, z + 0.4f, "slot_screen", false);
            d.slots.push_back(b.at(x - side * 1.3f, z));
        }
    }
    // Card and roulette tables.
    for (int k = 0; k < 3; ++k) {
        const float x = -5.0f + k * 5.0f;
        b.box(x - 1.4f, 0.0f, 1.0f, x + 1.4f, 0.85f, 3.0f, "felt_green");
        b.box(x - 1.5f, 0.85f, 0.9f, x + 1.5f, 0.9f, 3.1f, "gold", false);
    }
    b.box(-4.0f, 0.0f, 8.6f, 4.0f, 1.2f, 9.9f, "gold");               // cashier
    b.box(-4.0f, 1.2f, 9.6f, 4.0f, 3.2f, 9.9f, "glass_teal", false);
    b.box(-12.0f, 4.4f, -10.0f, 12.0f, 4.5f, 10.0f, "gold", false);    // ceiling trim
    b.light(-6.0f, 4.0f, 0.0f, {1.0f, 0.85f, 0.5f}, 14.0f, 1.3f);
    b.light(6.0f, 4.0f, 0.0f, {1.0f, 0.85f, 0.5f}, 14.0f, 1.3f);
    b.light(0.0f, 4.0f, 6.0f, {1.0f, 0.4f, 0.6f}, 10.0f, 1.0f);
}

float facingYaw(const std::string& f)
{
    if (f == "north") return 0.0f;
    if (f == "east") return 1.5707963f;
    if (f == "west") return -1.5707963f;
    return 3.1415927f; // south
}

} // namespace

const char* toString(InteriorKind k)
{
    switch (k) {
    case InteriorKind::Home: return "home";
    case InteriorKind::Office: return "office";
    case InteriorKind::Club: return "club";
    case InteriorKind::Casino: return "casino";
    }
    return "?";
}

std::vector<InteriorDef> loadInteriors(const std::filesystem::path& file)
{
    std::vector<InteriorDef> out;
    const auto text = readTextFile(file);
    if (!text) return out;
    JsonValue doc;
    try {
        doc = parseJson(*text);
    } catch (const std::exception& e) {
        throw std::runtime_error(file.string() + ": " + e.what());
    }
    const JsonValue* list = doc.find("interiors");
    if (!list || !list->isArray()) return out;
    for (const JsonValue& v : list->asArray()) {
        InteriorDef d;
        const std::string kind = v.stringOr("kind", "home");
        d.kind = kind == "office" ? InteriorKind::Office : kind == "club" ? InteriorKind::Club : kind == "casino" ? InteriorKind::Casino : InteriorKind::Home;
        d.name = v.stringOr("name", "Building");
        const JsonValue* chunk = v.find("chunk");
        const JsonValue* door = v.find("door");
        if (!chunk || !door || chunk->size() < 2 || door->size() < 2) continue;
        const float x = static_cast<float>((*chunk)[0].asNumber() * kChunkSize + (*door)[0].asNumber());
        const float z = static_cast<float>((*chunk)[1].asNumber() * kChunkSize + (*door)[1].asNumber());
        d.door = {x, 0.15f, z};
        d.doorYaw = facingYaw(v.stringOr("facing", "south"));
        // Each room floats above its building, far enough apart not to overlap.
        d.origin = {x, kInteriorFloorY + static_cast<float>(out.size()) * 12.0f, z};
        switch (d.kind) {
        case InteriorKind::Home: buildHome(d); break;
        case InteriorKind::Office: buildOffice(d); break;
        case InteriorKind::Club: buildClub(d); break;
        case InteriorKind::Casino: buildCasino(d); break;
        }
        out.push_back(std::move(d));
    }
    return out;
}

const InteriorDef* findInterior(const std::vector<InteriorDef>& interiors, const Vec3& p)
{
    if (p.y < kInteriorFloorY - 10.0f) return nullptr;
    for (const InteriorDef& d : interiors) {
        if (d.contains(p)) return &d;
    }
    return nullptr;
}

std::vector<AABB> staticExtraColliders(const std::vector<InteriorDef>& interiors)
{
    std::vector<AABB> out;
    for (const InteriorDef& d : interiors) {
        for (const InteriorBox& b : d.boxes) {
            if (b.solid) out.push_back(b.box);
        }
    }
    // The edge of the world, far out at sea.
    out.push_back(AABB{{kWorldEdgeX, -50.0f, -100000.0f}, {kWorldEdgeX + 20.0f, 1000.0f, 100000.0f}});
    return out;
}

} // namespace av::proto
