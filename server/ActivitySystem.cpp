// Multiplayer activities: street races (everyone gets a car on the grid,
// first through the checkpoints wins) and cops & robbers (players split into
// teams; cops must catch the robbers before the clock runs out). Also the
// room scoreboard, passenger seats and character-to-character collision.
#include "ServerWorld.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace server {

using namespace av;
using namespace av::proto;

namespace {
constexpr float kGatherTime = 8.0f;
constexpr float kCountdownTime = 4.0f;
constexpr float kRaceTimeLimit = 240.0f;
constexpr float kCopsRoundTime = 180.0f;
constexpr float kAutoStartInterval = 90.0f; // in race / cops rooms, between rounds
constexpr float kCheckpointRadius = 9.0f;

float flat(const Vec3& a, const Vec3& b) { return length(Vec3{a.x - b.x, 0, a.z - b.z}); }

std::string ordinal(int n)
{
    static const char* suffix[] = {"th", "st", "nd", "rd"};
    const int m = n % 100;
    return std::to_string(n) + (m >= 11 && m <= 13 ? "th" : suffix[(n % 10) < 4 ? n % 10 : 0]);
}
} // namespace

int ServerWorld::freeSeat(Entity vehicle, Entity except) const
{
    bool taken[4] = {false, false, false, false};
    const_cast<Registry&>(reg_).each<CharacterComponent>([&](Entity e, CharacterComponent& c) {
        if (e != except && c.vehicle == vehicle && c.seat >= 0 && c.seat < 4) {
            taken[c.seat] = true;
        }
    });
    for (int s = 1; s < 4; ++s) {
        if (!taken[s]) return s;
    }
    return -1;
}

void ServerWorld::separateCharacters()
{
    // Keep people on foot from walking through each other.
    std::vector<Entity> walkers;
    reg_.each<CharacterComponent>([&](Entity e, CharacterComponent& c) {
        if (c.mode == CharacterMode::OnFoot) walkers.push_back(e);
    });
    constexpr float kMin = 0.62f;
    for (std::size_t i = 0; i < walkers.size(); ++i) {
        CharacterComponent* a = reg_.get<CharacterComponent>(walkers[i]);
        for (std::size_t j = i + 1; j < walkers.size(); ++j) {
            CharacterComponent* b = reg_.get<CharacterComponent>(walkers[j]);
            Vec3 d = b->body.motor.position - a->body.motor.position;
            d.y = 0.0f;
            const float dist = length(d);
            if (dist >= kMin || std::fabs(a->body.motor.position.y - b->body.motor.position.y) > 1.5f) {
                continue;
            }
            const Vec3 n = dist > 1e-4f ? d / dist : Vec3{1, 0, 0};
            const float overlap = kMin - dist;
            // Players are pushed less than pedestrians (and their prediction stays close).
            const bool pa = reg_.has<PlayerComponent>(walkers[i]), pb = reg_.has<PlayerComponent>(walkers[j]);
            const float wa = pa && !pb ? 0.2f : (!pa && pb ? 0.8f : 0.5f);
            a->body.motor.position -= n * (overlap * wa);
            b->body.motor.position += n * (overlap * (1.0f - wa));
        }
    }
}

std::vector<ScoreEntry> ServerWorld::scoreboard() const
{
    std::vector<ScoreEntry> out;
    const_cast<Registry&>(reg_).each<PlayerComponent>([&](Entity, PlayerComponent& p) {
        out.push_back({p.name, p.points, p.role});
    });
    std::sort(out.begin(), out.end(), [](const ScoreEntry& a, const ScoreEntry& b) {
        return a.points != b.points ? a.points > b.points : a.name < b.name;
    });
    return out;
}

void ServerWorld::startRace(Entity requestedBy)
{
    if (activity_.kind != Activity::None) {
        notify(requestedBy, EventKind::Info, "Something is already running - wait for it to finish.");
        return;
    }
    if (races_.empty()) {
        notify(requestedBy, EventKind::Info, "This server has no races.");
        return;
    }
    activity_ = {};
    activity_.kind = Activity::Race;
    activity_.phase = Activity::Gathering;
    activity_.timer = kGatherTime;
    activity_.race = nextRace_++ % static_cast<int>(races_.size());
    reg_.each<PlayerComponent>([&](Entity e, PlayerComponent& p) {
        if (p.mission.index < 0) {
            activity_.participants.push_back(e);
            p.role = "racer";
            p.raceCheckpoint = 0;
            p.raceFinished = false;
        }
    });
    broadcast(EventKind::MissionStarted, "Street race: " + races_[static_cast<std::size_t>(activity_.race)].name +
                                             " - starting in " + std::to_string(static_cast<int>(kGatherTime)) + "s");
}

