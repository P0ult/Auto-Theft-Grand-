// Keeps the Sol Line in service (port of src/game/railsystem.js): a passenger train and a freight train
// shuttling between Dry Wells and Union Station. The line is single track except for the passing loop at
// Fern Creek, so a train may only run onto the single-track stretch either side of it (west: to Dry Wells,
// east: to Union Station) once the other train is off it. They cross at Fern Creek, the passenger train at
// the platform and the freight on the loop. Also: level crossings that close while a train is near
// (traffic waits at them), station and train blips, and helpers for boarding hints and missions.
#pragma once

#include "Systems.h"
#include "Train.h"

namespace atg {

class Game;
struct Blip;

class RailSystem : public System {
public:
	explicit RailSystem(Game& game);
	Game& game;
	const RailInfo* rail = nullptr;
	Ref<Vehicle> train;   // the passenger train (missions and the taxi / teleport code use this one)
	Ref<Vehicle> freight;
	struct Crossing { double s, x, z; bool active = false; };
	std::vector<Crossing> crossings;
	double hintT = 0;
	Ref<Vehicle> held;    // a mission has the line: other trains wait in the loop
	std::vector<RailStop> stations;

	std::vector<Train*> trains() const;
	int station(const std::string& key) const;
	Train* spawnTrain();
	Train* spawnFreight();
	void clearLineFor(Train* t);
	void releaseLine();
	void update(double dt) override;
	// distance along a heading to an active level crossing (for AI drivers), or infinity
	double crossingAhead(double x, double z, double fx, double fz, double maxD = 40) const;
	const RailStop* nearestStation(double x, double z) const;

private:
	Ref<Vehicle> tokenW, tokenE;
	std::vector<std::pair<Ref<Vehicle>, std::shared_ptr<Blip>>> blips;
	void dispatch(const std::vector<Train*>& trains);
};

} // namespace atg
