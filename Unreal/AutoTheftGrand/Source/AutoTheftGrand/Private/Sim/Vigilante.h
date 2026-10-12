// Vigilante (port of src/game/vigilante.js): sitting in the driver's seat of a police car, press J (the
// taxi-job button) to go on patrol. Dispatch sends you after a car full of suspects; they run, and from level
// 3 they shoot back. Take them all down before the clock runs out for a cash reward that grows with each
// level; the next call comes in straight after. Killing suspects doesn't count against you, and neither does
// the shooting.
#pragma once

#include "Systems.h"

namespace atg {

class Game;
class Ped;
class Vehicle;
struct Blip;

class Vigilante : public System {
public:
	explicit Vigilante(Game& g);
	Game& game;
	bool active = false, hinted = false;
	int level = 0, total = 0;
	double clock = 0, between = 0, earned = 0, outT = 0;
	RNG rng{ 99 };
	struct Suspect { Ref<Ped> ped; std::shared_ptr<Blip> blip; double fireT = NaN(); };
	std::vector<Suspect> suspects;
	Ref<Vehicle> car;

	void update(double dt) override;
	void start();
	void stop(const char* msg, bool failed = false);

private:
	bool ctl() const;
	void dispatch();
	void cleared();
};

} // namespace atg
