#include "ServerWorld.h"

#include "EasterEggs.h"
#include "Weapons.h"

#include "Core/FileUtil.h"
#include "Graphics/ObjLoader.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace server {

using namespace av;
using namespace av::proto;

namespace {
constexpr float kInterestRadius = 170.0f;
constexpr std::size_t kMaxSnapshotEntities = 60;

bool validName(const std::string& n)
{
    if (n.empty() || n.size() > 16) {
        return false;
    }
    for (const char c : n) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}
} // namespace

ServerWorld::ServerWorld(const ServerConfig& config, const std::filesystem::path& dataRoot)
    : config_(config)
    , dataRoot_(dataRoot)
    , rng_(config.seed ^ static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()))
    , hour_(config.startHour)
{
    // Relative save paths live in the per-user data folder (the install
    // folder may be read-only, e.g. a global npm package).
    // An empty save path means a throwaway room (online rooms): nothing persists.
    if (!config.savePath.empty()) {
        savePath_ = std::filesystem::path(config.savePath).is_absolute() ? std::filesystem::path(config.savePath)
                                                                          : userDataDir() / config.savePath;
    }
    materials_.loadFile(dataRoot / "assets/materials/materials.json");
    layout_ = std::make_unique<CityLayout>(CityLayout::fromFile(config.seed, dataRoot / "assets/maps/landmarks.json"));
    generator_ = std::make_unique<CityGenerator>(
        *layout_, materials_, loadObjFile(dataRoot / "assets/models/newsbox.obj", materials_, materials_.require("metal")));
    chunks_ = std::make_unique<ChunkManager>(*generator_, config.loadRadius);
    dynamicWorld_ = std::make_unique<DynamicCollisionWorld>(*chunks_);
    nav_ = std::make_unique<PedestrianNav>(*layout_);
    roads_ = std::make_unique<RoadNetwork>(*layout_);
    missions_ = loadMissions(dataRoot / "assets/missions/missions.json", *layout_);
    races_ = loadRaces(dataRoot / "assets/missions/races.json", *layout_);

    // Hidden collectibles on sidewalks around the city (deterministic per seed).
    Rng spots(config.seed * 7919u + 17u);
    for (int i = 0; i < 24; ++i) {
        const int cx = spots.rangeInt(-5, 2), cz = spots.rangeInt(-5, 6);
        const bool alongZ = spots.chance(0.5f);
        const float fixed = spots.chance(0.5f) ? PedestrianNav::kInset : kChunkSize - PedestrianNav::kInset;
        const float along = spots.range(18.0f, 46.0f);
        const float lx = alongZ ? fixed : along, lz = alongZ ? along : fixed;
        const float x = static_cast<float>(cx) * kChunkSize + lx, z = static_cast<float>(cz) * kChunkSize + lz;
        pickupSpots_.push_back({x, layout_->groundHeight(x, z), z});
    }
    setupStaticEntities();
    setupCombat();
    loadSave();
}

ServerWorld::~ServerWorld() = default;

std::uint16_t ServerWorld::assignNetId(Entity e)
{
    std::uint16_t id = nextNetId_++;
    if (nextNetId_ == 0) {
        nextNetId_ = 1;
    }
    reg_.add<NetworkComponent>(e, {id});
    netIds_[e] = id;
    return id;
}

std::uint16_t ServerWorld::netIdOf(Entity e) const
{
    const auto it = netIds_.find(e);
    return it == netIds_.end() ? 0 : it->second;
}

std::string ServerWorld::playerName(Entity e) const
{
    const PlayerComponent* p = reg_.get<PlayerComponent>(e);
    return p ? p->name : std::string();
}

std::size_t ServerWorld::playerCount() const { return reg_.count<PlayerComponent>(); }

