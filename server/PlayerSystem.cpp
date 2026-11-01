// Player input processing, vehicle entry/exit, melee, damage and the
// character state machine (entering / seated / exiting / knocked down).
#include "ServerWorld.h"

#include <cmath>

namespace server {

using namespace av;
using namespace av::proto;

namespace {
constexpr float kEnterTime = 0.8f;
constexpr float kExitTime = 0.6f;

std::vector<AABB> otherVehicleBoxes(Registry& reg, Entity self, const Vec3& near, float radius)
{
    std::vector<AABB> out;
    reg.each<VehicleComponent>([&](Entity e, VehicleComponent& v) {
        if (e != self && lengthSq(v.state.position - near) < radius * radius) {
            vehicleBoxes(v.state, vehicleSpec(v.model), out);
        }
    });
    return out;
}
} // namespace

void ServerWorld::updatePlayers(float dt)
{
    for (const Entity e : reg_.entitiesWith<PlayerComponent>()) {
        PlayerComponent* p = reg_.get<PlayerComponent>(e);
        if (!p) {
            continue;
        }
        // Each input frame advances the player by exactly one input step, as
        // on the client, so prediction and the server agree.
        int processed = 0;
        while (!p->pendingInputs.empty() && processed < 8) {
            const InputFrame f = p->pendingInputs.front();
            p->pendingInputs.pop_front();
            applyInput(e, f);
            p = reg_.get<PlayerComponent>(e); // applyInput may respawn but never destroys the player
            if (!p) {
                break;
            }
            p->lastInputSeq = f.seq;
            ++processed;
        }
        if (!p) {
            continue;
        }
        p->fallDamageCooldown = std::max(0.0f, p->fallDamageCooldown - dt);
        if (HealthComponent* h = reg_.get<HealthComponent>(e)) {
            h->sinceDamage += dt;
            if (h->sinceDamage > 6.0f && h->health > 0.0f) {
                h->health = std::min(h->maxHealth, h->health + 3.0f * dt);
            }
        }
    }
}

void ServerWorld::applyInput(Entity e, const InputFrame& in)
{
    CharacterComponent* c = reg_.get<CharacterComponent>(e);
    PlayerComponent* p = reg_.get<PlayerComponent>(e);
    if (!c || !p) {
        return;
    }
    if (p->frozen) {
        p->lastButtons = in.buttons;
        return; // race countdown: hold still
    }
    switch (c->mode) {
    case CharacterMode::OnFoot: {
        c->crouch = in.has(Buttons::Crouch);
        c->run = in.has(Buttons::Run);
        simulateOnFoot(c->body, in, kInputStep, *dynamicWorld_);
        const float impact = c->body.motor.landingImpact();
        if (impact > 12.0f && p->fallDamageCooldown <= 0.0f) {
            damage(e, (impact - 12.0f) * 6.0f, kNullEntity);
            p->fallDamageCooldown = 0.5f;
        }
        if (in.has(Buttons::Wave)) c->trigger(1);
        if (in.has(Buttons::Punch)) punch(e);
        if (in.has(Buttons::EnterExit)) tryEnterOrExit(e);
        if (in.has(Buttons::Interact)) interact(e);
        break;
    }
    case CharacterMode::InVehicle: {
        VehicleComponent* v = reg_.get<VehicleComponent>(c->vehicle);
        if (!v) {
            c->mode = CharacterMode::OnFoot;
            c->vehicle = kNullEntity;
            break;
        }
        if (c->seat == 0 && v->driver == e) {
            const std::vector<AABB> others = otherVehicleBoxes(reg_, c->vehicle, v->state.position, 12.0f);
            simulateDriving(v->state, v->model, in, kInputStep, *chunks_, &others);
            v->sirens = v->model == VehicleModel::Cruiser; // a stolen cruiser runs with its lights on
        }
        if (in.has(Buttons::EnterExit)) tryEnterOrExit(e);
        break;
    }
    default:
        break; // transitions and knock-downs ignore input
    }
    p->lastButtons = in.buttons;
}

void ServerWorld::updateCharacterModes(float dt)
{
    reg_.each<CharacterComponent>([&](Entity e, CharacterComponent& c) {
        c.punchCooldown = std::max(0.0f, c.punchCooldown - dt);
        VehicleComponent* v = reg_.get<VehicleComponent>(c.vehicle);
        switch (c.mode) {
        case CharacterMode::Entering: {
            if (!v) {
                c.mode = CharacterMode::OnFoot;
                c.vehicle = kNullEntity;
                break;
            }
            c.modeTimer -= dt;
            const float t = saturate(1.0f - c.modeTimer / kEnterTime);
            const Vec3 seat = vehicleSeatPosition(v->state, vehicleSpec(v->model), c.seat);
            c.body.motor.position = lerp(c.modeFrom, seat, smoothstep(0.0f, 1.0f, t));
            c.body.yaw = v->state.yaw;
            if (c.modeTimer <= 0.0f) {
                c.mode = CharacterMode::InVehicle;
            }
            break;
        }
        case CharacterMode::InVehicle:
            if (!v) {
                c.mode = CharacterMode::OnFoot;
                c.vehicle = kNullEntity;
                break;
            }
            c.body.motor.position = vehicleSeatPosition(v->state, vehicleSpec(v->model), c.seat);
            c.body.motor.velocity = v->state.velocity();
            c.body.yaw = v->state.yaw;
            break;
        case CharacterMode::Exiting: {
            c.modeTimer -= dt;
            const Vec3 door = v ? vehicleDoorPosition(v->state, vehicleSpec(v->model), c.seat) : c.modeFrom;
            const float t = saturate(1.0f - c.modeTimer / kExitTime);
            c.body.motor.position = lerp(c.modeFrom, door, smoothstep(0.0f, 1.0f, t));
            if (c.modeTimer <= 0.0f) {
                c.mode = CharacterMode::OnFoot;
                c.vehicle = kNullEntity;
                c.body.motor.teleport(door, *dynamicWorld_);
            }
            break;
        }
        case CharacterMode::KnockedDown:
            c.modeTimer -= dt;
            c.body.motor.step({}, false, false, dt, *chunks_);
            if (c.modeTimer <= 0.0f) {
                const HealthComponent* h = reg_.get<HealthComponent>(e);
                if (!h || h->health > 0.0f || reg_.has<PlayerComponent>(e)) {
                    c.mode = CharacterMode::OnFoot;
                }
            }
            break;
        case CharacterMode::OnFoot:
            break;
        }
    });
}

void ServerWorld::tryEnterOrExit(Entity e)
{
    CharacterComponent* c = reg_.get<CharacterComponent>(e);
    if (!c) {
        return;
    }
    if (c->mode == CharacterMode::InVehicle) {
        const VehicleComponent* v = reg_.get<VehicleComponent>(c->vehicle);
        if (v && std::fabs(v->state.speed) < 6.0f) {
            exitVehicle(e);
        }
        return;
    }
    if (c->mode != CharacterMode::OnFoot) {
        return;
    }
    // Nearest vehicle whose driver door (or body) is within reach.
    Entity best = kNullEntity;
    float bestD = 3.2f;
    reg_.each<VehicleComponent>([&](Entity ve, VehicleComponent& v) {
        const VehicleSpec& spec = vehicleSpec(v.model);
        const float dDoor = distance(vehicleDoorPosition(v.state, spec, 0), c->body.motor.position);
        const float dBody = distance(v.state.position, c->body.motor.position) - spec.width * 0.5f;
        const float d = std::min(dDoor, dBody + 0.5f);
        if (d < bestD) {
            bestD = d;
            best = ve;
        }
    });
    if (best == kNullEntity) {
        return;
    }
    VehicleComponent* v = reg_.get<VehicleComponent>(best);
    if (v->driver != kNullEntity) {
        if (reg_.has<PlayerComponent>(v->driver)) {
            // A friend is driving: ride along as a passenger.
            const int seat = freeSeat(best, e);
            if (seat < 0) {
                notify(e, EventKind::Info, "That car is full.");
                return;
            }
            enterVehicle(e, best);
            reg_.get<CharacterComponent>(e)->seat = seat;
            return;
        }
        // Carjacking: pull the NPC driver out; they run.
        const Entity victim = v->driver;
        exitVehicle(victim);
        if (NPCComponent* n = reg_.get<NPCComponent>(victim)) {
            n->driver = false;
            n->brain.start(*nav_, vehicleDoorPosition(v->state, vehicleSpec(v->model), 0), rng_);
            n->brain.threaten(c->body.motor.position, 10.0f);
        }
        scareNearby(v->state.position, 15.0f, 6.0f);
        if (reg_.has<PlayerComponent>(e)) {
            addWanted(e, v->model == VehicleModel::Cruiser ? 2 : 1,
                      v->model == VehicleModel::Cruiser ? "Stole a police car" : "Carjacking");
        }
    } else if (reg_.has<PlayerComponent>(e) && v->model == VehicleModel::Cruiser && v->owner != e) {
        addWanted(e, 2, "Stole a police car");
    }
    enterVehicle(e, best);
}

void ServerWorld::enterVehicle(Entity e, Entity vehicle)
{
    CharacterComponent* c = reg_.get<CharacterComponent>(e);
    VehicleComponent* v = reg_.get<VehicleComponent>(vehicle);
    if (!c || !v) {
        return;
    }
    c->mode = CharacterMode::Entering;
    c->modeTimer = kEnterTime;
    c->modeFrom = c->body.motor.position;
    c->vehicle = vehicle;
    c->seat = 0;
    c->crouch = false;
    if (v->driver != kNullEntity && v->driver != e) {
        return; // passenger (the caller assigns the seat)
    }
    v->driver = e;
    v->abandonedTime = 0.0f;
    if (reg_.has<PlayerComponent>(e)) {
        reg_.remove<DriverAIComponent>(vehicle); // the player takes over from the AI
        v->sirens = false;
    }
}

void ServerWorld::exitVehicle(Entity e)
{
    CharacterComponent* c = reg_.get<CharacterComponent>(e);
    if (!c) {
        return;
    }
    VehicleComponent* v = reg_.get<VehicleComponent>(c->vehicle);
    if (v && v->driver == e) {
        v->driver = kNullEntity;
    }
    c->mode = CharacterMode::Exiting;
    c->modeTimer = kExitTime;
    c->modeFrom = c->body.motor.position;
}

void ServerWorld::punch(Entity e)
{
    CharacterComponent* c = reg_.get<CharacterComponent>(e);
    if (!c || c->punchCooldown > 0.0f) {
        return;
    }
    c->punchCooldown = 0.6f;
    c->trigger(2);
    const Vec3 forward{std::sin(c->body.yaw), 0, std::cos(c->body.yaw)};
    const Vec3 at = c->body.motor.position;
    Entity victim = kNullEntity;
    float best = 1.6f;
    reg_.each<CharacterComponent>([&](Entity o, CharacterComponent& oc) {
        if (o == e || oc.mode != CharacterMode::OnFoot) {
            return;
        }
        const Vec3 d = oc.body.motor.position - at;
        const float dist = length(Vec3{d.x, 0, d.z});
        if (dist < best && dot(normalize(Vec3{d.x, 0, d.z}), forward) > 0.4f) {
            best = dist;
            victim = o;
        }
    });
    if (victim == kNullEntity) {
        return;
    }
    knockDown(victim, forward * 3.0f, 1.8f);
    damage(victim, 12.0f, e);
    scareNearby(at, 18.0f, 8.0f);
    if (reg_.has<PlayerComponent>(e) && !reg_.has<PlayerComponent>(victim)) {
        const NPCComponent* n = reg_.get<NPCComponent>(victim);
        addWanted(e, n && n->police ? 2 : 1, n && n->police ? "Assaulted an officer" : "Assault");
    }
}

void ServerWorld::damage(Entity target, float amount, Entity attacker)
{
    (void)attacker;
    HealthComponent* h = reg_.get<HealthComponent>(target);
    if (!h || amount <= 0.0f) {
        return;
    }
    h->health -= amount;
    h->sinceDamage = 0.0f;
    if (h->health <= 0.0f) {
        if (reg_.has<PlayerComponent>(target)) {
            hospitalize(target);
        } else if (CharacterComponent* c = reg_.get<CharacterComponent>(target)) {
            c->mode = CharacterMode::KnockedDown;
            c->modeTimer = 8.0f; // NPCs stay down, then are cleaned up by the population system
        }
    }
}

void ServerWorld::knockDown(Entity target, const Vec3& push, float seconds)
{
    CharacterComponent* c = reg_.get<CharacterComponent>(target);
    if (!c || c->mode == CharacterMode::InVehicle || c->mode == CharacterMode::Entering) {
        return;
    }
    c->mode = CharacterMode::KnockedDown;
    c->modeTimer = std::max(c->modeTimer, seconds);
    c->body.motor.velocity = Vec3{push.x, 3.0f, push.z};
    if (NPCComponent* n = reg_.get<NPCComponent>(target)) {
        n->brain.threaten(c->body.motor.position - push, 8.0f);
    }
}

} // namespace server
