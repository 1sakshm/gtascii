#pragma once

#include "Components.h"
#include "Interiors.h"
#include "ServerConfig.h"

#include "AI/Navigation.h"
#include "Graphics/Material.h"
#include "Physics/DynamicCollisionWorld.h"
#include "World/ChunkManager.h"
#include "World/CityGenerator.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace server {

// Something the network layer should deliver: an event for one player
// (target != 0) or for everyone, or a chat line.
struct OutgoingEvent {
    Entity target = av::kNullEntity;
    av::proto::Event event;
};

// The authoritative game simulation: world streaming, entities, AI, police,
// missions, economy and persistence. Network-agnostic: GameServer feeds it
// inputs and pulls snapshots/status/events out.
class ServerWorld {
public:
    ServerWorld(const ServerConfig& config, const std::filesystem::path& dataRoot);
    ~ServerWorld();

    // ---- sessions ----
    // Returns kNullEntity and sets `error` if the player cannot join.
    Entity joinPlayer(const std::string& name, const std::string& password, std::uint32_t appearance,
                      std::uint32_t session, std::string& error);
    void leavePlayer(Entity player);
    void queueInput(Entity player, const std::vector<av::proto::InputFrame>& frames);
    void command(Entity player, av::proto::CommandKind kind, int arg);
    // A cheat code typed into chat. Returns true if it was one.
    bool tryCheat(Entity player, const std::string& text);

    // ---- simulation ----
    void tick(float dt);

    // ---- replication ----
    av::proto::Snapshot buildSnapshot(Entity player) const;
    av::proto::PlayerStatus buildStatus(Entity player) const;
    std::vector<av::proto::ScoreEntry> scoreboard() const;
    std::vector<OutgoingEvent> takeEvents();
    std::uint16_t netIdOf(Entity e) const;
    std::string playerName(Entity e) const;

    void save();
    float hour() const noexcept { return hour_; }
    double time() const noexcept { return time_; }
    std::uint32_t tickCount() const noexcept { return tick_; }
    std::uint32_t seed() const noexcept { return config_.seed; }
    std::size_t playerCount() const;
    av::Registry& registry() { return reg_; }
    const std::vector<av::proto::MissionDef>& missions() const { return missions_; }

private:
    // ServerWorld.cpp
    std::uint16_t assignNetId(Entity e);
    Entity spawnCharacter(const Vec3& position, float yaw, std::uint32_t appearance);
    Entity spawnVehicle(av::VehicleModel model, int paint, const Vec3& position, float yaw);
    void destroyEntity(Entity e);
    void notify(Entity player, av::proto::EventKind kind, const std::string& text, int value = 0);
    void broadcast(av::proto::EventKind kind, const std::string& text);
    std::vector<Vec3> playerPositions() const;
    float distanceToNearestPlayer(const Vec3& p) const;
    Vec3 landmarkPoint(const std::string& type, const char* key, const Vec3& fallback) const;
    void rebuildDynamicObstacles();
    void setupStaticEntities();

    // PlayerSystem.cpp
    void updatePlayers(float dt);
    void applyInput(Entity player, const av::proto::InputFrame& in);
    void updateCharacterModes(float dt);
    void tryEnterOrExit(Entity character);
    void enterVehicle(Entity character, Entity vehicle);
    void exitVehicle(Entity character);
    void punch(Entity character);
    void damage(Entity target, float amount, Entity attacker);
    void knockDown(Entity target, const Vec3& push, float seconds);

    // NpcSystem.cpp
    void updatePopulation();
    void updatePedestrians(float dt);
    void updateVehicles(float dt);
    void resolveVehicleContacts();
    void scareNearby(const Vec3& where, float radius, float seconds);
    Entity spawnTrafficCar(const Vec3& near, float minDist, float maxDist);
    Entity spawnPedestrian(const Vec3& near, float minDist, float maxDist);

    // PoliceSystem.cpp
    void updatePolice(float dt);
    void addWanted(Entity player, int stars, const std::string& crime);
    void arrest(Entity player);
    void hospitalize(Entity player);
    void respawnPlayer(Entity player, const Vec3& where);

    // MissionSystem.cpp
    void updateMissions(float dt);
    void startMission(Entity player, int index);
    void endMission(Entity player, bool success, const std::string& reason);
    void interact(Entity player);
    void spawnOwnedVehicles(Entity player);

    // Persistence.cpp
    void loadSave();

    // CombatSystem.cpp: weapons, explosions, loot, cheats, interiors, helicopters.
    void setupCombat();
    void updateCombat(float dt);
    void fireWeapon(Entity shooter, float aimYaw);
    void explode(const Vec3& at, int weapon, Entity owner);
    void killed(Entity victim, Entity killer);
    void spawnDrop(const Vec3& at, std::uint8_t type, int amount, float ttl);
    void spawnEffect(av::proto::EffectType type, const Vec3& from, const Vec3& to, float ttl);
    bool interiorInteract(Entity player, InteractionComponent& ic);
    std::string interiorPrompt(Entity player, const InteractionComponent& ic) const;
    void giveWeapon(Entity player, int weapon, int ammo);
    int ammoOf(Entity player, int weapon) const;
    void collectDrops(Entity player);
    void updatePoliceGunfire(float dt);
    Entity spawnHelicopter(const Vec3& at, float yaw);

    // ActivitySystem.cpp: street races and cops & robbers.
    void updateActivity(float dt);
    void startRace(Entity requestedBy);
    void startCopsAndRobbers(Entity requestedBy);
    void endActivity(const std::string& summary);
    void applyActivityStatus(Entity player, av::proto::PlayerStatus& st) const;
    void separateCharacters();
    int freeSeat(Entity vehicle, Entity except) const;

    const ServerConfig config_;
    std::filesystem::path dataRoot_;
    std::filesystem::path savePath_; // absolute

    av::MaterialLibrary materials_;
    std::unique_ptr<av::CityLayout> layout_;
    std::unique_ptr<av::CityGenerator> generator_;
    std::unique_ptr<av::ChunkManager> chunks_;
    std::unique_ptr<av::DynamicCollisionWorld> dynamicWorld_;
    std::unique_ptr<av::PedestrianNav> nav_;
    std::unique_ptr<av::RoadNetwork> roads_;
    std::vector<av::proto::MissionDef> missions_;
    std::vector<Vec3> pickupSpots_;
    std::vector<av::proto::RaceDef> races_;
    std::vector<av::proto::InteriorDef> interiors_;
    std::vector<std::pair<Vec3, float>> helipads_;
    std::vector<Entity> helipadHelis_;
    float helipadTimer_ = 0.0f;

    struct Activity {
        enum Kind { None, Race, CopsAndRobbers } kind = None;
        enum Phase { Gathering, Countdown, Running } phase = Gathering;
        float timer = 0.0f;
        int race = 0;
        std::vector<Entity> participants, finishOrder, vehicles;
    } activity_;
    float autoActivityTimer_ = 0.0f;
    int nextRace_ = 0;

    av::Registry reg_;
    av::Rng rng_;
    std::uint16_t nextNetId_ = 1;
    std::unordered_map<Entity, std::uint16_t> netIds_;
    std::map<std::string, PlayerProfile> profiles_;
    std::vector<OutgoingEvent> events_;

    float hour_;
    double time_ = 0.0;
    std::uint32_t tick_ = 0;
    float autosaveTimer_ = 0.0f;
};

} // namespace server