void ServerWorld::setupStaticEntities()
{
    for (std::size_t i = 0; i < missions_.size(); ++i) {
        const Entity e = reg_.create();
        reg_.add<TransformComponent>(e, {missions_[i].start, 0.0f});
        InteractionComponent ic;
        ic.kind = MarkerKind::MissionStart;
        ic.missionIndex = static_cast<int>(i);
        ic.label = missions_[i].title;
        reg_.add<InteractionComponent>(e, ic);
        assignNetId(e);
    }
    auto marker = [this](MarkerKind kind, const Vec3& p, const std::string& label) {
        const Entity e = reg_.create();
        reg_.add<TransformComponent>(e, {p, 0.0f});
        InteractionComponent ic;
        ic.kind = kind;
        ic.label = label;
        reg_.add<InteractionComponent>(e, ic);
        assignNetId(e);
    };
    marker(MarkerKind::SavePoint, landmarkPoint("safehouse", "marker", layout_->spawn().position), "Save");
    if (layout_->findLandmark("dealership")) {
        marker(MarkerKind::Dealership, landmarkPoint("dealership", "marker", {}), "Sunshine Motors");
    }
    for (std::size_t i = 0; i < pickupSpots_.size(); ++i) {
        const Entity e = reg_.create();
        reg_.add<TransformComponent>(e, {pickupSpots_[i], 0.0f});
        reg_.add<PickupComponent>(e, {static_cast<int>(i)});
        assignNetId(e);
    }
}

Vec3 ServerWorld::landmarkPoint(const std::string& type, const char* key, const Vec3& fallback) const
{
    const Landmark* lm = layout_->findLandmark(type);
    if (!lm) {
        return fallback;
    }
    const JsonValue* at = lm->params.find(key);
    if (!at || at->size() != 2) {
        return fallback;
    }
    const float x = static_cast<float>(lm->chunk.x) * kChunkSize + static_cast<float>((*at)[0].asNumber());
    const float z = static_cast<float>(lm->chunk.z) * kChunkSize + static_cast<float>((*at)[1].asNumber());
    return {x, layout_->groundHeight(x, z), z};
}

Entity ServerWorld::spawnCharacter(const Vec3& position, float yaw, std::uint32_t appearance)
{
    const Entity e = reg_.create();
    CharacterComponent c;
    c.appearance = appearance;
    c.body.motor.teleport(position, *dynamicWorld_);
    c.body.yaw = yaw;
    reg_.add<CharacterComponent>(e, std::move(c));
    reg_.add<HealthComponent>(e);
    assignNetId(e);
    return e;
}

Entity ServerWorld::spawnVehicle(VehicleModel model, int paint, const Vec3& position, float yaw)
{
    const Entity e = reg_.create();
    VehicleComponent v;
    v.model = model;
    v.paint = paint;
    v.state.position = position;
    v.state.position.y = chunks_->groundHeight(position.x, position.z);
    v.state.yaw = yaw;
    reg_.add<VehicleComponent>(e, v);
    assignNetId(e);
    return e;
}

void ServerWorld::destroyEntity(Entity e)
{
    // A destroyed vehicle drops its occupants; a destroyed character frees its seat.
    if (VehicleComponent* v = reg_.get<VehicleComponent>(e)) {
        for (const Entity c : reg_.entitiesWith<CharacterComponent>()) {
            CharacterComponent* ch = reg_.get<CharacterComponent>(c);
            if (ch->vehicle == e) {
                if (reg_.has<PlayerComponent>(c)) {
                    ch->vehicle = kNullEntity;
                    ch->mode = CharacterMode::OnFoot;
                    ch->body.motor.teleport(vehicleDoorPosition(v->state, vehicleSpec(v->model), 0), *dynamicWorld_);
                } else {
                    destroyEntity(c);
                }
            }
        }
    }
    if (CharacterComponent* ch = reg_.get<CharacterComponent>(e)) {
        if (VehicleComponent* v = reg_.get<VehicleComponent>(ch->vehicle)) {
            if (v->driver == e) {
                v->driver = kNullEntity;
            }
        }
    }
    netIds_.erase(e);
    reg_.destroy(e);
}

void ServerWorld::notify(Entity player, EventKind kind, const std::string& text, int value)
{
    events_.push_back({player, Event{kind, value, text}});
}

void ServerWorld::broadcast(EventKind kind, const std::string& text)
{
    events_.push_back({kNullEntity, Event{kind, 0, text}});
}

