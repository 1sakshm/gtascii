// Phase 5: ECS registry, vehicle physics, navigation graphs, AI.
#include "TestHarness.h"

#include "AI/Navigation.h"
#include "AI/PedestrianBrain.h"
#include "AI/TrafficDriver.h"
#include "Entities/Registry.h"
#include "Graphics/Material.h"
#include "Physics/CollisionWorld.h"
#include "Physics/Vehicle.h"
#include "World/CityGenerator.h"
#include "World/CityLayout.h"

#include <cmath>
#include <filesystem>
#include <set>

using namespace av;

namespace {
struct Pos {
    float x, z;
};
struct Tag {
    int value = 0;
};

// Terrain from the real city layout, no colliders.
class LayoutWorld : public CollisionWorld {
public:
    explicit LayoutWorld(const CityLayout& l) : layout_(l) {}
    float groundHeight(float x, float z) const override { return layout_.groundHeight(x, z); }
    void gatherColliders(const AABB&, std::vector<AABB>&) const override {}

private:
    const CityLayout& layout_;
};
} // namespace

TEST_CASE("Registry: create, components, queries, destroy")
{
    Registry r;
    const Entity a = r.create(), b = r.create();
    CHECK(a != kNullEntity && a != b);
    r.add<Pos>(a, {1, 2});
    r.add<Pos>(b, {3, 4});
    r.add<Tag>(b, {7});
    CHECK(r.has<Pos>(a) && !r.has<Tag>(a));
    CHECK_EQ(r.get<Tag>(b)->value, 7);
    CHECK_EQ(r.count<Pos>(), std::size_t{2});

    Pos* stable = r.get<Pos>(a);
    for (int i = 0; i < 1000; ++i) {
        r.add<Pos>(r.create(), {0, 0}); // growth must not move existing components
    }
    CHECK(stable == r.get<Pos>(a) && stable->x == 1.0f);

    int withTag = 0;
    r.each<Tag>([&](Entity e, Tag& t) {
        CHECK(e == b);
        withTag += t.value;
    });
    CHECK_EQ(withTag, 7);

    r.destroy(b);
    CHECK(!r.valid(b));
    CHECK(r.get<Pos>(b) == nullptr && r.get<Tag>(b) == nullptr);
    r.remove<Pos>(a);
    CHECK(!r.has<Pos>(a) && r.valid(a));
}

TEST_CASE("Vehicle: accelerates along its heading, brakes, reverses")
{
    BoxCollisionWorld world;
    const VehicleSpec& spec = vehicleSpec(VehicleModel::Sedan);
    VehicleState s;
    VehicleControls go;
    go.throttle = 1.0f;
    for (int i = 0; i < 180; ++i) stepVehicle(s, spec, go, 1.0f / 60.0f, world);
    CHECK(s.speed > 12.0f && s.speed <= spec.maxSpeed);
    CHECK(s.position.z > 15.0f && std::fabs(s.position.x) < 0.01f); // yaw 0 drives along +Z
    CHECK(std::fabs(s.wheelSpin) > 0.0f);

    VehicleControls brake;
    brake.throttle = -1.0f;
    for (int i = 0; i < 240 && s.speed > 0.0f; ++i) stepVehicle(s, spec, brake, 1.0f / 60.0f, world);
    CHECK(s.speed <= 0.5f);
    for (int i = 0; i < 120; ++i) stepVehicle(s, spec, brake, 1.0f / 60.0f, world);
    CHECK(s.speed < -1.0f && s.speed >= -spec.maxReverse - 0.01f);

    // Coasting comes to a stop and never reverses by itself.
    VehicleState coast;
    coast.speed = 5.0f;
    for (int i = 0; i < 1200; ++i) stepVehicle(coast, spec, {}, 1.0f / 60.0f, world);
    CHECK_EQ(coast.speed, 0.0f);
}

TEST_CASE("Vehicle: positive steer turns left (+X when facing +Z)")
{
    BoxCollisionWorld world;
    const VehicleSpec& spec = vehicleSpec(VehicleModel::Coupe);
    VehicleState s;
    s.speed = 8.0f;
    VehicleControls c;
    c.throttle = 0.3f;
    c.steer = 1.0f;
    for (int i = 0; i < 60; ++i) stepVehicle(s, spec, c, 1.0f / 60.0f, world);
    CHECK(s.yaw > 0.3f);
    CHECK(s.position.x > 0.5f);
    CHECK(s.steer > 0.0f && s.steer <= spec.maxSteer);
}

TEST_CASE("Vehicle: walls stop the car and cause damage; water drowns it")
{
    BoxCollisionWorld world;
    world.boxes.push_back(AABB({-10, 0, 10}, {10, 3, 11}));
    const VehicleSpec& spec = vehicleSpec(VehicleModel::Sedan);
    VehicleState s;
    s.speed = 20.0f;
    float maxImpact = 0.0f;
    for (int i = 0; i < 120; ++i) {
        stepVehicle(s, spec, {}, 1.0f / 60.0f, world);
        maxImpact = std::max(maxImpact, s.lastImpact);
    }
    CHECK(s.position.z + spec.length * 0.5f <= 10.0f + 0.3f);
    CHECK(maxImpact > 10.0f);
    CHECK(s.damage > 0.1f);

    world.groundY = -2.0f;
    VehicleState sunk;
    stepVehicle(sunk, spec, {}, 1.0f / 60.0f, world);
    CHECK(sunk.drowned);
}

