// Weapon definitions (src/game/weapondefs.js) and how they are held (character.js HOLD) and fired from
// (MUZZLE offsets in the weapon model's space: barrel along +z, top along +y, origin at the grip).
#pragma once

#include "Core.h"

namespace atg {

struct WeaponDef {
	std::string id, name;
	int slot = 0;
	std::string type;   // melee, gun, launcher, thrown
	std::string hold;   // none, knife, bat, pistol, smg, shotgun, rifle, rpg
	double damage = 0, range = 0, rate = 0, spread = 0, recoil = 0, shake = 0, headMul = NaN(), spinUp = 0;
	int clip = 0, pellets = 1, ammoPack = 0;
	double price = 0, ammoPrice = 0;
	bool automatic = false, scope = false, heavy = false;
	std::string sound, icon;
	bool melee() const { return type == "melee"; }
	bool gun() const { return type == "gun"; }
};

const WeaponDef* FindWeapon(const std::string& id);
const std::vector<std::string>& WeaponOrder();
const std::map<std::string, WeaponDef>& Weapons();

// how a weapon sits in the right hand (position, Euler XYZ) and where its muzzle is
struct Hold { V3 p, r; };
const Hold& HoldFor(const std::string& id);
bool MuzzleFor(const std::string& id, V3& out);

} // namespace atg
