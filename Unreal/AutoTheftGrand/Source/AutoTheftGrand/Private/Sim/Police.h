// The police (port of src/game/police.js): the wanted level and its heat, witnesses, evading, patrol cars,
// pursuit drivers that route along the roads then drive straight at you and ram, cops on foot who shoot
// or come to arrest you, roadblock cops holding their line, BUSTED, and the helicopter with its
// searchlight (and its sniper at four stars).
#pragma once

#include "Skeleton.h"
#include "Systems.h"
#include "Traffic.h"

namespace atg {

class Game;
class Ped;
class Police;

Appearance CopAppearance(bool swat = false);

// routes along the road network toward the target, then drives directly at it and rams
class PursuitDriver : public LaneDriver {
public:
	PursuitDriver(Game& game, Vehicle* veh, const LaneStart* start, Police* police);
	Police* police;
	bool direct = false, wasDirect = false;
	double reverseT = 0, chaseStuck = 0;
	void update(double dt) override;
protected:
	bool chooseNext(const LanePath& cur, Exit& out) override;
};

// the police helicopter (its own simple flyer, not one of the aircraft)
struct PoliceHeli {
	V3 pos, vel;
	double yaw = 0, pitch = 0, rotor = 0, tailRotor = 0, spin = 0;
	double health = 700;
	bool down = false, done = false;
	double fireT = 2, leaveT = NaN();
	// the searchlight: a cone toward the target at night, and its spot light
	bool coneOn = false; double coneOpacity = 0, spotIntensity = 0; V3 lightAt;
};

class Police : public System, public IPolice {
public:
	explicit Police(Game& game);
	Game& game;
	double heat = 0;
	int level = 0;
	double lastSeen = 0;
	bool seen = false;
	V3 lastKnown;
	std::vector<Ref<Vehicle>> cars;
	std::vector<Ref<Ped>> cops;
	double spawnTimer = 0, arrestTimer = 0, patrolTimer = 10, lineTimer = 0;
	bool flash = false, enabled = true;
	std::unique_ptr<PoliceHeli> heli;

	V3 targetPos() const;
	void crime(double amount, const V3& pos, bool severe, bool noise = false);
	void raise(int l);
	void setLevel(int l);
	void clear();
	Ped* spawnCop(double x, double z, const struct PedOpts* opts = nullptr);
	struct CarOpts {
		std::string type;
		std::function<Character*(Vehicle*, int)> crew;
		int seats = -1;
		std::vector<Ref<Vehicle>>* list = nullptr;
		int siren = -1;
		bool hasRadius = false; double r0 = 0, r1 = 0;
	};
	Vehicle* spawnCar(bool pursuit = true, const V3* near = nullptr, const CarOpts& opts = CarOpts());
	void copThink(Ped* cop, double dt) override;
	void update(double dt) override;
	void reset() override;

	// IPolice
	int wantedLevel() const override { return level; }
	void clearWanted() override { reset(); }
	bool searching() const override { return flash; }
	void radarCones(std::vector<RadarCone>& out) const override;
	void radarCops(std::vector<V3>& out) const override;
	bool heliRay(double ox, double oy, double oz, double dx, double dy, double dz, double maxT, double& t) const override;
	void heliHit(double dmg) override;
	bool heliAlive(V3& pos) const override;

private:
	void updateLevel();
	void updateHeli(double dt);
};

} // namespace atg
