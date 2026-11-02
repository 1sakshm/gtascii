// Weapons (melee, guns, rockets, grenades), explosions, deaths and the loot
// they drop, police gunfire, cheat codes, enterable interiors (home, office,
// club, casino) and helicopter pads.
#include "ServerWorld.h"

#include "EasterEggs.h"
#include "Weapons.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace server {

using namespace av;
using namespace av::proto;

namespace {

constexpr float kChest = 1.3f;
constexpr float kTracerTtl = 0.15f;

float flat(const Vec3& a, const Vec3& b) { return length(Vec3{a.x - b.x, 0, a.z - b.z}); }
std::string cash(int amount) { return "$" + std::to_string(amount); }
Vec3 yawDir(float yaw) { return {std::sin(yaw), 0.0f, std::cos(yaw)}; }

// Distance along `ray` to a standing character (a vertical capsule), or -1.
float rayCharacter(const Ray& ray, const Vec3& feet, float maxT)
{
    const float radius = 0.42f;
    // Closest approach in the horizontal plane, then check the height there.
    const Vec3 d{ray.direction.x, 0.0f, ray.direction.z};
    const Vec3 o{ray.origin.x - feet.x, 0.0f, ray.origin.z - feet.z};
    const float a = dot(d, d);
    if (a < 1e-6f) return -1.0f;
    const float b = dot(o, d);
    const float c = dot(o, o) - radius * radius;
    const float disc = b * b - a * c;
    if (disc < 0.0f) return -1.0f;
    const float t = (-b - std::sqrt(disc)) / a;
    if (t < 0.0f || t > maxT) return -1.0f;
    const float y = ray.origin.y + ray.direction.y * t;
    return (y > feet.y + 0.1f && y < feet.y + 1.9f) ? t : -1.0f;
}

} // namespace

// A clear patch of ground near `around` (no walls within `radius`), for
// spawning vehicles next to a player.
static Vec3 openSpot(const ChunkManager& world, const Vec3& around, float radius, float yaw)
{
    std::vector<AABB> boxes;
    for (float dist : {9.0f, 12.0f, 7.0f, 16.0f}) {
        for (int k = 0; k < 12; ++k) {
            const float a = yaw + static_cast<float>(k) * (kTwoPi / 12.0f);
            const Vec3 p = around + Vec3{std::sin(a) * dist, 0.0f, std::cos(a) * dist};
            boxes.clear();
            world.gatherColliders(AABB({p.x - radius, around.y + 0.3f, p.z - radius}, {p.x + radius, around.y + 4.0f, p.z + radius}), boxes);
            if (boxes.empty() && world.waterLevel(p.x, p.z) < -1.0e8f) return p;
        }
    }
    return around + Vec3{std::sin(yaw), 0.0f, std::cos(yaw)} * 9.0f;
}

// ---- inventory ----------------------------------------------------------------

int ServerWorld::ammoOf(Entity player, int weapon) const
{
    if (weapon == Fists) return 1;
    const PlayerComponent* p = reg_.get<PlayerComponent>(player);
    if (!p) return reg_.has<NPCComponent>(player) ? 999 : 0; // NPCs never run dry
    if (!p->profile) return 0;
    const auto it = p->profile->inventory.find(weaponDef(weapon).name);
    return it == p->profile->inventory.end() ? 0 : it->second;
}

void ServerWorld::giveWeapon(Entity player, int weapon, int ammo)
{
    PlayerComponent* p = reg_.get<PlayerComponent>(player);
    CharacterComponent* c = reg_.get<CharacterComponent>(player);
    if (!p || !p->profile || !isWeapon(weapon) || weapon == Fists) return;
    int& count = p->profile->inventory[weaponDef(weapon).name];
    count = usesAmmo(weapon) ? std::min(9999, count + std::max(ammo, 1)) : 1;
    if (c && c->weapon == Fists) c->weapon = static_cast<std::uint8_t>(weapon);
}

// ---- firing ---------------------------------------------------------------------

