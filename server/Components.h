#pragma once

// ECS components for the server-side simulation. Entities are composed from
// these; systems in ServerWorld operate on the combinations they care about
// (e.g. Character + NPC = pedestrian, Vehicle + DriverAI = traffic car,
// Character + Player = connected player).

#include "AI/PedestrianBrain.h"
#include "AI/TrafficDriver.h"
#include "Entities/Registry.h"
#include "GameTypes.h"
#include "Missions.h"
#include "PlayerSim.h"
#include "Protocol.h"

#include <deque>
#include <map>
#include <set>
#include <string>

namespace server {

using av::Entity;
using av::Vec3;

struct TransformComponent {
    Vec3 position;
    float yaw = 0.0f;
};

struct NetworkComponent {
    std::uint16_t netId = 0;
};

struct HealthComponent {
    float health = 100.0f;
    float maxHealth = 100.0f;
    float sinceDamage = 100.0f; // seconds
};

struct CharacterComponent {
    av::proto::OnFootState body;           // motor + facing
    std::uint32_t appearance = 0;
    av::proto::CharacterMode mode = av::proto::CharacterMode::OnFoot;
    float modeTimer = 0.0f;
    Vec3 modeFrom;                         // start position of an enter/exit transition
    Entity vehicle = av::kNullEntity;
    int seat = 0;
    bool crouch = false, run = false, talking = false;
    std::uint8_t actionCounter = 0, actionType = 0; // replicated one-shot animations
    Vec3 moveCommand;                      // NPC intent
    bool jumpCommand = false;
    float punchCooldown = 0.0f;
    std::uint8_t weapon = 0;               // weapon in hand (av::proto::Weapon)
    float weaponCooldown = 0.0f;
    bool dead = false;                     // NPC killed: stays down, drops loot once

    void trigger(std::uint8_t type)
    {
        actionType = type;
        ++actionCounter;
    }
    std::uint8_t actionByte() const { return static_cast<std::uint8_t>((actionCounter << 2) | (actionType & 3)); }
};

struct VehicleComponent {
    av::VehicleModel model = av::VehicleModel::Sedan;
    int paint = 0;
    av::VehicleState state;
    av::VehicleControls controls;
    Entity driver = av::kNullEntity;
    Entity owner = av::kNullEntity;  // player who owns it (no theft crime)
    std::string ownerName;
    bool sirens = false;
    bool missionVehicle = false;
    float abandonedTime = 0.0f;
    bool exploded = false;           // wrecked by damage: blew up once
};

// AI that drives a vehicle (attached to the vehicle entity).
struct DriverAIComponent {
    av::TrafficDriver traffic;
    bool pursuit = false;    // police: chase `target` instead of following lanes
    Entity target = av::kNullEntity;
    float stuckTimer = 0.0f, reverseTimer = 0.0f;
};

struct NPCComponent {
    std::string name;
    av::PedestrianBrain brain;
    bool police = false;
    bool driver = false;     // sits in a traffic vehicle
    bool dancer = false;     // club crowd: stays on the dance floor
    float knockTimer = 0.0f;
};

struct PoliceComponent {
    Entity target = av::kNullEntity; // pursued player
    Entity car = av::kNullEntity;
    float contactTime = 0.0f;
};

struct PickupComponent {
    int id = 0;
};

struct InteractionComponent {
    av::proto::MarkerKind kind = av::proto::MarkerKind::MissionStart; // None: no visible marker
    float radius = 2.5f;
    int missionIndex = -1;
    std::string label;
    // Interiors and their furniture.
    enum class Action { Marker, Enter, Exit, Slots, Stocks, Sleep } action = Action::Marker;
    int interior = -1;
};

// Loot and weapon spawns lying on the ground (replicated as pickups).
struct DropComponent {
    std::uint8_t type = av::proto::PickupType::Cash; // PickupType or WeaponBase + weapon
    int amount = 0;           // cash, or ammo
    float ttl = 60.0f;        // < 0: permanent spawn point (respawns)
    float respawn = 0.0f;     // permanent spawns: seconds until it is back
};

// Short-lived visuals: tracers and explosions.
struct EffectComponent {
    av::proto::EffectType type = av::proto::EffectType::Tracer;
    float ttl = 0.2f;
    Vec3 to; // tracer end
};

// Rockets and grenades in flight.
struct ProjectileComponent {
    std::uint8_t weapon = 0;
    Entity owner = av::kNullEntity;
    Vec3 velocity;
    float fuse = 2.0f;
};

// Persistent per-player data (saved to disk, keyed by name).
struct PlayerProfile {
    std::string name;
    std::uint32_t passwordHash = 0;
    int money = 500;
    std::set<std::string> missionsDone;
    std::set<int> pickups;
    std::vector<std::pair<int, int>> ownedVehicles; // model, paint
    std::map<std::string, int> inventory;
    bool hasSavePoint = false;
    double playSeconds = 0.0;
};

struct MissionRun {
    int index = -1; // into ServerWorld::missions(); -1 = none
    int stage = 0;
    float timer = 0.0f;
    Entity vehicle = av::kNullEntity;
};

struct PlayerComponent {
    std::uint32_t session = 0;
    std::string name;
    PlayerProfile* profile = nullptr;
    std::deque<av::proto::InputFrame> pendingInputs;
    std::uint32_t lastInputSeq = 0;
    std::uint16_t lastButtons = 0;
    int wanted = 0;
    float unseenTime = 0.0f;
    float arrestTime = 0.0f;
    MissionRun mission;
    std::string prompt;
    float fallDamageCooldown = 0.0f;
    std::map<std::string, double> lastCrime; // crime -> time (debounces repeated reports)
    float policeSpawnCooldown = 0.0f;
    // Multiplayer activities.
    int points = 0;          // room scoreboard
    std::string role;        // "", "racer", "cop", "robber", "caught"
    int raceCheckpoint = 0;
    bool raceFinished = false;
    bool frozen = false;     // start-line countdown
    float catchTime = 0.0f;  // cops & robbers contact timer (on the robber)
    // Combat & cheats.
    bool god = false;
    int lastWeaponRequest = -1;
    std::string lastCheat;
    float casinoCooldown = 0.0f, stocksCooldown = 0.0f;
};

} // namespace server
