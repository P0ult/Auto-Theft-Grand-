// NPC crime (port of src/game/npccrime.js): the people of Los Soles break the law too. Pedestrians jaywalk,
// some drivers speed and run red lights, bumps between cars turn into road rage or a hit-and-run, and muggers
// and car thieves work the streets (more at night and in the rougher districts). A crime the police see, or
// that somebody calls in, puts wanted stars on the culprit, and the nearest patrol responds: a ticket for the
// small stuff; for the rest a pursuit, a tackle and a ride in the back of the cruiser. The player's own wanted
// level always comes first.
#pragma once

#include "Systems.h"
#include "Traffic.h"

namespace atg {

class Game;
class Ped;

// one suspect's case (npccrime.js rec)
struct NpcCase {
	Ref<Ped> ped;
	std::string crime;
	int stars = 0;
	bool known = false;
	double t = 0, seenT = 0;
	V3 pos, scene;
	std::string phase = "open";        // open, respond, confront, pulled, chase, arrest, escort, closed
	struct Unit { Ref<Vehicle> car; std::vector<Ref<Ped>> cops; };
	std::shared_ptr<Unit> unit;
	Ref<Vehicle> car;
	double callAt = NaN(), expire = NaN(), dispatchT = 0;
	int pedPersistent = -1, carPersistent = -1, shownStars = -1; // (-1: not recorded yet)
	std::string reaction, force, why;
	bool park = false, said = false;
	double stopT = 0, escortT = 0, talkT = 0, cuffT = 0, fleeAt = NaN();
};

// what a person has on (npccrime.js crimeTask)
struct CrimeTask {
	std::string kind;                  // jaywalk, mug, victim, steal, argue, rejoin, escort
	double x = 0, z = 0, sp = 0, t = 0, rt = 0, dur = 0, sayT = NaN();
	std::vector<int> nodeIds;          // (jaywalk: the walk area on the far side)
	Ref<Ped> victim, by, foe;
	std::string w, stage;
	Ref<Vehicle> car, other;
	bool occupied = false, tried = false, fight = false, hasFoe = false; // (hasFoe: a fight started, even if the foe is gone now)
	V3 from;
};

// a patrol car on a job: follows the roads to the suspect, pulls up near them, and runs a fleeing car down
class SuspectDriver : public LaneDriver {
public:
	SuspectDriver(Game& game, Vehicle* veh, std::shared_ptr<NpcCase> rec);
	std::weak_ptr<NpcCase> rec;
	double reverseT = 0, dist = 0;
	bool direct = false;
	void update(double dt) override;
protected:
	bool chooseNext(const LanePath& cur, Exit& out) override;
private:
	V3 tp() const;
};

// getting away: fast, through red lights, always taking the road that leads away from the police
class FleeDriver : public LaneDriver {
public:
	FleeDriver(Game& game, Vehicle* veh, std::function<V3()> from);
	std::function<V3()> from;
protected:
	bool chooseNext(const LanePath& cur, Exit& out) override;
};

// pulled over: ease to the kerb and wait (it reads as broken down to the traffic behind, which drives round)
class PullOverDriver : public LaneDriver {
public:
	PullOverDriver(Game& game, Vehicle* veh);
	double t = 0;
	void update(double dt) override;
	void resnap() override {}
};

class NpcCrime : public System, public INpcCrime {
public:
	explicit NpcCrime(Game& game);
	Game& game;
	std::vector<std::shared_ptr<NpcCase>> cases;
	double mugT, theftT, recklessT = 4, scanT = 0, spawnedT = -99;
	std::vector<std::pair<double, std::function<void()>>> later; // (little delays in game time)

	double level() const;
	bool running() const;
	struct CommitOpts { bool hasWitness = false; Vehicle* witness = nullptr; double seeR = 42; Vehicle* car = nullptr; double call = 0; double delay0 = 4, delay1 = 9; bool force = false; };
	NpcCase* commit(Ped* ped, const std::string& key, const CommitOpts& opts = CommitOpts());
	void update(double dt) override;
	bool copThink(Ped* cop, double dt) override;
	bool pedThink(Ped* ped, double dt) override;
	bool jaywalk(Ped* ped) override { return jaywalk(ped, false); }
	bool jaywalk(Ped* ped, bool force);
	void makeReckless(Vehicle* v);
	bool stage(const std::string& kind);
	void tagged(std::vector<NpcTag>& out) const override;

private:
	bool policeFree() const;
	V3 pp() const;
	void after(double sec, std::function<void()> fn);
	double clockDt(Ped* c);
	bool civ(const Ped* p) const;
	Ped* driverOf(Vehicle* v) const;
	Vehicle* policeSees(double x, double y, double z, double R = 40) const;
	void know(const std::shared_ptr<NpcCase>& rec, Vehicle* unit = nullptr);
	void dispatchUnit(const std::shared_ptr<NpcCase>& rec);
	void assign(const std::shared_ptr<NpcCase>& rec, Vehicle* car);
	void release(const std::shared_ptr<NpcCase>& rec);
	void close(const std::shared_ptr<NpcCase>& rec, const std::string& why);
	void scan();
	void updateCase(const std::shared_ptr<NpcCase>& rec, double dt);
	void direct(const std::shared_ptr<NpcCase>& rec, double dt);
	void carChase(const std::shared_ptr<NpcCase>& rec, Vehicle* sv);
	void run(Ped* c, double x, double z, double sp, double dt);
	Ped* nearestCop(const NpcCase& rec, Ped* ped) const;
	void stageTick(double dt);
	void stolen(Ped* ped, const CrimeTask& t);
	bool mugThink(Ped* m, CrimeTask& t, double dt);
	bool argueThink(Ped* ped, CrimeTask& t, double dt);
	void onCrash(Vehicle* A, Vehicle* B, double impact);
	void onPedHit(Character* c, Vehicle* v, double spd);
	void onViolence(Character* att, Character* vic, bool kill);
};

} // namespace atg