void ServerWorld::startCopsAndRobbers(Entity requestedBy)
{
    if (activity_.kind != Activity::None) {
        notify(requestedBy, EventKind::Info, "Something is already running - wait for it to finish.");
        return;
    }
    std::vector<Entity> players = reg_.entitiesWith<PlayerComponent>();
    if (players.size() < 2) {
        notify(requestedBy, EventKind::Info, "Cops & Robbers needs at least 2 players in the room.");
        return;
    }
    // Shuffle, then a third (at least one) become robbers.
    for (std::size_t i = players.size(); i > 1; --i) {
        std::swap(players[i - 1], players[static_cast<std::size_t>(rng_.rangeInt(0, static_cast<int>(i) - 1))]);
    }
    const std::size_t robbers = std::max<std::size_t>(1, players.size() / 3);
    activity_ = {};
    activity_.kind = Activity::CopsAndRobbers;
    activity_.phase = Activity::Gathering;
    activity_.timer = kGatherTime;
    for (std::size_t i = 0; i < players.size(); ++i) {
        PlayerComponent* p = reg_.get<PlayerComponent>(players[i]);
        if (p->mission.index >= 0) endMission(players[i], false, "A cops & robbers round started");
        p->role = i < robbers ? "robber" : "cop";
        p->wanted = 0;
        p->catchTime = 0.0f;
        activity_.participants.push_back(players[i]);
        notify(players[i], EventKind::MissionStarted,
               i < robbers ? "You're a ROBBER - escape the cops for 3 minutes!" : "You're a COP - catch the robbers!");
        // Everyone gets wheels: cruisers for cops, a fast coupe for robbers.
        const CharacterComponent* c = reg_.get<CharacterComponent>(players[i]);
        const Vec3 at = c->body.motor.position + Vec3{std::sin(c->body.yaw + kHalfPi), 0, std::cos(c->body.yaw + kHalfPi)} * 3.5f;
        const Entity car = spawnVehicle(i < robbers ? VehicleModel::Coupe : VehicleModel::Cruiser, 3, at, c->body.yaw);
        VehicleComponent* vc = reg_.get<VehicleComponent>(car);
        vc->owner = players[i];
        vc->missionVehicle = true;
        activity_.vehicles.push_back(car);
    }
}

void ServerWorld::endActivity(const std::string& summary)
{
    for (const Entity e : activity_.participants) {
        if (PlayerComponent* p = reg_.get<PlayerComponent>(e)) {
            p->role.clear();
            p->frozen = false;
            p->raceCheckpoint = 0;
            p->raceFinished = false;
            p->catchTime = 0.0f;
        }
    }
    // Activity cars become ordinary parked cars (cleaned up when far away).
    for (const Entity v : activity_.vehicles) {
        if (VehicleComponent* vc = reg_.get<VehicleComponent>(v)) {
            vc->missionVehicle = false;
            vc->owner = kNullEntity;
        }
    }
    broadcast(EventKind::MissionPassed, summary);
    activity_ = {};
    autoActivityTimer_ = 0.0f;
}

