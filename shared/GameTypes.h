#pragma once

#include <cstddef>
#include <cstdint>

// Types shared by the client and the server (gameplay vocabulary and
// network constants).
namespace av::proto {

inline constexpr std::uint8_t kProtocolVersion = 3;
inline constexpr std::uint16_t kDefaultPort = 41986;
inline constexpr std::uint16_t kUdpMagic = 0xA51C;
inline constexpr std::uint16_t kDiscoveryPort = 41987; // LAN room discovery (UDP broadcast)
inline constexpr std::uint16_t kDiscoveryMagic = 0xA51D;
inline constexpr std::size_t kMaxUdpPayload = 1200;
inline constexpr float kInputStep = 1.0f / 60.0f; // every input frame advances the player by this much

// What a room is for. Activities run in any room; the mode picks what starts automatically.
enum class GameMode : std::uint8_t { FreeRoam, Races, CopsAndRobbers };
const char* toString(GameMode m);

enum class CharacterMode : std::uint8_t { OnFoot, Entering, InVehicle, Exiting, KnockedDown };
// Effect: tracers, explosions and flying projectiles (subtype = EffectType).
enum class EntityKind : std::uint8_t { Character, Vehicle, Pickup, Marker, Effect };
enum class EffectType : std::uint8_t { Tracer, Explosion, Rocket, Grenade };
// Pickup subtypes: 0 flamingo statuette, 1 cash, 2 health, 3 jetpack, 10 + weapon id = that weapon.
namespace PickupType {
enum : std::uint8_t { Flamingo = 0, Cash = 1, Health = 2, Jetpack = 3, WeaponBase = 10 };
}
enum class MarkerKind : std::uint8_t { None, MissionStart, Objective, Checkpoint, SavePoint, Dealership, Door, Helipad };

namespace Buttons {
enum : std::uint16_t {
    Jump = 1 << 0,
    Run = 1 << 1,
    Crouch = 1 << 2,
    Interact = 1 << 3,  // edge: start mission / save / buy
    EnterExit = 1 << 4, // edge: get in / out of a vehicle
    Punch = 1 << 5,     // edge
    Wave = 1 << 6,      // edge
    Handbrake = 1 << 7,
    Fire = 1 << 8,      // held: attack with the current weapon
    Ascend = 1 << 9,    // held: jetpack / helicopter up
    Descend = 1 << 10,  // held: helicopter down
};
}

enum class EventKind : std::uint8_t {
    Info, MissionStarted, MissionPassed, MissionFailed, Money, Wanted, Arrested, Hospitalized, Pickup, Saved, Purchase,
};

// Entity flag bits in snapshots.
namespace EntityFlags {
enum : std::uint8_t { Player = 1 << 0, Police = 1 << 1, Sirens = 1 << 2, Talking = 1 << 3, Owned = 1 << 4, Robber = 1 << 5, Jetpack = 1 << 6, Dead = 1 << 7 };
}

} // namespace av::proto