void ServerWorld::fireWeapon(Entity shooter, float aimYaw)
{
    CharacterComponent* c = reg_.get<CharacterComponent>(shooter);
    if (!c || c->mode != CharacterMode::OnFoot || c->weaponCooldown > 0.0f || c->dead) return;
    PlayerComponent* p = reg_.get<PlayerComponent>(shooter);
    const int w = isWeapon(c->weapon) ? static_cast<int>(c->weapon) : static_cast<int>(Fists);
    const WeaponDef& def = weaponDef(w);
    if (usesAmmo(w) && ammoOf(shooter, w) <= 0) {
        c->weapon = Fists;
        return;
    }
    c->weaponCooldown = def.interval;
    c->body.yaw = aimYaw;
    const Vec3 feet = c->body.motor.position;
    const Vec3 forward = yawDir(aimYaw);

    if (def.kind == WeaponKind::Melee) {
        c->trigger(2);
        Entity victim = kNullEntity;
        float best = def.range;
        reg_.each<CharacterComponent>([&](Entity o, CharacterComponent& oc) {
            if (o == shooter || oc.mode != CharacterMode::OnFoot || oc.dead) return;
            const Vec3 d = oc.body.motor.position - feet;
            const float dist = length(Vec3{d.x, 0, d.z});
            if (dist < best && std::fabs(d.y) < 1.5f && dot(normalize(Vec3{d.x, 0, d.z}), forward) > 0.4f) {
                best = dist;
                victim = o;
            }
        });
        if (victim == kNullEntity) return;
        knockDown(victim, forward * (w == Fists ? 3.0f : 4.5f), 1.8f);
        damage(victim, def.damage, shooter);
        scareNearby(feet, 18.0f, 8.0f);
        if (p && !reg_.has<PlayerComponent>(victim)) {
            const NPCComponent* n = reg_.get<NPCComponent>(victim);
            addWanted(shooter, n && n->police ? 2 : 1, n && n->police ? "Assaulted an officer" : "Assault");
        }
        return;
    }

    if (p && p->profile) {
        int& count = p->profile->inventory[def.name];
        count = std::max(0, count - 1);
        if (count == 0) p->profile->inventory.erase(def.name);
    }
    c->trigger(3);
    const Vec3 muzzle = feet + Vec3{0.0f, kChest + 0.1f, 0.0f} + forward * 0.6f;

    // Auto-aim: the target closest to the crosshair inside the weapon's cone.
    Entity target = kNullEntity;
    float bestScore = 1e9f;
    reg_.each<CharacterComponent>([&](Entity o, CharacterComponent& oc) {
        if (o == shooter || oc.dead || oc.mode == CharacterMode::Entering) return;
        const HealthComponent* h = reg_.get<HealthComponent>(o);
        if (h && h->health <= 0.0f) return;
        const Vec3 to = oc.body.motor.position + Vec3{0, kChest, 0} - muzzle;
        const float dist = length(to);
        if (dist > def.range || dist < 0.3f) return;
        const float angle = std::acos(std::clamp(dot(normalize(Vec3{to.x, 0, to.z}), forward), -1.0f, 1.0f));
        const float cone = def.kind == WeaponKind::Grenade ? 0.0f : std::max(def.aimCone, 0.6f / std::max(dist, 1.0f));
        if (angle > cone) return;
        const float score = angle + dist * 0.004f;
        if (score >= bestScore) return;
        if (chunks_->raycast(Ray{muzzle, normalize(to)}, dist) < dist - 0.5f) return; // behind a wall
        bestScore = score;
        target = o;
    });
    Vec3 aim = forward;
    if (target != kNullEntity) {
        const CharacterComponent* tc = reg_.get<CharacterComponent>(target);
        aim = normalize(tc->body.motor.position + Vec3{0, kChest, 0} - muzzle);
    }

    if (def.kind == WeaponKind::Rocket || def.kind == WeaponKind::Grenade) {
        const Entity e = reg_.create();
        reg_.add<TransformComponent>(e, {muzzle, aimYaw});
        ProjectileComponent pr;
        pr.weapon = static_cast<std::uint8_t>(w);
        pr.owner = shooter;
        if (def.kind == WeaponKind::Rocket) {
            pr.velocity = aim * 45.0f;
            pr.fuse = 3.0f;
        } else {
            pr.velocity = forward * 13.0f + Vec3{0, 6.0f, 0};
            pr.fuse = 2.2f;
        }
        reg_.add<ProjectileComponent>(e, pr);
        assignNetId(e);
        scareNearby(feet, 30.0f, 8.0f);
        if (p) addWanted(shooter, 1, "Firing a weapon");
        return;
    }

    // Guns: one hitscan ray per pellet.
    const bool npc = p == nullptr;
    for (int pellet = 0; pellet < def.pellets; ++pellet) {
        float spread = def.spread * (target != kNullEntity ? 0.5f : 1.0f);
        if (npc) spread += 0.12f; // police are not marksmen
        Vec3 dir = aim;
        const float ry = rng_.range(-spread, spread), rp = rng_.range(-spread, spread) * 0.6f;
        dir = normalize(Vec3{dir.x * std::cos(ry) + dir.z * std::sin(ry), dir.y + rp, -dir.x * std::sin(ry) + dir.z * std::cos(ry)});
        const Ray ray{muzzle, dir};
        float hitT = chunks_->raycast(ray, def.range);
        Entity hit = kNullEntity;
        bool hitVehicle = false;
        reg_.each<CharacterComponent>([&](Entity o, CharacterComponent& oc) {
            if (o == shooter || oc.dead || oc.mode == CharacterMode::InVehicle) return;
            const float t = rayCharacter(ray, oc.body.motor.position, hitT);
            if (t >= 0.0f && t < hitT) {
                hitT = t;
                hit = o;
            }
        });
        reg_.each<VehicleComponent>([&](Entity ve, VehicleComponent& v) {
            if (lengthSq(v.state.position - muzzle) > (def.range + 6.0f) * (def.range + 6.0f)) return;
            std::vector<AABB> boxes;
            vehicleBoxes(v.state, vehicleSpec(v.model), boxes);
            for (const AABB& b : boxes) {
                const float t = intersectRayAABB(ray, b, hitT);
                if (t >= 0.0f && t < hitT) {
                    hitT = t;
                    hit = ve;
                    hitVehicle = true;
                }
            }
        });
        if (pellet == 0 || pellet % 3 == 0) spawnEffect(EffectType::Tracer, muzzle, ray.at(hitT), kTracerTtl);
        if (hit == kNullEntity) continue;
        if (hitVehicle) {
            VehicleComponent* v = reg_.get<VehicleComponent>(hit);
            v->state.damage = std::min(1.0f, v->state.damage + def.damage / 450.0f);
            continue;
        }
        float amount = def.damage * (npc ? 0.4f : 1.0f);
        damage(hit, amount, shooter);
        if (w == Shotgun || w == Sniper) knockDown(hit, dir * 4.0f, 1.5f);
        if (p && !reg_.has<PlayerComponent>(hit)) {
            const NPCComponent* n = reg_.get<NPCComponent>(hit);
            addWanted(shooter, n && n->police ? 2 : 1, n && n->police ? "Shot an officer" : "Shot someone");
        }
        if (NPCComponent* n = reg_.get<NPCComponent>(hit)) n->brain.threaten(feet, 12.0f);
    }
    scareNearby(feet, 40.0f, 10.0f);
    if (p) addWanted(shooter, 1, "Firing a weapon");
}