std::vector<OutgoingEvent> ServerWorld::takeEvents()
{
    std::vector<OutgoingEvent> out;
    out.swap(events_);
    return out;
}

std::vector<Vec3> ServerWorld::playerPositions() const
{
    std::vector<Vec3> out;
    const_cast<Registry&>(reg_).each<PlayerComponent>([&](Entity e, PlayerComponent&) {
        if (const CharacterComponent* c = reg_.get<CharacterComponent>(e)) {
            out.push_back(c->body.motor.position);
        }
    });
    return out;
}

float ServerWorld::distanceToNearestPlayer(const Vec3& p) const
{
    float best = 1e9f;
    for (const Vec3& q : playerPositions()) {
        best = std::min(best, length(Vec3{p.x - q.x, 0, p.z - q.z}));
    }
    return best;
}

void ServerWorld::rebuildDynamicObstacles()
{
    dynamicWorld_->dynamicBoxes.clear();
    reg_.each<VehicleComponent>([&](Entity, VehicleComponent& v) {
        vehicleBoxes(v.state, vehicleSpec(v.model), dynamicWorld_->dynamicBoxes);
    });
}

// ---- sessions --------------------------------------------------------------

Entity ServerWorld::joinPlayer(const std::string& name, const std::string& password, std::uint32_t appearance,
                               std::uint32_t session, std::string& error)
{
    if (!validName(name)) {
        error = "Names are 1-16 letters, digits, '_' or '-'.";
        return kNullEntity;
    }
    if (static_cast<int>(playerCount()) >= config_.maxPlayers) {
        error = "Server is full.";
        return kNullEntity;
    }
    bool online = false;
    reg_.each<PlayerComponent>([&](Entity, PlayerComponent& p) { online = online || p.name == name; });
    if (online) {
        error = "That name is already playing.";
        return kNullEntity;
    }
    const std::uint32_t hash = password.empty() ? 0 : hash32(static_cast<std::uint32_t>(std::hash<std::string>{}(password)));
    auto [it, created] = profiles_.try_emplace(name);
    PlayerProfile& profile = it->second;
    if (created) {
        profile.name = name;
        profile.passwordHash = hash;
    } else if (profile.passwordHash != 0 && profile.passwordHash != hash) {
        error = "Wrong password for this name.";
        return kNullEntity;
    }

    const SpawnPoint spawn = layout_->spawn();
    // Side by side on the safehouse path, so players never spawn inside each other.
    const float slot = static_cast<float>(playerCount() % 4) * 1.6f - 2.4f;
    const Vec3 at = spawn.position + Vec3{slot, 0.0f, 0.0f};
    chunks_->loadAll(at);
    const Entity e = spawnCharacter(at, spawn.yaw, appearance);
    PlayerComponent pc;
    pc.session = session;
    pc.name = name;
    pc.profile = &profile;
    reg_.add<PlayerComponent>(e, std::move(pc));
    spawnOwnedVehicles(e);
    broadcast(EventKind::Info, name + " joined the game");
    return e;
}

void ServerWorld::leavePlayer(Entity player)
{
    PlayerComponent* p = reg_.get<PlayerComponent>(player);
    if (!p) {
        return;
    }
    const std::string name = p->name;
    if (p->mission.index >= 0) {
        endMission(player, false, "");
    }
    // Owned vehicles disappear with their owner (they respawn at the safehouse).
    for (const Entity v : reg_.entitiesWith<VehicleComponent>()) {
        VehicleComponent* vc = reg_.get<VehicleComponent>(v);
        if (vc && vc->owner == player && vc->driver == kNullEntity) {
            destroyEntity(v);
        }
    }
    if (CharacterComponent* c = reg_.get<CharacterComponent>(player)) {
        if (c->vehicle != kNullEntity) {
            exitVehicle(player);
        }
    }
    save();
    destroyEntity(player);
    broadcast(EventKind::Info, name + " left the game");
}

