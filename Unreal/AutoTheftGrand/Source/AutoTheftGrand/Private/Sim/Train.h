// Sol Line trains (port of src/entities/train.js): a diesel locomotive and three passenger carriages, or a
// string of freight wagons, running on the railway polyline, on the main line or the passing loop at Fern
// Creek. It runs station to station on its own (accelerate, brake to the platform, dwell, reverse at the
// termini); climb into the cab to drive it yourself (W / S), or board a carriage at a platform and ride.
// The locomotive is a Vehicle (enter / exit / HUD / camera); the carriages are followers whose collisions
// with cars and people are handled here.
#pragma once

#include "TrainModels.h"
#include "Vehicle.h"

namespace atg {

struct RailInfo;

// a station on the line (map.landmarks.stations: the platform's edge, and its distance along the line)
struct RailStop { std::string key, name; double s = 0, x = 0, z = 0, y = 0, rot = 0; };
std::vector<RailStop> RailStops(const CityMap& map);

struct TrainOpts : SpawnOpts {
	double s = NaN(), dwell = NaN(), dirS = NaN();
	int track = -1, wagons = -1;
	double seed = 0;
};

class Train : public Vehicle {
public:
	Train(Game& game, const std::string& type, double x, double z, double yaw, const TrainOpts& opts);
	void setup(const SpawnOpts& opts) override;

	const RailInfo* rail = nullptr;
	bool freight = false;
	int track = 0;              // 1: takes the passing loop at Fern Creek
	double cruise = 24;
	std::vector<std::string> stopKeys; // (freight: only the ends of the line)
	double len = 0;
	double s = 0;               // arc length of the locomotive's nose
	double v = 0;               // speed along +s
	double dirS = -1;           // which way the autopilot is heading
	double dwell = 12;
	double limitLo = -kInf, limitHi = kInf; // signals: how far the nose / tail may go (set by the rail system)
	bool hold = false;
	double pitch = 0;
	double hornT = 0;
	bool headOn = false, tailOn = false; // (the renderer swaps the lamp materials)
	int atStation = -1, lastStation = -1;  // (indices into stops)
	std::vector<RailStop> stops;

	struct Car { std::string type; double len; bool coach; double seed; V3 pos; double yaw = 0, pitch = 0; const TrainModel* model = nullptr; };
	std::vector<Car> cars;
	std::vector<std::shared_ptr<Vehicle>> proxies; // (stand-ins so the contact code treats each carriage like a car)
	const TrainModel* locoModel = nullptr;

	void damage(double, Character* = nullptr) override {}
	void dent(double, double, double, double) override {}
	void explode() override {}
	bool armed() const override { return false; }
	double forwardSpeed() const override { return std::fabs(v); }
	double centreS() const { return s - len / 2; }
	M4 groupMatrix() const override;
	M4 bodyMatrix() const override;

	void playerControl(const class Input& in, double dt) override;
	struct Door { double x, z; int seat; };
	std::vector<Door> doorList() const;
	bool hasDoors() const override { return true; }
	V3 nearestDoor(const V3& p, int& seat) const override;
	bool doorFor(int seat, Character* c, V3& out) const override;
	void putIn(Character* c, int seat = 0) override;
	void takeOut(Character* c, const V3* at = nullptr) override;
	void update(double dt) override;
	void place();

private:
	mutable bool hasDoor = false;
	mutable Door door{};
	double autopilot(double dt);
	bool reversing(const std::vector<int>& sts, double c) const;
	void contacts(double dt);
};

} // namespace atg
