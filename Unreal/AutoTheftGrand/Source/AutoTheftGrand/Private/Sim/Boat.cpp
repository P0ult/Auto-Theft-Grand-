#include "Boat.h"
#include "Character.h"
#include "Collision.h"
#include "Combat.h"
#include "Game.h"
#include "Input.h"
#include "RoadLayout.h"
#include "Vehicles.h"

namespace atg {

namespace {
// vehicles.spawn of a boat
const bool GBoatFactory = [] {
	VehicleManager::Factories().push_back({ [](const VehicleDef& d) { return d.kind == "boat"; }, [](Game& g, const std::string& id, double x, double z, double yaw, const SpawnOpts& o) -> std::shared_ptr<Vehicle> {
		return std::make_shared<Boat>(g, id, x, z, yaw, o);
	} });
	return true;
}();

WeaponDef BoatGunDef() {
	WeaponDef d; d.id = "boatgun"; d.damage = 22; d.range = 300; d.spread = 0.02; d.sound = "smg"; d.type = "gun";
	return d;
}
}

double WaveHeight(const Game& game, double x, double z, double t, bool lake) {
	const double storm = game.env.weather == "storm" ? 2.2 : game.env.rain > 0.3 ? 1.4 : 1;
	const double A = (lake ? 0.25 : 1) * storm;
	return A * (0.16 * std::sin(x * 0.083 + t * 0.9) + 0.11 * std::sin(z * 0.061 - t * 1.2 + 1.1) + 0.06 * std::sin((x + z) * 0.19 + t * 1.8));
}

Boat::Boat(Game& g, const std::string& type, double x, double z, double yw, const SpawnOpts& opts) : Vehicle(g, type, x, z, yw, opts) {
	boat = &BuildBoatModel(def);
	layout.seats.clear();
	for (const Pt3& s : boat->seats) layout.seats.push_back({ s[0], s[1], s[2] });
	layout.doorPos = { boat->doorPos[0], boat->doorPos[1], boat->doorPos[2] };
	layout.seatHip = boat->seatHip;
	// setup()
	I = mass * (def.L * def.L + def.W * def.W) / 12;
	pos.y = waterLevel() - def.draft * 0.6;
}

double Boat::waterLevel() const { return game.map.WaterLevel(pos.x, pos.z); }
bool Boat::lake() const { return waterLevel() > WATER_Y + 1; }

// climb aboard from whichever side you're on (from a pontoon or out of the water)
bool Boat::doorFor(int seat, Character* c, V3& out) const {
	const Pt3& s = seat < (int)boat->seats.size() ? boat->seats[seat] : boat->seats[0];
	double side = 1;
	if (c) { double lx, lz; worldToLocal(c->pos.x, c->pos.z, lx, lz); side = lx >= 0 ? 1 : -1; }
	out = localToWorld(side * (def.W / 2 + 0.6), 0, s[2]);
	return true;
}

V3 Boat::nearestDoor(const V3& p, int& seat) const {
	const Pt3& s = boat->seats[0];
	double lx, lz; worldToLocal(p.x, p.z, lx, lz);
	seat = 0;
	return localToWorld((lx >= 0 ? 1 : -1) * (def.W / 2 + 0.6), 0, s[2]);
}

// a jet ski: astride, feet in the footwells, hands on the bars
bool Boat::feetFor(int, V3 out[2]) const { out[0] = V3(0.26, -0.28, 0.1); out[1] = V3(-0.26, -0.28, 0.1); return true; }
bool Boat::gripsFor(V3 out[2]) const { out[0] = V3(0, 0.26, 0.5); out[1] = out[0]; return true; }

void Boat::playerControl(const Input& in, double dt) {
	Vehicle::playerControl(in, dt);
	input.handbrake = false;
	if (armed() && in.vehFire() && gunT <= 0) fireGun();
}

void Boat::takeOut(Character* c, const V3* at) {
	Vehicle::takeOut(c, at);
	// stepping off into open water: you're swimming (not standing on the seabed)
	const double wl = waterLevel(), bed = game.map.TerrainHeight(c->pos.x, c->pos.z);
	if (wl - bed > 1.3 && c->pos.y < wl + 0.2) { c->pos.y = wl - 0.5; c->vel.y = 0; c->grounded = false; }
}

M4 Boat::gunMatrix() const {
	const Pt3& g = boat->gunPos;
	return groupMatrix() * M4::Compose(V3(g[0], g[1], g[2]), Quat::FromEuler(-gunPitch, gunYaw, 0, "YXZ"));
}

void Boat::fireGun(const V3* target) {
	if (!boat->hasGun) return;
	gunT = 0.1;
	const V3 muzzle = gunMatrix().apply(V3(0, 0, 1.3));
	V3 aim;
	if (target) aim = *target;
	else {
		const V3 cam = game.rig.camPos, dir = game.rig.lookDir();
		CombatHit hit;
		Combat* cb = dynamic_cast<Combat*>(game.combat);
		aim = cb && cb->raycast(cam.x, cam.y, cam.z, dir.x, dir.y, dir.z, 300, driver(), hit) ? hit.point : cam + dir * 300;
	}
	const V3 dir = (aim - muzzle).normalized();
	static const WeaponDef GUN = BoatGunDef();
	if (Combat* cb = dynamic_cast<Combat*>(game.combat)) cb->vehicleGun(driver(), muzzle, dir, GUN);
	if (driver() && driver()->isPlayer) game.rig.addShake(0.05);
}

void Boat::update(double dt) {
	if (removed) return;
	gunT -= dt;
	VehInput& inp = input;
	if (isWrecked() || !driver()) { inp.throttle = 0; inp.brake = 0; inp.steer *= 0.9; }
	const int steps = 2;
	const double h = dt / steps;
	for (int i = 0; i < steps; i++) boatStep(h);
	boatAfter(dt);
	updateVisual(dt);
}

void Boat::boatStep(double h) {
	const VehicleDef& d = def;
	const VehInput& inp = input;
	const double m = mass;
	const double s = std::sin(yaw), c = std::cos(yaw);
	const double fx = s, fz = c, rx = -c, rz = s;
	const double vLong = vel.x * fx + vel.z * fz, vLat = vel.x * rx + vel.z * rz;
	if (!floating) {
		// aground: dragged to a stop on the sand
		const double k = Max(0, 1 - 4 * h);
		vel.x *= k; vel.z *= k; r *= k;
		pos.x += vel.x * h; pos.z += vel.z * h;
		return;
	}
	const bool run = !exploded && !sunk && health > 0;
	double thrust = 0;
	if (run && inp.throttle > 0) thrust = inp.throttle * d.force;
	else if (run && inp.brake > 0) thrust = -inp.brake * d.force * (vLong > 1 ? 0.8 : 0.35);
	// hull drag: most of the thrust goes into pushing water until she climbs onto the plane
	const double kq = d.force * 0.85 / (d.top * d.top), kl = d.force * 0.15 / d.top;
	const double Fl = thrust - Sgn(vLong) * kq * vLong * vLong - kl * vLong;
	// the keel resists going sideways
	const double Flat = -vLat * m * 1.9;
	vel.x += (fx * Fl + rx * Flat) / m * h;
	vel.z += (fz * Fl + rz * Flat) / m * h;
	// rudder / jet steering: needs water flowing past it (or the prop turning)
	const double flow = Clamp((std::fabs(vLong) + (run ? inp.throttle * 3 : 0)) / 6, 0, 1);
	const double wantR = inp.steer * d.turn * flow * (vLong < -0.5 ? -1 : 1) * (1 - 0.35 * Clamp(std::fabs(vLong) / d.top, 0, 1));
	r += (wantR - r) * Min(1, h * 2.2);
	yaw += r * h;
	pos.x += vel.x * h; pos.z += vel.z * h;
	axLong = Damp(axLong, Fl / m, 6, h);
	ayLat = Damp(ayLat, r * vLong, 6, h);
}

void Boat::boatAfter(double dt) {
	const CityMap& map = game.map;
	const VehicleDef& d = def;
	const double wl = waterLevel();
	const bool onLake = wl > WATER_Y + 1;
	const double ground = map.TerrainHeight(pos.x, pos.z); // (the seabed, not a pier deck)
	const double depth = wl - ground;
	const double t = game.time;
	// aground when the keel touches bottom
	const bool wasFloat = floating;
	floating = depth > d.draft * 0.75;
	if (!floating && wasFloat && speedAbs() > 4) { damage(speedAbs() * 6); game.soundAt("crash", pos, 0.5); }
	// ride the swell; lift onto the plane at speed
	const double plane = Clamp((speedAbs() - d.top * 0.3) / (d.top * 0.4), 0, 1);
	const double wv = WaveHeight(game, pos.x, pos.z, t, onLake);
	double ty = wl + wv - d.draft * 0.6 + plane * d.draft * 0.35;
	if (sinking) ty = wl - sinkT * 0.6;
	if (!floating) ty = Max(ty, ground + d.draft * 0.9);
	heaveV += ((ty - pos.y) * 30 - heaveV * 7) * dt;
	pos.y += heaveV * dt;
	// pitch and roll: the waves under bow / stern / sides, bow up under power, heel into the turn
	const double L = d.L * 0.4, W = d.W * 0.45, s = std::sin(yaw), c = std::cos(yaw);
	const double wb = WaveHeight(game, pos.x + s * L, pos.z + c * L, t, onLake), ws = WaveHeight(game, pos.x - s * L, pos.z - c * L, t, onLake);
	const double wlft = WaveHeight(game, pos.x - c * W, pos.z + s * W, t, onLake), wrt = WaveHeight(game, pos.x + c * W, pos.z - s * W, t, onLake);
	const double tp = std::atan2(wb - ws, 2 * L) * 0.8 + plane * 0.07 + Clamp(axLong * 0.01, -0.05, 0.08);
	const double tr = std::atan2(wlft - wrt, 2 * W) * 0.8 + Clamp(-ayLat * 0.02, -0.25, 0.25) + (sinking ? Min(0.5, sinkT * 0.1) : 0);
	pitch = Damp(pitch, tp, 3, dt);
	roll = Damp(roll, tr, 3, dt);
	groundPitch = pitch; groundRoll = roll;
	airborne = false;
	// piers, pilings, the ship, the quay
	const auto& list = game.collision->obbContacts(pos.x, pos.z, yaw, hx, hz, pos.y + 0.2, contacts);
	for (const Contact& ct : list) resolveStatic(ct);
	// wrecked: burn, blow up, go down
	if (!exploded && health <= 0) { onFire = true; burnTime += dt; if (burnTime > 4) explode(); }
	if (exploded) { sinking = true; sinkT += dt; if (sinkT > 12 && !sunk) { sunk = true; game.events.vehicleSunk.emit(this); } }
	// spray, foam and the wake
	const double spd = speedAbs();
	propSpin += (input.throttle * 40 + spd) * dt;
	if (floating && spd > 2.5 && game.effects) {
		wakeT -= dt;
		IEffects* e = game.effects;
		const V3 stern(pos.x - s * d.L * 0.5, wl + 0.05, pos.z - c * d.L * 0.5);
		e->wakeAdd(vid, stern.x, wl + 0.04, stern.z, d.W * (0.8 + spd * 0.03), Clamp(spd / 12, 0.2, 1));
		if (wakeT <= 0) {
			wakeT = 0.05;
			e->foam(stern, s, c, spd, d.W);
			if (spd > 8) e->bowSpray(V3(pos.x + s * d.L * 0.3, wl + 0.2, pos.z + c * d.L * 0.3), s, c, spd, d.W);
		}
	} else if (game.effects) game.effects->wakeBreak(vid);
}

void Boat::updateVisual(double) {
	// lights at night and in the rain
	lightsOn = (game.env.night > 0.4 || game.env.rain > 0.3) && driver() && !isWrecked();
	if (boat->hasGun && driver() && driver()->isPlayer) {
		// the bow gun follows the camera
		const V3 dir = game.rig.lookDir();
		gunYaw = Clamp(WrapAngle(std::atan2(dir.x, dir.z) - yaw), -2.2, 2.2);
		gunPitch = Clamp(std::asin(Clamp(dir.y, -1, 1)), -0.3, 0.5);
	}
}

void Boat::explode() {
	if (exploded) return;
	Vehicle::explode();
	sinking = true;
	vy = 0; airborne = false;
}

} // namespace atg
