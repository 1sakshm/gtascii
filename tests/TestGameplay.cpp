// Phase 7: server-side gameplay driven through the public ServerWorld API
// (the same calls the network layer makes).
#include "TestHarness.h"

#include "ServerWorld.h"

#include <cmath>
#include <filesystem>

using namespace av;
using namespace av::proto;
using namespace server;

namespace {

struct World {
    ServerConfig config;
    std::unique_ptr<ServerWorld> world;
    Entity player = kNullEntity;
    std::uint32_t seq = 0;

    explicit World(int peds = 0, int cars = 0)
    {
        config.savePath = (std::filesystem::temp_directory_path() / "gtascii_test_gameplay.json").string();
        config.pedestrians = peds;
        config.trafficCars = cars;
        config.dayLengthMinutes = 0.0f;
        std::filesystem::remove(config.savePath);
        world = std::make_unique<ServerWorld>(config, AV_SOURCE_DIR);
        std::string error;
        player = world->joinPlayer("Tester", "", 5u, 1u, error);
    }
    ~World()
    {
        world.reset();
        std::filesystem::remove(config.savePath);
    }

    CharacterComponent& me() { return *world->registry().get<CharacterComponent>(player); }
    void place(const Vec3& p) { me().body.motor.setState(p, {}, true); }
    void input(std::uint16_t buttons, float moveX = 0, float moveZ = 0, float throttle = 0, float steer = 0, int frames = 1)
    {
        std::vector<InputFrame> batch;
        for (int i = 0; i < frames; ++i) {
            InputFrame f;
            f.seq = ++seq;
            f.buttons = i == 0 ? buttons : static_cast<std::uint16_t>(buttons & (Buttons::Run | Buttons::Crouch | Buttons::Handbrake));
            f.moveX = static_cast<std::int8_t>(moveX * 127);
            f.moveZ = static_cast<std::int8_t>(moveZ * 127);
            f.throttle = static_cast<std::int8_t>(throttle * 127);
            f.steer = static_cast<std::int8_t>(steer * 127);
            batch.push_back(f);
        }
        world->queueInput(player, batch);
    }
    // Feed input the way a real client does: two 60 Hz frames per 30 Hz tick.
    void hold(float throttle, float steer, float seconds)
    {
        for (int i = 0; i < static_cast<int>(seconds * 30.0f); ++i) {
            input(0, 0, 0, throttle, steer, 2);
            world->tick(1.0f / 30.0f);
        }
    }
    void run(float seconds)
    {
        for (int i = 0; i < static_cast<int>(seconds * 30.0f); ++i) {
            world->tick(1.0f / 30.0f);
        }
    }
};

} // namespace

TEST_CASE("Gameplay: joining gives a profile, a body and starting money")
{
    World w;
    CHECK(w.player != kNullEntity);
    const PlayerStatus st = w.world->buildStatus(w.player);
    CHECK_EQ(st.money, 500);
    CHECK_EQ(st.health, 100);
    CHECK(st.pickupsTotal == 24);
    const Snapshot snap = w.world->buildSnapshot(w.player);
    CHECK(snap.self.mode == CharacterMode::OnFoot);
    bool markers = false;
    for (const EntityState& e : snap.entities) markers = markers || e.kind == EntityKind::Marker;
    CHECK(markers); // the safehouse save point is right next to spawn
}

TEST_CASE("Gameplay: enter, drive and leave a vehicle")
{
    World w;
    const Vec3 spot{40.0f, 0.0f, 2.5f}; // eastbound lane in front of the safehouse
    Entity car = kNullEntity;
    // Use the vehicle factory through the public registry: spawn by buying nothing,
    // simply create one the way the population system does.
    car = w.world->registry().create();
    VehicleComponent vc;
    vc.model = VehicleModel::Sedan;
    vc.state.position = spot;
    vc.state.yaw = kHalfPi; // facing +X
    w.world->registry().add<VehicleComponent>(car, vc);

    w.place(vehicleDoorPosition(vc.state, vehicleSpec(vc.model), 0));
    w.input(Buttons::EnterExit);
    w.run(0.2f);
    CHECK(w.me().mode == CharacterMode::Entering);
    w.run(1.0f);
    CHECK(w.me().mode == CharacterMode::InVehicle);
    CHECK(w.me().vehicle == car);

    w.hold(1.0f, 0.0f, 2.5f); // full throttle
    const VehicleComponent* v = w.world->registry().get<VehicleComponent>(car);
    CHECK(v->state.position.x > spot.x + 8.0f);
    CHECK(w.world->buildStatus(w.player).prompt == "Press F to get out");

    w.hold(-1.0f, 0.0f, 2.0f); // brake
    w.hold(0.0f, 0.0f, 2.0f);
    w.input(Buttons::EnterExit);
    w.run(1.5f);
    CHECK(w.me().mode == CharacterMode::OnFoot);
    CHECK(w.world->buildStatus(w.player).wanted == 0); // an empty unowned car is not a carjacking
}

