// Weapons, loot, explosions, cheats, swimming, jetpack, helicopter and
// interiors, driven through the public ServerWorld API.
#include "TestHarness.h"

#include "ServerWorld.h"
#include "Weapons.h"

#include <cmath>
#include <filesystem>

using namespace av;
using namespace av::proto;
using namespace server;

namespace {

struct Arena {
    ServerConfig config;
    std::unique_ptr<ServerWorld> world;
    Entity player = kNullEntity;
    std::uint32_t seq = 0;

    Arena()
    {
        config.savePath = (std::filesystem::temp_directory_path() / "gtascii_test_combat.json").string();
        config.pedestrians = 0;
        config.trafficCars = 0;
        config.dayLengthMinutes = 0.0f;
        std::filesystem::remove(config.savePath);
        world = std::make_unique<ServerWorld>(config, AV_SOURCE_DIR);
        std::string error;
        player = world->joinPlayer("Shooter", "", 5u, 1u, error);
    }
    ~Arena()
    {
        world.reset();
        std::filesystem::remove(config.savePath);
    }
    Registry& reg() { return world->registry(); }
    CharacterComponent& me() { return *reg().get<CharacterComponent>(player); }
    PlayerComponent& pc() { return *reg().get<PlayerComponent>(player); }
    void place(const Vec3& p, float yaw = 0.0f)
    {
        me().body.motor.setState(p, {}, true);
        me().body.yaw = yaw;
    }
    // Two 60 Hz frames per 30 Hz tick, buttons held for the whole time.
    void hold(std::uint16_t buttons, float seconds, int weapon = 0, float aimYaw = 0.0f, Vec3 move = {}, float throttle = 0.0f)
    {
        for (int i = 0; i < static_cast<int>(seconds * 30.0f); ++i) {
            std::vector<InputFrame> batch;
            for (int k = 0; k < 2; ++k) {
                InputFrame f;
                f.seq = ++seq;
                f.buttons = buttons;
                f.weapon = static_cast<std::uint8_t>(weapon);
                f.aim = static_cast<std::int8_t>(std::lround(aimYaw / kPi * 127.0f));
                f.moveX = static_cast<std::int8_t>(move.x * 127);
                f.moveZ = static_cast<std::int8_t>(move.z * 127);
                f.throttle = static_cast<std::int8_t>(throttle * 127);
                batch.push_back(f);
            }
            world->queueInput(player, batch);
            world->tick(1.0f / 30.0f);
        }
    }
    Entity pedestrian(const Vec3& at)
    {
        const Entity e = reg().create();
        CharacterComponent c;
        c.body.motor.setState(at, {}, true);
        reg().add<CharacterComponent>(e, std::move(c));
        reg().add<HealthComponent>(e);
        NPCComponent n;
        n.name = "Target";
        n.dancer = true; // stands still (no fleeing) so the test is deterministic
        reg().add<NPCComponent>(e, n);
        return e;
    }
    int count(std::uint8_t dropType)
    {
        int n = 0;
        reg().each<DropComponent>([&](Entity, DropComponent& d) { n += d.type == dropType && d.ttl >= 0.0f ? 1 : 0; });
        return n;
    }
};

} // namespace

TEST_CASE("Combat: cheat codes are recognised (and never reach chat)")
{
    Arena a;
    CHECK(!a.world->tryCheat(a.player, "hello everyone"));
    CHECK(a.world->tryCheat(a.player, "FULLCLIP"));
    const PlayerStatus st = a.world->buildStatus(a.player);
    for (int w = 1; w < kWeaponCount; ++w) CHECK((st.weaponsOwned & (1u << w)) != 0);
    const int before = st.money;
    CHECK(a.world->tryCheat(a.player, "rich boy")); // spaces and case don't matter
    CHECK_EQ(a.world->buildStatus(a.player).money, before + 50000);
}

TEST_CASE("Combat: a pistol kills a pedestrian, who drops cash")
{
    Arena a;
    a.world->tryCheat(a.player, "fullclip");
    a.place({0.0f, 0.0f, 20.0f}); // on the north-south road at x = 0
    const Entity victim = a.pedestrian({0.0f, 0.0f, 27.0f});
    const int ammoBefore = a.pc().profile->inventory["Pistol"];
    a.hold(Buttons::Fire, 0.5f, Pistol, 0.0f);
    a.hold(Buttons::Fire, 2.5f, Pistol, 0.0f);
    const PlayerStatus st = a.world->buildStatus(a.player);
    CHECK_EQ(static_cast<int>(st.weapon), static_cast<int>(Pistol));
    CHECK(st.ammo < ammoBefore);
    const HealthComponent* h = a.reg().get<HealthComponent>(victim);
    CHECK(h && h->health <= 0.0f);
    CHECK(a.reg().get<CharacterComponent>(victim)->dead);
    CHECK(a.count(PickupType::Cash) >= 1);
    CHECK(st.wanted >= 1);

    // Walking over the cash picks it up.
    const int money = st.money;
    a.reg().each<DropComponent>([&](Entity e, DropComponent& d) {
        if (d.type == PickupType::Cash) a.place(a.reg().get<TransformComponent>(e)->position);
    });
    a.hold(0, 0.3f);
    CHECK(a.world->buildStatus(a.player).money > money);
}

