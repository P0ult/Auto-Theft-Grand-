// The glue rules (port of src/game/gameplay.js): stats, WASTED and BUSTED with the slow-motion death
// camera and the respawn at the hospital or the police station, drift and stunt bonuses, damage feedback,
// smoke and fire on damaged cars, skid marks, the title-screen flyover and first-time hints.
#pragma once

#include "Systems.h"

namespace atg {

class Game;

// free roam's weapons: id, ammo
const std::vector<std::pair<std::string, double>>& FreeRoamKit();

class Gameplay : public System {
public:
	explicit Gameplay(Game& game);
	Game& game;
	std::string state = "menu"; // menu, playing, dead, busted, respawning
	double deathTimer = 0;
	std::string deathKind;
	bool shardShown = false;
	double shardAt = 1.1, shardTime = 0, audioStart = -1;
	double drift = 0, driftTime = 0, driftCombo = 0;
	double airTime = 0; bool hasAirStart = false; V3 airStart; double airFlips = 0;
	V3 lastPos;
	double flyT = 0;
	std::map<std::string, bool> hinted;
	std::function<void()> afterRespawn;

	void onPlayerDeath();
	void onBusted();
	void respawn(const std::string& kind);
	void update(double dt) override;

private:
	struct DeathCam { double dx, dz, d0, h0, side; };
	bool hasDeathCam = false; DeathCam deathCam{};
	void beginDeathScreen(const std::string& kind, double slowmo);
	void deathCamera(double dt);
	void flyover(double dt);
};

} // namespace atg
