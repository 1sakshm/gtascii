// Wanted level and police: crimes raise stars, police cars pursue, officers
// chase on foot and make arrests, and staying out of sight lowers the level.
#include "ServerWorld.h"

#include <cmath>

namespace server {

using namespace av;
using namespace av::proto;

namespace {
constexpr float kSightRange = 55.0f;
constexpr float kLoseTime = 16.0f; // seconds unseen per star lost

float flatDistance(const Vec3& a, const Vec3& b) { return length(Vec3{a.x - b.x, 0, a.z - b.z}); }
} // namespace

void ServerWorld::addWanted(Entity player, int stars, const std::string& crime)
{
    PlayerComponent* p = reg_.get<PlayerComponent>(player);
    if (!p || stars <= 0) {
        return;
    }
    auto& last = p->lastCrime[crime];
    if (last > 0.0 && time_ - last < 3.0) {
        return; // the same incident reported again
    }
    last = time_;
    const int before = p->wanted;
    p->wanted = std::min(5, p->wanted + stars);
    p->unseenTime = 0.0f;
    if (p->wanted != before) {
        notify(player, EventKind::Wanted, crime, p->wanted);
    }
}

void ServerWorld::respawnPlayer(Entity player, const Vec3& where)
{
    CharacterComponent* c = reg_.get<CharacterComponent>(player);
    if (!c) {
        return;
    }
    if (VehicleComponent* v = reg_.get<VehicleComponent>(c->vehicle)) {
        if (v->driver == player) {
            v->driver = kNullEntity;
        }
    }
    c->vehicle = kNullEntity;
    c->mode = CharacterMode::OnFoot;
    chunks_->loadAll(where);
    c->body.motor.teleport(where, *chunks_);
    if (PlayerComponent* p = reg_.get<PlayerComponent>(player)) {
        p->pendingInputs.clear();
    }
}

void ServerWorld::arrest(Entity player)
{
    PlayerComponent* p = reg_.get<PlayerComponent>(player);
    if (!p || !p->profile) {
        return;
    }
    const int fine = std::max(100, p->profile->money / 10);
    p->profile->money = std::max(0, p->profile->money - fine);
    p->wanted = 0;
    p->arrestTime = 0.0f;
    if (p->mission.index >= 0) {
        endMission(player, false, "You were arrested");
    }
    respawnPlayer(player, landmarkPoint("police_station", "respawn", layout_->spawn().position));
    notify(player, EventKind::Arrested, "ARRESTED - fined $" + std::to_string(fine), -fine);
}

void ServerWorld::hospitalize(Entity player)
{
    PlayerComponent* p = reg_.get<PlayerComponent>(player);
    if (!p || !p->profile) {
        return;
    }
    const int bill = std::min(p->profile->money, 200);
    p->profile->money -= bill;
    p->wanted = 0;
    if (HealthComponent* h = reg_.get<HealthComponent>(player)) {
        h->health = h->maxHealth;
    }
    if (p->mission.index >= 0) {
        endMission(player, false, "You were hospitalized");
    }
    respawnPlayer(player, landmarkPoint("hospital", "respawn", layout_->spawn().position));
    notify(player, EventKind::Hospitalized, "HOSPITALIZED - medical bill $" + std::to_string(bill), -bill);
}

void ServerWorld::updatePolice(float dt)
{
    // ---- per player: spawning units and losing the heat ----
    for (const Entity pe : reg_.entitiesWith<PlayerComponent>()) {
        PlayerComponent* p = reg_.get<PlayerComponent>(pe);
        const CharacterComponent* pc = reg_.get<CharacterComponent>(pe);
        if (!p || !pc) {
            continue;
        }
        p->policeSpawnCooldown = std::max(0.0f, p->policeSpawnCooldown - dt);
        const Vec3 pos = pc->body.motor.position;
        int units = 0;
        bool seen = false;
        reg_.each<PoliceComponent>([&](Entity oe, PoliceComponent& pol) {
            if (pol.target != pe) {
                return;
            }
            ++units;
            if (const CharacterComponent* oc = reg_.get<CharacterComponent>(oe)) {
                seen = seen || flatDistance(oc->body.motor.position, pos) < kSightRange;
            }
        });
        if (p->wanted <= 0 || p->role == "robber") {
            p->unseenTime = 0.0f;
            continue; // in cops & robbers the cops are other players
        }
        if (seen) {
            p->unseenTime = 0.0f;
        } else {
            p->unseenTime += dt;
            if (p->unseenTime > kLoseTime) {
                p->unseenTime = 0.0f;
                --p->wanted;
                notify(pe, EventKind::Wanted, p->wanted > 0 ? "Wanted level dropped" : "You lost the police", p->wanted);
            }
        }
        const int desired = std::min(p->wanted, 4);
        if (units < desired && p->policeSpawnCooldown <= 0.0f) {
            const Entity car = spawnTrafficCar(pos, 70.0f, 110.0f);
            if (car != kNullEntity) {
                VehicleComponent* v = reg_.get<VehicleComponent>(car);
                v->model = VehicleModel::Cruiser;
                v->sirens = true;
                DriverAIComponent* ai = reg_.get<DriverAIComponent>(car);
                ai->pursuit = true;
                ai->target = pe;
                const Entity officer = v->driver;
                NPCComponent* n = reg_.get<NPCComponent>(officer);
                n->police = true;
                n->name = "Officer";
                reg_.get<CharacterComponent>(officer)->appearance = 0x0FF1CE00u + static_cast<std::uint32_t>(rng_.rangeInt(0, 255));
                reg_.add<PoliceComponent>(officer, {pe, car, 0.0f});
                p->policeSpawnCooldown = 4.0f;
            } else {
                p->policeSpawnCooldown = 0.5f;
            }
        }

        // Boxed in: stationary in a car next to a police cruiser.
        if (pc->mode == CharacterMode::InVehicle) {
            const VehicleComponent* mine = reg_.get<VehicleComponent>(pc->vehicle);
            bool cornered = false;
            reg_.each<VehicleComponent>([&](Entity ve, VehicleComponent& v) {
                if (ve != pc->vehicle && v.model == VehicleModel::Cruiser && v.driver != kNullEntity &&
                    flatDistance(v.state.position, pos) < 6.5f) {
                    cornered = true;
                }
            });
            if (mine && cornered && std::fabs(mine->state.speed) < 1.2f) {
                p->arrestTime += dt;
            } else {
                p->arrestTime = std::max(0.0f, p->arrestTime - dt);
            }
            if (p->arrestTime > 3.0f) {
                arrest(pe);
            }
        }
    }

    // ---- officers ----
    for (const Entity oe : reg_.entitiesWith<PoliceComponent>()) {
        PoliceComponent* pol = reg_.get<PoliceComponent>(oe);
        CharacterComponent* oc = reg_.get<CharacterComponent>(oe);
        if (!pol || !oc) {
            continue;
        }
        PlayerComponent* target = reg_.get<PlayerComponent>(pol->target);
        const CharacterComponent* tc = reg_.get<CharacterComponent>(pol->target);
        const Vec3 me = oc->body.motor.position;
        const float nearest = distanceToNearestPlayer(me);
        const bool active = target && tc && target->wanted > 0;

        if (!active) {
            // Stand down: go away once nobody is watching.
            if (nearest > 70.0f) {
                if (pol->car != kNullEntity && reg_.valid(pol->car)) {
                    destroyEntity(pol->car); // takes the officer with it if seated
                }
                if (reg_.valid(oe)) {
                    destroyEntity(oe);
                }
                continue;
            }
            if (VehicleComponent* car = reg_.get<VehicleComponent>(pol->car)) {
                car->sirens = false;
                car->controls = {};
                car->controls.handbrake = true;
            }
            if (oc->mode == CharacterMode::OnFoot) {
                simulateOnFoot(oc->body, InputFrame{}, dt, *dynamicWorld_);
            }
            continue;
        }

        const Vec3 targetPos = tc->body.motor.position;
        const float dist = flatDistance(me, targetPos);
        VehicleComponent* car = reg_.get<VehicleComponent>(pol->car);

        if (oc->mode == CharacterMode::InVehicle && car && car->driver == oe) {
            // Pursuit driving: aim slightly ahead of the target.
            DriverAIComponent* ai = reg_.get<DriverAIComponent>(pol->car);
            const Vec3 lead = targetPos + tc->body.motor.velocity * 0.8f;
            VehicleControls c;
            c.steer = steerTowards(car->state, vehicleSpec(car->model), lead);
            c.throttle = dist > 16.0f ? 1.0f : clampv((dist - 7.0f) * 0.15f, -1.0f, 1.0f);
            if (ai) {
                if (c.throttle > 0.5f && std::fabs(car->state.speed) < 1.0f) {
                    ai->stuckTimer += dt;
                } else {
                    ai->stuckTimer = 0.0f;
                }
                if (ai->stuckTimer > 1.5f) {
                    ai->reverseTimer = 1.2f;
                    ai->stuckTimer = 0.0f;
                }
                if (ai->reverseTimer > 0.0f) {
                    ai->reverseTimer -= dt;
                    c.throttle = -1.0f;
                    c.steer = -c.steer;
                }
            }
            car->controls = c;
            car->sirens = true;
            // Close to a suspect on foot: get out and chase.
            if (dist < 14.0f && tc->mode == CharacterMode::OnFoot && std::fabs(car->state.speed) < 2.5f) {
                exitVehicle(oe);
            }
            continue;
        }
        if (oc->mode != CharacterMode::OnFoot) {
            continue;
        }
        // On foot: run the suspect down, or get back in the car to follow a driver.
        Vec3 goal = targetPos;
        bool toCar = false;
        if (tc->mode == CharacterMode::InVehicle && car && car->driver == kNullEntity && flatDistance(me, car->state.position) < 30.0f) {
            goal = vehicleDoorPosition(car->state, vehicleSpec(car->model), 0);
            toCar = true;
        }
        const Vec3 d = goal - me;
        InputFrame f;
        const Vec3 dir = normalize(Vec3{d.x, 0, d.z});
        f.moveX = static_cast<std::int8_t>(std::lround(dir.x * 127.0f));
        f.moveZ = static_cast<std::int8_t>(std::lround(dir.z * 127.0f));
        f.buttons = Buttons::Run;
        simulateOnFoot(oc->body, f, dt, *dynamicWorld_);
        if (toCar && flatDistance(me, goal) < 1.5f) {
            enterVehicle(oe, pol->car);
            continue;
        }
        const bool reachable = tc->mode == CharacterMode::OnFoot || tc->mode == CharacterMode::KnockedDown ||
                               (tc->mode == CharacterMode::InVehicle && reg_.get<VehicleComponent>(tc->vehicle) &&
                                std::fabs(reg_.get<VehicleComponent>(tc->vehicle)->state.speed) < 1.0f);
        if (dist < 1.6f && reachable) {
            pol->contactTime += dt;
            if (pol->contactTime > 1.2f) {
                pol->contactTime = 0.0f;
                arrest(pol->target);
            }
        } else {
            pol->contactTime = std::max(0.0f, pol->contactTime - dt * 0.5f);
        }
        if (nearest > 200.0f) {
            destroyEntity(oe);
        }
    }
}

} // namespace server