void ServerWorld::updateActivity(float dt)
{
    // Rooms created for races / cops & robbers start rounds on their own.
    if (activity_.kind == Activity::None) {
        autoActivityTimer_ += dt;
        const std::size_t players = playerCount();
        if (config_.mode == GameMode::Races && players >= 1 && autoActivityTimer_ > kAutoStartInterval) {
            autoActivityTimer_ = 0.0f;
            startRace(kNullEntity);
        } else if (config_.mode == GameMode::CopsAndRobbers && players >= 2 && autoActivityTimer_ > kAutoStartInterval * 0.5f) {
            autoActivityTimer_ = 0.0f;
            startCopsAndRobbers(kNullEntity);
        }
        return;
    }
    // Drop participants who left.
    activity_.participants.erase(std::remove_if(activity_.participants.begin(), activity_.participants.end(),
                                                [this](Entity e) { return !reg_.has<PlayerComponent>(e); }),
                                 activity_.participants.end());
    if (activity_.participants.empty()) {
        activity_ = {};
        return;
    }
    activity_.timer -= dt;

    if (activity_.kind == Activity::Race) {
        const RaceDef& race = races_[static_cast<std::size_t>(activity_.race)];
        if (activity_.phase == Activity::Gathering && activity_.timer <= 0.0f) {
            // Starting grid: two columns behind the start line, one car each.
            const Vec3 fwd{std::sin(race.startYaw), 0, std::cos(race.startYaw)};
            const Vec3 left{std::cos(race.startYaw), 0, -std::sin(race.startYaw)};
            for (std::size_t i = 0; i < activity_.participants.size(); ++i) {
                const Entity pe = activity_.participants[i];
                const Vec3 slot = race.start - fwd * (static_cast<float>(i / 2) * 7.0f) + left * ((i % 2) ? -3.0f : 0.0f);
                respawnPlayer(pe, slot);
                const Entity car = spawnVehicle(VehicleModel::Coupe, static_cast<int>(i % 8), slot, race.startYaw);
                VehicleComponent* vc = reg_.get<VehicleComponent>(car);
                vc->owner = pe;
                vc->missionVehicle = true;
                vc->driver = pe;
                activity_.vehicles.push_back(car);
                CharacterComponent* c = reg_.get<CharacterComponent>(pe);
                c->mode = CharacterMode::InVehicle;
                c->vehicle = car;
                c->seat = 0;
                PlayerComponent* p = reg_.get<PlayerComponent>(pe);
                p->frozen = true;
                p->wanted = 0;
            }
            activity_.phase = Activity::Countdown;
            activity_.timer = kCountdownTime;
            broadcast(EventKind::Info, "On your marks...");
        } else if (activity_.phase == Activity::Countdown && activity_.timer <= 0.0f) {
            for (const Entity pe : activity_.participants) {
                reg_.get<PlayerComponent>(pe)->frozen = false;
            }
            activity_.phase = Activity::Running;
            activity_.timer = kRaceTimeLimit;
            broadcast(EventKind::MissionStarted, "GO! GO! GO!");
        } else if (activity_.phase == Activity::Running) {
            for (const Entity pe : activity_.participants) {
                PlayerComponent* p = reg_.get<PlayerComponent>(pe);
                const CharacterComponent* c = reg_.get<CharacterComponent>(pe);
                if (p->raceFinished || p->raceCheckpoint >= static_cast<int>(race.checkpoints.size())) continue;
                if (flat(c->body.motor.position, race.checkpoints[static_cast<std::size_t>(p->raceCheckpoint)]) < kCheckpointRadius) {
                    ++p->raceCheckpoint;
                    if (p->raceCheckpoint >= static_cast<int>(race.checkpoints.size())) {
                        p->raceFinished = true;
                        activity_.finishOrder.push_back(pe);
                        const int place = static_cast<int>(activity_.finishOrder.size());
                        const int prize = place == 1 ? 1000 : place == 2 ? 500 : place == 3 ? 250 : 100;
                        p->points += std::max(0, 4 - place);
                        if (p->profile) p->profile->money += prize;
                        broadcast(EventKind::Info, p->name + " finished " + ordinal(place) + "!");
                        notify(pe, EventKind::Money, "Finished " + ordinal(place) + " - prize $" + std::to_string(prize), prize);
                    }
                }
            }
            if (activity_.finishOrder.size() == activity_.participants.size() || activity_.timer <= 0.0f) {
                const std::string winner = activity_.finishOrder.empty() ? "nobody" : reg_.get<PlayerComponent>(activity_.finishOrder[0])->name;
                endActivity("Race over - winner: " + winner);
            }
        }
        return;
    }

    // Cops & robbers.
    if (activity_.phase == Activity::Gathering && activity_.timer <= 0.0f) {
        activity_.phase = Activity::Running;
        activity_.timer = kCopsRoundTime;
        broadcast(EventKind::MissionStarted, "Cops & Robbers: the chase is on!");
        return;
    }
    if (activity_.phase != Activity::Running) {
        return;
    }
    int robbersLeft = 0;
    for (const Entity re : activity_.participants) {
        PlayerComponent* robber = reg_.get<PlayerComponent>(re);
        if (robber->role != "robber") continue;
        const CharacterComponent* rc = reg_.get<CharacterComponent>(re);
        const VehicleComponent* rv = reg_.get<VehicleComponent>(rc->vehicle);
        const bool robberSlow = rc->mode != CharacterMode::InVehicle || !rv || std::fabs(rv->state.speed) < 2.0f;
        bool contact = false;
        Entity catcher = kNullEntity;
        for (const Entity ce : activity_.participants) {
            const PlayerComponent* cop = reg_.get<PlayerComponent>(ce);
            if (cop->role != "cop") continue;
            const CharacterComponent* cc = reg_.get<CharacterComponent>(ce);
            const float reach = cc->mode == CharacterMode::InVehicle || rc->mode == CharacterMode::InVehicle ? 5.0f : 2.0f;
            if (robberSlow && flat(cc->body.motor.position, rc->body.motor.position) < reach) {
                contact = true;
                catcher = ce;
            }
        }
        robber->catchTime = contact ? robber->catchTime + dt : std::max(0.0f, robber->catchTime - dt);
        if (robber->catchTime > 1.5f) {
            robber->role = "caught";
            if (PlayerComponent* cop = reg_.get<PlayerComponent>(catcher)) {
                cop->points += 2;
                broadcast(EventKind::Info, cop->name + " caught " + robber->name + "!");
            }
            continue;
        }
        ++robbersLeft;
    }
    if (robbersLeft == 0) {
        for (const Entity e : activity_.participants) {
            PlayerComponent* p = reg_.get<PlayerComponent>(e);
            if (p->role == "cop") {
                p->points += 2;
                if (p->profile) p->profile->money += 500;
            }
        }
        endActivity("Cops win! Every robber was caught.");
    } else if (activity_.timer <= 0.0f) {
        for (const Entity e : activity_.participants) {
            PlayerComponent* p = reg_.get<PlayerComponent>(e);
            if (p->role == "robber") {
                p->points += 3;
                if (p->profile) p->profile->money += 1000;
            }
        }
        endActivity("Robbers win! " + std::to_string(robbersLeft) + " escaped.");
    }
}

