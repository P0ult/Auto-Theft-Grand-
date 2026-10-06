// Pickups (port of src/game/pickups.js): cash, weapons, health, armour and the hidden packages, the glowing
// markers the missions and services use, the Spray Shack and the safehouse's save point. The pickups and
// markers are plain state; ATGPickups draws them.
#pragma once

#include "Systems.h"

namespace atg {

class Game;
struct Blip;

struct MarkerOpts {
	bool hasY = false; double y = 0;
	double radius = 1.5;
	uint32_t color = 0xffd23f;
	double height = NaN();          // (unset: 3 for wide markers, else 1.8)
	bool arrow = true, blip = true;
	std::string icon, label;        // (icon unset: a dot)
	bool vehicleOnly = false, footOnly = false;
	std::function<void(class Marker*)> onEnter;
};

// a glowing cylinder you drive or walk into, with a bobbing arrow over it and a blip on the radar
class Marker {
public:
	Marker(Game& game, double x, double z, const MarkerOpts& opts);
	Game& game;
	V3 pos;
	double radius, height;
	uint32_t color;
	bool arrow;
	double arrowY = 0, arrowSpin = 0, time = 0; // (the arrow's height and turn, the shader's clock)
	std::shared_ptr<Blip> blip;
	std::function<void(Marker*)> onEnter;
	bool vehicleOnly, footOnly;
	bool inside = false, removed = false, visible = true;
	void setPos(double x, double z);
	void update(double dt, double t);
	void remove();
};

struct PickupData {
	std::string weapon;
	double ammo = 0, amount = 0;
	int packageId = -1;
	bool hasY = false; double y = 0;        // (on a deck, a roof, a ship)
	double life = kInf, respawn = 0;
	bool blip = false;
};

class Pickup {
public:
	Pickup(Game& game, const std::string& kind, const V3& pos, const PickupData& data);
	Game& game;
	std::string kind;          // money, health, armor, weapon, package
	PickupData data;
	V3 pos;
	double t, life, respawn, hiddenUntil = 0;
	double spin = 0, bob = 0.7; // (the mesh's turn and height)
	uint32_t glow;             // (the glow disc's colour)
	bool visible = true, removed = false;
	std::shared_ptr<Blip> blip;
	void update(double dt);
	void remove();
};

class Pickups : public System, public IPickups {
public:
	explicit Pickups(Game& game);
	Game& game;
	std::vector<std::shared_ptr<Pickup>> list;
	std::vector<std::shared_ptr<Marker>> markers; // (a Set in the browser game: kept in insertion order)
	std::vector<Marker*> services;
	std::set<int> collectedPackages;
	struct Spot { int id; double x, z; };
	std::vector<Spot> packageSpots;
	std::vector<std::shared_ptr<Pickup>> packages;
	Marker* saveMarker = nullptr;
	double t = 0;

	Marker* addMarker(double x, double z, const MarkerOpts& opts = MarkerOpts());
	void removeMarker(Marker* m);
	void dropMoney(const V3& pos, int amount) override;
	void dropWeapon(const V3& pos, const std::string& weapon, int ammo) override;
	Pickup* spawn(const std::string& kind, double x, double z, const PickupData& data = PickupData());
	void refreshPackages();
	void spray();
	void eat();
	void update(double dt) override;
	bool collect(Pickup* p);
	void spawnSafehouseRewards();

private:
	std::vector<std::shared_ptr<Pickup>> rewardPickups;
	void setupWorld();
	void removeFromList(Pickup* p);
};

} // namespace atg
