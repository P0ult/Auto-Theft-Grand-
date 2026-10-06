// The special ability (port of src/game/special.js): a meter under the minimap that fills by driving fast,
// drifting, flying off jumps and taking people down (headshots most). Caps Lock or Z (or both sticks clicked)
// slows the world for a few seconds: bullet time on foot, a slowed world with extra grip at the wheel. The screen
// loses some colour while it's on; the meter drains in real time, and pressing again stops it early.
#pragma once

#include "Systems.h"

namespace atg {

class Game;

class Special : public System {
public:
	explicit Special(Game& game);
	Game& game;
	double meter = 1;
	bool active = false;
	double k = 0;          // the screen effect, eased in and out
	double lastBlocked = 0;
	double chroma = 0.0022; // (postfx.js uChroma: the colour fringing it adds to)

	void add(double x);
	bool pressed() const;
	void start();
	void stop();
	// (uses the real frame time: the meter mustn't drain slower because the world is slowed)
	void update(double dt) override;
	// extra tyre grip for the player's car while it's on (vehicle.js)
	double grip() const;
};

} // namespace atg
