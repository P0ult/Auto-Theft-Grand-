// Traffic (port of src/game/traffic.js): lane-following drivers on the road network (the city grid, the
// freeway and its ramps, highways, town roads, roundabouts). They obey the grid's traffic lights, yield at
// junctions, roundabouts and merges, slow for curves, follow the car ahead, brake for people, honk, switch
// lanes and pass broken-down cars. The LaneDriver is also what the police and mission drivers build on.
#pragma once

#include "Systems.h"
#include "Vehicle.h"

namespace atg {

class Game;
class RoadNet;
struct REdge;
struct RNode;

// the traffic lights' cycle (shaders.js signalState): axis 0 = north-south, 1 = east-west
enum class ESignal : uint8_t { Green, Yellow, Red };
ESignal SignalState(double t, int axis);

struct LanePath {
	bool turn = false;
	std::vector<V3> pts;
	std::vector<double> cum;
	double len = 0;
	std::vector<float> vmax;  // curve speed limit per point
	int e = -1, dir = 0, lane = 0, node = -1;
	double speed = 14;
	std::shared_ptr<LanePath> from, to; // (turns)
};
struct LaneStart { int e = -1, dir = 0, lane = 0; double s = 0; };
bool NearestLane(const RoadNet& net, double x, double z, double yaw, bool hasYaw, LaneStart& out, const std::function<bool(const REdge&)>& filter = nullptr, double y = NaN());

class LaneDriver : public VehicleAI {
public:
	LaneDriver(Game& game, Vehicle* veh, const LaneStart* start, bool noSnap = false);
	Game& game;
	Vehicle* veh;
	const RoadNet& net;
	double cruiseFactor, cruise = 14;
	bool fixedCruise = false;
	std::vector<std::shared_ptr<LanePath>> paths;
	double s = 0, blockedTime = 0, honkTimer = 0, panic = 0;
	bool ignoreLights = false;
	double stuck = 0, impatient = 0, wait = 0, jam = 0;
	bool queued = false, waitingLight = false;
	struct Bypass { Ref<Vehicle> v; double side; double t; };
	bool hasBypass = false; Bypass bypass;
	double bypassT = 0, obsD = 99, offLevel = 0, lat = 0;
	Vehicle* obsObj = nullptr;

	void update(double dt) override;
	void resnap();
	void start(const LaneStart& st);
	std::shared_ptr<LanePath> lanePath(int e, int dir, int lane);
	V3 pointAhead(double ahead);
	double obstacleAhead(double maxD);

private:
	struct Exit { int e, dir; };
	std::vector<Exit> options(const LanePath& cur);
	double heading(int e, int dir, bool atStart) const;
	bool chooseNext(const LanePath& cur, Exit& out);
	int laneFor(const LanePath& cur, const Exit& o);
	std::shared_ptr<LanePath> turnPath(const std::shared_ptr<LanePath>& from, const std::shared_ptr<LanePath>& to, int node);
	void ensurePaths();
	double project(const LanePath& path, double x, double z, double s0, double s1);
	double curveLimit(double dist);
	double junctionControl(const LanePath& cur, double remain, double speed);
	bool exitBlocked(const LanePath* lane);
	struct Line { std::vector<double> x, y, z, s; int n = 0; } line;
	void aheadLine(double maxD);
};

class Traffic : public System {
public:
	explicit Traffic(Game& game);
	Game& game;
	std::vector<Ref<Vehicle>> cars;
	int maxCars;
	double spawnTimer = 0;
	bool ignoreView = false;
	std::string pickType(const std::string& district);
	Vehicle* spawnCar(const LaneStart& start, double s0, const std::string& type = "");
	void populate(int count = -1);
	struct Sample { LaneStart start; double s0, x, y, z; };
	static bool SampleLane(Game& game, double cx, double cz, double rMin, double rMax, Sample& out, const std::function<bool(const REdge&)>& filter = nullptr);
	void trySpawn(double minDist = 70);
	double density() const;
	void update(double dt) override;
	void despawn(Vehicle* v);
};

LanePath MakePath(std::vector<V3> pts);
V3 SampleOn(const LanePath& path, double s);

} // namespace atg