void ServerWorld::queueInput(Entity player, const std::vector<InputFrame>& frames)
{
    PlayerComponent* p = reg_.get<PlayerComponent>(player);
    if (!p) {
        return;
    }
    for (const InputFrame& f : frames) {
        // Redundant frames arrive several times; keep only new ones, in order.
        const bool known = f.seq <= p->lastInputSeq ||
                           std::any_of(p->pendingInputs.begin(), p->pendingInputs.end(),
                                       [&f](const InputFrame& q) { return q.seq == f.seq; });
        if (!known) {
            p->pendingInputs.push_back(f);
        }
    }
    std::sort(p->pendingInputs.begin(), p->pendingInputs.end(),
              [](const InputFrame& a, const InputFrame& b) { return a.seq < b.seq; });
    while (p->pendingInputs.size() > 30) {
        p->pendingInputs.pop_front(); // a client running far ahead cannot speed itself up
    }
}

void ServerWorld::command(Entity player, CommandKind kind, int arg)
{
    (void)arg;
    if (!reg_.has<PlayerComponent>(player)) {
        return;
    }
    if (kind == CommandKind::StartRace) {
        startRace(player);
        return;
    }
    if (kind == CommandKind::StartCopsAndRobbers) {
        startCopsAndRobbers(player);
        return;
    }
    if (kind == CommandKind::ToggleJetpack) {
        PlayerComponent* p = reg_.get<PlayerComponent>(player);
        CharacterComponent* c = reg_.get<CharacterComponent>(player);
        if (p && c && p->profile) {
            if (!p->profile->inventory.count("Jetpack")) {
                notify(player, EventKind::Info, "You don't have a jetpack. Find one on the beach, or try a cheat...");
            } else if (findInterior(interiors_, c->body.motor.position)) {
                notify(player, EventKind::Info, "Not indoors!");
            } else {
                c->body.jetpack = !c->body.jetpack;
                notify(player, EventKind::Info, c->body.jetpack ? "Jetpack on - Space up, Z down, Shift fast" : "Jetpack off");
            }
        }
        return;
    }
    if (kind == CommandKind::SkipHour) {
        if (config_.allowCheats) {
            hour_ = std::fmod(hour_ + 1.0f, 24.0f);
        } else {
            notify(player, EventKind::Info, "Time skipping is disabled on this server.");
        }
    }
}

// ---- simulation ------------------------------------------------------------

void ServerWorld::tick(float dt)
{
    time_ += dt;
    ++tick_;
    if (config_.dayLengthMinutes > 0.0f) {
        hour_ = std::fmod(hour_ + dt * 24.0f / (config_.dayLengthMinutes * 60.0f), 24.0f);
    }

    const std::vector<Vec3> foci = playerPositions();
    if (!foci.empty()) {
        chunks_->update(foci, 2);
    }
    rebuildDynamicObstacles();

    updatePlayers(dt);
    updateCharacterModes(dt);
    updateCombat(dt);
    if (tick_ % 15 == 0) {
        updatePopulation();
    }
    updatePedestrians(dt);
    updateVehicles(dt);
    resolveVehicleContacts();
    separateCharacters();
    updatePolice(dt);
    updateMissions(dt);
    updateActivity(dt);

    reg_.each<PlayerComponent>([dt](Entity, PlayerComponent& p) {
        if (p.profile) {
            p.profile->playSeconds += dt;
        }
    });
    autosaveTimer_ += dt;
    if (config_.autosaveSeconds > 0.0f && autosaveTimer_ >= config_.autosaveSeconds) {
        autosaveTimer_ = 0.0f;
        save();
    }
}

// ---- replication -----------------------------------------------------------