void ServerWorld::explode(const Vec3& at, int weapon, Entity owner)
{
    const WeaponDef& def = weaponDef(weapon);
    const float radius = def.range;
    spawnEffect(EffectType::Explosion, at, at, 0.9f);
    reg_.each<CharacterComponent>([&](Entity o, CharacterComponent& oc) {
        if (oc.dead) return;
        const Vec3 d = oc.body.motor.position + Vec3{0, 0.9f, 0} - at;
        const float dist = length(d);
        if (dist > radius) return;
        const float f = 1.0f - dist / radius;
        if (oc.mode == CharacterMode::OnFoot || oc.mode == CharacterMode::KnockedDown) {
            knockDown(o, normalize(Vec3{d.x, 0.0f, d.z}) * (4.0f + 6.0f * f), 2.5f);
        }
        damage(o, def.damage * f, owner);
    });
    reg_.each<VehicleComponent>([&](Entity, VehicleComponent& v) {
        const float dist = distance(v.state.position, at);
        if (dist < radius + 2.0f) {
            v.state.damage = std::min(1.0f, v.state.damage + 1.5f * (1.0f - dist / (radius + 2.0f)) + 0.1f);
        }
    });
    scareNearby(at, 60.0f, 12.0f);
    if (reg_.has<PlayerComponent>(owner)) addWanted(owner, 2, "Explosion");
}

void ServerWorld::spawnEffect(EffectType type, const Vec3& from, const Vec3& to, float ttl)
{
    const Entity e = reg_.create();
    reg_.add<TransformComponent>(e, {from, 0.0f});
    EffectComponent fx;
    fx.type = type;
    fx.ttl = ttl;
    fx.to = to;
    reg_.add<EffectComponent>(e, fx);
    assignNetId(e);
}

void ServerWorld::spawnDrop(const Vec3& at, std::uint8_t type, int amount, float ttl)
{
    const Entity e = reg_.create();
    Vec3 p = at;
    p.y = chunks_->groundHeight(at.x, at.z);
    if (at.y > p.y + 0.5f) p.y = at.y; // on a floor (interiors, roofs)
    reg_.add<TransformComponent>(e, {p, 0.0f});
    DropComponent d;
    d.type = type;
    d.amount = amount;
    d.ttl = ttl;
    reg_.add<DropComponent>(e, d);
    assignNetId(e);
}

// ---- death and loot -----------------------------------------------------------

void ServerWorld::killed(Entity victim, Entity killer)
{
    CharacterComponent* c = reg_.get<CharacterComponent>(victim);
    if (!c) return;
    const Vec3 at = c->body.motor.position;
    auto scatter = [&](float r) { return at + Vec3{rng_.range(-r, r), 0.0f, rng_.range(-r, r)}; };
    if (PlayerComponent* p = reg_.get<PlayerComponent>(victim)) {
        if (p->profile) {
            const int lost = std::min(p->profile->money / 10, 5000);
            if (lost > 0) {
                p->profile->money -= lost;
                spawnDrop(scatter(0.8f), PickupType::Cash, lost, 90.0f);
            }
            if (c->weapon != Fists) {
                const int w = c->weapon;
                const int ammo = ammoOf(victim, w);
                p->profile->inventory.erase(weaponDef(w).name);
                spawnDrop(scatter(1.0f), static_cast<std::uint8_t>(PickupType::WeaponBase + w), std::max(ammo, 1), 90.0f);
                c->weapon = Fists;
            }
        }
        if (killer != victim && reg_.has<PlayerComponent>(killer)) {
            broadcast(EventKind::Info, playerName(killer) + " killed " + p->name);
            if (PlayerComponent* kp = reg_.get<PlayerComponent>(killer)) ++kp->points;
        }
        return;
    }
    // NPC.
    c->dead = true;
    c->mode = CharacterMode::KnockedDown;
    c->modeTimer = 25.0f;
    const NPCComponent* n = reg_.get<NPCComponent>(victim);
    const bool police = n && n->police;
    spawnDrop(scatter(0.7f), PickupType::Cash, police ? rng_.rangeInt(40, 150) : rng_.rangeInt(5, 80), 60.0f);
    const int weapon = c->weapon != Fists ? c->weapon : (police ? Pistol : (rng_.chance(0.15f) ? rng_.rangeInt(Bat, Smg) : -1));
    if (weapon > 0) {
        spawnDrop(scatter(0.9f), static_cast<std::uint8_t>(PickupType::WeaponBase + weapon), usesAmmo(weapon) ? weaponDef(weapon).pickupAmmo / 2 : 1, 60.0f);
    }
    if (reg_.has<PlayerComponent>(killer)) addWanted(killer, police ? 2 : 1, police ? "Killed an officer" : "Murder");
}

