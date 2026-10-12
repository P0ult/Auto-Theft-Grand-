// Street races (port of src/game/races.js): a chequered flag on the map marks each start. Roll up in a car,
// pay the entry and race three locals through the checkpoints. First place takes the pot, second gets
// double the entry back, third gets the entry back. Best times are kept in the stats.
#pragma once

#include "Missions.h"

namespace atg {

class Game;
class Marker;
struct Blip;

struct RaceDef {
	std::string id, name;
	int entry = 0, prize = 0;
	std::vector<std::string> cars;
	std::vector<V2> pts; // the waypoints the route is threaded through (the road network fills in between)
};

struct Race {
	RaceDef def;
	std::vector<V3> dense;   // the line for the AI, a point every ~18 m
	std::vector<V3> cps;     // checkpoints every ~140 m
	double len = 0;
	double lanes[2] = { 1.8, 1.8 };
	double sx = 0, sz = 0, syaw = 0;
	Marker* marker = nullptr;
};

class RaceSystem : public System {
public:
	explicit RaceSystem(Game& g);
	Game& game;
	std::vector<Race> races;
	RNG rng{ 4711 };

	struct Rival { Ref<Vehicle> car; Ref<Ped> driver; std::unique_ptr<RaceDriver> ai; std::shared_ptr<Blip> blip; bool done = false; double t = 0; };
	struct Active {
		Race* race = nullptr;
		Ref<Vehicle> car;
		std::vector<std::unique_ptr<Rival>> rivals;
		int idx = 0, count = 3, pi = 0;
		double t = -3.5, out = 0;
		bool go = false;
		Marker* marker = nullptr;
		std::vector<Rival*> finished;
	};
	std::unique_ptr<Active> active;
	bool isActive() const { return active != nullptr; }

	void offer(int index);
	void start(Race& race);
	void update(double dt) override;

private:
	void plan(RaceDef def);
	void marker(Active& a);
	void finish();
	void end(const std::string& title, const std::string& kind, const std::string& sub);
};

std::string RaceTime(double t); // races.js fmt: m:ss.ss

} // namespace atg
