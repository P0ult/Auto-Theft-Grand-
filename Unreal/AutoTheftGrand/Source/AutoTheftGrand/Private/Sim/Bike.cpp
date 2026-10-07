#include "Bike.h"
#include "Character.h"
#include "Game.h"
#include "Vehicles.h"

namespace atg {

// vehicles.spawn of a motorbike or bicycle makes a Bike (skateboards are their own class)
static const bool GBikeFactory = [] {
	VehicleManager::Factories().push_back({ [](const VehicleDef& d) { return !d.bike.empty() && !d.board; }, [](Game& g, const std::string& id, double x, double z, double yaw, const SpawnOpts& o) -> std::shared_ptr<Vehicle> {
		return std::make_shared<Bike>(g, id, x, z, yaw, o);
	} });
	return true;
}();

Bike::Bike(Game& g, const std::string& type, double x, double z, double yw, const SpawnOpts& opts) : Vehicle(g, type, x, z, yw, opts) {
	if (!def.board) { // (a skateboard sets up its own model)
		bike = &BuildBikeModel(def);
		layout.seats.clear();
		for (const Pt3& s : bike->seats) layout.seats.push_back({ s[0], s[1], s[2] });
		layout.doorPos = { bike->doorPos[0], bike->doorPos[1], bike->doorPos[2] };
		layout.seatHip = bike->seatHip;
	}
	// setup(): lean, the last heading, the pedals, the throw timer
	lean = 0; yawPrev = yaw; pedalPhase = 0; throwT = 0;
}

void Bike::setup(const SpawnOpts&) {}

// get on from whichever side you're standing
bool Bike::doorFor(int seat, Character* c, V3& out) const {
	double side = 1;
	if (c) { double lx, lz; worldToLocal(c->pos.x, c->pos.z, lx, lz); side = lx >= 0 ? 1 : -1; }
	out = localToWorld(0.85 * side, 0, seat == 0 ? -0.15 : -0.55);
	return true;
}

V3 Bike::nearestDoor(const V3& p, int& seat) const {
	double lx, lz; worldToLocal(p.x, p.z, lx, lz);
	const double side = lx >= 0 ? 1 : -1;
	seat = 0;
	return localToWorld(0.85 * side, 0, -0.15);
}

// where the rider's feet go (hip-relative, bike axes): pegs, or the pedals going round
bool Bike::feetFor(int seat, V3 out[2]) const {
	if (bike->hasFeet) {
		if (seat == 0) { out[0] = V3(bike->feet[0][0], bike->feet[0][1], bike->feet[0][2]); out[1] = V3(bike->feet[1][0], bike->feet[1][1], bike->feet[1][2]); }
		else { out[0] = V3(0.22, -0.4, -0.1); out[1] = V3(-0.22, -0.4, -0.1); }
		return true;
	}
	const Pt3& s = bike->seats[0];
	const double hipY = s[1] + bike->seatHip, ph = pedalPhase;
	const double cy = 0.3 - hipY, cz = -0.02 - s[2];
	out[0] = V3(0.13, cy + std::sin(ph) * 0.17, cz + std::cos(ph) * 0.17);
	out[1] = V3(-0.13, cy - std::sin(ph) * 0.17, cz - std::cos(ph) * 0.17);
	return true;
}

// the grips, hip-relative (y, z in the first entry)
bool Bike::gripsFor(V3 out[2]) const {
	const Pt3& s = bike->seats[0];
	out[0] = V3(0, bike->grips[0] - (s[1] + bike->seatHip), bike->grips[1] - s[2]);
	out[1] = out[0];
	return true;
}

void Bike::update(double dt) {
	Vehicle::update(dt);
	if (removed) return;
	if (def.offroad) surface = 1; // knobbly tyres don't mind the dirt
	if (def.pedal && bike && !bike->crank.Empty()) {
		// the cranks turn with the back wheel while pedalling, and freewheel when coasting
		if (driver() && input.throttle > 0.05 && speed() > -0.5) pedalPhase += Max(speed(), 2) * dt / 0.62;
	}
	throwT -= dt;
}

void Bike::updateVisual(double dt) {
	Vehicle::updateVisual(dt);
	if (tb) return; // (tumbling: the rigid body sets the pose)
	// lean into the turn: tan(lean) = v * yawRate / g (from the heading change, so it works for stand-ins too)
	const double yr = dt > 0 ? WrapAngle(yaw - yawPrev) / dt : 0;
	yawPrev = yaw;
	double tgt = 0;
	const bool ridden = driver() && !isWrecked();
	if (ridden && !airborne) tgt = Clamp(std::atan2(speed() * yr, 9.81), -0.8, 0.8);
	else if (!ridden && speedAbs() < 1 && !airborne) tgt = isWrecked() ? 1.35 : 0.2; // on its side / its stand
	lean = Damp(lean, tgt, ridden ? 7 : 3, dt);
}

// group.rotation.z = groundRoll - lean
M4 Bike::groupMatrix() const {
	if (tb || hasNetQ) return Vehicle::groupMatrix();
	return M4::Compose(pos, Quat::FromEuler(-groundPitch, yaw, groundRoll - lean, "YXZ"));
}

// off you come
void Bike::throwRiders(double impact) {
	if (throwT > 0) return;
	throwT = 1;
	const double vx = vel.x, vz = vel.z;
	std::vector<std::shared_ptr<Character>> occ;
	for (auto& o : occupants) if (o) occ.push_back(o);
	for (auto& o : occ) {
		const V3 p = localToWorld(0, 0, 0.6);
		takeOut(o.get(), &p);
		o->pos.y = pos.y + 1.0;
		const V3 imp(vx * 0.55, 2.2 + impact * 0.12, vz * 0.55);
		o->vel = imp;
		o->knockDown(imp);
		DamageInfo di; di.type = "fall";
		o->takeDamage(Min(40, impact * 2), di);
		game.events.exitedVehicle.emit(o.get(), this);
		if (o->isPlayer) game.rig.addShake(0.6);
	}
	vel = vel * 0.6;
}

} // namespace atg
