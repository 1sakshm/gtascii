// Ambient population: pedestrians on the sidewalk graph, traffic on the road
// network, vehicle physics for everything not driven by a player, and
// vehicle contacts (crashes, pedestrians struck).
#include "ServerWorld.h"

#include <cmath>
#include <iterator>

namespace server {

using namespace av;
using namespace av::proto;

namespace {

const char* kFirstNames[] = {"Rosa", "Marco", "Jules", "Dee", "Tony", "Lena", "Cruz", "Mabel", "Rex", "Ivy",
                             "Sol", "Nico", "Gus", "Vera", "Hank", "Lulu", "Benny", "Carmen", "Duke", "Faye"};
const char* kLastNames[] = {"Alvarez", "Brooks", "Castillo", "Diaz", "Ellis", "Flores", "Grant", "Herrera",
                            "Iglesias", "Jordan", "Kane", "Lopez", "Morales", "Nash", "Ortiz", "Price"};

float horizontalDistance(const Vec3& a, const Vec3& b) { return length(Vec3{a.x - b.x, 0, a.z - b.z}); }

InputFrame frameFor(const Vec3& dir, bool run)
{
    InputFrame f;
    f.moveX = static_cast<std::int8_t>(std::lround(clampv(dir.x, -1.0f, 1.0f) * 127.0f));
    f.moveZ = static_cast<std::int8_t>(std::lround(clampv(dir.z, -1.0f, 1.0f) * 127.0f));
    f.buttons = run ? Buttons::Run : 0;
    return f;
}

} // namespace

Entity ServerWorld::spawnPedestrian(const Vec3& near, float minDist, float maxDist)
{
    const float a = rng_.range(0.0f, kTwoPi), d = rng_.range(minDist, maxDist);
    NavNode node;
    if (!nav_->nearest(near + Vec3{std::cos(a) * d, 0, std::sin(a) * d}, node)) {
        return kNullEntity;
    }
    Vec3 p = nav_->position(node);
    p += Vec3{rng_.range(-3.0f, 3.0f), 0, rng_.range(-3.0f, 3.0f)};
    const float dist = horizontalDistance(p, near);
    if (dist < minDist * 0.8f || chunks_->find(worldToChunk(p)) == nullptr) {
        return kNullEntity;
    }
    const Entity e = spawnCharacter(p, rng_.range(-kPi, kPi), rng_.next());
    NPCComponent n;
    n.name = std::string(kFirstNames[rng_.rangeInt(0, static_cast<int>(std::size(kFirstNames)) - 1)]) + " " +
             kLastNames[rng_.rangeInt(0, static_cast<int>(std::size(kLastNames)) - 1)];
    n.brain.start(*nav_, p, rng_);
    reg_.add<NPCComponent>(e, std::move(n));
    return e;
}

Entity ServerWorld::spawnTrafficCar(const Vec3& near, float minDist, float maxDist)
{
    const float a = rng_.range(0.0f, kTwoPi), d = rng_.range(minDist, maxDist);
    const Intersection at = roads_->nearest(near + Vec3{std::cos(a) * d, 0, std::sin(a) * d});
    if (!roads_->exists(at)) {
        return kNullEntity;
    }
    const Heading headings[4] = {Heading::North, Heading::East, Heading::South, Heading::West};
    const Heading h = headings[rng_.rangeInt(0, 3)];
    if (!roads_->hasRoad(at, h)) {
        return kNullEntity;
    }
    const Vec3 start = roads_->laneStart(at, h) + headingVector(h) * rng_.range(0.0f, 30.0f);
    const float dist = horizontalDistance(start, near);
    if (dist < minDist * 0.8f || chunks_->find(worldToChunk(start)) == nullptr) {
        return kNullEntity;
    }
    bool clear = true;
    reg_.each<VehicleComponent>([&](Entity, VehicleComponent& v) {
        clear = clear && lengthSq(v.state.position - start) > 12.0f * 12.0f;
    });
    if (!clear) {
        return kNullEntity;
    }
    const float r = rng_.uniform();
    const VehicleModel model = r < 0.43f ? VehicleModel::Sedan : r < 0.62f ? VehicleModel::Cab : r < 0.79f ? VehicleModel::Van
                             : r < 0.95f ? VehicleModel::Coupe : VehicleModel::Sports;
    const Vec3 dir = headingVector(h);
    const Entity car = spawnVehicle(model, rng_.rangeInt(0, 7), start, std::atan2(dir.x, dir.z));
    DriverAIComponent ai;
    ai.traffic.start(*roads_, at, h, rng_);
    ai.traffic.cruiseSpeed = rng_.range(9.0f, 13.0f);
    reg_.add<DriverAIComponent>(car, std::move(ai));

    const Entity driver = spawnCharacter(start, 0.0f, rng_.next());
    NPCComponent n;
    n.name = "Driver";
    n.driver = true;
    reg_.add<NPCComponent>(driver, std::move(n));
    CharacterComponent* c = reg_.get<CharacterComponent>(driver);
    c->mode = CharacterMode::InVehicle;
    c->vehicle = car;
    reg_.get<VehicleComponent>(car)->driver = driver;
    return car;
}

void ServerWorld::updatePopulation()
{
    const std::vector<Vec3> players = playerPositions();
    if (players.empty()) {
        return;
    }

    // Despawn what is far from everyone.
    for (const Entity e : reg_.entitiesWith<NPCComponent>()) {
        const NPCComponent* n = reg_.get<NPCComponent>(e);
        const CharacterComponent* c = reg_.get<CharacterComponent>(e);
        const HealthComponent* h = reg_.get<HealthComponent>(e);
        if (!n || !c || n->driver || n->police) {
            continue;
        }
        const bool dead = h && h->health <= 0.0f && (c->mode != CharacterMode::KnockedDown || c->modeTimer <= 0.0f);
        if (dead || distanceToNearestPlayer(c->body.motor.position) > 140.0f) {
            destroyEntity(e);
        }
    }
    for (const Entity e : reg_.entitiesWith<VehicleComponent>()) {
        const VehicleComponent* v = reg_.get<VehicleComponent>(e);
        if (!v || v->missionVehicle || v->owner != kNullEntity || reg_.has<PlayerComponent>(v->driver)) {
            continue;
        }
        const DriverAIComponent* ai = reg_.get<DriverAIComponent>(e);
        if (ai && ai->pursuit) {
            continue; // police cars are managed by the police system
        }
        const float d = distanceToNearestPlayer(v->state.position);
        if (d > 180.0f || (v->driver == kNullEntity && v->abandonedTime > 120.0f && d > 60.0f)) {
            destroyEntity(e);
        }
    }

    // Top up around each player.
    for (const Vec3& p : players) {
        int peds = 0, cars = 0;
        reg_.each<NPCComponent>([&](Entity e, NPCComponent& n) {
            const CharacterComponent* c = reg_.get<CharacterComponent>(e);
            if (c && !n.driver && !n.police && horizontalDistance(c->body.motor.position, p) < 110.0f) {
                ++peds;
            }
        });
        reg_.each<VehicleComponent>([&](Entity, VehicleComponent& v) {
            if (horizontalDistance(v.state.position, p) < 150.0f) {
                ++cars;
            }
        });
        for (int i = 0; i < 3 && peds < config_.pedestrians; ++i) {
            if (spawnPedestrian(p, 40.0f, 95.0f) != kNullEntity) {
                ++peds;
            }
        }
        for (int i = 0; i < 2 && cars < config_.trafficCars; ++i) {
            if (spawnTrafficCar(p, 60.0f, 130.0f) != kNullEntity) {
                ++cars;
            }
        }
    }
}

void ServerWorld::scareNearby(const Vec3& where, float radius, float seconds)
{
    reg_.each<NPCComponent>([&](Entity e, NPCComponent& n) {
        const CharacterComponent* c = reg_.get<CharacterComponent>(e);
        if (c && !n.police && !n.driver && c->mode == CharacterMode::OnFoot &&
            horizontalDistance(c->body.motor.position, where) < radius) {
            n.brain.threaten(where, seconds);
        }
    });
}

void ServerWorld::updatePedestrians(float dt)
{
    // Characters that matter for perception.
    struct Mover {
        Vec3 position, velocity;
    };
    std::vector<Mover> fastCars, players;
    reg_.each<VehicleComponent>([&](Entity, VehicleComponent& v) {
        if (std::fabs(v.state.speed) > 7.0f) {
            fastCars.push_back({v.state.position, v.state.velocity()});
        }
    });
    reg_.each<PlayerComponent>([&](Entity e, PlayerComponent&) {
        if (const CharacterComponent* c = reg_.get<CharacterComponent>(e)) {
            players.push_back({c->body.motor.position, c->body.motor.velocity});
        }
    });

    std::vector<Entity> idle; // candidates for conversations
    reg_.each<NPCComponent>([&](Entity e, NPCComponent& n) {
        CharacterComponent* c = reg_.get<CharacterComponent>(e);
        if (!c || n.driver || n.police || n.dancer || c->mode != CharacterMode::OnFoot) {
            return;
        }
        const Vec3 pos = c->body.motor.position;
        // Level of detail: distant pedestrians think and move less often.
        const float far = distanceToNearestPlayer(pos);
        const int stride = far > 90.0f ? 3 : 1;
        if ((tick_ + e) % static_cast<std::uint32_t>(stride) != 0) {
            return;
        }
        const float step = dt * static_cast<float>(stride);

        for (const Mover& car : fastCars) {
            const Vec3 d = pos - car.position;
            if (length(Vec3{d.x, 0, d.z}) < 8.0f && dot(d, car.velocity) > 0.0f) {
                n.brain.threaten(car.position, 5.0f);
            }
        }
        for (const Mover& pl : players) {
            if (horizontalDistance(pos, pl.position) < 0.85f && length(pl.velocity) > 1.0f) {
                n.brain.bump(pl.position);
            }
        }

        const PedCommand cmd = n.brain.update(*nav_, pos, step, rng_);
        c->talking = cmd.talk;
        if (cmd.wave) {
            c->trigger(1);
        }
        c->run = cmd.run;
        simulateOnFoot(c->body, frameFor(cmd.moveDirection, cmd.run), step, *dynamicWorld_);
        if (cmd.face) {
            const float delta = wrapAngle(cmd.faceYaw - c->body.yaw);
            c->body.yaw = wrapAngle(c->body.yaw + clampv(delta, -6.0f * step, 6.0f * step));
        }
        if (n.brain.available()) {
            idle.push_back(e);
        }
    });

    // Occasionally pair up nearby pedestrians for a chat.
    if (tick_ % 20 == 0) {
        for (std::size_t i = 0; i < idle.size(); ++i) {
            for (std::size_t j = i + 1; j < idle.size(); ++j) {
                CharacterComponent* a = reg_.get<CharacterComponent>(idle[i]);
                CharacterComponent* b = reg_.get<CharacterComponent>(idle[j]);
                if (a && b && horizontalDistance(a->body.motor.position, b->body.motor.position) < 2.4f && rng_.chance(0.3f)) {
                    const float secs = rng_.range(4.0f, 9.0f);
                    reg_.get<NPCComponent>(idle[i])->brain.talkTo(b->body.motor.position, secs);
                    reg_.get<NPCComponent>(idle[j])->brain.talkTo(a->body.motor.position, secs);
                    a->trigger(1);
                }
            }
        }
    }
}

void ServerWorld::updateVehicles(float dt)
{
    for (const Entity e : reg_.entitiesWith<VehicleComponent>()) {
        VehicleComponent* v = reg_.get<VehicleComponent>(e);
        if (!v) {
            continue;
        }
        if (v->driver != kNullEntity && reg_.has<PlayerComponent>(v->driver)) {
            continue; // stepped by the player's input frames
        }
        const VehicleSpec& spec = vehicleSpec(v->model);
        VehicleControls controls;
        DriverAIComponent* ai = reg_.get<DriverAIComponent>(e);
        const CharacterComponent* driver = reg_.get<CharacterComponent>(v->driver);
        const bool driverSeated = driver && driver->mode == CharacterMode::InVehicle;
        if (ai && driverSeated && !ai->pursuit) {
            // Perception: nearest thing ahead in our lane, blocked intersection.
            TrafficDriver::Perception per;
            const Vec3 f = v->state.forward(), l = v->state.left();
            auto consider = [&](const Vec3& p) {
                const Vec3 d = p - v->state.position;
                const float ahead = dot(d, f), lateral = std::fabs(dot(d, l));
                if (ahead > 0.5f && ahead < 30.0f && lateral < 2.2f) {
                    per.obstacleAhead = std::min(per.obstacleAhead, ahead - spec.length * 0.5f);
                }
            };
            const Vec3 next = roads_->position(ai->traffic.nextIntersection());
            reg_.each<VehicleComponent>([&](Entity o, VehicleComponent& ov) {
                if (o == e) {
                    return;
                }
                consider(ov.state.position);
                const Vec3 d = ov.state.position - next;
                if (std::fabs(d.x) < 6.0f && std::fabs(d.z) < 6.0f && !ai->traffic.turning()) {
                    per.intersectionBlocked = true;
                }
            });
            reg_.each<CharacterComponent>([&](Entity, CharacterComponent& c) {
                if (c.mode == CharacterMode::OnFoot || c.mode == CharacterMode::KnockedDown) {
                    consider(c.body.motor.position);
                }
            });
            controls = ai->traffic.update(*roads_, v->state, spec, per, dt, rng_);
        } else if (ai && driverSeated && ai->pursuit) {
            controls = v->controls; // set by the police system
        } else {
            controls.handbrake = std::fabs(v->state.speed) < 2.0f;
            v->abandonedTime += dt;
        }
        // Other vehicles as obstacles.
        std::vector<AABB> others;
        reg_.each<VehicleComponent>([&](Entity o, VehicleComponent& ov) {
            if (o != e && lengthSq(ov.state.position - v->state.position) < 12.0f * 12.0f) {
                vehicleBoxes(ov.state, vehicleSpec(ov.model), others);
            }
        });
        if (std::fabs(v->state.speed) > 0.01f || controls.throttle != 0.0f || !v->state.drowned) {
            stepVehicle(v->state, spec, controls, dt, *chunks_, &others);
        }
    }
}

void ServerWorld::resolveVehicleContacts()
{
    // Vehicles striking people on foot.
    for (const Entity ve : reg_.entitiesWith<VehicleComponent>()) {
        VehicleComponent* v = reg_.get<VehicleComponent>(ve);
        if (!v || std::fabs(v->state.speed) < 2.5f) {
            continue;
        }
        const VehicleSpec& spec = vehicleSpec(v->model);
        const float r = spec.width * 0.5f + 0.35f;
        const auto circles = vehicleCircles(v->state, spec);
        for (const Entity ce : reg_.entitiesWith<CharacterComponent>()) {
            CharacterComponent* c = reg_.get<CharacterComponent>(ce);
            if (!c || c->mode != CharacterMode::OnFoot || c->vehicle == ve ||
                std::fabs(c->body.motor.position.y - v->state.position.y) > 1.6f) {
                continue; // (helicopters overhead)
            }
            bool hit = false;
            for (const Vec3& circle : circles) {
                hit = hit || horizontalDistance(circle, c->body.motor.position) < r;
            }
            if (!hit) {
                continue;
            }
            const float speed = std::fabs(v->state.speed);
            knockDown(ce, v->state.velocity() * 0.7f + v->state.left() * (dot(c->body.motor.position - v->state.position, v->state.left()) > 0 ? 2.0f : -2.0f), 2.5f);
            damage(ce, speed * 3.5f, v->driver);
            v->state.speed *= 0.85f;
            scareNearby(c->body.motor.position, 20.0f, 8.0f);
            if (reg_.has<PlayerComponent>(v->driver) && v->driver != ce) {
                const NPCComponent* n = reg_.get<NPCComponent>(ce);
                addWanted(v->driver, n && n->police ? 2 : 1, n && n->police ? "Ran over an officer" : "Hit a pedestrian");
            }
        }
    }

    // Vehicle-vehicle crashes: separate overlapping cars and trade momentum.
    const std::vector<Entity> cars = reg_.entitiesWith<VehicleComponent>();
    for (std::size_t i = 0; i < cars.size(); ++i) {
        VehicleComponent* a = reg_.get<VehicleComponent>(cars[i]);
        for (std::size_t j = i + 1; j < cars.size() && a; ++j) {
            VehicleComponent* b = reg_.get<VehicleComponent>(cars[j]);
            if (!b || lengthSq(a->state.position - b->state.position) > 36.0f ||
                std::fabs(a->state.position.y - b->state.position.y) > 2.0f) {
                continue;
            }
            const float ra = vehicleSpec(a->model).width * 0.5f, rb = vehicleSpec(b->model).width * 0.5f;
            for (const Vec3& ca : vehicleCircles(a->state, vehicleSpec(a->model))) {
                for (const Vec3& cb : vehicleCircles(b->state, vehicleSpec(b->model))) {
                    Vec3 d = cb - ca;
                    d.y = 0.0f;
                    const float dist = length(d);
                    if (dist >= ra + rb || dist < 1e-4f) {
                        continue;
                    }
                    const Vec3 n = d / dist;
                    const float push = (ra + rb - dist) * 0.5f;
                    a->state.position -= n * push;
                    b->state.position += n * push;
                    const float closing = dot(a->state.velocity() - b->state.velocity(), n);
                    if (closing > 0.5f) {
                        a->state.speed *= 0.7f;
                        b->state.speed *= 0.7f;
                        a->state.damage = std::min(1.0f, a->state.damage + closing * 0.01f);
                        b->state.damage = std::min(1.0f, b->state.damage + closing * 0.01f);
                        for (auto [me, other] : {std::pair{a, b}, std::pair{b, a}}) {
                            if (reg_.has<PlayerComponent>(me->driver) && other->model == VehicleModel::Cruiser &&
                                closing > 3.0f && other->owner != me->driver) {
                                addWanted(me->driver, 1, "Rammed a police car");
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace server
