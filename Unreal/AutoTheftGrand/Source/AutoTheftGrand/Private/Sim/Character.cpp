#include "Character.h"
#include "Animal.h"
#include "Collision.h"
#include "Game.h"
#include "Vehicle.h"

namespace atg {

namespace {
int NextId = 1;
double SphereT(double ox, double oy, double oz, double dx, double dy, double dz, double cx, double cy, double cz, double r) {
	const double lx = ox - cx, ly = oy - cy, lz = oz - cz;
	const double b = lx * dx + ly * dy + lz * dz;
	const double c = lx * lx + ly * ly + lz * lz - r * r;
	const double h = b * b - c;
	if (h < 0) return -1;
	const double t = -b - std::sqrt(h);
	return t >= 0 ? t : -1;
}
double CylT(double ox, double oy, double oz, double dx, double dy, double dz, double cx, double cz, double r, double y0, double y1) {
	const double lx = ox - cx, lz = oz - cz;
	const double a = dx * dx + dz * dz;
	if (a < 1e-9) return -1;
	const double b = 2 * (lx * dx + lz * dz);
	const double c = lx * lx + lz * lz - r * r;
	const double disc = b * b - 4 * a * c;
	if (disc < 0) return -1;
	const double t = (-b - std::sqrt(disc)) / (2 * a);
	if (t < 0) return -1;
	const double y = oy + dy * t;
	if (y < y0 || y > y1) return -1;
	return t;
}
}

Character::Character(Game& g, const Appearance& look, double hp, double arm, const std::string& tm)
	: game(g), uid(NextId++), appearance(look), health(hp), maxHealth(hp), armor(arm), team(tm) {
	RestOffsets(appearance, rest);
	pose.Reset(rest);
	anim = std::make_unique<Animator>(&pose, rest);
	height = 1.8 * (appearance.height ? appearance.height : 1);
	weapons["fist"] = { kInf, kInf };
	updatePose();
}

void Character::setPosition(double x, double y, double z) { pos.set(x, y, z); vel.set(0, 0, 0); }
void Character::setPosition(double x, double z) { setPosition(x, game.map.GroundHeight(x, z), z); }

void Character::giveWeapon(const std::string& id, double ammo) {
	const WeaponDef* def = FindWeapon(id);
	if (!def) return;
	WeaponSlot& w = weapons[id];
	if (def->melee()) { w.ammo = kInf; w.clip = kInf; return; }
	w.ammo += ammo;
	if (w.clip == 0) { const double take = Min(def->clip, w.ammo); w.clip = take; w.ammo -= take; }
}

void Character::equip(const std::string& id) {
	if (!weapons.count(id)) return;
	weapon = id;
	weaponLocalRot = Quat::FromEuler(HoldFor(id).r.x, HoldFor(id).r.y, HoldFor(id).r.z);
	weaponVisible = true;
}

bool Character::hasWeaponModel() const { return weapon != "fist" && FindWeapon(weapon) != nullptr; }

const WeaponDef& Character::weaponDef() const {
	const WeaponDef* d = FindWeapon(weapon);
	return d ? *d : *FindWeapon("fist");
}
std::string Character::holdType() const { const WeaponDef* d = FindWeapon(weapon); return d ? d->hold : "none"; }

M4 Character::rootMatrix() const {
	const double h = appearance.height ? appearance.height : 1;
	if (vehicle) return vehicle->bodyMatrix() * M4::Compose(rootLocalPos, Quat::FromEuler(rootLocalRot.x, rootLocalRot.y, rootLocalRot.z), V3(h, h, h));
	return M4::Compose(pos, Quat::FromEuler(0, yaw, 0), V3(h, h, h));
}

M4 Character::weaponMatrix() const {
	return pose.world[Bone::rHand] * M4::Compose(HoldFor(weapon).p, weaponLocalRot);
}

V3 Character::muzzleWorld() {
	V3 mz;
	if (hasWeaponModel() && MuzzleFor(weapon, mz)) {
		updatePose();
		return weaponMatrix().apply(mz);
	}
	return chestPos().addScaled(forward(), 0.4);
}

// ------------------------------------------------------------------ physics & animation
void Character::update(double dt) {
	if (removed) return;
	if (anim->action) {
		AnimAction& act = *anim->action;
		if (!act.hitDone && act.hitTime >= 0 && act.t >= act.hitTime) { act.hitDone = true; if (act.onHit) { auto f = act.onHit; f(); } }
	}
	if (vehicle) {
		seatState(animState);
		animState.speed = 0;
		animState.grounded = true;
		animState.swim = false;
		animState.aim = aiming && weaponDef().gun();
		animState.aimPitch = aimPitch;
		animState.weapon = holdType();
		leanOut(dt);
		anim->update(dt, animState);
		updatePose();
		orientWeapon();
		return;
	}
	animState.sit = 0;
	if (ragdolling) {
		ragdoll->update(dt);
		if (!Finite(ragdoll->pos[0]) || !Finite(ragdoll->pos[2])) { recoverFrom(); return; }
		ragdoll->apply(rootMatrix());
		updatePose();
		downTime += dt;
		if (!dead && (ragdoll->settled || downTime > 3.5) && downTime > 1.2) getUp();
		return;
	}
	physics(dt);
	if (pos.finite() && Finite(vel.x) && Finite(vel.z)) { hasGoodPos = true; goodPos = pos; }
	else recoverFrom();
	const double dy = !IsSet(lastYaw) ? 0 : WrapAngle(yaw - lastYaw);
	lastYaw = yaw;
	AnimState& st = animState;
	st.turn = dt > 0 ? dy / dt : 0;
	const double hs = Hypot(vel.x, vel.z);
	st.speed = hs;
	st.moveAngle = hs > 0.2 ? WrapAngle(std::atan2(vel.x, vel.z) - yaw) : 0;
	st.grounded = grounded;
	st.vy = vel.y;
	st.swim = swimming;
	st.aim = aiming && (weaponDef().gun() || weaponDef().type == "launcher");
	st.aimPitch = aimPitch;
	st.weapon = holdType();
	st.crouch = crouching && !swimming;
	st.bike.clear(); st.hasFeet = false;
	weaponVisible = !swimming;
	anim->update(dt, st);
	updatePose();
	orientWeapon();
}

void Character::seatState(AnimState& st) {
	Vehicle* v = vehicle;
	st.sit = seat == 0 ? 1 : 2;
	if (v && (!v->def.bike.empty() || v->def.astride)) {
		st.bike = seat == 0 ? (v->def.bike.empty() ? "moto" : v->def.bike) : "pillion";
		st.hasFeet = v->feetFor(seat, st.feet);
		st.hasGrips = seat == 0 && v->gripsFor(st.grips);
		st.steer = v->steerAngle;
		st.boardCrouch = v->def.board ? v->crouch : 0;
	} else { st.bike.clear(); st.hasFeet = false; }
}

void Character::leanOut(double dt) {
	Vehicle* v = vehicle;
	if (seat < 0 || seat >= (int)v->layout.seats.size()) return;
	const V3& seatP = v->layout.seats[seat];
	if (!hasSeatPos) { hasSeatPos = true; seatPos = rootLocalPos; }
	if (!v->def.bike.empty()) {
		const bool aim = aiming && animState.aim;
		leanK = aim ? 1 : 0;
		const double ay = hasAimYaw ? aimYaw : hasAimDir ? std::atan2(aimDir.x, aimDir.z) : v->yaw;
		animState.aimTwist = aim ? Clamp(WrapAngle(ay - v->yaw), -1.7, 1.7) : 0;
		return;
	}
	animState.aimTwist = 0;
	const double want = aiming && seat > 0 && animState.aim && v->def.kind.empty() ? 1 : 0;
	leanK = leanK + (want - leanK) * (1 - std::exp(-9 * dt));
	const double k = leanK < 0.002 ? 0 : leanK;
	const double side = seatP.x >= 0 ? 1 : -1;
	double rel = 0;
	if (k > 0) {
		const double ay = hasAimYaw ? aimYaw : hasAimDir ? std::atan2(aimDir.x, aimDir.z) : v->yaw;
		const double out = side * kPi / 2;
		rel = out + Clamp(WrapAngle(ay - v->yaw - out), -1.95, 1.95);
	}
	const double e = k * k * (3 - 2 * k);
	rootLocalPos.set(seatPos.x + side * 0.62 * e, seatPos.y + 0.42 * e, seatPos.z - 0.05 * e);
	rootLocalRot.set(0, rel * e, -side * 0.12 * e);
	leaning = e > 0.5;
}

void Character::orientWeapon() {
	if (!hasWeaponModel()) return;
	const WeaponDef& def = weaponDef();
	const bool aimingGun = aiming && (def.gun() || def.type == "launcher") && !ragdolling;
	const double k = anim->w.aim;
	const Hold& h = HoldFor(weapon);
	const Quat holdQ = Quat::FromEuler(h.r.x, h.r.y, h.r.z);
	if (!aimingGun || k < 0.05) { weaponLocalRot = holdQ; return; }
	V3 dir = hasAimDir ? aimDir : V3(std::sin(yaw) * std::cos(aimPitch), std::sin(aimPitch), std::cos(yaw) * std::cos(aimPitch));
	dir.normalize();
	// a rotation whose +z is the aim and +y is (roughly) up (three.js lookAt then a half turn about y)
	V3 X = V3(0, 1, 0).cross(dir);
	if (X.lengthSq() < 1e-12) X = V3(1, 0, 0);
	X.normalize();
	const V3 Y = dir.cross(X);
	const Quat qW = Quat::FromMatrix(M4::Basis(X, Y, dir).m);
	const Quat qH = pose.world[Bone::rHand].rotation();
	const Quat qL = qH.inverse() * qW;
	weaponLocalRot = Quat::Slerp(holdQ, qL, Min(1, k));
}

void Character::physics(double dt) {
	const double accelR = swimming ? 6 : grounded ? 24 : (airAccel ? airAccel : 3);
	const double tx = moveTargetX, tz = moveTargetZ;
	const double dvx = tx - vel.x, dvz = tz - vel.z;
	const double dl = Hypot(dvx, dvz);
	const double maxDv = accelR * dt;
	if (dl > maxDv) { vel.x += dvx / dl * maxDv; vel.z += dvz / dl * maxDv; }
	else { vel.x = tx; vel.z = tz; }
	if (!grounded && !swimming) vel.y -= 19 * dt * game.gravity;
	const double oldY = pos.y;
	pos.x += vel.x * dt;
	pos.z += vel.z * dt;
	pos.y += vel.y * dt;
	const auto res = game.collision->resolveCircle(pos.x, pos.z, radius, pos.y + 0.3, 1.5);
	if (res.hit) { pos.x = res.x; pos.z = res.z; onWallHit(res.hit); }
	const double gh = game.collision->floorHeight(pos.x, pos.z, oldY);
	const double depth = WATER_Y - game.map.GroundHeight(pos.x, pos.z);
	if (depth > 1.35 && pos.y < WATER_Y - 0.85) {
		if (!swimming) onEnterWater();
		swimming = true;
		grounded = false;
		pos.y = WATER_Y - 1.05;
		vel.y = 0;
		return;
	}
	if (swimming && depth < 1.2) { swimming = false; pos.y = gh; }
	if (swimming) return;
	if (pos.y <= gh + 0.001 || (grounded && pos.y - gh < 0.45 && vel.y <= 0.01)) {
		if (!grounded && vel.y < -13) onHardLanding(-vel.y);
		if (gh - oldY > 0.6 && grounded) {
			pos.x -= vel.x * dt; pos.z -= vel.z * dt;
			vel.x *= 0.2; vel.z *= 0.2;
		} else pos.y = gh;
		vel.y = Max(0, vel.y);
		grounded = true;
	} else grounded = false;
}

bool Character::jump(double v) {
	if (!grounded || swimming || vehicle || ragdolling) return false;
	vel.y = v;
	grounded = false;
	pos.y += 0.05;
	return true;
}

void Character::faceTowards(double x, double z, double dt, double rate) {
	const double target = std::atan2(x - pos.x, z - pos.z);
	yaw = dt > 0 ? DampAngle(yaw, target, rate, dt) : target;
}

// ------------------------------------------------------------------ damage
bool Character::takeDamage(double amount, const DamageInfo& info) {
	if (dead || invincible) return false;
	if (protectUntil > 0 && game.time < protectUntil) return false;
	double dmg = amount;
	if (info.part == "head") dmg *= IsSet(info.headMul) ? info.headMul : 4;
	if (info.part == "limb") dmg *= 0.7;
	if (armor > 0 && info.type != "fall" && info.type != "drown") {
		const double absorb = Min(armor, dmg * 0.8);
		armor -= absorb;
		dmg -= absorb;
	}
	health -= dmg;
	lastDamager = info.source;
	lastAnimalDamager = info.animalSource;
	lastHitTime = game.time;
	if (info.source || info.animalSource) onDamaged(info.source, dmg, info);
	if (info.source) game.events.charDamaged.emit(this, dmg, info.source);
	if (health <= 0) {
		health = 0;
		die(info);
		return true;
	}
	if (!ragdolling && info.type != "fire") {
		if (info.knockdown) knockDown(info.hasImpulse ? info.impulse : V3());
		else anim->play("flinch");
	}
	return false;
}

void Character::die(const DamageInfo& info) {
	if (dead) return;
	dead = true;
	health = 0;
	auto self = shared_from_this(); // (an occupant ejected from a car must outlive this call)
	if (vehicle) vehicle->ejectOccupant(this, true);
	startRagdoll(info.hasImpulse ? info.impulse : V3(), info.hasHitPoint ? &info.hitPoint : nullptr);
	weaponVisible = false;
	onDeath(info);
	game.events.death.emit(this, info);
}

void Character::startRagdoll(const V3& impulse, const V3* hitPoint) {
	if (!ragdoll) ragdoll = std::make_unique<Ragdoll>(&pose, game.collision.get());
	updatePose();
	if (hitPoint) {
		ragdoll->start(vel, nullptr);
		const int pi = ragdoll->nearestParticle(hitPoint->x, hitPoint->y, hitPoint->z);
		ragdoll->push(pi, impulse.x, impulse.y, impulse.z);
	} else ragdoll->start(vel, &impulse);
	ragdolling = true;
	downTime = 0;
	anim->action = nullptr;
}

void Character::knockDown(const V3& impulse) {
	if (dead || vehicle) return;
	startRagdoll(impulse);
	onKnockedDown();
}

void Character::getUp() {
	const double* p = ragdoll->pos;
	const double hx = p[0], hz = p[2];
	const double dx = p[9] - hx, dz = p[11] - hz;
	const double y = std::atan2(-dx, -dz);
	ragdolling = false;
	pos.set(hx, game.map.GroundHeight(hx, hz), hz);
	yaw = y;
	vel.set(0, 0, 0);
	anim->beginBlend(0.3);
	anim->play("getup");
	onGotUp();
}

void Character::recoverFrom() {
	if (hasGoodPos) pos = goodPos; else pos.set(0, game.map.GroundHeight(0, 0), 0);
	vel.set(0, 0, 0);
	moveTargetX = moveTargetZ = 0;
	if (!Finite(yaw)) yaw = 0;
	if (!Finite(aimPitch)) aimPitch = 0;
	hasAimDir = false;
	if (ragdolling && !dead) { ragdolling = false; downTime = 0; anim->beginBlend(0.2); }
}

bool Character::rayHit(double ox, double oy, double oz, double dx, double dy, double dz, double maxT, BodyHit& out) {
	if (removed) return false;
	if (ragdolling) {
		bool found = false;
		for (int i = 0; i < Ragdoll::N; i++) {
			const V3 c = ragdoll->particle(i);
			const double r = i == 3 ? 0.14 : i < 2 ? 0.2 : 0.12;
			const double t = SphereT(ox, oy, oz, dx, dy, dz, c.x, c.y, c.z, r);
			if (t >= 0 && t < maxT && (!found || t < out.t)) { found = true; out = { t, i == 3 ? "head" : i < 3 ? "torso" : "limb", i }; }
		}
		return found;
	}
	const double h = height;
	const double base = pos.y + (vehicle ? 0.35 : 0);
	V3 hp;
	if (vehicle) { updatePose(); hp = pose.WorldPos(Bone::head); }
	else hp = V3(pos.x, base + h * 0.93, pos.z);
	const double th = SphereT(ox, oy, oz, dx, dy, dz, hp.x, hp.y, hp.z, 0.14);
	double tb = -1;
	if (!vehicle) {
		const double r = crouching ? 0.36 : 0.3;
		const double top = base + (crouching ? h * 0.62 : h * 0.84), bot = base;
		tb = CylT(ox, oy, oz, dx, dy, dz, pos.x, pos.z, r, bot, top);
	}
	bool found = false;
	if (th >= 0 && th < maxT) { out = { th, "head", -1 }; found = true; }
	if (tb >= 0 && tb < maxT && (!found || tb < out.t - 0.05)) {
		const double hy = oy + dy * tb - base;
		out = { tb, hy < h * 0.5 ? "limb" : "torso", -1 };
		found = true;
	}
	return found;
}

void Character::remove() { removed = true; }

} // namespace atg
