#include "Aircraft.h"
#include "Character.h"
#include "Combat.h"
#include "Game.h"
#include "Hud.h"
#include "RoadLayout.h"
#include "Vehicles.h"

namespace atg {

namespace {
const double AirG = 9.81;
const V3 AirX(1, 0, 0), AirY(0, 1, 0), AirZ(0, 0, 1);
double AirWaterDepth(const Game& g, double x, double z) { return WATER_Y - g.map.GroundHeight(x, z); }

WeaponDef AirGun(const char* id, double damage, double range, double spread, const char* sound) {
	WeaponDef d; d.id = id; d.damage = damage; d.range = range; d.spread = spread; d.sound = sound; d.type = "gun";
	return d;
}
const WeaponDef& CANNON() { static const WeaponDef d = AirGun("cannon", 34, 900, 0.012, "rifle"); return d; }
const WeaponDef& MINIGUN() { static const WeaponDef d = AirGun("minigun", 26, 500, 0.02, "smg"); return d; }

// vehicles.spawn of a plane / jet, a helicopter or the tank
const bool GAirFactory = [] {
	VehicleManager::Factories().push_back({ [](const VehicleDef& d) { return d.kind == "plane" || d.kind == "jet"; }, [](Game& g, const std::string& id, double x, double z, double yaw, const SpawnOpts& o) -> std::shared_ptr<Vehicle> { return std::make_shared<Plane>(g, id, x, z, yaw, o); } });
	VehicleManager::Factories().push_back({ [](const VehicleDef& d) { return d.kind == "heli"; }, [](Game& g, const std::string& id, double x, double z, double yaw, const SpawnOpts& o) -> std::shared_ptr<Vehicle> { return std::make_shared<Heli>(g, id, x, z, yaw, o); } });
	VehicleManager::Factories().push_back({ [](const VehicleDef& d) { return d.kind == "tank"; }, [](Game& g, const std::string& id, double x, double z, double yaw, const SpawnOpts& o) -> std::shared_ptr<Vehicle> { return std::make_shared<Tank>(g, id, x, z, yaw, o); } });
	return true;
}();

void AirSeats(VehicleLayout& layout, const AircraftModel& m) {
	layout.seats.clear();
	for (const Pt3& s : m.seats) layout.seats.push_back({ s[0], s[1], s[2] });
	layout.doorPos = { m.doorPos[0], m.doorPos[1], m.doorPos[2] };
}
}

V3 AimPoint(Game& game, double maxDist, Character* exclude) {
	const V3 cam = game.rig.camPos, dir = game.rig.lookDir();
	CombatHit hit;
	Combat* cb = dynamic_cast<Combat*>(game.combat);
	if (cb && cb->raycast(cam.x, cam.y, cam.z, dir.x, dir.y, dir.z, maxDist, exclude, hit) && hit.t > 6) return hit.point;
	return cam + dir * maxDist;
}

// ------------------------------------------------------------------ AirVehicle
AirVehicle::AirVehicle(Game& g, const std::string& type, double x, double z, double yw, const SpawnOpts& opts) : Vehicle(g, type, x, z, yw, opts) {
	air = &BuildAircraftModel(def, color, type);
	AirSeats(layout, *air);
	// setup(): the simulation's reference is the ground point under the centre of gravity
	cgHeight = air->cgY;
	hx = (IsSet(def.colW) ? def.colW : def.W) / 2;
	I = mass * (def.L * def.L + (2 * hx) * (2 * hx)) / 12;
	quat = Quat::FromAxisAngle(AirY, yaw);
	grounded = true; gearDown = true; gearK = 1;
	power = 0; spool = 0;
	angVel = V3();
	gunT = 0; missileT = 0; side = 1;
	propAngle = Rand() * 6;
	burnTime = 0;
}

double AirVehicle::altitude() const { return pos.y - Max(game.map.GroundHeight(pos.x, pos.z), WATER_Y); }

V3 AirVehicle::localPoint(double x, double y, double z) const { return quat.rotate(V3(x, y - cgHeight, z)) + cgPoint(); }

// _placeGroup: rotate about the centre of gravity rather than the ground point
M4 AirVehicle::groupMatrix() const {
	const V3 v = quat.rotate(V3(0, cgHeight, 0));
	return M4::Compose(V3(pos.x - v.x, pos.y + cgHeight - v.y, pos.z - v.z), quat);
}

double AirVehicle::strobe() const { return std::fmod(game.time, 1.3) < 0.07 && !exploded ? 12 : 0.05; }

void AirVehicle::putIn(Character* c, int seat) {
	Vehicle::putIn(c, seat);
	c->hiddenInVehicle = seat >= air->visibleSeats;
}

void AirVehicle::takeOut(Character* c, const V3* at) {
	Vehicle::takeOut(c, at);
	c->hiddenInVehicle = false;
}

// the surface under the aircraft (terrain, bridge decks, roofs for helicopters) and whether it is water
AirVehicle::Floor AirVehicle::floor() const {
	const double x = pos.x, z = pos.z;
	const double ground = def.kind == "heli" ? game.collision->floorHeight(x, z, pos.y + 0.6) : game.collision->surfaceHeight(x, z, pos.y + 1.2);
	const bool wet = AirWaterDepth(game, x, z) > 1.2 && ground < WATER_Y;
	return wet ? Floor{ WATER_Y - 0.2, true } : Floor{ ground, false };
}

void AirVehicle::crash(double speed, const V3* where) {
	if (exploded) return;
	const V3 c = where ? *where : cgPoint();
	if (game.effects) game.effects->sparks(c, 24);
	game.soundAt("crash", c, 1);
	if (driver() && driver()->isPlayer) game.rig.addShake(1);
	damage(speed * 55, lastDamager.get());
	if (health <= 0 || speed > 22) explode();
}

void AirVehicle::explode() {
	if (exploded) return;
	exploded = true;
	onFire = false;
	health = 0;
	power = 0;
	const V3 c = cgPoint();
	if (game.combat) game.combat->vehicleExplosion(this, c);
	for (auto& o : occupants) if (o) { DamageInfo di; di.type = "explosion"; di.source = lastDamager.get(); auto keep = o; keep->takeDamage(1000, di); }
	game.events.vehicleExploded.emit(this);
	wreckTime = 0;
	if (!grounded) angVel.set((Rand() - 0.5) * 2, (Rand() - 0.5) * 3, (Rand() - 0.5) * 3);
}

// tumble to the ground as a wreck
void AirVehicle::wreckStep(double h) {
	const Floor fl = floor();
	if (!grounded) {
		vel.y -= AirG * h;
		vel *= 1 - 0.15 * h;
		if (angVel.lengthSq() > 1e-6) quat = quat * Quat::FromAxisAngle(angVel.normalized(), angVel.length() * h);
	}
	pos.addScaled(vel, h);
	if (pos.y <= fl.y) {
		if (!grounded && vel.y < -8 && game.effects) game.effects->explosion(cgPoint(), 5);
		pos.y = fl.y;
		vel.y = 0;
		vel.x *= std::exp(-4 * h); vel.z *= std::exp(-4 * h);
		angVel *= std::exp(-6 * h);
		if (!grounded) {
			// settle roughly level with a bit of a lean
			const V3 f = quat.rotate(AirZ);
			const double yw = std::atan2(f.x, f.z);
			const double ex = -0.05 + Rand() * 0.1;
			const double ez = (Rand() - 0.5) * 0.35;
			quat = Quat::FromEuler(ex, yw, ez, "YXZ");
		}
		grounded = true;
		if (fl.water && !sunk) { sunk = true; if (game.effects) game.effects->splash(cgPoint(), 3); }
	}
	if (sunk) pos.y = Max(game.map.GroundHeight(pos.x, pos.z), pos.y - h * 0.8);
}

// cars / buildings: push out when slow, crash when fast
void AirVehicle::statics() {
	const auto& list = game.collision->obbContacts(pos.x, pos.z, yaw, hx, hz * 0.92, pos.y + cgHeight - 0.7, contacts);
	for (const Contact& ct : list) {
		if (ct.obj->kind == CollObj::Circle && ct.obj->breakable && !ct.obj->broken) { game.breakProp(ct.obj); continue; }
		const double vn = vel.x * ct.nx + vel.z * ct.nz;
		if (-vn > 12 || (vel.length() > 16 && !grounded)) { const V3 at(ct.px, pos.y + cgHeight, ct.pz); crash(Max(-vn, vel.length() * 0.7), &at); if (exploded) return; }
		pos.x += ct.nx * ct.depth; pos.z += ct.nz * ct.depth;
		if (vn < 0) { vel.x -= ct.nx * vn * 1.3; vel.z -= ct.nz * vn * 1.3; if (-vn > 3) damage((-vn - 3) * 20); }
	}
}

bool AirVehicle::water(const Floor& fl) {
	if (!fl.water || sunk) return false;
	if (game.effects) game.effects->splash(cgPoint(), 3);
	if (vel.length() > 22) { explode(); return true; }
	sunk = true;
	onSunk();
	game.events.vehicleSunk.emit(this);
	return true;
}

// parked, empty and still: skip the physics (the door / canopy can still animate)
bool AirVehicle::asleep(double) {
	if (driver() || !grounded || exploded || sunk || health <= 0 || spool > 0.005 || vel.lengthSq() > 1e-4) return false;
	airborne = false;
	return true;
}

// ------------------------------------------------------------------ planes and jets
void Plane::playerControl(const Input& in, double dt) {
	Ctl& c = ctl;
	const auto& gp = in.gp;
	if (in.key("KeyW") || gp.rt > 0.3) power = Min(1, power + dt * (0.5 + gp.rt * 0.3));
	if (in.key("KeyS") || gp.lt > 0.3) power = Max(0, power - dt * 0.7);
	c.brake = in.key("Space") || gp.buttons[1] ? 1 : 0; // (pad: B)
	c.reverse = grounded && power == 0 && in.key("KeyS");
	// the mouse behaves like a spring-centred stick
	double mdx, mdy; in.lookDelta(mdx, mdy);
	mouseP = Clamp(mouseP * std::exp(-dt * 6) - mdy * 7, -1, 1);
	mouseR = Clamp(mouseR * std::exp(-dt * 6) + mdx * 7, -1, 1);
	const double keyP = (in.key("ArrowDown") ? 1 : 0) - (in.key("ArrowUp") ? 1 : 0);
	const double keyR = (in.key("KeyD") || in.key("ArrowRight") ? 1 : 0) - (in.key("KeyA") || in.key("ArrowLeft") ? 1 : 0);
	c.pitch = Clamp(keyP + mouseP + gp.ly, -1, 1);
	c.roll = Clamp(keyR + mouseR + gp.lx, -1, 1);
	c.yaw = Clamp((in.key("KeyE") ? 1 : 0) - (in.key("KeyQ") ? 1 : 0), -1, 1);
	horn = false;
	if (armed() && !exploded && health > 0) {
		if (in.vehFire() && gunT <= 0) fireCannon();
		if (in.vehAltPressed() && missileT <= 0) fireMissile();
	}
}

void Plane::fireCannon() {
	gunT = 0.07;
	const V3 f = quat.rotate(AirZ);
	const Pt3& mz = air->muzzles[0];
	const V3 muzzle = localPoint(mz[0], mz[1], mz[2]);
	// converge on the crosshair (a point far along the nose)
	if (Combat* cb = dynamic_cast<Combat*>(game.combat)) cb->vehicleGun(driver(), muzzle, f, CANNON());
	if (driver() && driver()->isPlayer) game.rig.addShake(0.08);
}

void Plane::fireMissile() {
	missileT = 0.6;
	const auto& pyl = air->pylons;
	side = (side + 1) % (int)pyl.size();
	const Pt3& p = pyl[side];
	const V3 at = localPoint(p[0], p[1], p[2]);
	const V3 f = quat.rotate(AirZ);
	Combat* cb = dynamic_cast<Combat*>(game.combat);
	if (!cb) return;
	bool heli = false;
	Vehicle* target = cb->lockTarget(cgPoint(), f, this, 1000, &heli);
	Combat::ProjectileOpts o; o.speed = 70; o.maxSpeed = 230; o.accel = 140; o.hasInherit = true; o.inherit = vel; o.target = target; o.targetHeli = heli; o.turn = 2.6; o.radius = 10; o.damage = 300; o.life = 7;
	cb->fireProjectile(driver(), "missile", at, f, o);
	if ((target || heli) && driver() && driver()->isPlayer && game.hud) game.hud->help("<b>Missile locked</b>", 1.2);
}

void Plane::update(double dt) {
	if (removed || asleep(dt)) return;
	Ctl& c = ctl;
	gunT -= dt; missileT -= dt;
	const bool ctrl = driver() && health > 0 && !exploded;
	if (!ctrl) {
		c.pitch = c.roll = c.yaw = 0;
		c.reverse = false;
		c.brake = grounded ? 1 : 0;
		if (!driver() && grounded) power = Max(0, power - dt * 0.5);
		if (health <= 0 && !exploded) { power = 0; c.roll = 0.6; c.pitch = -0.4; }
	}
	if (health <= 0 && !exploded) {
		onFire = true;
		burnTime += dt;
		if (burnTime > (grounded ? 4.5 : 14)) explode();
	}
	const int steps = (int)Max(1, std::ceil(dt * 90));
	const double h = dt / steps;
	for (int i = 0; i < steps && !removed; i++) {
		if (exploded || sunk) wreckStep(h);
		else fly(h);
	}
	const V3 f = quat.rotate(AirZ);
	yaw = std::atan2(f.x, f.z);
	airborne = !grounded;
	if (!exploded && !sunk) statics();
	// landing gear: tucks away once climbing out, comes down low and slow
	const double alt = altitude();
	if (grounded) gearDown = true;
	else if (alt > 35 && forwardSpeed() > def.vRotate) gearDown = false;
	else if (alt < 28) gearDown = true;
	gearK = Clamp(gearK + (gearDown ? dt : -dt) * 0.8, 0, 1);
	visual(dt);
}

void Plane::fly(double h) {
	const VehicleDef& d = def;
	Ctl& c = ctl;
	Quat& q = quat;
	V3 f = q.rotate(AirZ);
	V3 u = q.rotate(AirY);
	// the engine spools toward the lever
	const double lever = health > 0 ? power : 0;
	spool += (lever - spool) * Min(1, h * (d.kind == "jet" ? 0.9 : 1.6));
	const double V = vel.length();
	const double vf = vel.dot(f);
	const double auth = Clamp((vf - d.vStall * 0.3) / (d.vStall * 0.7), 0, 1);
	const Floor fl = floor();

	if (grounded) {
		// ----------------------------------------------------------- on the wheels
		double pitch = std::asin(Clamp(f.y, -1, 1));
		double yw = std::atan2(f.x, f.z);
		const double steer = Clamp(c.roll + c.yaw, -1, 1);
		yw -= steer * h * Clamp(1.1 / (1 + std::fabs(vf) / 9), 0.12, 1.1) * (std::fabs(vf) > 0.3 || spool > 0.2 ? 1 : 0);
		const bool canRotate = vf > d.vRotate * 0.82;
		const double tP = canRotate && c.pitch > 0 ? 0.22 * c.pitch : 0;
		pitch += Clamp(tP - pitch, -h * 0.5, h * 0.5);
		q = Quat::FromEuler(-pitch, yw, 0, "YXZ");
		double v = vf;
		if (c.reverse) v += (-2.5 - v) * Min(1, h * 1.5); // push back slowly
		else {
			v += d.thrust * spool * h;
			v -= Sgn(v) * Min(std::fabs(v), (0.25 + c.brake * 5.5) * h + v * v * d.drag * h);
		}
		vel.set(std::sin(yw) * v, 0, std::cos(yw) * v);
		pos.addScaled(vel, h);
		pos.y = fl.y;
		if (water(fl)) return;
		// rotate and lift off
		if (vf > d.vRotate && pitch > 0.05) {
			grounded = false;
			f = q.rotate(AirZ);
			vel = f * vf;
			pos.y += 0.05;
		} else if (pos.y > fl.y + 0.6) grounded = false; // rolled off an edge
		return;
	}

	// ------------------------------------------------------------- flying
	// control rates (body frame): x pitch (+ nose down), y yaw (+ nose left), z roll (+ right wing down)
	const double tx = -c.pitch * d.pitchRate * (0.25 + 0.75 * auth);
	const double ty = -c.yaw * d.yawRate * (0.3 + 0.7 * auth);
	const double tz = c.roll * d.rollRate * (0.2 + 0.8 * auth);
	const double k = 1 - std::exp(-h * 5), kr = 1 - std::exp(-h * 8);
	angVel.x += (tx - angVel.x) * k;
	angVel.y += (ty - angVel.y) * k;
	angVel.z += (tz - angVel.z) * kr;
	const double w = angVel.length();
	if (w > 1e-6) q = q * Quat::FromAxisAngle(angVel / w, w * h);
	// weathervane: the tail pulls the nose onto the flight path (strongly sideways, weakly in pitch unless stalled)
	if (V > 3) {
		const V3 vb = q.inverse().rotate(vel / V);
		const double beta = std::atan2(vb.x, Max(0.05, vb.z));
		const double alpha = std::atan2(-vb.y, Max(0.05, vb.z));
		const double kY = 1.6 + 1.4 * auth, kP = 0.35 * auth + 1.2 * (1 - auth);
		if (vb.z > 0) q = q * Quat::FromEuler(Clamp(alpha * kP * h, -0.05, 0.05), Clamp(beta * kY * h, -0.05, 0.05), 0, "YXZ");
	}
	// stalled / hanging on the prop: gravity swings the nose over toward the ground (hammerhead)
	if (auth < 0.65) {
		const double ks = (0.65 - auth) / 0.65;
		const V3 fw = q.rotate(AirZ);
		V3 tdir(0, -1, 0);
		if (V > 5 && vel.dot(fw) > 0.5 * V) tdir = tdir.lerp(vel / V, 1 - ks).normalized();
		V3 wv = fw.cross(tdir);
		double sn = wv.length();
		const double cs = fw.dot(tdir);
		if (sn < 0.02) { if (cs > 0) sn = 0; else { wv = q.rotate(AirX); sn = 1; } }
		if (sn > 0) {
			const double ang = std::atan2(wv.length(), cs);
			q = Quat::FromAxisAngle(wv / wv.length(), Min(ang, ks * 1.9 * h)) * q;
		}
	}
	q.normalize();
	f = q.rotate(AirZ);
	u = q.rotate(AirY);
	// bank-to-turn: a banked wing swings the heading (and the flight path with it) toward the low wing
	{
		const V3 l = q.rotate(AirX);
		const double horiz = std::sqrt(Max(0, 1 - f.y * f.y));
		const double turn = -AirG * l.y / Max(V, 22) * auth * horiz * 1.15;
		if (std::fabs(turn) > 1e-5) { const Quat tq = Quat::FromAxisAngle(AirY, turn * h); q = tq * q; vel = tq.rotate(vel); f = tq.rotate(f); u = tq.rotate(u); }
	}
	// forces: the wings cancel the part of gravity across the flight path (hands off = fly straight in any attitude);
	// the part along the path remains, so climbs cost speed and dives build it. Below stall speed lift fades.
	const double liftK = Clamp(vf / d.vStall, 0, 1);
	V3 acc = f * (d.thrust * spool);
	acc.y -= AirG;
	const double lift = AirG * liftK * liftK;
	acc.x -= f.x * f.y * lift; acc.y += (1 - f.y * f.y) * lift; acc.z -= f.z * f.y * lift;
	acc.addScaled(vel, -V * d.drag - (1 - auth) * 0.05);
	if (c.brake) acc.addScaled(vel, -0.25);
	vel.addScaled(acc, h);
	// air grip: the velocity swings onto the nose, keeping most of its energy (arcade handling)
	const double kv = 1 - std::exp(-h * (2.6 * auth));
	const double along = vel.dot(f);
	const double v0 = vel.length();
	const V3 wv = vel - f * along;
	vel.addScaled(wv, -kv);
	const double v1 = vel.length();
	if (v1 > 1e-3) vel *= (v1 + (v0 - v1) * 0.8) / v1; // never adds energy
	pos.addScaled(vel, h);
	// ------------------------------------------------------------- contact
	if (pos.y <= fl.y) {
		if (water(fl)) { pos.y = fl.y; grounded = true; vel *= 0.3; return; }
		const V3 l = q.rotate(AirX);
		const double pitch = std::asin(Clamp(f.y, -1, 1)), roll = std::atan2(l.y, u.y);
		const double sink = -vel.y;
		pos.y = fl.y;
		if (!gearDown || gearK < 0.6) {
			// belly landing
			crash(Max(sink * 2, V * 0.35));
			if (exploded) return;
		} else if (sink > 7.5 || std::fabs(roll) > 0.5 || pitch < -0.2) {
			crash(Max(sink, V * 0.5));
			if (exploded) return;
		} else if (sink > 3.5) damage((sink - 3.5) * 40);
		grounded = true;
		angVel.set(0, 0, 0);
		vel.y = 0;
		const double yw = std::atan2(f.x, f.z);
		q = Quat::FromEuler(-Clamp(pitch, 0, 0.22), yw, 0, "YXZ");
		if (game.effects) game.effects->tireSmoke(localPoint(hx * 0.7, 0.2, -1), 1);
		game.soundAt("bodyhit", cgPoint(), 0.4);
		return;
	}
	// nose / wingtips / tail touching terrain in flight
	const double L = d.L, W = d.W;
	const double pts[4][3] = { { 0, cgHeight, L * 0.48 }, { W * 0.48, cgHeight, -L * 0.1 }, { -W * 0.48, cgHeight, -L * 0.1 }, { 0, cgHeight + 0.5, -L * 0.48 } };
	for (const auto& p3 : pts) {
		const V3 p = localPoint(p3[0], p3[1], p3[2]);
		if (p.y < game.map.GroundHeight(p.x, p.z) - 0.1) { crash(Max(V * 0.8, 23), &p); return; }
	}
}

void Plane::visual(double dt) {
	propAngle += spool * dt * 55;
}

// the afterburners' length (scale z) with a flicker; 0 when off (exploded or below 55% spool)
double Plane::flameScale(int i) const {
	const double ab = exploded ? 0 : Clamp((spool - 0.55) / 0.45, 0, 1);
	if (ab <= 0.02) return 0;
	return (0.15 + ab * (i % 2 ? 0.9 : 1)) * (0.85 + Rand() * 0.3);
}

// ------------------------------------------------------------------ helicopters
Heli::Heli(Game& g, const std::string& type, double x, double z, double yw, const SpawnOpts& opts) : AirVehicle(g, type, x, z, yw, opts) {
	tiltP = 0; tiltR = 0; yawRate = 0;
	rotorAngle = Rand() * 6; tailAngle = 0;
	gunYaw = 0; gunPitch = 0;
	dustT = 0;
}

void Heli::playerControl(const Input& in, double dt) {
	Ctl& c = ctl;
	const auto& gp = in.gp;
	c.coll = Clamp((in.key("Space") ? 1 : 0) - (in.key("ShiftLeft") || in.key("ShiftRight") || in.key("KeyC") ? 1 : 0) + gp.rt - gp.lt, -1, 1);
	c.pitch = Clamp((in.key("KeyW") || in.key("ArrowUp") ? 1 : 0) - (in.key("KeyS") || in.key("ArrowDown") ? 1 : 0) - gp.ly, -1, 1);
	c.yaw = Clamp((in.key("KeyD") || in.key("ArrowRight") ? 1 : 0) - (in.key("KeyA") || in.key("ArrowLeft") ? 1 : 0) + gp.lx, -1, 1);
	c.roll = Clamp((in.key("KeyE") ? 1 : 0) - (in.key("KeyQ") ? 1 : 0), -1, 1);
	horn = false;
	if (armed() && !exploded && health > 0 && spool > 0.6) {
		aimT -= dt;
		const bool fire = in.vehFire(), alt = in.vehAltPressed();
		if (aimT <= 0 || fire || alt) { aimT = 0.12; aimAt = AimPoint(game, 700, driver()); hasAim = true; }
		if (fire && gunT <= 0) fireMinigun();
		if (alt && missileT <= 0) fireRocket();
	}
}

void Heli::fireMinigun() {
	gunT = 0.06;
	const Pt3& mz = air->muzzles[0];
	const V3 muzzle = localPoint(mz[0], mz[1], mz[2]);
	const V3 dir = (hasAim ? aimAt - muzzle : quat.rotate(AirZ)).normalized();
	if (Combat* cb = dynamic_cast<Combat*>(game.combat)) cb->vehicleGun(driver(), muzzle, dir, MINIGUN());
	if (driver() && driver()->isPlayer) game.rig.addShake(0.05);
}

void Heli::fireRocket() {
	missileT = 0.3;
	const auto& pods = air->pods;
	side = (side + 1) % (int)pods.size();
	const Pt3& p = pods[side];
	const V3 at = localPoint(p[0], p[1], p[2]);
	const V3 dir = (hasAim ? aimAt - at : quat.rotate(AirZ)).normalized();
	Combat* cb = dynamic_cast<Combat*>(game.combat);
	if (!cb) return;
	Combat::ProjectileOpts o; o.speed = 95; o.hasInherit = true; o.inherit = vel; o.radius = 8.5; o.damage = 240; o.life = 5;
	cb->fireProjectile(driver(), "rocket", at, dir, o);
}

void Heli::update(double dt) {
	if (removed || asleep(dt)) return;
	gunT -= dt; missileT -= dt;
	Ctl& c = ctl;
	const bool ctrl = driver() && health > 0 && !exploded;
	if (!ctrl) { c.pitch = c.roll = c.yaw = 0; c.coll = grounded ? 0 : -0.3; }
	if (health <= 0 && !exploded) {
		onFire = true;
		burnTime += dt;
		if (burnTime > (grounded ? 4 : 12)) explode();
	}
	const int steps = (int)Max(1, std::ceil(dt * 90));
	const double h = dt / steps;
	for (int i = 0; i < steps && !removed; i++) {
		if (exploded || sunk) wreckStep(h);
		else fly(h);
	}
	airborne = !grounded;
	if (!exploded && !sunk) heliStatics();
	// rotor downwash kicks up dust / spray
	dustT -= dt;
	if (spool > 0.6 && !exploded && dustT <= 0) {
		const double alt = pos.y - floor().y;
		if (alt < 16) {
			dustT = 0.05;
			const double ang = Rand() * kPi * 2, rad = 3 + Rand() * 6;
			const double x = pos.x + std::cos(ang) * rad, z = pos.z + std::sin(ang) * rad;
			const bool wet = AirWaterDepth(game, x, z) > 1;
			const double y = wet ? WATER_Y : game.map.GroundHeight(x, z);
			if (game.effects) { if (wet) game.effects->splash(V3(x, y, z), 0.5); else game.effects->dust(V3(x, y + 0.2, z), 1.4); }
		}
	}
	visual(dt);
}

void Heli::fly(double h) {
	const VehicleDef& d = def;
	Ctl& c = ctl;
	const bool alive = health > 0;
	const double want = alive && driver() ? 1 : 0;
	spool += (want - spool) * h * (want ? 0.4 : grounded ? 0.12 : 0.35);
	const double maxT = 0.42;
	// attitude: tilt the rotor disc; hands off = the auto-hover gently brakes the drift
	const double sy = std::sin(yaw), cy = std::cos(yaw);
	const double vFwd = vel.x * sy + vel.z * cy, vLeft = vel.x * cy - vel.z * sy;
	double tP = c.pitch ? c.pitch * maxT : Clamp(-vFwd * 0.05, -0.3, 0.3);
	double tR = c.roll ? c.roll * maxT * 0.8 : Clamp(vLeft * 0.06, -0.3, 0.3);
	if (grounded) { tP = 0; tR = 0; }
	if (!alive) { tP = 0.15; tR = 0.2; }
	tiltP = Damp(tiltP, tP, 2.4, h);
	tiltR = Damp(tiltR, tR, 2.4, h);
	double yr = grounded ? 0 : -c.yaw * 1.45;
	if (!alive) yr = 4.2 * (1 - spool * 0.5);
	yawRate = Damp(yawRate, yr, 3, h);
	yaw = WrapAngle(yaw + yawRate * h);
	quat = Quat::FromEuler(tiltP, yaw, tiltR, "YXZ");
	const V3 u = quat.rotate(AirY);
	const double lift = spool * (AirG + c.coll * 11) / Max(0.6, u.y);
	V3 acc = u * lift;
	acc.y -= AirG;
	const double kd = AirG * std::tan(maxT) / d.vMax;
	acc.x -= vel.x * kd; acc.z -= vel.z * kd;
	if (alive && !grounded) {
		// arcade handling: sideways slip bleeds off (the tail weathervanes the airframe into the airflow, so a turn
		// carries the speed round with it), and with the stick centred the auto-hover also holds position
		if (!c.roll) { const double k = 1.3 + Min(1, std::fabs(vFwd) / 20) * 1.2; acc.x -= cy * vLeft * k; acc.z += sy * vLeft * k; }
		if (!c.pitch && !c.roll) { acc.x -= vel.x * 0.5; acc.z -= vel.z * 0.5; }
	}
	acc.y -= vel.y * (1.1 * spool + 0.05);
	vel.addScaled(acc, h);
	const Floor fl = floor();
	// ground-effect flare: holding descend near the ground settles you gently onto it
	const double hAbove = pos.y - fl.y;
	if (alive && spool > 0.8 && hAbove < 12) vel.y = Max(vel.y, -(1.6 + hAbove * 0.45));
	pos.addScaled(vel, h);
	if (pos.y <= fl.y) {
		if (water(fl)) { pos.y = fl.y; grounded = true; return; }
		const double sink = -vel.y;
		const double tilt = Max(std::fabs(tiltP), std::fabs(tiltR));
		if (!grounded) {
			if (!alive || sink > 8 || tilt > 0.6) { crash(Max(sink, 23 * (alive ? 0 : 1)) + Hypot(vel.x, vel.z) * 0.4); if (exploded) return; }
			else if (sink > 4) damage((sink - 4) * 45);
		}
		pos.y = fl.y;
		vel.y = Max(0, vel.y);
		vel.x *= std::exp(-5 * h); vel.z *= std::exp(-5 * h);
		grounded = true;
	} else if (pos.y > fl.y + 0.08) grounded = false;
}

void Heli::heliStatics() {
	const auto& list = game.collision->obbContacts(pos.x, pos.z, yaw, Max(hx, def.rotorR * 0.6), hz * 0.85, pos.y + 0.4, contacts);
	for (const Contact& ct : list) {
		if (ct.obj->kind == CollObj::Circle && ct.obj->breakable && !ct.obj->broken) { game.breakProp(ct.obj); continue; }
		const double vn = vel.x * ct.nx + vel.z * ct.nz;
		pos.x += ct.nx * ct.depth; pos.z += ct.nz * ct.depth;
		if (vn < 0) {
			vel.x -= ct.nx * vn * 1.4; vel.z -= ct.nz * vn * 1.4;
			if (-vn > 14) { const V3 at(ct.px, pos.y + 2, ct.pz); crash(-vn, &at); }
			else if (-vn > 4) { damage((-vn - 4) * 30); if (game.effects) game.effects->sparks(V3(ct.px, pos.y + 3, ct.pz), 12); game.soundAt("metalhit", pos, 0.8); }
			if (exploded) return;
		}
	}
}

void Heli::visual(double dt) {
	rotorAngle += spool * dt * 26;
	tailAngle += spool * dt * 90;
	// the chin gun follows the aim point
	if (air->hasGun && hasAim && driver() && driver()->isPlayer) {
		const V3 lp = quat.inverse().rotate(aimAt - cgPoint());
		const Pt3& gp = air->gunAt;
		const double gy = lp.y + cgHeight - gp[1];
		const double ty = std::atan2(lp.x - gp[0], lp.z - gp[2]), tp = std::atan2(gy, Hypot(lp.x - gp[0], lp.z - gp[2]));
		gunYaw = Damp(gunYaw, Clamp(ty, -1.6, 1.6), 8, dt);
		gunPitch = Damp(gunPitch, Clamp(tp, -1.0, 0.25), 8, dt);
	}
}

// ------------------------------------------------------------------ the tank
Tank::Tank(Game& g, const std::string& type, double x, double z, double yw, const SpawnOpts& opts) : Vehicle(g, type, x, z, yw, opts) {
	air = &BuildAircraftModel(def, color, type);
	AirSeats(layout, *air);
}

void Tank::putIn(Character* c, int seat) { Vehicle::putIn(c, seat); c->hiddenInVehicle = true; }
void Tank::takeOut(Character* c, const V3* at) { Vehicle::takeOut(c, at); c->hiddenInVehicle = false; }

void Tank::playerControl(const Input& in, double dt) {
	Vehicle::playerControl(in, dt);
	aimT -= dt;
	if (aimT <= 0) { aimT = 0.1; aimAt = AimPoint(game, 900, driver()); hasAim = true; }
	if (in.vehFire() && reload <= 0 && !exploded) fireCannon();
}

// group x bodyGroup x turret (yaw on its ring) x gun pivot (elevation) x gun (recoil)
M4 Tank::gunMatrix() const {
	const M4 body = groupMatrix() * M4::Compose(V3(), Quat::FromEuler(bodyPitch, 0, bodyRoll));
	const M4 turret = M4::Compose(V3(air->turretAt[0], turretY, air->turretAt[2]), Quat::FromEuler(0, turretYaw + turretTurn, turretTilt));
	const M4 pivot = M4::Compose(V3(air->gunPivot[0], air->gunPivot[1], air->gunPivot[2]), Quat::FromEuler(-gunPitch, 0, 0));
	const M4 gun = M4::Compose(V3(0, 0, -recoil * 0.55), Quat());
	return body * turret * pivot * gun;
}

V3 Tank::muzzleWorld() const { return gunMatrix().apply(V3(0, 0, 5.75)); }

void Tank::fireCannon() {
	reload = 1.8;
	recoil = 1;
	const M4 m = gunMatrix();
	const V3 muzzle = m.apply(V3(0, 0, 5.75));
	const V3 dir = m.applyDir(V3(0, 0, 1)).normalized();
	Combat* cb = dynamic_cast<Combat*>(game.combat);
	if (cb) { Combat::ProjectileOpts o; o.speed = 190; o.gravity = 2.5; o.radius = 10; o.damage = 320; o.life = 6; cb->fireProjectile(driver(), "shell", muzzle, dir, o); }
	if (game.effects) {
		game.effects->muzzleFlash(muzzle, dir, true);
		for (int i = 0; i < 4; i++) { V3 p = muzzle + dir * (1.5 + i); p.y = muzzle.y - 0.5; game.effects->dust(p, 1.5); }
	}
	game.soundAt("explosion", muzzle, 0.55);
	game.soundAt("rpg", muzzle, 1);
	if (driver() && driver()->isPlayer) game.rig.addShake(0.7);
	bodyPitchV -= 1.2 * std::cos(turretYaw);
	bodyRollV += 0.8 * std::sin(turretYaw);
	game.events.gunshot.emit(driver(), muzzle, "cannon");
}

void Tank::update(double dt) {
	Vehicle::update(dt);
	reload -= dt;
	recoil = Max(0, recoil - dt * 1.6);
}

// tracks: no sliding sideways, pivot steering, heavy acceleration
void Tank::step(double h) {
	const VehicleDef& d = def;
	const VehInput& inp = input;
	if (airborne) {
		pos.x += vel.x * h; pos.z += vel.z * h;
		vy -= AirG * h; pos.y += vy * h;
		return;
	}
	const double s = std::sin(yaw), c = std::cos(yaw);
	const double fx = s, fz = c, rx = -c, rz = s;
	double vLong = vel.x * fx + vel.z * fz, vLat = vel.x * rx + vel.z * rz;
	const double v0 = vLong;
	const double thr = isWrecked() ? 0 : inp.throttle - inp.brake;
	const double target = thr >= 0 ? thr * d.top : thr * d.top * 0.5;
	const double dv = target - vLong;
	const double acc = std::fabs(vLong) > 0.3 && Sgn(dv) != Sgn(vLong) ? 7 : d.accel;
	vLong += Clamp(dv, -acc * h, acc * h);
	if (inp.handbrake) vLong *= std::exp(-4 * h);
	vLat *= std::exp(-9 * h);
	const double dir = vLong < -0.5 ? -1 : 1;
	const double rT = isWrecked() ? 0 : inp.steer * d.turn * dir * (1 - 0.35 * Min(1, std::fabs(vLong) / d.top));
	r += (rT - r) * Min(1, h * 6);
	vel.x = fx * vLong + rx * vLat; vel.z = fz * vLong + rz * vLat;
	axLong = Damp(axLong, (vLong - v0) / h, 8, h);
	ayLat = Damp(ayLat, r * vLong, 8, h);
	yaw += r * h;
	pos.x += vel.x * h; pos.z += vel.z * h;
	slipRear = 0; slipFront = 0; wheelspin = 0;
	// track speeds for the road wheels
	trackL = vLong + r * 1.5; trackR = vLong - r * 1.5;
}

void Tank::explode() {
	if (exploded) return;
	exploded = true;
	onFire = false;
	health = 0;
	if (game.combat) game.combat->vehicleExplosion(this, pos + V3(0, 1.6, 0));
	for (auto& o : occupants) if (o) { DamageInfo di; di.type = "explosion"; di.source = lastDamager.get(); auto keep = o; keep->takeDamage(1000, di); }
	// the turret gets blown off its ring
	turretVy = 9; turretSpin = (Rand() - 0.5) * 4;
	game.events.vehicleExploded.emit(this);
	wreckTime = 0;
}

void Tank::updateVisual(double dt) {
	// hull suspension rock
	const double tp = Clamp(axLong * 0.01, -0.05, 0.05), tr = Clamp(-ayLat * 0.006, -0.04, 0.04);
	bodyPitchV += ((tp - bodyPitch) * 70 - bodyPitchV * 9) * dt;
	bodyRollV += ((tr - bodyRoll) * 70 - bodyRollV * 9) * dt;
	bodyPitch += bodyPitchV * dt; bodyRoll += bodyRollV * dt;
	// road wheels
	wheelL += trackL * dt / 0.36; wheelR += trackR * dt / 0.36;
	// the turret and gun toward the aim point (world-stable while the hull turns)
	if (exploded) {
		if (IsSet(turretVy)) {
			turretVy -= AirG * dt;
			turretY = Max(1.78, turretY + turretVy * dt);
			if (turretY <= 1.78 && turretVy < 0) { turretVy = NaN(); turretTilt = 0.25; }
			turretTurn += turretSpin * dt;
		}
		return;
	}
	if (hasAim && driver()) {
		const double tx = pos.x, tz = pos.z;
		const double want = WrapAngle(std::atan2(aimAt.x - tx, aimAt.z - tz) - yaw);
		const double dyaw = WrapAngle(want - turretYaw);
		turretYaw = WrapAngle(turretYaw + Clamp(dyaw, -1.25 * dt, 1.25 * dt));
		const double hd = Hypot(aimAt.x - tx, aimAt.z - tz);
		const double wantP = Clamp(std::atan2(aimAt.y - (pos.y + 2.25), hd - 2.5) - groundPitch * std::cos(turretYaw), -0.14, 0.36);
		gunPitch += Clamp(wantP - gunPitch, -0.7 * dt, 0.7 * dt);
	}
}

} // namespace atg
