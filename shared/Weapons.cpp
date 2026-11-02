#include "Weapons.h"

namespace av::proto {

const WeaponDef& weaponDef(int id)
{
    //                       name               hud       kind                 dmg   range  interval pel spread  cone   ammo price auto
    static const WeaponDef table[kWeaponCount] = {
        {"Fists",           "FISTS",   WeaponKind::Melee,   12.0f,  1.6f, 0.60f, 1, 0.00f, 0.70f,   0,    0, false},
        {"Baseball bat",    "BAT",     WeaponKind::Melee,   30.0f,  2.1f, 0.75f, 1, 0.00f, 0.70f,   0,  100, false},
        {"Knife",           "KNIFE",   WeaponKind::Melee,   45.0f,  1.7f, 0.50f, 1, 0.00f, 0.70f,   0,  150, false},
        {"Pistol",          "PISTOL",  WeaponKind::Gun,     22.0f, 60.0f, 0.32f, 1, 0.025f, 0.22f, 36,  300, false},
        {"Uzi",             "UZI",     WeaponKind::Gun,     12.0f, 45.0f, 0.085f, 1, 0.06f, 0.22f, 150,  600, true},
        {"Shotgun",         "SHOTGUN", WeaponKind::Gun,     12.0f, 24.0f, 0.95f, 8, 0.10f, 0.26f,  24,  700, false},
        {"Assault rifle",   "RIFLE",   WeaponKind::Gun,     26.0f, 85.0f, 0.12f, 1, 0.03f, 0.18f, 120, 1200, true},
        {"Sniper rifle",    "SNIPER",  WeaponKind::Gun,    100.0f, 220.0f, 1.40f, 1, 0.00f, 0.07f,  12, 1500, false},
        {"Minigun",         "MINIGUN", WeaponKind::Gun,     14.0f, 60.0f, 0.045f, 1, 0.08f, 0.24f, 400, 3000, true},
        {"Rocket launcher", "ROCKETS", WeaponKind::Rocket, 140.0f,  6.5f, 1.60f, 1, 0.00f, 0.15f,   6, 2500, false},
        {"Grenades",        "GRENADE", WeaponKind::Grenade, 110.0f, 5.5f, 0.90f, 1, 0.00f, 0.00f,   8,  400, false},
    };
    return table[isWeapon(id) ? id : 0];
}

} // namespace av::proto
