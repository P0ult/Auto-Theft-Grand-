// Fort Carver and the other hardware spots (port of src/game/military.js): streams in the parked aircraft and
// tanks the world builder placed (respawning them a while after they're taken or destroyed), garrisons the base
// with soldiers on their posts and patrols, and enforces the restricted zone: a warning on the way in, then the
// army opens fire and the police send a three-star response.
#pragma once

#include "Systems.h"

namespace atg {

class Game;
class Ped;
class Vehicle;
struct Appearance;

// the Fort Carver uniform (army.js soldierLook; the garrison and the army share it)
Appearance SoldierLook(RNG& rng);

class Military : public System {
public:
	explicit Military(Game& game);
	Game& game;
	struct Fixed {
		std::string type;
		double x, z, yaw, y, respawn;
		Ref<Vehicle> veh, old;
		bool hasVeh = false; // (veh reads null once it is removed; this remembers there was one)
		double timer = 0;
		bool respawned = false;
	};
	std::vector<Fixed> fixed;
	std::vector<Ref<Ped>> soldiers;
	bool garrisoned = false, inside = false, alerted = false;
	double grace = 0, t = 0, raiseT = 0;

	bool inBase(const V3& p, double margin = 0) const;
	void alert(const std::string& msg = "");
	void update(double dt) override;

private:
	void streamVehicles(const V3& pos);
	void garrison(const V3& pos);
	Ped* spawnSoldier(int post, RNG& rng);
};

} // namespace atg
