#pragma once

#include <cstdint>

// The weapon table, shared by the server (damage, fire rate, ammo) and the
// client (HUD, sounds, which mesh goes in the character's hand).
namespace av::proto {

enum class WeaponKind : std::uint8_t { Melee, Gun, Rocket, Grenade };

struct WeaponDef {
    const char* name;
    const char* shortName; // HUD
    WeaponKind kind;
    float damage;      // per hit / pellet; explosion damage at the centre
    float range;       // m (melee reach, gun range, explosion radius for rockets & grenades)
    float interval;    // s between shots
    int pellets;       // shotgun
    float spread;      // radians of random error per shot
    float aimCone;     // auto-aim half angle (radians)
    int pickupAmmo;    // ammo in one pickup
    int price;         // value when dropped (not used for sale yet)
    bool automatic;    // keeps firing while the button is held
};

inline constexpr int kWeaponCount = 11; // 0 = fists, 1..10 = weapons

enum Weapon : std::uint8_t {
    Fists = 0, Bat, Knife, Pistol, Smg, Shotgun, Rifle, Sniper, Minigun, RocketLauncher, Grenades,
};

const WeaponDef& weaponDef(int id);
inline bool isWeapon(int id) { return id >= 0 && id < kWeaponCount; }
inline bool usesAmmo(int id) { return id >= Pistol; }

} // namespace av::proto
