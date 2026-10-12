// Raiding the MV Pacific Star (port of src/game/shipraid.js). The ship at Port Morena carries a crew of
// deckhands (hi-vis and hard hats), hired security (plate carriers and balaclavas) and the captain up on the
// bridge. Walk up the gangway and you're told to get off; draw a gun, shoot, hurt anyone or overstay and the
// whole ship turns on you, and the captain radios the coast guard. Up on the bridge the captain's safe holds the
// payroll; three containers are full of contraband. The crew and the loot come back a while after you've
// cleaned the ship out. The ship itself (Gen/CargoShip) is built here: its collision goes into the game's
// world, its meshes are drawn by ATGWorld.
#pragma once

#include "Systems.h"
#include "CargoShip.h"

namespace atg {

class Game;
class Ped;
class Marker;
struct Blip;

class ShipRaid : public System {
public:
	explicit ShipRaid(Game& game);
	Game& game;
	CargoShip ship;                         // (ship.safe etc.: the browser's S)
	std::vector<std::shared_ptr<Ped>> crew;
	bool spawned = false, alerted = false, warned = false, safeDone = false;
	double aboardT = 0, radioT = -1, lootedAt = -1e9, farT = 0, safeK = 0;
	int lootLeft = 0;
	double lastSafe = 0;
	Marker* safeMarker = nullptr;
	std::vector<Marker*> markers;
	std::shared_ptr<Blip> blip;

	bool restocked() const;
	void alert(const std::string& msg = "");
	void update(double dt) override;
	void spawn();
	void despawn();

private:
	Ped* nearestCrew(const V3& pos, const std::string& role);
	void openContainer(Marker* m, double x, double z);
	void openSafe();
	void checkDone();
};

} // namespace atg
