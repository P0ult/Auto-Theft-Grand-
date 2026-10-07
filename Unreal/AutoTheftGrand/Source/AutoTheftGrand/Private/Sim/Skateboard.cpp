#include "Skateboard.h"
#include "Character.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "Vehicles.h"

namespace atg {

// vehicles.spawn of a skateboard
static const bool GBoardFactory = [] {
	VehicleManager::Factories().push_back({ [](const VehicleDef& d) { return d.board; }, [](Game& g, const std::string& id, double x, double z, double yaw, const SpawnOpts& o) -> std::shared_ptr<Vehicle> {
		return std::make_shared<Skateboard>(g, id, x, z, yaw, o);
	} });
	return true;
}();

Skateboard::Skateboard(Game& g, const std::string& type, double x, double z, double yw, const SpawnOpts& opts) : Bike(g, type, x, z, yw, opts) {
	board = &BuildBoardModel(def);
	// the rider stands side-on, left foot forward, facing the board's left
	layout.seats = { V3(0, BOARD_DECK_Y, 0) };
	layout.stand = true; layout.standYaw = -kPi / 2;
	layout.seatHip = 0;
	layout.doorPos = V3(0.7, 0, 0);
}

bool Skateboard::doorFor(int, Character* c, V3& out) const {
	double side = 1;
	if (c) { double lx, lz; worldToLocal(c->pos.x, c->pos.z, lx, lz); side = lx >= 0 ? 1 : -1; }
	out = localToWorld(0.45 * side, 0, 0);
	return true;
}

V3 Skateboard::nearestDoor(const V3& p, int& seat) const {
	seat = 0;
	return V3(pos.x, p.y, pos.z);
}

// feet on the deck (the rider's frame: +x is towards the nose), the back foot pushing off the ground
bool Skateboard::feetFor(int, V3 out[2]) const {
	const double deckY = BOARD_DECK_Y + 0.02;
	const double p = pushK, ph = std::fmod(pushPhase, 1.0);
	// the push stroke: plant beside the board, sweep back, lift and swing forward again
	const double sweep = ph < 0.55 ? ph / 0.55 : 1 - (ph - 0.55) / 0.45;
	const double lift = ph < 0.55 ? 0 : std::sin((ph - 0.55) / 0.45 * kPi) * 0.12;
	const double bx = -0.22 + p * (0.12 - 0.5 * sweep), by = deckY + p * (-0.1 + lift), bz = 0.02 + p * 0.16;
	out[0] = V3(0.24, deckY - crouch * 0.1, 0.02);
	out[1] = V3(bx, by, bz);
	return true;
}

void Skateboard::playerControl(const Input& in, double dt) {
	Bike::playerControl(in, dt);
	input.handbrake = false;
	wantOllie = in.hit("handbrake");
}

bool Skateboard::ollie() {
	if (airborne || !driver() || popT > 0) return false;
	airborne = true;
	vy = 4.3 + Min(1.2, speedAbs() * 0.08);
	popT = 0.25;
	flip = 0; flipV = 0; shove = 0; shoveV = 0; trickName.clear(); airT = 0;
	game.soundAt("clink", pos, 0.9);
	game.soundAt("footstep", pos, 1.5);
	return true;
}

void Skateboard::update(double dt) {
	if (wantOllie) { wantOllie = false; ollie(); }
	// NPC riders (and the player) push along with the back foot when below cruising speed
	const bool pushing = driver() && !airborne && !tb && input.throttle > 0.1 && speed() > -0.5 && speedAbs() < def.top * 0.85;
	pushK = Damp(pushK, pushing ? 1 : 0, 8, dt);
	if (pushing || pushK > 0.05) pushPhase += dt / 0.85;
	else pushPhase = 0;
	// no push, no go: coast between strokes (the tyre model sees the throttle only on the push)
	const double thr = input.throttle;
	if (pushing) { const double ph = std::fmod(pushPhase, 1.0); input.throttle = ph < 0.55 ? thr * 1.6 : 0; }
	const bool wasAir = airborne;
	Bike::update(dt);
	input.throttle = thr;
	if (removed) return;
	popT -= dt;
	// tricks in the air
	if (airborne && !tb) {
		airT += dt;
		const double st = input.steer;
		if (std::fabs(st) > 0.5 && flipV == 0 && airT < 0.35) { flipV = -Sgn(st) * 15; trickName = st > 0 ? "KICKFLIP" : "HEELFLIP"; }
		if (input.brake > 0.5 && shoveV == 0 && airT < 0.35) {
			shoveV = 13;
			const size_t at = trickName.find("FLIP");
			if (trickName.empty()) trickName = "SHOVE-IT";
			else if (at != std::string::npos) trickName.replace(at, 4, "FLIP SHOVE-IT");
		}
		flip += flipV * dt; shove += shoveV * dt;
		// the flip slows to catch it on a whole turn
		if (flipV != 0 && std::fabs(flip) > kTau * 0.8) { flipV *= 0.9; if (std::fabs(flip) >= kTau - 0.12) { flip = 0; flipV = 0; flipDone++; } }
		if (shoveV != 0 && shove > kPi * 0.8) { shoveV *= 0.9; if (shove >= kPi - 0.1) { shove = 0; shoveV = 0; shoveDone = true; } }
	}
	if (wasAir && !airborne && !tb) land();
	skid = 0; // (no tyre smoke or skid marks from urethane wheels)
	// only smooth ground rolls: on sand, grass or dirt a board digs in and stops
	pavedT -= dt;
	if (pavedT <= 0) { pavedT = 0.25; paved = isPaved(); }
	surface = 1;
	if (!paved && !airborne && !tb) { const double k = Max(0, 1 - 3.5 * dt); vel.x *= k; vel.z *= k; }
	crouch = Damp(crouch, airborne ? 0.3 : popT > 0 ? 0.2 : pushK * 0.08 + 0.05, 14, dt);
}

bool Skateboard::isPaved() const {
	const CityMap& map = game.map;
	const double x = pos.x, z = pos.z;
	if (map.IsOnRoad(x, z) || map.InCity(x, z)) return true;
	if (pos.y > map.GroundHeight(x, z) + 0.2) return true; // on a deck, a ramp or a bridge
	for (const PadSurface& p : map.padSurfaces) {
		const double s = std::sin(p.yaw), c = std::cos(p.yaw), dx = x - p.cx, dz = z - p.cz;
		if (std::fabs(dx * c - dz * s) < p.hx && std::fabs(dx * s + dz * c) < p.hz) return true;
	}
	return false;
}

void Skateboard::land() {
	const bool clean = std::fabs(flip) < 0.5 && std::fabs(shove) < 0.45;
	const bool did = !trickName.empty() && (flipDone || shoveDone);
	Player* pl = driver() && driver()->isPlayer ? dynamic_cast<Player*>(driver()) : nullptr;
	if (!clean && driver()) { throwRiders(6); flip = 0; shove = 0; }
	else if (did && pl) {
		const double cash = 25 + std::round(airT * 40) + (flipDone && shoveDone ? 40 : 0);
		pl->money += cash;
		if (game.hud) game.hud->bigMessage(trickName, "hint", 1.4, "+$" + std::to_string((int)cash));
		if (game.audio) game.audio->play("cash", 0.4);
		game.stats.tricks++;
		game.events.skateTrick.emit(trickName, cash, this);
	}
	game.soundAt("clink", pos, 1);
	flip = 0; shove = 0; flipV = 0; shoveV = 0; trickName.clear(); flipDone = 0; shoveDone = false;
	popT = 0.15;
}

M4 Skateboard::deckMatrix() const {
	const double pop = popT > 0 ? std::sin(Clamp(popT / 0.25, 0, 1) * kPi) * 0.35 : 0;
	return M4::Compose(V3(0, board->deckY + (airborne ? 0.04 : 0), 0), Quat::FromEuler(airborne ? -pop : 0, shove, flip));
}

// the board tilts with the carve (the trucks) more than a bike leans
M4 Skateboard::groupMatrix() const {
	if (tb || hasNetQ) return Vehicle::groupMatrix();
	return M4::Compose(pos, Quat::FromEuler(-groundPitch, yaw, groundRoll - lean * 0.35, "YXZ"));
}

} // namespace atg
