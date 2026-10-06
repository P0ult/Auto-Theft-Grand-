// The vehicle manager (port of src/game/vehicles.js): spawning, parked cars streamed round the player,
// vehicle-vehicle and vehicle-person collisions, running people over, and the walk-to-the-door, open,
// carjack, sit-down, close sequences for getting in and out (for anyone, not just the player).
#pragma once

#include "Vehicle.h"
#include <functional>

namespace atg {

class Game;
class Character;

class VehicleManager {
public:
	explicit VehicleManager(Game& game) : game(game) {}
	Game& game;
	std::vector<std::shared_ptr<Vehicle>> list;
	std::map<int, Ref<Vehicle>> parked;  // parking spot -> vehicle
	std::set<int> consumedSpots;
	double streamTimer = 0;
	int maxParked = 26;
	// other kinds (bikes, boats, aircraft, the tank, trains) register their own classes
	using Factory = std::function<std::shared_ptr<Vehicle>(Game&, const std::string&, double, double, double, const SpawnOpts&)>;
	static std::vector<std::pair<std::function<bool(const VehicleDef&)>, Factory>>& Factories();
	// can this kind be spawned yet? (road cars always; the others once their class is ported)
	static bool Supported(const VehicleDef& def);

	Vehicle* spawn(const std::string& id, double x, double z, double yaw, const SpawnOpts& opts = SpawnOpts());
	void remove(Vehicle* v);
	void update(double dt);
	Vehicle* nearestEnterable(const V3& pos, double maxDist = 4.5);
	bool isBusy(const Character* c) const;
	bool enter(Character* c, Vehicle* v, int seat = 0, bool force = false);
	bool seatNow(Character* c, Vehicle* v, int seat = 0);
	bool exit(Character* c);
	V3 doorTarget(Vehicle* v, int seat, Character* c = nullptr);
	std::shared_ptr<Vehicle> shared(Vehicle* v) const;
	// (vehicles built outside spawn: the trains)
	Vehicle* add(std::shared_ptr<Vehicle> v) { list.push_back(v); return v.get(); }
	// contacts (the trains run these for their carriages)
	void carCar(Vehicle* A, Vehicle* B);
	void carPed(Vehicle* v, Character* c);

private:
	struct Seq {
		bool enter = true;
		std::shared_ptr<Character> chr;
		std::shared_ptr<Vehicle> veh;
		int seat = 0;
		std::string phase;
		double t = 0;
		bool force = false;
		V3 fromLocal, toLocal;
	};
	std::vector<Seq> seqs;
	void runOver(Vehicle* v, Character* c, double spd);
	bool runSeq(Seq& s, double dt);
	void beginSit(Seq& s);
	void streamParked();
};

} // namespace atg