TEST_CASE("PedestrianNav: nodes on sidewalks, symmetric connected graph")
{
    const CityLayout layout(1986);
    const PedestrianNav nav(layout);
    std::vector<NavNode> neighbors, back;
    int checked = 0;
    for (int cx = -4; cx <= 3; ++cx) {
        for (int cz = -3; cz <= 5; ++cz) {
            for (int k = 0; k < 4; ++k) {
                const NavNode n{{cx, cz}, k};
                if (!nav.valid(n)) continue;
                ++checked;
                const Vec3 p = nav.position(n);
                const Vec3 local = p - chunkOrigin(n.chunk);
                CHECK(layout.surface(n.chunk, local.x, local.z) == Surface::Sidewalk);
                neighbors.clear();
                nav.neighbors(n, neighbors);
                CHECK(neighbors.size() >= 2);
                for (const NavNode& m : neighbors) {
                    back.clear();
                    nav.neighbors(m, back);
                    CHECK(std::find(back.begin(), back.end(), n) != back.end());
                    CHECK(distance(p, nav.position(m)) < 60.0f); // edges are short hops
                }
            }
        }
    }
    CHECK(checked > 200);
    CHECK(!nav.valid({{4, 0}, 0})); // nobody walks on the sea

    // Flood fill: the whole city block network is one connected graph.
    std::set<std::tuple<int, int, int>> seen;
    std::vector<NavNode> stack{{{0, 0}, 0}};
    while (!stack.empty() && seen.size() < 400) {
        const NavNode n = stack.back();
        stack.pop_back();
        if (!seen.insert({n.chunk.x, n.chunk.z, n.corner}).second) continue;
        if (std::abs(n.chunk.x) > 4 || std::abs(n.chunk.z) > 4) continue;
        neighbors.clear();
        nav.neighbors(n, neighbors);
        stack.insert(stack.end(), neighbors.begin(), neighbors.end());
    }
    CHECK(seen.size() > 150);
}

TEST_CASE("RoadNetwork: lanes are on asphalt, drive on the right")
{
    const CityLayout layout(1986);
    const RoadNetwork roads(layout);
    for (int k = -3; k <= 3; ++k) {
        for (int m = -3; m <= 3; ++m) {
            const Intersection i{k, m};
            for (const Heading h : {Heading::North, Heading::East, Heading::South, Heading::West}) {
                if (!roads.hasRoad(i, h)) continue;
                for (float t : {0.0f, 0.5f, 1.0f}) {
                    const Vec3 p = lerp(roads.laneStart(i, h), roads.laneEnd(i, h), t);
                    const ChunkCoord c = worldToChunk(p);
                    const Vec3 local = p - chunkOrigin(c);
                    CHECK(layout.surface(c, local.x, local.z) == Surface::Road);
                }
            }
        }
    }
    CHECK(!roads.hasRoad({3, 0}, Heading::East)); // the coast road has no cross street into the sea
    // Northbound traffic keeps to the east (+X) side of the centre line.
    CHECK(laneOffset(Heading::North).x > 0.0f);
    CHECK(laneOffset(Heading::East).z > 0.0f);
}

TEST_CASE("TrafficDriver: follows lanes through intersections")
{
    const CityLayout layout(1986);
    const RoadNetwork roads(layout);
    const LayoutWorld world(layout);
    const VehicleSpec& spec = vehicleSpec(VehicleModel::Sedan);
    Rng rng(42);
    TrafficDriver driver;
    driver.start(roads, {0, 0}, Heading::South, rng);
    VehicleState car;
    car.position = roads.laneStart({0, 0}, Heading::South);
    car.yaw = 0.0f; // south = +Z
    int intersections = 0;
    Intersection last = driver.nextIntersection();
    int offRoad = 0;
    for (int i = 0; i < 60 * 90; ++i) {
        const VehicleControls c = driver.update(roads, car, spec, {}, 1.0f / 60.0f, rng);
        stepVehicle(car, spec, c, 1.0f / 60.0f, world);
        if (!(driver.nextIntersection() == last)) {
            ++intersections;
            last = driver.nextIntersection();
        }
        const ChunkCoord cc = worldToChunk(car.position);
        const Vec3 local = car.position - chunkOrigin(cc);
        offRoad += layout.surface(cc, local.x, local.z) == Surface::Road ? 0 : 1;
    }
    CHECK(intersections >= 8);          // ~90 s at city speed crosses many blocks
    CHECK(offRoad < 60 * 3);            // under 3 s total with a wheel off the asphalt
    CHECK(car.damage == 0.0f);
}

TEST_CASE("PedestrianBrain: walks the graph and flees from threats")
{
    const CityLayout layout(1986);
    const PedestrianNav nav(layout);
    Rng rng(3);
    PedestrianBrain brain;
    const Vec3 start = nav.position({{0, 0}, 0}) + Vec3{0, 0, 10};
    brain.start(nav, start, rng);
    bool moved = false;
    for (int i = 0; i < 300 && !moved; ++i) {
        const PedCommand cmd = brain.update(nav, start, 1.0f / 30.0f, rng);
        moved = length(cmd.moveDirection) > 0.5f;
    }
    CHECK(moved);

    brain.threaten(start + Vec3{1, 0, 0}, 5.0f);
    CHECK(brain.state() == PedState::Fleeing);
    const PedCommand flee = brain.update(nav, start, 1.0f / 30.0f, rng);
    CHECK(flee.run);
    CHECK(length(flee.moveDirection) > 0.5f);
}