TEST_CASE("Gameplay: assault raises the wanted level and police respond")
{
    World w(6, 0);
    w.run(1.0f); // let pedestrians spawn
    // Find a pedestrian and stand right behind them, facing them.
    Entity victim = kNullEntity;
    w.world->registry().each<NPCComponent>([&](Entity e, NPCComponent& n) {
        if (!n.driver && !n.police && victim == kNullEntity) victim = e;
    });
    CHECK(victim != kNullEntity);
    if (victim == kNullEntity) return;
    CharacterComponent* vc = w.world->registry().get<CharacterComponent>(victim);
    const Vec3 at = vc->body.motor.position;
    w.place(at + Vec3{0, 0, -1.0f});
    w.me().body.yaw = 0.0f; // facing +Z, towards the victim
    vc->body.motor.setState(at, {}, true);
    w.input(Buttons::Punch);
    w.world->tick(1.0f / 30.0f);
    CHECK(w.world->registry().get<CharacterComponent>(victim)->mode == CharacterMode::KnockedDown);
    CHECK(w.world->buildStatus(w.player).wanted >= 1);

    w.run(12.0f);
    int officers = 0;
    w.world->registry().each<PoliceComponent>([&](Entity, PoliceComponent& p) { officers += p.target == w.player ? 1 : 0; });
    CHECK(officers >= 1);
}

TEST_CASE("Gameplay: pickups pay out once and persist in the profile")
{
    World w;
    Snapshot snap = w.world->buildSnapshot(w.player);
    Vec3 pickup;
    bool found = false;
    // Pickups are replicated entities; find one anywhere via the registry.
    w.world->registry().each<PickupComponent>([&](Entity e, PickupComponent&) {
        if (!found) {
            pickup = w.world->registry().get<TransformComponent>(e)->position;
            found = true;
        }
    });
    CHECK(found);
    w.place(pickup);
    w.run(0.2f);
    PlayerStatus st = w.world->buildStatus(w.player);
    CHECK_EQ(st.money, 600);
    CHECK_EQ(st.pickupsFound, 1);
    w.run(0.5f);
    CHECK_EQ(w.world->buildStatus(w.player).money, 600); // only once
}

TEST_CASE("Gameplay: checkpoint mission start to finish pays the reward")
{
    World w;
    int index = -1;
    for (std::size_t i = 0; i < w.world->missions().size(); ++i) {
        if (w.world->missions()[i].id == "beach_sprint") index = static_cast<int>(i);
    }
    CHECK(index >= 0);
    if (index < 0) return;
    const MissionDef& m = w.world->missions()[static_cast<std::size_t>(index)];

    w.place(m.start);
    w.run(0.2f);
    CHECK(w.world->buildStatus(w.player).prompt.find("Beach Sprint") != std::string::npos);
    w.input(Buttons::Interact);
    w.run(0.2f);
    PlayerStatus st = w.world->buildStatus(w.player);
    CHECK_EQ(st.missionTitle, std::string("Beach Sprint"));
    CHECK(st.marker == MarkerKind::Checkpoint);
    CHECK(st.missionTimer > 0.0f);

    for (const Vec3& p : m.points) {
        w.place(p);
        w.run(0.2f);
    }
    st = w.world->buildStatus(w.player);
    CHECK(st.missionTitle.empty());
    CHECK_EQ(st.money, 500 + m.reward);
}
