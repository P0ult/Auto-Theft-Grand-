// The army at five stars (port of src/game/army.js; police.js runs one to five): Barracks troop trucks and Ranger
// jeeps that run you down and unload soldiers, a Mammoth tank that hunts you down and shells you, and a Warhawk
// gunship flown by an autopilot that strafes you with its chin gun and rocket pods. They pull out when the stars
// drop below five, and everything resets when you're wasted or busted.
#pragma once

#include "Systems.h"

namespace atg {

class Game;
class Ped;
class Vehicle;
struct Blip;

class Army : public System {
public:
	explicit Army(Game& game);
	Game& game;
	struct Unit {
		Ref<Vehicle> veh;
		std::string kind;   // truck, jeep, tank, heli
		bool unloaded = false, leaving = false;
		double fireT = 3;
		// the gunship: its burst, the next burst and rocket, where it is on its orbit and which way round
		double burst = 0, burstT = 4, rocketT = 6, orbit = 0, leaveT = 0;
		double side = 1;
		// the tank: how far off it aims, until when
		bool hasMiss = false; V3 miss; double missT = 0;
	};
	std::vector<Unit> units;
	std::vector<Ref<Ped>> troops;   // soldiers on foot
	bool active = false, enabled = true;
	double time = 0;                // seconds at five stars
	double spawnT = 0, radioT = 0;

	V3 targetPos() const;
	void update(double dt) override;
	void reset() override;
	Ped* soldier(double x, double z, const std::string& weapon = "", bool hasYaw = false, double yaw = 0, bool hasY = false, double y = 0);

private:
	std::vector<Ref<Vehicle>> scratch; // (the police's spawnCar keeps the army's cars off its own list)
	void activate();
	void standDown();
	void spawn(double dt);
	Unit* ground(const std::string& type, int seats, const std::string& kind);
	void spawnHeli();
	void convoy(Unit& u, double dt);
	void tank(Unit& u, double dt);
	void fly(Unit& u, double dt);
	bool friendlyNear(const V3& p, double r, const Vehicle* self) const;
	void removeUnit(Unit& u);
	void despawn();
	void blipList(std::vector<Blip>& out) const;
};

} // namespace atg