Snapshot ServerWorld::buildSnapshot(Entity player) const
{
    Snapshot s;
    s.tick = tick_;
    s.serverTime = static_cast<float>(time_);
    s.hour = hour_;
    const PlayerComponent* pc = reg_.get<PlayerComponent>(player);
    const CharacterComponent* me = reg_.get<CharacterComponent>(player);
    if (!pc || !me) {
        return s;
    }
    s.ackInput = pc->lastInputSeq;
    s.self.mode = me->mode;
    s.self.position = me->body.motor.position;
    s.self.velocity = me->body.motor.velocity;
    s.self.yaw = me->body.yaw;
    s.self.grounded = me->body.motor.grounded();
    s.self.vehicle = netIdOf(me->vehicle);
    s.self.seat = static_cast<std::uint8_t>(me->seat);
    if (const VehicleComponent* v = reg_.get<VehicleComponent>(me->vehicle)) {
        s.self.vehiclePosition = v->state.position;
        s.self.vehicleYaw = v->state.yaw;
        s.self.vehicleSpeed = v->state.speed;
        s.self.vehicleSteer = v->state.steer;
        s.self.vehicleDamage = v->state.damage;
        s.self.vehicleVerticalSpeed = v->state.verticalSpeed;
        s.self.vehicleRotor = v->state.rotor;
    }
    s.self.jetpack = me->body.jetpack;

    const Vec3 eye = me->body.motor.position;
    struct Item {
        float d;
        EntityState e;
    };
    std::vector<Item> items;
    Registry& reg = const_cast<Registry&>(reg_);
    auto inRange = [&eye](const Vec3& p, float& d) {
        d = length(p - eye); // 3D: rooms upstairs do not see the street (and vice versa)
        return d < kInterestRadius;
    };

    reg.each<CharacterComponent>([&](Entity e, CharacterComponent& c) {
        float d;
        if (e == player || !inRange(c.body.motor.position, d)) {
            return;
        }
        EntityState st;
        st.netId = netIdOf(e);
        st.kind = EntityKind::Character;
        st.position = c.body.motor.position;
        st.yaw = c.body.yaw;
        st.appearance = c.appearance;
        st.mode = c.mode;
        st.grounded = c.body.motor.grounded();
        st.crouch = c.crouch;
        st.action = c.actionByte();
        const Vec3 v = c.body.motor.velocity;
        st.speed = std::sqrt(v.x * v.x + v.z * v.z);
        st.verticalSpeed = v.y;
        st.vehicle = netIdOf(c.vehicle);
        st.subtype = c.weapon;
        if (c.body.jetpack) st.flags |= EntityFlags::Jetpack;
        if (c.dead) st.flags |= EntityFlags::Dead;
        if (const PlayerComponent* pl = reg_.get<PlayerComponent>(e)) {
            st.flags |= EntityFlags::Player;
            if (pl->role == "cop") st.flags |= EntityFlags::Police;
            if (pl->role == "robber") st.flags |= EntityFlags::Robber;
        }
        if (const NPCComponent* n = reg_.get<NPCComponent>(e)) {
            if (n->police) st.flags |= EntityFlags::Police;
        }
        if (c.talking) st.flags |= EntityFlags::Talking;
        items.push_back({d, st});
    });
    reg.each<VehicleComponent>([&](Entity e, VehicleComponent& v) {
        float d;
        if (!inRange(v.state.position, d)) {
            return;
        }
        EntityState st;
        st.netId = netIdOf(e);
        st.kind = EntityKind::Vehicle;
        st.position = v.state.position;
        st.yaw = v.state.yaw;
        st.model = static_cast<std::uint8_t>(v.model);
        st.paint = static_cast<std::uint8_t>(v.paint);
        st.steer = v.model == VehicleModel::Helicopter ? v.state.rotor : v.state.steer;
        st.wheelSpin = v.state.wheelSpin < 0 ? v.state.wheelSpin + kTwoPi : v.state.wheelSpin;
        st.pitch = v.state.pitch;
        st.roll = v.state.roll;
        st.damage = v.state.damage;
        st.speed = v.state.speed;
        if (v.sirens) st.flags |= EntityFlags::Sirens;
        if (v.model == VehicleModel::Cruiser) st.flags |= EntityFlags::Police;
        if (v.owner == player) st.flags |= EntityFlags::Owned;
        items.push_back({d * 0.9f, st}); // vehicles slightly preferred: they are big
    });
    reg.each<InteractionComponent>([&](Entity e, InteractionComponent& ic) {
        const TransformComponent* t = reg_.get<TransformComponent>(e);
        float d;
        if (!t || !inRange(t->position, d)) {
            return;
        }
        if (ic.kind == MarkerKind::None) {
            return; // furniture you can use, not a visible marker
        }
        if (ic.kind == MarkerKind::MissionStart && pc->mission.index >= 0) {
            return; // hide other mission starts while on a mission
        }
        EntityState st;
        st.netId = netIdOf(e);
        st.kind = EntityKind::Marker;
        st.position = t->position;
        st.subtype = static_cast<std::uint8_t>(ic.kind);
        items.push_back({d * 1.2f, st});
    });
    reg.each<PickupComponent>([&](Entity e, PickupComponent& pk) {
        const TransformComponent* t = reg_.get<TransformComponent>(e);
        float d;
        if (!t || (pc->profile && pc->profile->pickups.count(pk.id)) || !inRange(t->position, d)) {
            return;
        }
        EntityState st;
        st.netId = netIdOf(e);
        st.kind = EntityKind::Pickup;
        st.position = t->position;
        items.push_back({d * 1.3f, st});
    });

    reg.each<DropComponent>([&](Entity e, DropComponent& dc) {
        const TransformComponent* t = reg_.get<TransformComponent>(e);
        float d;
        if (!t || dc.respawn > 0.0f || !inRange(t->position, d)) {
            return;
        }
        EntityState st;
        st.netId = netIdOf(e);
        st.kind = EntityKind::Pickup;
        st.position = t->position;
        st.subtype = dc.type;
        items.push_back({d * 1.1f, st});
    });
    reg.each<EggComponent>([&](Entity e, EggComponent& egg) {
        const TransformComponent* t = reg_.get<TransformComponent>(e);
        float d;
        if (!t || (pc->profile && pc->profile->pickups.count(kGoldenEggIdBase + egg.index)) || !inRange(t->position, d)) {
            return;
        }
        EntityState st;
        st.netId = netIdOf(e);
        st.kind = EntityKind::Pickup;
        st.position = t->position;
        st.subtype = PickupType::GoldenEgg;
        items.push_back({d * 1.2f, st});
    });
    reg.each<EffectComponent>([&](Entity e, EffectComponent& fx) {
        const TransformComponent* t = reg_.get<TransformComponent>(e);
        float d;
        if (!t || !inRange(t->position, d)) {
            return;
        }
        EntityState st;
        st.netId = netIdOf(e);
        st.kind = EntityKind::Effect;
        st.position = t->position;
        st.subtype = static_cast<std::uint8_t>(fx.type);
        const Vec3 span = fx.to - t->position;
        st.yaw = std::atan2(span.x, span.z);
        st.speed = std::sqrt(span.x * span.x + span.z * span.z);
        st.verticalSpeed = span.y;
        items.push_back({d * 0.5f, st}); // short-lived and important
    });
    reg.each<ProjectileComponent>([&](Entity e, ProjectileComponent& pr) {
        const TransformComponent* t = reg_.get<TransformComponent>(e);
        float d;
        if (!t || !inRange(t->position, d)) {
            return;
        }
        EntityState st;
        st.netId = netIdOf(e);
        st.kind = EntityKind::Effect;
        st.position = t->position;
        st.subtype = static_cast<std::uint8_t>(pr.weapon == Grenades ? EffectType::Grenade : EffectType::Rocket);
        st.yaw = t->yaw;
        items.push_back({d * 0.5f, st});
    });

    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.d < b.d; });
    for (std::size_t i = 0; i < items.size() && i < kMaxSnapshotEntities; ++i) {
        s.entities.push_back(items[i].e);
    }
    return s;
}

