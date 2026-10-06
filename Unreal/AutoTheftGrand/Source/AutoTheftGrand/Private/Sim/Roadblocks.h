// Police roadblocks (port of src/game/roadblocks.js): from three stars, while you drive, the police close the
// road a little way ahead of you with a line of cruisers, lights flashing, officers in cover behind them. Four
// stars brings the SWAT Enforcer and a stinger, a spike strip across the lanes in front of the block that
// bursts your tyres. Out in the country the Sheriff's SUVs set them up. They're cleared away once you're past.
#pragma once

#include "Systems.h"

namespace atg {

class Game;
class Ped;
struct Blip;

class Roadblocks : public System {
public:
	explicit Roadblocks(Game& game);
	Game& game;
	struct Spike { double x, z, y, half, rotY, len; }; // (the strip: centre, height, half length, turn, length)
	struct Block {
		double x, z, y, tx, tz, ux, uz, wU, wD;
		std::vector<Ref<Vehicle>> cars;
		std::vector<Ref<Ped>> cops;
		double t = 0;
		bool hasSpike = false; Spike spike{};
	};
	std::vector<Block> blocks;
	double timer = 6;
	bool enabled = true;

	void blipList(std::vector<Blip>& out) const; // (the radar: the cars, flashing red / blue squares)
	void update(double dt) override;
	void reset() override;
	bool place(Vehicle* v); // (find a stretch of road ahead of the car and close it)

private:
	void stinger(Block& b);
	void remove(int i);
};

} // namespace atg