TEST_CASE("Combat: a rocket blows up a car")
{
    Arena a;
    a.world->tryCheat(a.player, "fullclip");
    a.place({0.0f, 0.0f, 10.0f});
    const Entity car = a.reg().create();
    VehicleComponent v;
    v.state.position = {0.0f, 0.0f, 30.0f};
    a.reg().add<VehicleComponent>(car, v);
    a.hold(Buttons::Fire, 0.1f, RocketLauncher, 0.0f);
    a.hold(0, 2.0f, RocketLauncher);
    const VehicleComponent* after = a.reg().get<VehicleComponent>(car);
    CHECK(after && after->state.damage >= 1.0f);
    CHECK(after && after->exploded);
}

TEST_CASE("Swimming: deep water floats you, and the edge of the map stops you")
{
    Arena a;
    const float water = CityLayout::kWaterLevel;
    a.place({kWorldEdgeX - 40.0f, -4.0f, 0.0f});
    a.hold(0, 1.5f);
    CHECK(a.me().body.swimming);
    CHECK(std::fabs(a.me().body.motor.position.y - (water - 1.35f)) < 0.3f);
    a.hold(Buttons::Run, 20.0f, 0, 0.0f, {1.0f, 0.0f, 0.0f}); // swim out to sea for 20 s (~70 m)
    CHECK(a.me().body.motor.position.x < kWorldEdgeX);
    CHECK(a.me().body.motor.position.x > kWorldEdgeX - 2.0f);
}

TEST_CASE("Jetpack: ROCKETMAN, then Space flies you up")
{
    Arena a;
    a.place({0.0f, 0.0f, 20.0f});
    CHECK(a.world->tryCheat(a.player, "rocketman"));
    CHECK(a.me().body.jetpack);
    a.hold(Buttons::Ascend, 2.0f);
    CHECK(a.me().body.motor.position.y > 8.0f);
    a.world->command(a.player, CommandKind::ToggleJetpack, 0);
    CHECK(!a.me().body.jetpack);
}

TEST_CASE("Helicopter: spins up, climbs and flies forward")
{
    Arena a;
    a.place({0.0f, 0.0f, 20.0f});
    CHECK(a.world->tryCheat(a.player, "flyboy"));
    Entity heli = kNullEntity;
    a.reg().each<VehicleComponent>([&](Entity e, VehicleComponent& v) {
        if (v.model == VehicleModel::Helicopter && distance(v.state.position, Vec3{0, 0, 20}) < 18.0f) heli = e;
    });
    CHECK(heli != kNullEntity);
    if (heli == kNullEntity) return;
    VehicleComponent& v = *a.reg().get<VehicleComponent>(heli);
    a.me().mode = CharacterMode::InVehicle;
    a.me().vehicle = heli;
    a.me().seat = 0;
    v.driver = a.player;
    a.hold(Buttons::Ascend, 5.0f);
    CHECK(v.state.rotor > 0.9f);
    CHECK(v.state.position.y > 10.0f);
    const Vec3 hover = v.state.position;
    a.hold(0, 3.0f, 0, 0.0f, {}, 1.0f);
    CHECK(distance(v.state.position, hover) > 20.0f);
    CHECK(v.state.position.y > 8.0f); // stays up without holding the collective
}

TEST_CASE("Interiors: the casino door takes you inside, the slots take your money, and you can leave")
{
    Arena a;
    Vec3 door, exitDoor, slot;
    bool haveDoor = false, haveExit = false, haveSlot = false;
    a.reg().each<InteractionComponent>([&](Entity e, InteractionComponent& ic) {
        const Vec3 p = a.reg().get<TransformComponent>(e)->position;
        if (ic.label == "Flamingo Casino" && ic.action == InteractionComponent::Action::Enter) { door = p; haveDoor = true; }
        if (ic.label == "Flamingo Casino" && ic.action == InteractionComponent::Action::Exit) { exitDoor = p; haveExit = true; }
        if (ic.action == InteractionComponent::Action::Slots && !haveSlot) { slot = p; haveSlot = true; }
    });
    CHECK(haveDoor && haveExit && haveSlot);
    a.place(door);
    a.hold(0, 0.1f);
    a.hold(Buttons::Interact, 0.04f);
    a.hold(0, 0.2f);
    CHECK(a.me().body.motor.position.y > kInteriorFloorY - 1.0f);
    a.hold(0, 1.0f);
    CHECK(a.me().body.motor.position.y > kInteriorFloorY - 1.0f); // standing on the floor, not falling

    a.place(slot);
    // Five spins: each one costs $100 and pays 0, 100, 200, 500 or 5000.
    for (int spin = 0; spin < 5; ++spin) {
        const int money = a.world->buildStatus(a.player).money;
        a.hold(Buttons::Interact, 0.04f);
        a.hold(0, 1.3f); // the machine's cooldown
        const int change = a.world->buildStatus(a.player).money - money;
        CHECK(change == -100 || change == 0 || change == 100 || change == 400 || change == 4900);
        CHECK(a.pc().casinoCooldown < 1.0f);
    }

    a.place(exitDoor);
    a.hold(Buttons::Interact, 0.04f);
    a.hold(0, 0.2f);
    CHECK(a.me().body.motor.position.y < 5.0f);
    const Vec3 out = a.me().body.motor.position;
    CHECK(std::hypot(out.x - door.x, out.z - door.z) < 4.0f);
}
