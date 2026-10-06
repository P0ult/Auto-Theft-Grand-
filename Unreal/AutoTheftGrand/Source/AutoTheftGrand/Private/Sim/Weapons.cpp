#include "Weapons.h"

namespace atg {

const std::map<std::string, WeaponDef>& Weapons() {
	static const std::map<std::string, WeaponDef> W = [] {
		std::map<std::string, WeaponDef> m;
		auto melee = [&](const char* id, const char* name, int slot, const char* hold, double dmg, double range, double price, const char* icon) {
			WeaponDef d; d.id = id; d.name = name; d.slot = slot; d.type = "melee"; d.hold = hold; d.damage = dmg; d.range = range; d.price = price; d.icon = icon;
			m[id] = d;
		};
		melee("fist", "Fists", 0, "none", 9, 1.25, 0, "fist");
		melee("knife", "Knife", 1, "knife", 40, 1.45, 150, "knife");
		melee("bat", "Baseball Bat", 1, "bat", 30, 1.9, 100, "bat");
		auto gun = [&](const char* id, const char* name, int slot, const char* type, const char* hold, double dmg, double rate, int clip, double spread, double range, bool aut,
			double price, double ammoPrice, int ammoPack, int pellets, double recoil, const char* sound, const char* icon, double shake) -> WeaponDef& {
			WeaponDef d; d.id = id; d.name = name; d.slot = slot; d.type = type; d.hold = hold; d.damage = dmg; d.rate = rate; d.clip = clip; d.spread = spread; d.range = range;
			d.automatic = aut; d.price = price; d.ammoPrice = ammoPrice; d.ammoPack = ammoPack; d.pellets = pellets; d.recoil = recoil; d.sound = sound; d.icon = icon; d.shake = shake;
			m[id] = d;
			return m[id];
		};
		gun("pistol", "9mm Pistol", 2, "gun", "pistol", 26, 0.22, 17, 0.012, 140, false, 400, 60, 34, 1, 0.9, "pistol", "pistol", 0.25);
		gun("smg", "Micro SMG", 3, "gun", "smg", 17, 0.07, 32, 0.035, 110, true, 1200, 120, 96, 1, 0.35, "smg", "smg", 0.15);
		gun("shotgun", "Pump Shotgun", 4, "gun", "shotgun", 14, 0.85, 7, 0.075, 60, false, 1500, 150, 14, 9, 1.2, "shotgun", "shotgun", 0.6);
		gun("rifle", "Assault Rifle", 5, "gun", "rifle", 32, 0.105, 30, 0.016, 220, true, 3500, 200, 90, 1, 0.45, "rifle", "rifle", 0.25);
		gun("rpg", "Rocket Launcher", 6, "launcher", "rpg", 200, 1.6, 1, 0.004, 300, false, 8000, 800, 4, 1, 1.4, "rpg", "rpg", 1);
		{ WeaponDef d; d.id = "grenade"; d.name = "Grenades"; d.slot = 7; d.type = "thrown"; d.hold = "none"; d.damage = 150; d.rate = 1.0; d.clip = 1; d.price = 600; d.ammoPrice = 300; d.ammoPack = 4; d.icon = "grenade"; m["grenade"] = d; }
		{ WeaponDef& d = gun("sniper", "Sniper Rifle", 5, "gun", "rifle", 150, 1.3, 5, 0.0005, 650, false, 6000, 300, 20, 1, 1.5, "sniper", "sniper", 0.8); d.scope = true; d.headMul = 8; }
		{ WeaponDef& d = gun("minigun", "Minigun", 6, "gun", "rifle", 18, 0.032, 600, 0.05, 160, true, 25000, 2000, 600, 1, 0.2, "smg", "minigun", 0.22); d.spinUp = 0.55; d.heavy = true; }
		{ WeaponDef d; d.id = "molotov"; d.name = "Molotov Cocktails"; d.slot = 7; d.type = "thrown"; d.hold = "none"; d.damage = 30; d.rate = 1.0; d.clip = 1; d.price = 450; d.ammoPrice = 250; d.ammoPack = 5; d.icon = "molotov"; m["molotov"] = d; }
		return m;
	}();
	return W;
}

const WeaponDef* FindWeapon(const std::string& id) { auto it = Weapons().find(id); return it == Weapons().end() ? nullptr : &it->second; }

const std::vector<std::string>& WeaponOrder() {
	static const std::vector<std::string> O = { "fist", "knife", "bat", "pistol", "smg", "shotgun", "rifle", "rpg", "grenade", "sniper", "minigun", "molotov" };
	return O;
}

const Hold& HoldFor(const std::string& id) {
	static const std::map<std::string, Hold> H = {
		{ "knife", { { 0, -0.07, 0.02 }, { kPi / 2, 0, 0 } } }, { "bat", { { 0, -0.07, 0.02 }, { kPi / 2 + 0.3, 0, 0 } } },
		{ "pistol", { { 0, -0.075, 0.02 }, { kPi / 2, 0, 0 } } }, { "smg", { { 0, -0.075, 0.02 }, { kPi / 2, 0, 0 } } },
		{ "shotgun", { { 0, -0.075, 0.02 }, { kPi / 2, 0, 0 } } }, { "rifle", { { 0, -0.075, 0.02 }, { kPi / 2, 0, 0 } } },
		{ "rpg", { { 0, -0.08, 0.02 }, { kPi / 2, 0, 0 } } }, { "grenade", { { 0, -0.08, 0.03 }, { 0, 0, 0 } } },
	};
	auto it = H.find(id);
	return it == H.end() ? H.at("pistol") : it->second;
}

bool MuzzleFor(const std::string& id, V3& out) {
	static const std::map<std::string, V3> M = {
		{ "pistol", { 0, 0.06, 0.16 } }, { "smg", { 0, 0.06, 0.27 } }, { "shotgun", { 0, 0.07, 0.66 } }, { "rifle", { 0, 0.075, 0.61 } },
		{ "rpg", { 0, 0.1, 0.75 } }, { "sniper", { 0, 0.07, 0.94 } }, { "minigun", { 0, 0.04, 0.84 } },
	};
	auto it = M.find(id);
	if (it == M.end()) return false;
	out = it->second;
	return true;
}

} // namespace atg
