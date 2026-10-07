// The walk-in shops' clerks (port of src/game/shops.js): a clerk behind each counter who serves you when you
// step up to the counter (the marker), greets you, and reacts when you point a gun at them. The burger bar,
// the stores, the bar, the cafe and the pet shop hand over the till (and call the cops); the Gun Barn's owner
// pulls a shotgun. Kill the clerk and the shop is shut until you've been gone a while. Also the stores' stock
// (what each item does) and the drinks' sway.
#pragma once

#include "Systems.h"
#include "CityMap.h"

namespace atg {

class Game;
class Ped;
class Marker;
class Animal;

struct ClerkDef {
	std::vector<std::string> greet, threat;
	bool hostile = false;
	std::string weapon, label, icon;
	uint32_t color = 0xffffff;
	int cash[2] = { 0, 0 };
};

class ShopSystem : public System {
public:
	explicit ShopSystem(Game& game);
	Game& game;

	struct Shop {
		const InteriorShell* it = nullptr;
		const ClerkDef* def = nullptr;
		std::shared_ptr<Ped> clerk;     // (kept after it's removed, as the JavaScript keeps the object)
		std::string state = "closed";   // closed, calm, handsup, robbed, hostile, dead
		double respawnAt = 0, t = 0, robbedAt = -1e9;
		bool greeted = false;
		Marker* marker = nullptr;
		std::vector<std::shared_ptr<Animal>> pets;
	};
	std::vector<Shop> shops;

	// after a respawn: a clerk who was shooting at you goes back behind the counter
	void calmDown();
	Shop* shopAt(double x, double z);
	// the player stepped up to the counter
	void serve(Shop& s);
	void update(double dt) override;

private:
	Ped* spawnClerk(Shop& s);
	void stockKennels(Shop& s);
	void think(Shop& s, double dt);
	bool threatened(Ped* c);
};

} // namespace atg
