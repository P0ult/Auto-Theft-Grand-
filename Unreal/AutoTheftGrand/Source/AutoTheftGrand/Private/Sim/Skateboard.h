// Skateboards (port of the Skateboard class in src/entities/skateboard.js): a tiny, light bike on the car tyre
// model. The rider stands side-on on the deck, pushes with the back foot to get going, carves by leaning,
// ollies with Space and flips the board in the air (A / D: kickflip / heelflip, S: shove-it). Land it clean
// for a cash bonus; land it crooked and you bail. Only smooth ground rolls. The model is Gen/BoardModel.
#pragma once

#include "Bike.h"
#include "BoardModel.h"

namespace atg {

class Skateboard : public Bike {
public:
	Skateboard(Game& game, const std::string& type, double x, double z, double yaw, const SpawnOpts& opts);
	const BoardModel* board = nullptr;
	double pushPhase = 0, pushK = 0, flip = 0, flipV = 0, shove = 0, shoveV = 0, popT = 0, airT = 0;
	std::string trickName;
	bool wantOllie = false;

	bool doorFor(int seat, Character* c, V3& out) const override;
	V3 nearestDoor(const V3& p, int& seat) const override;
	bool feetFor(int seat, V3 out[2]) const override;
	bool gripsFor(V3[2]) const override { return false; }
	void playerControl(const Input& in, double dt) override;
	bool ollie();
	void update(double dt) override;
	M4 groupMatrix() const override;
	// the deck on the body: the tail pops and the board flips and spins under the rider
	M4 deckMatrix() const;
	void onCrash(double impact, CollObj*) override { if (impact > 3.8) throwRiders(impact); }
	void onCrashVehicle(double impact, Vehicle*) override { if (impact > 3.8) throwRiders(impact); }

private:
	int flipDone = 0;
	bool shoveDone = false, paved = true;
	double pavedT = 0;
	bool isPaved() const;
	void land();
};

} // namespace atg