void ServerWorld::collectDrops(Entity player)
{
    PlayerComponent* p = reg_.get<PlayerComponent>(player);
    CharacterComponent* c = reg_.get<CharacterComponent>(player);
    if (!p || !c || !p->profile || c->mode != CharacterMode::OnFoot) return;
    const Vec3 pos = c->body.motor.position;
    for (const Entity e : reg_.entitiesWith<EggComponent>()) {
        const EggComponent* egg = reg_.get<EggComponent>(e);
        const TransformComponent* t = reg_.get<TransformComponent>(e);
        if (!egg || !t || p->profile->pickups.count(kGoldenEggIdBase + egg->index) || flat(t->position, pos) > 1.6f ||
            std::fabs(t->position.y - pos.y - 0.6f) > 2.2f) {
            continue;
        }
        p->profile->pickups.insert(kGoldenEggIdBase + egg->index);
        int found = 0;
        for (int k = 0; k < kGoldenEggCount; ++k) found += p->profile->pickups.count(kGoldenEggIdBase + k) ? 1 : 0;
        const int total = static_cast<int>(reg_.count<EggComponent>());
        if (found < total) {
            notify(player, EventKind::Pickup, "GOLDEN EGG " + std::to_string(found) + "/" + std::to_string(total) + " - found " + egg->hint +
                                                  ". Others hide high up, far out, and where the music is loud.");
        } else {
            p->profile->money += 100000;
            p->profile->ownedVehicles.push_back({static_cast<int>(VehicleModel::Sports), kGoldPaint});
            const Entity car = spawnVehicle(VehicleModel::Sports, kGoldPaint, openSpot(*chunks_, pos, 2.8f, c->body.yaw), c->body.yaw);
            if (VehicleComponent* vc = reg_.get<VehicleComponent>(car)) {
                vc->owner = player;
                vc->ownerName = p->name;
            }
            notify(player, EventKind::MissionPassed, "ALL GOLDEN EGGS! +$100,000 and a solid gold Vortex GT (it's yours, and it waits at your safehouse)", 100000);
            broadcast(EventKind::Info, p->name + " found every golden egg!");
        }
        save();
    }
    for (const Entity e : reg_.entitiesWith<DropComponent>()) {
        DropComponent* d = reg_.get<DropComponent>(e);
        const TransformComponent* t = reg_.get<TransformComponent>(e);
        if (!d || !t || d->respawn > 0.0f || flat(t->position, pos) > 1.3f || std::fabs(t->position.y - pos.y) > 2.0f) continue;
        if (d->type == PickupType::Cash) {
            p->profile->money += d->amount;
            notify(player, EventKind::Money, "Picked up " + cash(d->amount), d->amount);
        } else if (d->type == PickupType::Health) {
            HealthComponent* h = reg_.get<HealthComponent>(player);
            if (!h || h->health >= h->maxHealth) continue;
            h->health = h->maxHealth;
            notify(player, EventKind::Pickup, "Health restored");
        } else if (d->type == PickupType::Jetpack) {
            p->profile->inventory["Jetpack"] = 1;
            c->body.jetpack = true;
            notify(player, EventKind::Pickup, "JETPACK! Space: up, Z: down. Press J to put it on or take it off");
        } else if (d->type >= PickupType::WeaponBase && isWeapon(d->type - PickupType::WeaponBase)) {
            const int w = d->type - PickupType::WeaponBase;
            giveWeapon(player, w, d->amount);
            notify(player, EventKind::Pickup,
                   std::string("Picked up ") + weaponDef(w).name + (usesAmmo(w) ? " (" + std::to_string(d->amount) + ")" : ""));
        } else {
            continue;
        }
        if (d->ttl < 0.0f) {
            d->respawn = 45.0f; // a permanent spawn: back in a while
        } else {
            destroyEntity(e);
        }
    }
}

// ---- per tick -----------------------------------------------------------------

