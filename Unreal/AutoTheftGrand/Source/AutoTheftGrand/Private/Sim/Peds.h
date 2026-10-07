// Pedestrians (port of src/game/peds.js): spawning and despawning round the player, wandering the walk graph
// on the pavements, reactions (fleeing, cowering, hands up, fighting back), gangs with their territories,
// followers and guards, and the bodies left behind.
#pragma once

#include "Character.h"
#include "Systems.h"
#include <functional>

namespace atg {

class CityMap;
class Animal;

struct GangDef { std::string id, name; uint32_t color; std::string district; bool friendly; std::vector<std::string> weapons; bool aggroOnly; double range; };
const std::map<std::string, GangDef>& Gangs();
const std::vector<std::string>& GangOrder();     // (in the JavaScript object's order)
Appearance SaintLook(RNG& rng);
const std::vector<std::string>& PedLines(const std::string& kind);

struct PedOpts {
	std::string brain = "civilian", gang, state = "wander";
	bool persistent = false;
	std::string missionTag;
	double accuracy = 0.5, damageMul = 0.55;
	double health = 100, armor = 0;
	std::string team = "civilian";
	bool hasY = false; double y = 0;
	bool hasYaw = false; double yaw = 0;
	std::string weapon;
	bool hasAppearance = false; Appearance appearance;
};

class Ped : public Character {
public:
	Ped(Game& game, const Appearance& look, const PedOpts& opts);
	std::string brain, gang, state;
	double stateTime = 0, walkSpeed;
	int node = -1, prevNode = -1;
	V3 targetPos;
	Ref<Character> threat;
	V3 threatPos;
	double brave, fireTimer = 0, meleeTimer = 0, thinkTimer, lastSay = -10;
	Ref<Character> follow;
	int followSlot = 0;
	bool persistent;
	std::string missionTag;
	double accuracy, damageMul;
	bool hostile = false;
	double deathTime = 0;
	int animLod = 0;
	double offX = 0, offZ = 0, nodeOffset = 0;
	Ref<Ped> talkPartner;
	double idleTime = 6, cowerTime = 4;
	bool hasGuardFace = false; double guardFace = 0;
	double gotoSpeed = 0; std::string afterGoto;
	std::function<void()> onArrive;
	std::function<void(double)> scriptThink;
	int jaywalker = -1;                   // -1 unset, 0 never
	bool hasFleeSpeed = false; double fleeSpeed = 0;
	double moneyDrop = 0;
	// street crime (npccrime.js) keeps its state here: what they're up to, their case, the case a cop is on
	std::shared_ptr<struct CrimeTask> crimeTask;
	std::shared_ptr<struct NpcCase> npcCase, npcTask;
	int npcWanted = 0;                    // (the stars over their head)
	bool shopClerk = false;
	Ref<Animal> walkedDog;
	double loot = 0, crimeClock = NaN();
	bool hasNpcAim = false; double npcAimAccuracy = 0, npcAimDamageMul = 0; // (a cop's aim before a shoot-out)
	bool criminal = false;                // (vigilante targets: killing them is no crime)
	// police (police.js): the car a cop came in, the spot a roadblock cop holds, when they next shout
	Ref<Vehicle> homeCar;
	// soldiers (military.js, army.js): a garrison post's patrol between two points; response: sent by the army
	// at five stars (killing one doesn't lock Fort Carver down)
	bool soldier = false, response = false;
	bool hasPatrol = false; double patrol[2][2] = {}; int patrolLeg = 0; double patrolWait = 0;
	bool hasHoldPos = false; V3 holdPos;
	double lineT = NaN();
	std::map<std::string, double> num;    // (free slots for systems that hang their own numbers on a person)
	std::map<std::string, std::string> str;

	void say(const std::string& text);
	void setState(const std::string& s) { if (state != s) { state = s; stateTime = 0; } }
	bool goTo(double x, double z, double speed, double dt, double arriveDist = 0.4);
	void stop() { moveTargetX = moveTargetZ = 0; }
	void think(double dt);
	void update(double dt) override;
	void onDamaged(Character* src, double dmg, const DamageInfo& info) override;
	void onCarjacked(Character* by) override;
	void onBumped(Vehicle* v) override;
	void fleeFrom(const V3& p) override { threat = nullptr; threatPos = p; setState("flee"); }
	void wander(double dt);
	void flee(double dt);
	void attack(double dt);
	void followLeader(double dt);
	void guard(double dt);
};

class PedManager : public System {
public:
	explicit PedManager(Game& game);
	Game& game;
	std::vector<std::shared_ptr<Ped>> list;
	int maxPeds;
	double spawnTimer = 0;
	std::map<std::string, bool> gangAggro;
	bool ignoreView = false;

	Ped* spawnPed(double x, double z, const PedOpts& opts = PedOpts());
	Appearance districtLook(RNG& rng, const std::string& district);
	void populate(int n = 20);
	void spawnAmbient();
	bool inView(double x, double z, double margin = 0) const;
	double density() const;
	void onNoise(Character* src, const V3& pos, double radius, bool gunfire);
	void onPedDamaged(Ped* ped, Character* src, double dmg, const DamageInfo& info);
	void onDeath(Character* c, const DamageInfo& info);
	void remove(Character* p);
	void update(double dt) override;
	std::shared_ptr<Ped> shared(Ped* p) const;
};

// walkAreaAt (citymap.js): the block or town whose pavement graph covers a point
struct WalkArea { const std::vector<int>* nodeIds = nullptr; std::string district; };
bool WalkAreaAt(const CityMap& map, double x, double z, WalkArea& out);

} // namespace atg