PlayerStatus ServerWorld::buildStatus(Entity player) const
{
    PlayerStatus st;
    const PlayerComponent* p = reg_.get<PlayerComponent>(player);
    if (!p || !p->profile) {
        return st;
    }
    st.money = p->profile->money;
    if (const HealthComponent* h = reg_.get<HealthComponent>(player)) {
        st.health = static_cast<std::uint8_t>(std::clamp(h->health, 0.0f, 100.0f));
    }
    st.wanted = static_cast<std::uint8_t>(p->wanted);
    st.prompt = p->prompt;
    st.pickupsFound = static_cast<std::uint16_t>(std::count_if(p->profile->pickups.begin(), p->profile->pickups.end(),
                                                                [](int id) { return id < kGoldenEggIdBase; }));
    st.missionsDone = static_cast<std::uint16_t>(p->profile->missionsDone.size());
    st.missionsTotal = static_cast<std::uint16_t>(missions_.size());
    st.playSeconds = static_cast<std::uint32_t>(p->profile->playSeconds);
    st.frozen = p->frozen ? 1 : 0;
    st.god = p->god ? 1 : 0;
    st.eggsTotal = static_cast<std::uint8_t>(reg_.count<EggComponent>());
    for (int k = 0; k < kGoldenEggCount; ++k) st.eggsFound = static_cast<std::uint8_t>(st.eggsFound + (p->profile->pickups.count(kGoldenEggIdBase + k) ? 1 : 0));
    st.weaponsOwned = 1;
    for (int w = 1; w < kWeaponCount; ++w) {
        if (ammoOf(player, w) > 0) st.weaponsOwned = static_cast<std::uint16_t>(st.weaponsOwned | (1u << w));
    }
    if (const CharacterComponent* pc = reg_.get<CharacterComponent>(player)) {
        st.weapon = pc->weapon;
        st.ammo = static_cast<std::uint16_t>(usesAmmo(pc->weapon) ? std::min(ammoOf(player, pc->weapon), 65535) : 0);
    }
    st.pickupsTotal = static_cast<std::uint16_t>(pickupSpots_.size());
    for (const auto& [item, count] : p->profile->inventory) {
        if (count > 0) {
            st.inventory.push_back({item, static_cast<std::uint16_t>(count)});
        }
    }
    for (const auto& [model, paint] : p->profile->ownedVehicles) {
        (void)paint;
        st.inventory.push_back({std::string("Keys: ") + vehicleSpec(static_cast<VehicleModel>(model)).name, 1});
    }

    if (p->mission.index >= 0) {
        const MissionDef& m = missions_[static_cast<std::size_t>(p->mission.index)];
        st.missionTitle = m.title;
        st.missionTimer = m.timeLimit > 0.0f ? p->mission.timer : -1.0f;
        const CharacterComponent* c = reg_.get<CharacterComponent>(player);
        const bool inVehicle = c && c->mode == CharacterMode::InVehicle;
        switch (m.type) {
        case MissionType::Courier:
            st.objective = p->mission.stage == 0 ? "Pick up the package" : "Deliver the package";
            st.marker = MarkerKind::Objective;
            st.markerPosition = m.points[static_cast<std::size_t>(std::min(p->mission.stage, 1))];
            break;
        case MissionType::Checkpoints:
            if (m.requiresVehicle && !inVehicle) {
                st.objective = "Get a vehicle";
            } else {
                st.objective = "Checkpoint " + std::to_string(p->mission.stage + 1) + "/" + std::to_string(m.points.size());
                st.marker = MarkerKind::Checkpoint;
                st.markerPosition = m.points[static_cast<std::size_t>(p->mission.stage)];
            }
            break;
        case MissionType::Delivery:
            if (p->mission.stage == 0) {
                st.objective = std::string("Get in the ") + vehicleSpec(m.vehicle).name;
                if (const VehicleComponent* v = reg_.get<VehicleComponent>(p->mission.vehicle)) {
                    st.marker = MarkerKind::Objective;
                    st.markerPosition = v->state.position;
                }
            } else {
                st.objective = "Deliver the car";
                if (const VehicleComponent* v = reg_.get<VehicleComponent>(p->mission.vehicle)) {
                    st.objective += " (damage " + std::to_string(static_cast<int>(v->state.damage * 100)) + "%)";
                }
                st.marker = MarkerKind::Objective;
                st.markerPosition = m.points[0];
            }
            break;
        case MissionType::Escape:
            st.objective = "Lose the police";
            break;
        }
    }
    applyActivityStatus(player, st);
    return st;
}

} // namespace server
