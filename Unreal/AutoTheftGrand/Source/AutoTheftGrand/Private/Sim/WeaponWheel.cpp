#include "WeaponWheel.h"
#include "Game.h"
#include "Gameplay.h"
#include "Weapons.h"

namespace atg {

namespace {
const double SLOW = 0.18;     // game speed while the wheel is open
const double HOLD = 0.22;     // seconds LB has to be held to open the wheel
}

WeaponWheel::WeaponWheel(Game& g) : game(g) {}

std::vector<std::string> WeaponWheel::owned() const {
	const Player& p = *game.player;
	std::vector<std::string> out;
	for (const std::string& id : WeaponOrder()) {
		auto it = p.weapons.find(id);
		if (p.vehicle) { if (p.carWeaponOk(id) && it != p.weapons.end() && it->second.clip + it->second.ammo > 0) out.push_back(id); }
		else if (it != p.weapons.end() && (FindWeapon(id)->type == "melee" || it->second.clip + it->second.ammo > 0)) out.push_back(id);
	}
	return out;
}

bool WeaponWheel::allowed() const {
	const Player& p = *game.player;
	return game.gameplay && game.gameplay->state == "playing" && !game.cutscene && !game.menuOpen && !p.dead && !p.ragdolling && game.input.enabled && !game.phoneOpen;
}

void WeaponWheel::preUpdate(double) {
	Input& input = game.input;
	Player& p = *game.player;
	const bool padLB = !p.vehicle && input.gpDown(GP::LB);
	if (padLB) padT += input.frameDt;
	else if (!open) {
		if (padT > 0 && padT < HOLD && allowed()) p.cycleWeapon(-1); // (a tap)
		padT = 0;
	}
	const bool want = allowed() && (input.key("Tab") || (padLB && padT >= HOLD));
	if (want && !open) show();
	else if (!want && open) hide(true);
	if (!open) return;
	// the cursor: mouse movement or the right stick
	cx += input.mouse.dx; cy += input.mouse.dy;
	const double L = Hypot(cx, cy);
	if (L > 90) { cx *= 90 / L; cy *= 90 / L; }
	if (Hypot(input.gp.rx, input.gp.ry) > 0.5) { cx = input.gp.rx * 90; cy = input.gp.ry * 90; }
	input.mouse.dx = 0; input.mouse.dy = 0; input.gp.rx = 0; input.gp.ry = 0;
	if (Hypot(cx, cy) > 28 && !list.empty()) {
		const int n = (int)list.size();
		const double a = std::atan2(cy, cx) + kPi / 2; // 0 at the top, clockwise
		sel = (((int)std::floor(a / (kPi * 2 / n) + 0.5) % n) + n) % n; // (Math.round)
	}
}

void WeaponWheel::show() {
	Player& p = *game.player;
	list = owned();
	if (list.empty()) return;
	open = true;
	int i = -1;
	for (int k = 0; k < (int)list.size(); k++) if (list[k] == p.weapon) { i = k; break; }
	sel = Max(0, i);
	cx = 0; cy = 0;
	game.slowmo["wheel"] = SLOW;
	game.sound("ui", 0.8);
}

void WeaponWheel::hide(bool apply) {
	Player& p = *game.player;
	open = false;
	game.slowmo.erase("wheel");
	padT = 0;
	if (apply && sel >= 0 && sel < (int)list.size() && list[sel] != p.weapon) p.switchTo(list[sel]);
}

} // namespace atg