void ServerWorld::updateCombat(float dt)
{
    reg_.each<CharacterComponent>([dt](Entity, CharacterComponent& c) { c.weaponCooldown = std::max(0.0f, c.weaponCooldown - dt); });
    reg_.each<PlayerComponent>([dt](Entity, PlayerComponent& p) {
        p.casinoCooldown = std::max(0.0f, p.casinoCooldown - dt);
        p.stocksCooldown = std::max(0.0f, p.stocksCooldown - dt);
    });

    // Effects fade.
    for (const Entity e : reg_.entitiesWith<EffectComponent>()) {
        EffectComponent* fx = reg_.get<EffectComponent>(e);
        if (fx && (fx->ttl -= dt) <= 0.0f) destroyEntity(e);
    }

    // Rockets and grenades.
    for (const Entity e : reg_.entitiesWith<ProjectileComponent>()) {
        ProjectileComponent* pr = reg_.get<ProjectileComponent>(e);
        TransformComponent* t = reg_.get<TransformComponent>(e);
        if (!pr || !t) continue;
        pr->fuse -= dt;
        const Vec3 from = t->position;
        bool boom = pr->fuse <= 0.0f;
        if (pr->weapon == Grenades) {
            pr->velocity.y -= 18.0f * dt;
            Vec3 next = from + pr->velocity * dt;
            const float ground = std::max(chunks_->groundHeight(next.x, next.z), chunks_->waterLevel(next.x, next.z));
            if (next.y < ground + 0.1f) {
                next.y = ground + 0.1f;
                pr->velocity = Vec3{pr->velocity.x * 0.55f, -pr->velocity.y * 0.35f, pr->velocity.z * 0.55f};
            }
            const float len = length(next - from);
            if (len > 1e-4f && chunks_->raycast(Ray{from, (next - from) / len}, len) < len) {
                pr->velocity = Vec3{-pr->velocity.x * 0.4f, pr->velocity.y, -pr->velocity.z * 0.4f}; // bounce off walls
                next = from;
            }
            t->position = next;
        } else {
            const Vec3 next = from + pr->velocity * dt;
            const float len = length(next - from);
            const float wall = chunks_->raycast(Ray{from, (next - from) / std::max(len, 1e-4f)}, len);
            Vec3 hitAt = next;
            if (wall < len) {
                boom = true;
                hitAt = from + (next - from) * (wall / std::max(len, 1e-4f));
            }
            reg_.each<CharacterComponent>([&](Entity o, CharacterComponent& oc) {
                if (o != pr->owner && !oc.dead && distance(oc.body.motor.position + Vec3{0, 0.9f, 0}, next) < 1.0f) boom = true;
            });
            reg_.each<VehicleComponent>([&](Entity, VehicleComponent& v) {
                if (boom || lengthSq(v.state.position - next) > 100.0f) return;
                std::vector<AABB> boxes;
                vehicleBoxes(v.state, vehicleSpec(v.model), boxes);
                for (const AABB& b : boxes) {
                    const AABB grown(b.min - Vec3{0.3f, 0.3f, 0.3f}, b.max + Vec3{0.3f, 0.5f, 0.3f});
                    const float hitT = intersectRayAABB(Ray{from, (next - from) / std::max(len, 1e-4f)}, grown, len);
                    if (hitT >= 0.0f) {
                        boom = true;
                        hitAt = from + (next - from) * (hitT / std::max(len, 1e-4f));
                    }
                }
            });
            if (next.y < chunks_->groundHeight(next.x, next.z)) boom = true;
            t->position = hitAt;
            t->yaw = std::atan2(pr->velocity.x, pr->velocity.z);
        }
        if (boom) {
            const Vec3 at = t->position;
            const int weapon = pr->weapon;
            const Entity owner = pr->owner;
            destroyEntity(e);
            explode(at, weapon, owner);
        }
    }

    // Wrecks blow up (once) and take their occupants with them.
    for (const Entity ve : reg_.entitiesWith<VehicleComponent>()) {
        VehicleComponent* v = reg_.get<VehicleComponent>(ve);
        if (!v || v->exploded || v->state.damage < 1.0f || v->state.drowned) continue;
        v->exploded = true;
        const Vec3 at = v->state.position + Vec3{0, 1.0f, 0};
        reg_.each<CharacterComponent>([&](Entity o, CharacterComponent& oc) {
            if (oc.vehicle == ve && oc.mode == CharacterMode::InVehicle) damage(o, 300.0f, kNullEntity);
        });
        explode(at, RocketLauncher, kNullEntity);
    }

    // Loot: expire; permanent spawns come back.
    for (const Entity e : reg_.entitiesWith<DropComponent>()) {
        DropComponent* d = reg_.get<DropComponent>(e);
        if (!d) continue;
        if (d->ttl >= 0.0f) {
            if ((d->ttl -= dt) <= 0.0f) destroyEntity(e);
        } else if (d->respawn > 0.0f) {
            d->respawn = std::max(0.0f, d->respawn - dt);
        }
    }
    for (const Entity e : reg_.entitiesWith<PlayerComponent>()) collectDrops(e);

    // Club crowd: sway and wave.
    reg_.each<NPCComponent>([&](Entity e, NPCComponent& n) {
        if (!n.dancer) return;
        CharacterComponent* c = reg_.get<CharacterComponent>(e);
        if (!c || c->dead || c->mode != CharacterMode::OnFoot) return;
        c->body.yaw = wrapAngle(c->body.yaw + std::sin(static_cast<float>(time_) * 2.0f + static_cast<float>(e)) * 1.5f * dt);
        if (rng_.chance(dt * 0.4f)) c->trigger(1);
        c->body.motor.step({}, rng_.chance(dt * 0.25f), false, dt, *dynamicWorld_);
    });

    updatePoliceGunfire(dt);

    // Helicopters wait at their pads.
    helipadTimer_ -= dt;
    if (helipadTimer_ <= 0.0f && !helipads_.empty()) {
        helipadTimer_ = 20.0f;
        helipadHelis_.resize(helipads_.size(), kNullEntity);
        for (std::size_t i = 0; i < helipads_.size(); ++i) {
            const Vec3 pad = helipads_[i].first;
            if (distanceToNearestPlayer(pad) > 150.0f) continue; // only where someone can see it appear
            const VehicleComponent* v = reg_.get<VehicleComponent>(helipadHelis_[i]);
            const bool parked = v && flat(v->state.position, pad) < 15.0f && v->state.damage < 1.0f;
            const bool someoneNear = distanceToNearestPlayer(pad) < 25.0f;
            if (!parked && !someoneNear) helipadHelis_[i] = spawnHelicopter(pad, helipads_[i].second);
        }
    }
}

