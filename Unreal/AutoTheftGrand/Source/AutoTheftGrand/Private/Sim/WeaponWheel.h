// GTA V's weapon wheel (port of src/ui/weaponwheel.js): hold Tab (or LB on a pad, on foot) and the game slows right
// down while a ring of every weapon you carry fans out; point at one with the mouse or the right stick and let go
// to draw it. A quick tap of LB still flicks back to the previous weapon. In a car the wheel offers what you can
// shoot from a seat. The state is here; ATGHUD draws the ring.
#pragma once

#include "Systems.h"

namespace atg {

class Game;

class WeaponWheel : public System {
public:
	explicit WeaponWheel(Game& game);
	Game& game;
	bool open = false;
	int sel = -1;
	double cx = 0, cy = 0;          // the virtual cursor
	double padT = 0;
	std::vector<std::string> list;  // the weapons on the ring, clockwise from the top

	bool allowed() const;
	// runs before the camera reads the mouse, so the look stops while you're choosing
	void preUpdate(double dt) override;

private:
	std::vector<std::string> owned() const;
	void show();
	void hide(bool apply);
};

} // namespace atg
