// Free roam extras (port of src/game/freeroam.js): unlimited cash and ammo, and fast travel to any landmark
// (the pause menu's Teleport tab, or T). Destinations snap to the nearest street (a kerbside lane when
// driving, the pavement on foot); an aircraft that is already flying arrives in the air over the
// destination at the same height and speed. freeroam.js roadSpot is FindRoadSpot (Phone.h).
#pragma once

#include "Systems.h"
#include "TaxiSystem.h"  // for FreeDestination

namespace atg {

class Game;
struct Cheats;
struct VehicleDef;

constexpr double FREE_CASH = 99999999;

// admin.js: what the spawner offers, by group (trains need their rails, so they're left out), and the cheat
// switches (key, label, what it does)
struct SpawnGroup { std::string name; std::vector<std::string> ids; };
const std::vector<SpawnGroup>& SpawnGroups();
struct AdminToggle { std::string key, label, tip; bool Cheats::*flag; };
const std::vector<AdminToggle>& AdminToggles();

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

	// ---- the admin tools (admin.js): single player free roam, except the vehicle spawner, which works online too.
	// Each returns the line the Admin tab and the console show.
	bool allowed() const;
	bool canSpawn() const { return active(); }
	std::string toggle(const std::string& key, int on = -1); // (on: -1 flips it)
	std::string heal();
	std::string weapons();
	std::string wanted(double n);
	std::string setTime(double h);
	std::string weather(const std::string& w);
	std::string repair();
	std::string flip();
	std::string clearArea();
	std::string explodeNearby();
	std::string skydive();
	std::string bodyguard();
	std::string enemies();
	// air: -1 as the spawner decides (planes in the air; helicopters on the ground unless it's cramped), 0 / 1
	std::string summon(const std::string& id, bool into = true, int air = -1);
	std::string run(const std::string& line);
	std::string traffic = "normal", peds = "normal"; // (the street density)
	bool hasColor = false; uint32_t color = 0;      // (the spawner's paint)
	std::vector<Ref<Vehicle>> summoned;

private:
	int guards = 0, baseCars = -1, basePeds = -1;
	bool cramped(double x, double z, const VehicleDef& d);
	void density();
	bool adminApplied = false, adminSlow = false, adminFreeze = false;
	double normalTimeScale = 1, normalEnvTimeScale = 1;
	double riotTimer = 0;
	void applyAdminPowers(double dt);
	std::string tpGroup;
	std::vector<StoreItem> teleportRows();
};

} // namespace atg