void ServerWorld::updatePoliceGunfire(float dt)
{
    (void)dt;
    for (const Entity e : reg_.entitiesWith<PoliceComponent>()) {
        const PoliceComponent* pol = reg_.get<PoliceComponent>(e);
        CharacterComponent* c = reg_.get<CharacterComponent>(e);
        if (!pol || !c || c->dead || c->mode != CharacterMode::OnFoot || c->weaponCooldown > 0.0f) continue;
        const PlayerComponent* target = reg_.get<PlayerComponent>(pol->target);
        const CharacterComponent* tc = reg_.get<CharacterComponent>(pol->target);
        if (!target || !tc || target->wanted < 2) continue;
        const Vec3 to = tc->body.motor.position - c->body.motor.position;
        const float dist = length(to);
        if (dist > 28.0f || dist < 3.0f) continue; // close in: they try to cuff you instead
        c->weapon = Pistol;
        fireWeapon(e, std::atan2(to.x, to.z));
        c->weaponCooldown = rng_.range(1.0f, 1.8f);
    }
}

Entity ServerWorld::spawnHelicopter(const Vec3& at, float yaw)
{
    const Entity e = spawnVehicle(VehicleModel::Helicopter, 0, at, yaw);
    if (VehicleComponent* v = reg_.get<VehicleComponent>(e)) v->missionVehicle = true; // not traffic: never despawned by the population system
    return e;
}

// ---- cheats ---------------------------------------------------------------------

