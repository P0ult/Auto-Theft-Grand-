// Keyboard, mouse and gamepad state with the browser game's action bindings (src/core/input.js). Keys are
// named by their KeyboardEvent codes ("KeyW", "Space", "ShiftLeft", "Digit1", ...); the Unreal side (or a
// test) feeds key presses, mouse movement and the pad, then the game asks by action name.
#pragma once

#include "Core.h"

namespace atg {

// pad buttons in the W3C "standard" layout, named the Xbox way
namespace GP { enum : int { A = 0, B = 1, X = 2, Y = 3, LB = 4, RB = 5, LT = 6, RT = 7, VIEW = 8, MENU = 9, LS = 10, RS = 11, UP = 12, DOWN = 13, LEFT = 14, RIGHT = 15, HOME = 16, COUNT = 17 }; }

class Input {
public:
	std::set<std::string> keys, pressed, released;
	struct Mouse { double dx = 0, dy = 0; bool left = false, right = false, leftPressed = false, rightPressed = false; double wheel = 0; } mouse;
	struct Pad {
		double lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0, ltPrev = 0, rtPrev = 0;
		bool buttons[GP::COUNT] = {}, prev[GP::COUNT] = {};
		bool connected = false;
		std::string family = "xbox";
	} gp;
	bool enabled = true;
	double sensitivity = 1;
	bool invertY = false;
	bool inVehicle = false;     // picks the on-foot or in-vehicle pad bindings
	double frameDt = 1.0 / 60;
	std::string lastDevice = "kbm";
	std::set<int> mask;         // pad buttons something else (the phone) has taken this frame

	// ---- feeding (from the platform)
	void KeyDown(const std::string& code) { if (!keys.count(code)) { keys.insert(code); pressed.insert(code); lastDevice = "kbm"; } }
	void KeyUp(const std::string& code) { keys.erase(code); released.insert(code); }
	void ClearKeys() { keys.clear(); mouse.left = mouse.right = false; }
	// a new pad reading: sticks after the radial dead zone, triggers 0..1, buttons in GP order
	void SetPad(bool connected, double lx, double ly, double rx, double ry, double lt, double rt, const bool buttons[GP::COUNT]);

	// ---- queries (input.js)
	bool down(const std::string& action) const;
	bool hit(const std::string& action) const;
	bool key(const std::string& code) const { return keys.count(code) > 0; }
	bool keyHit(const std::string& code) const { return pressed.count(code) > 0; }
	double moveX() const;
	double moveY() const;
	double throttle() const;
	double brake() const;
	double steer() const;
	void lookDelta(double& dx, double& dy) const;
	bool fireDown() const { return mouse.left || (!inVehicle && gp.rt > 0.45); }
	bool firePressed() const { return mouse.leftPressed || (!inVehicle && gp.rt > 0.45 && gp.rtPrev <= 0.45); }
	bool aimDown() const { return mouse.right || (inVehicle ? gp.buttons[GP::LB] : gp.lt > 0.4); }
	bool attackPressed() const { return firePressed(); }
	bool driveByFire() const { return mouse.left || (gp.buttons[GP::LB] && gp.buttons[GP::RB]); }
	bool vehFire() const { return mouse.left || gp.buttons[GP::RB]; }
	bool vehAltPressed() const { return mouse.rightPressed || gpPressedIdx(GP::LB); }
	bool gpDown(int i) const { return gp.buttons[i] && !mask.count(i); }
	bool gpHit(int i) const { return gpPressedIdx(i); }

	void endFrame();

	// the keyboard codes bound to an action (for the controls screen)
	static const std::vector<std::string>& Bindings(const std::string& action);

private:
	int gpMap(const std::string& action) const;
	bool gpDownAction(const std::string& action) const;
	bool gpHitAction(const std::string& action) const { const int i = gpMap(action); return i >= 0 && gpPressedIdx(i); }
	bool gpPressedIdx(int i) const { return gp.buttons[i] && !gp.prev[i] && !mask.count(i); }
};

} // namespace atg
