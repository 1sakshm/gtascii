// Missions, interaction points (save, dealership), collectibles and the
// money they move around.
#include "ServerWorld.h"

#include <cmath>

namespace server {

using namespace av;
using namespace av::proto;

namespace {
constexpr int kPickupReward = 100;
constexpr int kAllPickupsBonus = 5000;
const VehicleModel kDealershipStock[] = {VehicleModel::Sedan, VehicleModel::Van, VehicleModel::Coupe, VehicleModel::Sports};

float flatDistance(const Vec3& a, const Vec3& b) { return length(Vec3{a.x - b.x, 0, a.z - b.z}); }

std::string money(int amount) { return "$" + std::to_string(amount); }
} // namespace

void ServerWorld::spawnOwnedVehicles(Entity player)
{
    PlayerComponent* p = reg_.get<PlayerComponent>(player);
    if (!p || !p->profile) {
        return;
    }
    const Vec3 garage = landmarkPoint("safehouse", "garage", layout_->spawn().position + Vec3{12, 0, 0});
    for (std::size_t i = 0; i < p->profile->ownedVehicles.size(); ++i) {
        const auto [model, paint] = p->profile->ownedVehicles[i];
        // First car in the driveway, the rest along the street out front.
        const Vec3 at = i == 0 ? garage : Vec3{12.0f + 7.0f * static_cast<float>(i), 0.0f, 2.5f};
        const Entity v = spawnVehicle(static_cast<VehicleModel>(model), paint, at, i == 0 ? kPi : kHalfPi);
        VehicleComponent* vc = reg_.get<VehicleComponent>(v);
        vc->owner = player;
        vc->ownerName = p->name;
    }
}

void ServerWorld::interact(Entity player)
{
    PlayerComponent* p = reg_.get<PlayerComponent>(player);
    const CharacterComponent* c = reg_.get<CharacterComponent>(player);
    if (!p || !c || !p->profile) {
        return;
    }
    const Vec3 pos = c->body.motor.position;
    for (const Entity e : reg_.entitiesWith<InteractionComponent>()) {
        const InteractionComponent* ic = reg_.get<InteractionComponent>(e);
        const TransformComponent* t = reg_.get<TransformComponent>(e);
        if (!ic || !t || flatDistance(t->position, pos) > ic->radius || std::fabs(t->position.y - pos.y) > 3.0f) {
            continue;
        }
        if (ic->action != InteractionComponent::Action::Marker) {
            if (interiorInteract(player, *reg_.get<InteractionComponent>(e))) return;
            continue;
        }
        switch (ic->kind) {
        case MarkerKind::MissionStart:
            if (p->mission.index < 0) {
                startMission(player, ic->missionIndex);
            }
            return;
        case MarkerKind::SavePoint:
            p->profile->hasSavePoint = true;
            save();
            notify(player, EventKind::Saved, "Game saved");
            return;
        case MarkerKind::Dealership: {
            for (const VehicleModel m : kDealershipStock) {
                bool owned = false;
                for (const auto& [model, paint] : p->profile->ownedVehicles) {
                    (void)paint;
                    owned = owned || model == static_cast<int>(m);
                }
                if (owned) {
                    continue;
                }
                const VehicleSpec& spec = vehicleSpec(m);
                if (p->profile->money < spec.price) {
                    notify(player, EventKind::Info, std::string("You need ") + money(spec.price) + " for the " + spec.name);
                    return;
                }
                p->profile->money -= spec.price;
                const int paint = rng_.rangeInt(0, 7);
                p->profile->ownedVehicles.push_back({static_cast<int>(m), paint});
                const Vec3 at = landmarkPoint("dealership", "delivery", pos + Vec3{0, 0, -6});
                const Entity v = spawnVehicle(m, paint, at, kHalfPi);
                VehicleComponent* vc = reg_.get<VehicleComponent>(v);
                vc->owner = player;
                vc->ownerName = p->name;
                notify(player, EventKind::Purchase, std::string("Bought a ") + spec.name + " - it's parked out front", -spec.price);
                save();
                return;
            }
            notify(player, EventKind::Info, "You already own every car on the lot.");
            return;
        }
        default:
            break;
        }
    }
}

void ServerWorld::startMission(Entity player, int index)
{
    PlayerComponent* p = reg_.get<PlayerComponent>(player);
    if (!p || index < 0 || index >= static_cast<int>(missions_.size())) {
        return;
    }
    const MissionDef& m = missions_[static_cast<std::size_t>(index)];
    if (activity_.kind != Activity::None && !p->role.empty()) {
        notify(player, EventKind::Info, "Finish the current race or round first.");
        return;
    }
    if (p->wanted > 0 && m.type != MissionType::Escape) {
        notify(player, EventKind::Info, "Lose the police before starting a job.");
        return;
    }
    p->mission = {};
    p->mission.index = index;
    p->mission.timer = m.timeLimit;
    if (m.type == MissionType::Delivery) {
        const Entity v = spawnVehicle(m.vehicle, 5, m.vehicleSpawn, m.vehicleYaw);
        VehicleComponent* vc = reg_.get<VehicleComponent>(v);
        vc->missionVehicle = true;
        vc->owner = player; // taking it is not theft
        p->mission.vehicle = v;
    }
    if (m.type == MissionType::Escape) {
        p->wanted = 0;
        addWanted(player, m.wanted, "The heat is on");
    }
    notify(player, EventKind::MissionStarted, m.title + ": " + m.briefing);
}

void ServerWorld::endMission(Entity player, bool success, const std::string& reason)
{
    PlayerComponent* p = reg_.get<PlayerComponent>(player);
    if (!p || p->mission.index < 0) {
        return;
    }
    const MissionDef& m = missions_[static_cast<std::size_t>(p->mission.index)];
    if (success && p->profile) {
        const bool repeat = p->profile->missionsDone.count(m.id) != 0;
        const int reward = repeat ? m.reward / 2 : m.reward;
        p->profile->money += reward;
        p->profile->missionsDone.insert(m.id);
        p->points += 1;
        notify(player, EventKind::MissionPassed, m.title + " passed! +" + money(reward), reward);
    } else {
        notify(player, EventKind::MissionFailed, m.title + " failed: " + reason);
    }
    if (VehicleComponent* v = reg_.get<VehicleComponent>(p->mission.vehicle)) {
        v->missionVehicle = false;
        if (v->driver != player) {
            destroyEntity(p->mission.vehicle);
        } else {
            v->owner = kNullEntity; // keep driving it, but it's no longer yours
        }
    }
    if (p->profile) {
        p->profile->inventory.erase("Package");
    }
    p->mission = {};
    save();
}

void ServerWorld::updateMissions(float dt)
{
    for (const Entity pe : reg_.entitiesWith<PlayerComponent>()) {
        PlayerComponent* p = reg_.get<PlayerComponent>(pe);
        const CharacterComponent* c = reg_.get<CharacterComponent>(pe);
        if (!p || !c || !p->profile) {
            continue;
        }
        const Vec3 pos = c->body.motor.position;
        const bool onFoot = c->mode == CharacterMode::OnFoot;
        const bool driving = c->mode == CharacterMode::InVehicle && c->seat == 0;

        // Context prompt.
        p->prompt.clear();
        if (onFoot) {
            reg_.each<InteractionComponent>([&](Entity e, InteractionComponent& ic) {
                const TransformComponent* t = reg_.get<TransformComponent>(e);
                if (!t || flatDistance(t->position, pos) > ic.radius || !p->prompt.empty() ||
                    std::fabs(t->position.y - pos.y) > 3.0f) {
                    return;
                }
                if (ic.action != InteractionComponent::Action::Marker) {
                    p->prompt = interiorPrompt(pe, ic);
                } else if (ic.kind == MarkerKind::MissionStart && p->mission.index < 0) {
                    const MissionDef& m = missions_[static_cast<std::size_t>(ic.missionIndex)];
                    const bool repeat = p->profile->missionsDone.count(m.id) != 0;
                    p->prompt = "Press E to start '" + m.title + "' (" + money(repeat ? m.reward / 2 : m.reward) +
                                (repeat ? ", replay)" : ")");
                } else if (ic.kind == MarkerKind::SavePoint) {
                    p->prompt = "Press E to save your game";
                } else if (ic.kind == MarkerKind::Dealership) {
                    p->prompt = "Sunshine Motors - all sold out for you";
                    for (const VehicleModel m : kDealershipStock) {
                        bool owned = false;
                        for (const auto& [model, paint] : p->profile->ownedVehicles) {
                            (void)paint;
                            owned = owned || model == static_cast<int>(m);
                        }
                        if (!owned) {
                            p->prompt = std::string("Press E to buy a ") + vehicleSpec(m).name + " for " + money(vehicleSpec(m).price);
                            break;
                        }
                    }
                }
            });
            if (p->prompt.empty()) {
                // Nearby vehicle hint.
                reg_.each<VehicleComponent>([&](Entity, VehicleComponent& v) {
                    if (p->prompt.empty() && flatDistance(v.state.position, pos) < 3.5f) {
                        p->prompt = v.driver != kNullEntity && !reg_.has<PlayerComponent>(v.driver)
                                        ? "Press F to carjack"
                                        : (v.driver == kNullEntity ? "Press F to get in" : "");
                    }
                });
            }
        } else if (driving) {
            p->prompt = "Press F to get out";
        }

        // Collectibles.
        reg_.each<PickupComponent>([&](Entity e, PickupComponent& pk) {
            const TransformComponent* t = reg_.get<TransformComponent>(e);
            if (!t || p->profile->pickups.count(pk.id) || flatDistance(t->position, pos) > 1.8f ||
                std::fabs(t->position.y - pos.y) > 2.5f) {
                return;
            }
            p->profile->pickups.insert(pk.id);
            p->profile->money += kPickupReward;
            ++p->profile->inventory["Flamingo statuette"];
            const int found = static_cast<int>(p->profile->pickups.size());
            notify(pe, EventKind::Pickup,
                   "Flamingo statuette " + std::to_string(found) + "/" + std::to_string(pickupSpots_.size()) + " +" +
                       money(kPickupReward),
                   kPickupReward);
            if (found == static_cast<int>(pickupSpots_.size())) {
                p->profile->money += kAllPickupsBonus;
                notify(pe, EventKind::Money, "All flamingos found! +" + money(kAllPickupsBonus), kAllPickupsBonus);
            }
        });

        // Active mission.
        if (p->mission.index < 0) {
            continue;
        }
        const MissionDef& m = missions_[static_cast<std::size_t>(p->mission.index)];
        MissionRun& run = p->mission;
        if (m.timeLimit > 0.0f) {
            run.timer -= dt;
            if (run.timer <= 0.0f) {
                endMission(pe, false, "Out of time");
                continue;
            }
        }
        switch (m.type) {
        case MissionType::Courier: {
            const Vec3 goal = m.points[static_cast<std::size_t>(std::min(run.stage, 1))];
            if (flatDistance(pos, goal) < (driving ? 6.0f : 3.0f)) {
                if (run.stage == 0) {
                    run.stage = 1;
                    p->profile->inventory["Package"] = 1;
                    notify(pe, EventKind::Info, "Package collected - now deliver it");
                } else {
                    endMission(pe, true, "");
                }
            }
            break;
        }
        case MissionType::Checkpoints: {
            if (m.requiresVehicle && !driving) {
                break;
            }
            const Vec3 goal = m.points[static_cast<std::size_t>(run.stage)];
            if (flatDistance(pos, goal) < (m.requiresVehicle ? 7.5f : 3.0f)) {
                ++run.stage;
                if (run.stage >= static_cast<int>(m.points.size())) {
                    endMission(pe, true, "");
                } else {
                    notify(pe, EventKind::Info, "Checkpoint " + std::to_string(run.stage) + "/" + std::to_string(m.points.size()));
                }
            }
            break;
        }
        case MissionType::Delivery: {
            const VehicleComponent* v = reg_.get<VehicleComponent>(run.vehicle);
            if (!v || v->state.damage >= 1.0f || v->state.drowned) {
                endMission(pe, false, "The car was wrecked");
                break;
            }
            const bool inIt = driving && c->vehicle == run.vehicle;
            if (run.stage == 0 && inIt) {
                run.stage = 1;
            } else if (run.stage == 1 && inIt && flatDistance(pos, m.points[0]) < 7.5f) {
                if (v->state.damage < m.damageLimit) {
                    endMission(pe, true, "");
                } else {
                    endMission(pe, false, "Too much damage");
                }
            }
            break;
        }
        case MissionType::Escape:
            if (p->wanted == 0) {
                endMission(pe, true, "");
            }
            break;
        }
    }
}

} // namespace server