bool ServerWorld::tryCheat(Entity player, const std::string& text)
{
    std::string code;
    for (char ch : text) {
        if (std::isalpha(static_cast<unsigned char>(ch))) code += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    static const char* kCodes[] = {"fullclip", "healme", "richboy", "calmdown", "maxwanted", "flyboy",
                                   "rocketman", "speedy", "iamgod", "sunrise", "midnight", "boomtown",
                                   "letsparty", "highroller"};
    if (std::none_of(std::begin(kCodes), std::end(kCodes), [&](const char* k) { return code == k; })) return false;
    PlayerComponent* p = reg_.get<PlayerComponent>(player);
    CharacterComponent* c = reg_.get<CharacterComponent>(player);
    HealthComponent* h = reg_.get<HealthComponent>(player);
    if (!p || !c || !p->profile) return true;
    if (!config_.allowCheats) {
        notify(player, EventKind::Info, "Cheats are disabled on this server.");
        return true;
    }
    const Vec3 pos = c->body.motor.position;
    std::string what;
    if (code == "fullclip") {
        for (int w = Bat; w < kWeaponCount; ++w) giveWeapon(player, w, weaponDef(w).pickupAmmo * 5);
        what = "all weapons";
    } else if (code == "healme") {
        if (h) h->health = h->maxHealth;
        what = "full health";
    } else if (code == "richboy") {
        p->profile->money += 50000;
        what = "+$50000";
    } else if (code == "calmdown") {
        p->wanted = 0;
        what = "wanted level cleared";
    } else if (code == "maxwanted") {
        p->wanted = 4;
        addWanted(player, 1, "Cheating");
        what = "five stars - good luck";
    } else if (code == "flyboy") {
        spawnHelicopter(openSpot(*chunks_, pos, 5.5f, c->body.yaw), c->body.yaw);
        what = "a helicopter";
    } else if (code == "rocketman") {
        p->profile->inventory["Jetpack"] = 1;
        c->body.jetpack = true;
        what = "a jetpack (J toggles it, Space up, Z down)";
    } else if (code == "speedy") {
        spawnVehicle(VehicleModel::Sports, rng_.rangeInt(0, 7), openSpot(*chunks_, pos, 2.8f, c->body.yaw + kHalfPi), c->body.yaw);
        what = "a Vortex GT";
    } else if (code == "iamgod") {
        p->god = !p->god;
        what = p->god ? "invincible" : "mortal again";
    } else if (code == "sunrise") {
        hour_ = 7.0f;
        what = "morning";
    } else if (code == "midnight") {
        hour_ = 0.0f;
        what = "midnight";
    } else if (code == "letsparty" || code == "highroller") {
        const InteriorKind kind = code == "letsparty" ? InteriorKind::Club : InteriorKind::Casino;
        for (std::size_t i = 0; i < interiors_.size(); ++i) {
            if (interiors_[i].kind != kind) continue;
            // Walk in through the front door (dancers and all).
            InteractionComponent door;
            door.action = InteractionComponent::Action::Enter;
            door.interior = static_cast<int>(i);
            const int wanted = p->wanted;
            p->wanted = 0;
            interiorInteract(player, door);
            p->wanted = wanted;
            what = "a ride to " + interiors_[i].name;
        }
    } else if (code == "boomtown") {
        reg_.each<VehicleComponent>([&](Entity, VehicleComponent& v) {
            if (distance(v.state.position, pos) < 40.0f && v.driver != player) v.state.damage = 1.0f;
        });
        what = "every car nearby";
    }
    notify(player, EventKind::Info, "Cheat activated: " + what);
    return true;
}

// ---- interiors ---------------------------------------------------------------------

void ServerWorld::setupCombat()
{
    interiors_ = loadInteriors(dataRoot_ / "assets/maps/interiors.json");
    chunks_->extraColliders = staticExtraColliders(interiors_);
    auto add = [this](MarkerKind kind, const Vec3& at, InteractionComponent::Action action, int interior, float radius,
                      const std::string& label) {
        const Entity e = reg_.create();
        reg_.add<TransformComponent>(e, {at, 0.0f});
        InteractionComponent ic;
        ic.kind = kind;
        ic.action = action;
        ic.interior = interior;
        ic.radius = radius;
        ic.label = label;
        reg_.add<InteractionComponent>(e, ic);
        if (kind != MarkerKind::None) assignNetId(e);
    };
    for (std::size_t i = 0; i < interiors_.size(); ++i) {
        const InteriorDef& d = interiors_[i];
        const int idx = static_cast<int>(i);
        add(MarkerKind::Door, {d.door.x, layout_->groundHeight(d.door.x, d.door.z), d.door.z}, InteractionComponent::Action::Enter, idx, 1.8f, d.name);
        add(MarkerKind::Door, d.exit, InteractionComponent::Action::Exit, idx, 1.5f, d.name);
        for (const Vec3& s : d.slots) add(MarkerKind::None, s, InteractionComponent::Action::Slots, idx, 1.2f, "slots");
        for (const Vec3& s : d.computers) add(MarkerKind::None, s, InteractionComponent::Action::Stocks, idx, 1.1f, "stocks");
        if (d.kind == InteriorKind::Home) add(MarkerKind::None, d.bed, InteractionComponent::Action::Sleep, idx, 1.8f, "bed");
    }

    // Weapons lying around town (at the collectible spots, which are known to be reachable), a jetpack and health.
    for (int w = Bat; w < kWeaponCount && !pickupSpots_.empty(); ++w) {
        const Vec3 at = pickupSpots_[static_cast<std::size_t>(w * 2) % pickupSpots_.size()] + Vec3{1.6f, 0.0f, 0.8f};
        const Entity e = reg_.create();
        reg_.add<TransformComponent>(e, {at, 0.0f});
        reg_.add<DropComponent>(e, {static_cast<std::uint8_t>(PickupType::WeaponBase + w), usesAmmo(w) ? weaponDef(w).pickupAmmo : 1, -1.0f, 0.0f});
        assignNetId(e);
    }
    auto permanent = [this](const Vec3& at, std::uint8_t type) {
        const Entity e = reg_.create();
        reg_.add<TransformComponent>(e, {at, 0.0f});
        reg_.add<DropComponent>(e, {type, 1, -1.0f, 0.0f});
        assignNetId(e);
    };
    if (!pickupSpots_.empty()) permanent(pickupSpots_.back() + Vec3{-1.5f, 0.0f, 0.0f}, PickupType::Jetpack);
    permanent(landmarkPoint("hospital", "respawn", layout_->spawn().position) + Vec3{2.5f, 0.0f, 0.0f}, PickupType::Health);
    permanent(landmarkPoint("police_station", "respawn", layout_->spawn().position) + Vec3{-2.5f, 0.0f, 0.0f}, PickupType::Health);

    // Helipads: Bayfront Park and the lighthouse beach.
    if (const Landmark* park = layout_->findLandmark("park")) {
        const Vec3 at{park->chunk.x * kChunkSize + 32.0f, 0.0f, park->chunk.z * kChunkSize + 32.0f};
        helipads_.push_back({at, 0.0f});
    }
    if (const Landmark* lh = layout_->findLandmark("lighthouse")) {
        const Vec3 at{lh->chunk.x * kChunkSize + 24.0f, 0.0f, lh->chunk.z * kChunkSize + 46.0f};
        helipads_.push_back({at, kHalfPi});
    }
    for (const auto& [at, yaw] : helipads_) {
        add(MarkerKind::Helipad, at + Vec3{0.0f, layout_->groundHeight(at.x, at.z), 0.0f}, InteractionComponent::Action::Marker, -1, 0.0f, "Helipad");
        (void)yaw;
    }

    // Golden eggs. Rooftop ones sit on the tallest thing in their chunk.
    const std::vector<EggSpot> spots = eggSpots(interiors_);
    for (std::size_t i = 0; i < spots.size(); ++i) {
        Vec3 at = spots[i].position;
        if (spots[i].where == EggSpot::Where::HighestRoof) {
            const auto chunk = generator_->generate(ChunkCoord{spots[i].chunkX, spots[i].chunkZ});
            float best = -1.0f;
            for (const AABB& b : chunk->colliders) {
                if (b.max.y > best) {
                    best = b.max.y;
                    at = {(b.min.x + b.max.x) * 0.5f, b.max.y + 0.25f, (b.min.z + b.max.z) * 0.5f};
                }
            }
        }
        const Entity e = reg_.create();
        reg_.add<TransformComponent>(e, {at, 0.0f});
        reg_.add<EggComponent>(e, {static_cast<int>(i), spots[i].hint});
        assignNetId(e);
    }
}

std::string ServerWorld::interiorPrompt(Entity player, const InteractionComponent& ic) const
{
    const PlayerComponent* p = reg_.get<PlayerComponent>(player);
    if (ic.interior < 0 || ic.interior >= static_cast<int>(interiors_.size()) || !p) return "";
    const InteriorDef& d = interiors_[static_cast<std::size_t>(ic.interior)];
    switch (ic.action) {
    case InteractionComponent::Action::Enter: return p->wanted > 0 ? d.name + " - lose the cops first" : "Press E to enter " + d.name;
    case InteractionComponent::Action::Exit: return "Press E to leave " + d.name;
    case InteractionComponent::Action::Slots: return "Press E to play the slots ($100)";
    case InteractionComponent::Action::Stocks:
        return p->stocksCooldown > 0.0f ? "Market closed - try again in " + std::to_string(static_cast<int>(p->stocksCooldown)) + " s"
                                        : "Press E to trade stocks ($1000 stake)";
    case InteractionComponent::Action::Sleep: return "Press E to sleep until morning and save";
    default: return "";
    }
}

bool ServerWorld::interiorInteract(Entity player, InteractionComponent& ic)
{
    PlayerComponent* p = reg_.get<PlayerComponent>(player);
    CharacterComponent* c = reg_.get<CharacterComponent>(player);
    if (!p || !c || !p->profile || ic.interior < 0 || ic.interior >= static_cast<int>(interiors_.size())) return false;
    const InteriorDef& d = interiors_[static_cast<std::size_t>(ic.interior)];
    switch (ic.action) {
    case InteractionComponent::Action::Enter: {
        if (p->wanted > 0) {
            notify(player, EventKind::Info, "The doorman won't let you in with the police on your tail.");
            return true;
        }
        c->body.jetpack = false;
        c->body.motor.teleport(d.entry, *dynamicWorld_);
        c->body.yaw = d.entryYaw;
        notify(player, EventKind::Info, "Welcome to " + d.name);
        if (d.kind == InteriorKind::Club) {
            int present = 0;
            reg_.each<NPCComponent>([&](Entity e, NPCComponent& n) {
                const CharacterComponent* dc = reg_.get<CharacterComponent>(e);
                if (n.dancer && dc && !dc->dead && d.contains(dc->body.motor.position)) ++present;
            });
            for (std::size_t k = static_cast<std::size_t>(present); k < d.dancers.size(); ++k) {
                const Entity e = spawnCharacter(d.dancers[k], rng_.range(-kPi, kPi), static_cast<std::uint32_t>(rng_.next()));
                NPCComponent n;
                n.name = "Dancer";
                n.dancer = true;
                reg_.add<NPCComponent>(e, n);
            }
        }
        return true;
    }
    case InteractionComponent::Action::Exit: {
        const Vec3 out = d.door + yawDir(d.doorYaw) * 1.6f;
        c->body.motor.teleport({out.x, layout_->groundHeight(out.x, out.z) + 0.2f, out.z}, *dynamicWorld_);
        c->body.yaw = d.doorYaw;
        return true;
    }
    case InteractionComponent::Action::Slots: {
        if (p->casinoCooldown > 0.0f) return true;
        if (p->profile->money < 100) {
            notify(player, EventKind::Info, "You need $100 to play.");
            return true;
        }
        p->casinoCooldown = 1.2f;
        p->profile->money -= 100;
        const float roll = rng_.uniform();
        const int win = roll < 0.015f ? 5000 : roll < 0.07f ? 500 : roll < 0.30f ? 200 : roll < 0.42f ? 100 : 0;
        p->profile->money += win;
        if (win >= 5000) {
            notify(player, EventKind::MissionPassed, "JACKPOT! " + cash(win), win);
            broadcast(EventKind::Info, p->name + " hit the jackpot at the " + d.name + "!");
        } else if (win > 0) {
            notify(player, EventKind::Money, "Cherries! You win " + cash(win), win - 100);
        } else {
            notify(player, EventKind::Money, "No luck. -$100", -100);
        }
        return true;
    }
    case InteractionComponent::Action::Stocks: {
        if (p->stocksCooldown > 0.0f) return true;
        if (p->profile->money < 1000) {
            notify(player, EventKind::Info, "You need $1000 to trade.");
            return true;
        }
        p->stocksCooldown = 30.0f;
        const int change = static_cast<int>(rng_.range(-0.25f, 0.45f) * 1000.0f);
        p->profile->money += change;
        notify(player, EventKind::Money, change >= 0 ? "Your stocks went up: +" + cash(change) : "The market crashed: -" + cash(-change), change);
        return true;
    }
    case InteractionComponent::Action::Sleep: {
        hour_ = 8.0f;
        if (HealthComponent* h = reg_.get<HealthComponent>(player)) h->health = h->maxHealth;
        p->profile->hasSavePoint = true;
        save();
        notify(player, EventKind::Saved, "You slept until 8 AM. Game saved.");
        return true;
    }
    default:
        return false;
    }
}

} // namespace server