void ServerWorld::applyActivityStatus(Entity player, PlayerStatus& st) const
{
    const PlayerComponent* p = reg_.get<PlayerComponent>(player);
    if (!p || p->role.empty() || activity_.kind == Activity::None) {
        return;
    }
    st.missionTimer = activity_.timer;
    if (activity_.kind == Activity::Race) {
        const RaceDef& race = races_[static_cast<std::size_t>(activity_.race)];
        st.missionTitle = "Race: " + race.name;
        if (activity_.phase == Activity::Gathering) {
            st.objective = "Get ready - you'll be placed on the grid";
        } else if (activity_.phase == Activity::Countdown) {
            const int n = static_cast<int>(std::ceil(activity_.timer));
            st.objective = n > 1 ? std::to_string(n - 1) + "..." : "GO!";
        } else if (p->raceFinished) {
            st.objective = "Finished! Waiting for the others";
        } else {
            // Position: finished racers first, then by checkpoints, then distance.
            const CharacterComponent* me = reg_.get<CharacterComponent>(player);
            const Vec3 next = race.checkpoints[static_cast<std::size_t>(p->raceCheckpoint)];
            int ahead = static_cast<int>(activity_.finishOrder.size());
            for (const Entity o : activity_.participants) {
                if (o == player) continue;
                const PlayerComponent* op = reg_.get<PlayerComponent>(o);
                if (op->raceFinished) continue;
                const CharacterComponent* oc = reg_.get<CharacterComponent>(o);
                if (op->raceCheckpoint > p->raceCheckpoint ||
                    (op->raceCheckpoint == p->raceCheckpoint &&
                     flat(oc->body.motor.position, next) < flat(me->body.motor.position, next))) {
                    ++ahead;
                }
            }
            st.objective = "Checkpoint " + std::to_string(p->raceCheckpoint + 1) + "/" + std::to_string(race.checkpoints.size()) +
                           "  -  position " + std::to_string(ahead + 1) + "/" + std::to_string(activity_.participants.size());
            st.marker = MarkerKind::Checkpoint;
            st.markerPosition = next;
        }
        return;
    }
    st.missionTitle = "Cops & Robbers";
    if (activity_.phase == Activity::Gathering) {
        st.objective = p->role == "cop" ? "You're a cop - grab your cruiser" : "You're a robber - get ready to run";
        return;
    }
    if (p->role == "robber") {
        st.objective = "Escape! Don't let the cops catch you";
        st.wanted = 5;
    } else if (p->role == "caught") {
        st.objective = "Caught! Watch the rest of the chase";
    } else {
        // Point cops at the nearest robber still running.
        const CharacterComponent* me = reg_.get<CharacterComponent>(player);
        float best = 1e9f;
        int left = 0;
        for (const Entity o : activity_.participants) {
            const PlayerComponent* op = reg_.get<PlayerComponent>(o);
            if (op->role != "robber") continue;
            ++left;
            const CharacterComponent* oc = reg_.get<CharacterComponent>(o);
            const float d = flat(oc->body.motor.position, me->body.motor.position);
            if (d < best) {
                best = d;
                st.marker = MarkerKind::Objective;
                st.markerPosition = oc->body.motor.position;
            }
        }
        st.objective = "Catch the robbers (" + std::to_string(left) + " left, nearest " + std::to_string(static_cast<int>(best)) + " m)";
    }
}

} // namespace server
