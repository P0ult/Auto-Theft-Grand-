// Free roam extras (port of src/game/freeroam.js): unlimited cash and ammo, and fast travel to any landmark
// (the pause menu's Teleport tab, or T). Destinations snap to the nearest street (a kerbside lane when
// driving, the pavement on foot); an aircraft that is already flying arrives in the air over the
// destination at the same height and speed. freeroam.js roadSpot is FindRoadSpot (Phone.h).
#pragma once

#include "Systems.h"
#include "TaxiSystem.h"  // for FreeDestination

namespace atg {

class Game;

constexpr double FREE_CASH = 99999999;

class FreeRoam : public System {
public:
	explicit FreeRoam(Game& g);
	Game& game;

	bool active() const;
	bool toggleCheat(const std::string& key);
	bool setCheat(const std::string& key, bool enabled);
	void setWanted(int level);
	bool setTimeOfDay(double hours);
	bool setWeather(const std::string& weather);
	void earlyInput(double dt) override;
	void update(double dt) override;

	std::vector<FreeDestination> destinations() const;
	void teleport(const FreeDestination& d, bool instant = false);
	void place(const FreeDestination& d);
	// the pause menu's Teleport tab, as a panel: the groups, then a group's destinations
	void openTeleport();

private:
	bool adminApplied = false, adminSlow = false, adminFreeze = false;
	double normalTimeScale = 1, normalEnvTimeScale = 1;
	double riotTimer = 0;
	void applyAdminPowers(double dt);
	std::string tpGroup;
	std::vector<StoreItem> teleportRows();
};

} // namespace atg
