#include "Input.h"

namespace atg {

namespace {
const std::map<std::string, std::vector<std::string>>& BINDINGS() {
	static const std::map<std::string, std::vector<std::string>> B = {
		{ "forward", { "KeyW", "ArrowUp" } }, { "back", { "KeyS", "ArrowDown" } }, { "left", { "KeyA", "ArrowLeft" } }, { "right", { "KeyD", "ArrowRight" } },
		{ "sprint", { "ShiftLeft", "ShiftRight" } }, { "jump", { "Space" } }, { "handbrake", { "Space" } }, { "enter", { "KeyF", "Enter" } },
		{ "reload", { "KeyR" } }, { "nextWeapon", { "KeyE" } }, { "prevWeapon", { "KeyQ" } }, { "crouch", { "KeyC", "ControlLeft" } },
		{ "horn", { "KeyH" } }, { "radio", { "KeyN" } }, { "camera", { "KeyV" } }, { "map", { "KeyM" } }, { "pause", { "Escape", "KeyP" } },
		{ "skip", { "Space", "Enter" } }, { "lookBehind", { "KeyB" } }, { "hydraulics", { "KeyG" } }, { "shop", { "KeyY" } }, { "teleport", { "KeyT" } },
		{ "hail", { "KeyH" } }, { "passenger", { "KeyG" } }, { "taxiJob", { "KeyJ" } }, { "skipTrip", { "Space" } }, { "chat", { "Slash" } },
		{ "console", { "Backquote" } }, { "pet", { "KeyK" } }, { "nitro", { "ShiftLeft", "ShiftRight" } }, { "cinematic", { "KeyX" } },
	};
	return B;
}
// action -> [button on foot, button in a vehicle] (-1: none)
const std::map<std::string, std::pair<int, int>>& GP_BIND() {
	static const std::map<std::string, std::pair<int, int>> B = {
		{ "sprint", { GP::A, -1 } }, { "jump", { GP::X, -1 } }, { "reload", { GP::B, -1 } }, { "enter", { GP::Y, GP::Y } },
		{ "prevWeapon", { -1, -1 } }, { "nextWeapon", { GP::RB, -1 } }, { "crouch", { GP::LS, -1 } }, { "handbrake", { -1, GP::RB } },
		{ "horn", { -1, GP::LS } }, { "lookBehind", { -1, GP::RS } }, { "camera", { -1, GP::VIEW } }, { "radio", { -1, GP::RIGHT } },
		{ "hydraulics", { -1, GP::UP } }, { "taxiJob", { -1, GP::LEFT } }, { "nitro", { -1, GP::A } }, { "teleport", { -1, -1 } },
		{ "hail", { GP::RIGHT, -1 } }, { "passenger", { GP::LEFT, -1 } }, { "map", { GP::DOWN, GP::DOWN } }, { "pause", { GP::MENU, GP::MENU } },
		{ "skip", { GP::A, GP::A } }, { "skipTrip", { -1, GP::A } }, { "pet", { GP::RS, -1 } }, { "cinematic", { -1, GP::B } },
	};
	return B;
}
}

const std::vector<std::string>& Input::Bindings(const std::string& action) {
	static const std::vector<std::string> none;
	auto it = BINDINGS().find(action);
	return it == BINDINGS().end() ? none : it->second;
}

void Input::SetPad(bool connected, double lx, double ly, double rx, double ry, double lt, double rt, const bool buttons[GP::COUNT]) {
	Pad& g = gp;
	for (int i = 0; i < GP::COUNT; i++) g.prev[i] = g.buttons[i];
	g.ltPrev = g.lt; g.rtPrev = g.rt;
	g.connected = connected;
	if (!connected) { g.lx = g.ly = g.rx = g.ry = g.lt = g.rt = 0; for (bool& b : g.buttons) b = false; return; }
	auto stick = [](double x, double y, double& ox, double& oy) {
		const double m = std::hypot(x, y);
		if (m < 0.16) { ox = oy = 0; return; }
		const double s = (std::min)(1.0, (m - 0.16) / 0.84) / m;
		ox = x * s; oy = y * s;
	};
	stick(lx, ly, g.lx, g.ly);
	stick(rx, ry, g.rx, g.ry);
	g.lt = lt < 0.06 ? 0 : (std::min)(1.0, lt);
	g.rt = rt < 0.06 ? 0 : (std::min)(1.0, rt);
	bool any = false;
	for (int i = 0; i < GP::COUNT; i++) { g.buttons[i] = buttons[i]; any = any || buttons[i]; }
	g.buttons[GP::LT] = g.buttons[GP::LT] || g.lt > 0.4;
	g.buttons[GP::RT] = g.buttons[GP::RT] || g.rt > 0.4;
	if (any || std::fabs(g.lx) + std::fabs(g.ly) + std::fabs(g.rx) + std::fabs(g.ry) > 0.2 || g.lt + g.rt > 0.2) lastDevice = "gamepad";
}

bool Input::down(const std::string& action) const {
	for (const std::string& c : Bindings(action)) if (keys.count(c)) return true;
	return gpDownAction(action);
}
bool Input::hit(const std::string& action) const {
	for (const std::string& c : Bindings(action)) if (pressed.count(c)) return true;
	return gpHitAction(action);
}

double Input::moveX() const { double v = (down("right") ? 1 : 0) - (down("left") ? 1 : 0); if (std::fabs(gp.lx) > std::fabs(v)) v = gp.lx; return v; }
double Input::moveY() const { double v = (down("forward") ? 1 : 0) - (down("back") ? 1 : 0); if (std::fabs(gp.ly) > std::fabs(v)) v = -gp.ly; return v; }
double Input::throttle() const { return (std::max)(down("forward") ? 1.0 : 0.0, gp.rt); }
double Input::brake() const { return (std::max)(down("back") ? 1.0 : 0.0, gp.lt); }
double Input::steer() const { double v = (down("left") ? 1 : 0) - (down("right") ? 1 : 0); if (std::fabs(gp.lx) > 0.05) v = -gp.lx; return v; }

void Input::lookDelta(double& dx, double& dy) const {
	const double s = 0.0022 * sensitivity;
	dx = mouse.dx * s; dy = mouse.dy * s;
	const Pad& g = gp;
	const double dt = (std::min)(frameDt, 0.1);
	if (g.rx != 0 || g.ry != 0) {
		const double m = std::hypot(g.rx, g.ry);
		const double k = std::pow((std::min)(1.0, m), 1.6) / (m > 0 ? m : 1);
		const double rate = (inVehicle ? 2.6 : aimDown() ? 1.7 : 3.3) * sensitivity;
		dx += g.rx * k * rate * dt; dy += g.ry * k * rate * 0.8 * dt;
	}
	if (invertY) dy = -dy;
}

int Input::gpMap(const std::string& action) const {
	auto it = GP_BIND().find(action);
	if (it == GP_BIND().end()) return -1;
	return inVehicle ? it->second.second : it->second.first;
}
bool Input::gpDownAction(const std::string& action) const {
	const int i = gpMap(action);
	if (action == "handbrake" && gp.buttons[GP::LB]) return false; // LB + RB is a drive-by
	return i >= 0 && gp.buttons[i] && !mask.count(i);
}

void Input::endFrame() {
	mask.clear();
	pressed.clear();
	released.clear();
	mouse.dx = mouse.dy = 0;
	mouse.leftPressed = mouse.rightPressed = false;
	mouse.wheel = 0;
}

} // namespace atg
