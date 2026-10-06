#include "Vehicle.h"
#include "Character.h"
#include "Game.h"
#include "Input.h"

namespace atg {

namespace {
constexpr double G = 9.81;
int NextVid = 1;
double SatF(double x) {
	const double ax = std::fabs(x);
	if (ax < 1) return x * (1.5 - 0.5 * x * x);
	return Sign(x) * (1 - 0.12 * Min(1, (ax - 1) / 2.5));
}
const V3 UPV(0, 1, 0);
bool DrvIsPlayer(const Vehicle* v) { return v->driver() && v->driver()->isPlayer; }
}

Vehicle::Vehicle(Game& g, const std::string& t, double x, double z, double yw, const SpawnOpts& opts)
	: game(g), vid(NextVid++), type(t), def(*FindVehicle(t)) {
	color = opts.hasColor ? opts.color : RandPick(def.colors);
	if (def.kind.empty() && def.bike.empty()) {
		model = &BuildVehicleModel(def);
		layout.seats.clear();
		for (const Pt3& s : model->seats) layout.seats.push_back({ s[0], s[1], s[2] });
		layout.doorPos = { model->doorPos[0], model->doorPos[1], model->doorPos[2] };
		layout.seatHip = model->seatHip;
		layout.hasHull = true; layout.hull = model->hull;
		layout.doorMax = model->doorMax;
	}
	yaw = yw;
	pos.set(x, 0, z);
	pos.y = opts.hasY ? opts.y : game.map.GroundHeight(x, z);
	hx = def.W / 2; hz = def.L / 2;
	mass = def.mass;
	I = def.mass * (def.L * def.L + def.W * def.W) / 12;
	a = def.wheelbase * 0.5; b = def.wheelbase * 0.5;
	health = maxHealth = def.Health();
	lastGroundY = pos.y;
	locked = opts.locked;
	parked = opts.parked;
	sirenPhase = Rand() * 10;
	persistent = opts.persistent;
	missionTag = opts.missionTag;
}

V3 Vehicle::localToWorld(double x, double y, double z) const {
	const double s = std::sin(yaw), c = std::cos(yaw);
	return { pos.x + x * c + z * s, pos.y + y, pos.z - x * s + z * c };
}
void Vehicle::worldToLocal(double x, double z, double& lx, double& lz) const {
	const double s = std::sin(yaw), c = std::cos(yaw);
	const double dx = x - pos.x, dz = z - pos.z;
	lx = dx * c - dz * s; lz = dx * s + dz * c;
}
V3 Vehicle::doorWorld() const { return localToWorld(layout.doorPos.x, 0, layout.doorPos.z); }
V3 Vehicle::seatWorld(int i) const { const V3& s = layout.seats[i]; return localToWorld(s.x, s.y, s.z); }

M4 Vehicle::groupMatrix() const {
	Quat q;
	if (tb) q = tb->q;
	else if (hasNetQ) q = netQ;
	else q = Quat::FromEuler(-groundPitch, yaw, groundRoll, "YXZ");
	return M4::Compose(pos, q);
}
M4 Vehicle::bodyMatrix() const {
	const double y = Clamp(bodyY, -0.15, 0.15) + (def.hydraulics ? Clamp(hydraulic, -0.1, 0.5) : 0) - (flat ? 0.07 : 0);
	return groupMatrix() * M4::Compose(V3(0, y, 0), Quat::FromEuler(bodyPitch, 0, bodyRoll));
}

// ------------------------------------------------------------------ occupants
void Vehicle::putIn(Character* c, int seat) {
	auto keep = c->shared_from_this();
	Character* prev = occupants[seat].get();
	if (prev && prev != c) takeOut(prev);
	occupants[seat] = keep;
	c->vehicle = this;
	c->seat = seat;
	c->vel.set(0, 0, 0);
	c->ragdolling = false;
	const auto& S = layout.seats;
	const V3& s = seat < (int)S.size() ? S[seat] : S.back();
	const double h = c->appearance.height ? c->appearance.height : 1;
	if (layout.stand) { c->rootLocalPos = s; c->rootLocalRot.set(0, layout.standYaw, 0); }
	else { c->rootLocalPos.set(s.x, s.y + layout.seatHip - (c->anim->hipH - 0.46) * h, s.z); c->rootLocalRot.set(0, 0, 0); }
	c->seatPos = c->rootLocalPos; c->hasSeatPos = true; c->leanK = 0; c->leaning = false;
	c->yaw = yaw;
	if (c->hasWeaponModel() && !c->weaponDef().gun()) c->weaponVisible = false;
	parked = false;
}

void Vehicle::takeOut(Character* c, const V3* at) {
	auto keep = c->shared_from_this();
	const int seat = seatOf(c);
	if (seat >= 0) occupants[seat].reset();
	c->vehicle = nullptr;
	c->hasSeatPos = false; c->leanK = 0; c->leaning = false; c->hasAimYaw = false;
	c->seat = -1;
	const V3 p = at ? *at : doorWorld();
	c->pos.set(p.x, game.collision->floorHeight(p.x, p.z, pos.y + 0.4), p.z);
	c->yaw = yaw;
	c->vel.set(vel.x * 0.5, 0, vel.z * 0.5);
	c->weaponVisible = true;
	if (seat == 0) { input = VehInput(); }
}

void Vehicle::ejectOccupant(Character* c, bool dead) {
	const V3 p = localToWorld(hx + 0.8, 0, 0);
	takeOut(c, &p);
	if (dead) c->pos.y = pos.y + 0.5;
}

// ------------------------------------------------------------------ player control
void Vehicle::playerControl(const Input& in, double) {
	input.throttle = in.throttle();
	input.brake = in.brake();
	input.steer = in.steer();
	input.handbrake = in.down("handbrake");
	horn = in.down("horn");
	if (def.hydraulics && in.hit("hydraulics")) hydraulicV += 4.5;
	if (def.police && in.hit("horn") && in.key("ShiftLeft")) sirenOn = !sirenOn;
}

// ------------------------------------------------------------------ simulation
void Vehicle::update(double dt) {
	if (removed) return;
	VehInput& inp = input;
	const bool wrecked = isWrecked();
	if (wrecked || !driver()) { inp.throttle = 0; inp.steer *= 0.9; if (!driver() && !wrecked) { inp.brake = 0; inp.handbrake = speedAbs() < 3 || !driver(); } }
	const double spd = std::fabs(speed());
	const double maxSteer = def.steer / (1 + spd / 20);
	double target = inp.steer * maxSteer;
	if (std::fabs(inp.steer) < 0.05 && spd > 5 && driver() && driver()->isPlayer) {
		const double velYaw = std::atan2(vel.x, vel.z);
		const double slip = WrapAngle(velYaw - yaw);
		if (speed() > 0) target = Clamp(slip * 0.6, -maxSteer, maxSteer);
	}
	steerAngle = Damp(steerAngle, target, inp.steer == 0 ? 10 : 7, dt);
	if (inp.handbrake) rearGrip = Max(0.36, rearGrip - dt * 4);
	else rearGrip = Min(1, rearGrip + dt * 1.1);
	if (tb) { tumble(dt); updateVisual(dt); return; }
	const int steps = dt > 1.0 / 45 ? 3 : 2;
	const double h = dt / steps;
	for (int i = 0; i < steps; i++) step(h);
	afterPhysics(dt);
	rolloverCheck(dt);
	updateVisual(dt);
}

// ------------------------------------------------------------------ crash physics: tumbling
const Vehicle::Hull& Vehicle::hullPoints() {
	if (hull) return *hull;
	hull = std::make_unique<Hull>();
	const double L = def.L, W = def.W, c = def.clearance;
	auto add = [&](double x, double y, double z, bool wheel = false) { hull->pts.push_back({ V3(x, y, z), wheel }); };
	const VehicleModel::Hull* H = layout.hasHull ? &layout.hull : nullptr;
	if (!def.bike.empty()) {
		for (double z : { def.wheelbase / 2, -def.wheelbase / 2 }) add(0, 0, z, true);
		for (double sx : { 1.0, -1.0 }) { add(sx * 0.38, def.H * 0.8, def.wheelbase * 0.35); add(sx * 0.25, def.H * 0.45, -def.wheelbase * 0.3); add(sx * 0.2, c + 0.1, 0); }
		add(0, def.H * 0.75, -0.2);
	} else {
		for (double sx : { 1.0, -1.0 }) {
			for (double sz : { 1.0, -1.0 }) {
				add(sx * def.track / 2, 0, sz * def.wheelbase / 2, true);
				add(sx * W * 0.46, c + 0.06, sz * L * 0.47);
				add(sx * W * 0.5, H ? H->beltY : def.H * 0.6, sz * L * 0.45);
			}
			add(sx * W * 0.5, H ? H->beltY : def.H * 0.6, 0);
			const double rx = H ? H->roofX : W * 0.4, ry = H ? H->roofY : def.H, z0 = H ? H->roofZ0 : -L * 0.25, z1 = H ? H->roofZ1 : L * 0.1;
			add(sx * rx, ry, z0); add(sx * rx, ry, z1); add(sx * rx, ry, (z0 + z1) / 2);
		}
	}
	hull->cgH = H ? H->cgH : Max(0.35, def.H * 0.4);
	for (HullPt& q : hull->pts) q.p.y -= hull->cgH;
	const double m = mass, H2 = def.H * def.H, L2 = L * L, W2 = W * W;
	hull->inv = V3(12 / (m * (H2 + L2)), 12 / (m * (W2 + L2)), 12 / (m * (W2 + H2)));
	return *hull;
}

V3 Vehicle::iinv(V3 v) const {
	const V3& inv = hull->inv;
	const Quat& q = tb->q;
	v = q.inverse().rotate(v);
	v.set(v.x * inv.x, v.y * inv.y, v.z * inv.z);
	return q.rotate(v);
}
void Vehicle::impulse(const V3& J, const V3& rr) {
	vel.addScaled(J, 1 / mass);
	tb->w += iinv(rr.cross(J));
}

void Vehicle::startTumble(const V3* w, double addVy) {
	if (!canTumble()) return;
	const Hull& hl = hullPoints();
	if (!tb) {
		tb = std::make_unique<Tumble>();
		tb->q = Quat::FromEuler(-groundPitch, yaw, groundRoll, "YXZ");
		tb->w = V3(0, r, 0);
		tb->rPrev = r;
		vel.y = airborne ? vy : Max(0, groundVy);
		airborne = true;
		bodyPitch = bodyRoll = bodyY = 0;
		bodyPitchV = bodyRollV = bodyYV = 0;
		tb->cg = pos + tb->q.rotate(V3(0, hl.cgH, 0));
	}
	if (w) tb->w += *w;
	vel.y += addVy;
	tb->rest = 0; tb->settled = false;
	if (!def.bike.empty() && driver()) throwRiders(9);
}

void Vehicle::endTumble() {
	const V3 f = tb->q.rotate(V3(0, 0, 1));
	yaw = std::atan2(f.x, f.z);
	r = tb->w.y;
	vy = 0; vel.y = 0;
	airborne = false;
	groundPitch = 0; groundRoll = 0;
	pos.y = game.collision->surfaceHeight(pos.x, pos.z, pos.y + 1.2);
	lastGroundY = pos.y;
	tb.reset();
	flipped = false;
}

void Vehicle::tumble(double dt) {
	Tumble& T = *tb;
	const Hull& hl = hullPoints();
	CollisionWorld& col = *game.collision;
	const double g = G * game.gravity;
	T.cg = pos + T.q.rotate(V3(0, hl.cgH, 0));
	T.w.y += r - T.rPrev;
	const int steps = 4;
	const double h = dt / steps;
	int touching = 0, wheels = 0;
	double maxImpact = 0, scrape = 0;
	bool hasHit = false; V3 hitAt;
	for (int s = 0; s < steps; s++) {
		vel.y -= g * h;
		T.cg.addScaled(vel, h);
		const double wl = T.w.length();
		if (wl > 1e-6) { T.q = (Quat::FromAxisAngle(T.w / wl, wl * h) * T.q).normalize(); }
		T.w *= 1 - 0.08 * h;
		touching = 0; wheels = 0;
		double push = 0; V3 pn;
		for (const HullPt& hp : hl.pts) {
			const V3 rr = T.q.rotate(hp.p);
			const V3 p = T.cg + rr;
			const double gy = col.surfaceHeight(p.x, p.z, p.y + 0.6);
			const double pen = gy - p.y;
			if (pen <= 0) continue;
			touching++;
			if (hp.wheel) wheels++;
			const double e = 0.6;
			const double ghx = col.surfaceHeight(p.x + e, p.z, p.y + 1) - col.surfaceHeight(p.x - e, p.z, p.y + 1);
			const double ghz = col.surfaceHeight(p.x, p.z + e, p.y + 1) - col.surfaceHeight(p.x, p.z - e, p.y + 1);
			V3 n(-ghx / (2 * e), 1, -ghz / (2 * e));
			if (std::fabs(n.x) > 1.5 || std::fabs(n.z) > 1.5) n.set(0, 1, 0);
			n.normalize();
			if (pen > push) { push = pen; pn = n; }
			const V3 vp = T.w.cross(rr) + vel;
			const double vn = vp.dot(n);
			if (vn >= 0) continue;
			auto denom = [&](const V3& rv) { return 1 / mass + iinv(rr.cross(rv)).cross(rr).dot(rv); };
			const double kn = denom(n);
			const double eRest = hp.wheel ? 0.05 : -vn > 2 ? 0.22 : 0;
			const double jn = -(1 + eRest) * vn / kn;
			impulse(n * jn, rr);
			if (-vn > maxImpact) { maxImpact = -vn; hitAt = p; hasHit = true; }
			V3 vp2 = T.w.cross(rr) + vel;
			const V3 vt = vp2 - n * vp2.dot(n);
			const double vtl = vt.length();
			if (vtl < 1e-4) continue;
			if (hp.wheel) {
				V3 ax = T.q.rotate(V3(1, 0, 0)); ax = (ax - n * ax.dot(n)).normalized();
				V3 fw = T.q.rotate(V3(0, 0, 1)); fw = (fw - n * fw.dot(n)).normalized();
				const double vs = vt.dot(ax), vf = vt.dot(fw);
				const double js = Clamp(-vs / denom(ax), -0.9 * jn, 0.9 * jn);
				impulse(ax * js, rr);
				const double jf = Clamp(-vf / denom(fw), -0.03 * jn, 0.03 * jn);
				impulse(fw * jf, rr);
			} else {
				const V3 tdir = vt * (-1 / vtl);
				const double jt = Min(vtl / denom(tdir), 0.5 * jn);
				impulse(tdir * jt, rr);
				if (vtl > 3) scrape = Max(scrape, vtl);
			}
		}
		if (push > 0) T.cg.addScaled(pn, push * 0.7);
	}
	pos = T.cg - T.q.rotate(V3(0, hl.cgH, 0));
	{ const V3 f = T.q.rotate(V3(0, 0, 1)); if (Hypot(f.x, f.z) > 0.15) yaw = std::atan2(f.x, f.z); }
	r = T.w.y; T.rPrev = r;
	vy = vel.y;
	if (maxImpact > 3.5 && hasHit) {
		damage((maxImpact - 3.5) * 16);
		dent(hitAt.x, hitAt.y, hitAt.z, maxImpact * 2.2);
		if (game.time - lastHit > 0.2) {
			lastHit = game.time;
			game.soundAt("crash", hitAt, Clamp(maxImpact / 12, 0.25, 1));
			if (game.effects) { game.effects->sparks(hitAt, maxImpact); game.effects->dust(hitAt, 1.2); }
			if (DrvIsPlayer(this)) game.rig.addShake(Min(0.9, maxImpact / 14));
		}
		for (auto& o : occupants) if (o && !o->isPlayer && maxImpact > 7) { DamageInfo di; di.type = "vehicle"; o->takeDamage((maxImpact - 7) * 3, di); }
		if (DrvIsPlayer(this) && maxImpact > 9) { DamageInfo di; di.type = "fall"; driver()->takeDamage((maxImpact - 9) * 1.5, di); }
		crashParts(hitAt, maxImpact);
	}
	if (!tb) return; // (a crash can end it: occupants dying, the car exploding)
	T.scrapeT -= dt;
	if (scrape > 3 && T.scrapeT <= 0) {
		T.scrapeT = 0.12;
		const V3 p = localToWorld(0, 0.1, 0);
		if (game.effects) game.effects->sparks(p, Min(10, scrape));
		if (Rand() < 0.3) game.soundAt("metalhit", p, 0.35);
	}
	const double px = pos.x, pz = pos.z;
	game.collision->obbContacts(pos.x, pos.z, yaw, hx, hz, pos.y + 0.3, contacts);
	for (const Contact& ct : std::vector<Contact>(contacts)) resolveStatic(ct);
	if (!tb) return;
	T.cg.x += pos.x - px; T.cg.z += pos.z - pz;
	T.w.y += r - T.rPrev; T.rPrev = r;
	const V3 up = T.q.rotate(V3(0, 1, 0));
	const double spd = vel.length(), spin = T.w.length();
	flipped = up.y < 0.5;
	if (up.y > 0.8 && wheels >= 3 && spin < 2.4 && std::fabs(vel.y) < 3) {
		if (++T.landed > 2) { endTumble(); common(dt); return; }
	} else T.landed = 0;
	if (touching && spd < 0.6 && spin < 0.6) T.rest += dt; else T.rest = 0;
	if (T.rest > 0.5 && !T.settled) T.settled = true;
	if (T.settled) {
		vel *= 0.5; T.w *= 0.5;
		if (DrvIsPlayer(this) && !T.hinted) {
			T.hinted = true;
			if (game.hud) game.hud->help(up.y > -0.6 ? "On its side: <b>A / D</b> to rock it back onto its wheels, or <b>F</b> to climb out." : "Upside down! Get out (<b>F</b>) before it catches fire.", 5);
		}
	}
	T.rockT -= dt;
	Character* drv = driver();
	if (T.settled && drv && drv->isPlayer && std::fabs(input.steer) > 0.5 && T.rockT <= 0 && up.y > -0.6) {
		T.rockT = 0.9;
		const V3 f = T.q.rotate(V3(0, 0, 1));
		T.w.addScaled(f, -Sgn(input.steer) * (!def.bike.empty() ? 1.5 : 3.2));
		vel.y += 1.6;
		T.settled = false; T.rest = 0;
	}
	if (T.settled && flipped && def.bike.empty() && !isWrecked()) {
		T.flipT += dt;
		if (up.y < -0.2 && T.flipT > 6 && health > 0) health = 0;
		T.bailT += dt;
		if (T.bailT > 1.5) for (auto& o : occupants) if (o && !o->isPlayer && !o->dead && !o->remote && !game.vehicles.isBusy(o.get())) { auto keep = o; game.vehicles.exit(keep.get()); keep->bailFlee = this; }
	} else T.flipT = Max(0, T.flipT - dt);
	common(dt);
}

void Vehicle::common(double dt) {
	if (!sunk && WATER_Y - game.map.GroundHeight(pos.x, pos.z) > 1.0 && pos.y < WATER_Y - 0.3) {
		sunk = true;
		onSunk();
		game.events.vehicleSunk.emit(this);
		if (game.effects) game.effects->splash(pos, 3);
		if (tb) endTumble();
	}
	if (!exploded && !sunk && health <= 0) {
		onFire = true;
		burnTime += dt;
		if (burnTime > 4.5) explode();
	}
	if (!tb) return;
	skid = 0;
	wheelRot += tb->settled ? (flipped && driver() ? input.throttle * 12 * dt : 0) : 0;
}

void Vehicle::rolloverCheck(double dt) {
	if (!canTumble() || airborne || !def.bike.empty()) return;
	const double cgH = layout.hasHull ? layout.hull.cgH : def.H * 0.4;
	const double ssf = def.track / (2 * cgH) * (stable ? 1.6 : 1);
	const double lat = std::fabs(ayLat) / G;
	rollT = lat > ssf * 1.02 && speedAbs() > 8 ? rollT + dt : 0;
	const bool steep = std::fabs(groundRoll) > 0.62 && speedAbs() > 3;
	if (rollT > 0.18 || steep) {
		rollT = 0;
		const V3 rgt(-std::cos(yaw), 0, std::sin(yaw));
		const double dir = steep ? -Sgn(groundRoll) : -Sgn(ayLat);
		const V3 d2 = rgt * dir;
		const V3 w = UPV.cross(d2) * (2.6 + Rand());
		startTumble(&w, 1.2);
	}
}

void Vehicle::blast(const V3&, double k, const V3& dir) {
	if (!canTumble() || k < 0.15) {
		vel.addScaled(dir, k * 9 * 1500 / mass);
		if (k > 0.4) { airborne = true; vy = Max(vy, k * 7); }
		return;
	}
	const double lift = k * 9 * Min(1.6, 1500 / mass);
	startTumble(nullptr, 0);
	vel.addScaled(dir, k * 8 * Min(1.6, 1500 / mass));
	vel.y = Max(vel.y, lift);
	tb->w += UPV.cross(dir) * (k * (3 + Rand() * 3));
	tb->w += V3(Rand() - 0.5, Rand() - 0.5, Rand() - 0.5) * (k * 3);
}

void Vehicle::crashTumble(const V3& n, double impact, double k, bool wall) {
	if (!canTumble()) return;
	const double eff = impact * k;
	const V3 f = fwd();
	const double side = std::fabs(n.x * f.z - n.z * f.x);
	const V3 d = V3(n.x, 0, n.z).normalized();
	if (!def.bike.empty()) { if (eff > 5) { const V3 w = UPV.cross(d) * (eff * 0.35); startTumble(&w, eff * 0.12); } return; }
	const double tall = Clamp((layout.hasHull ? layout.hull.cgH : 0.6) / 0.62, 0.8, 1.8);
	if (side > 0.6) {
		const double p = Clamp((eff * tall - 12) / 12, 0, 0.9);
		if (Rand() < p) { const V3 w = UPV.cross(d) * ((wall ? -1 : 1) * Clamp((eff * tall - 9) * 0.32, 1.5, 7.5)); startTumble(&w, Clamp(eff * 0.14, 1, 4)); }
	} else if (eff > 17) {
		const double p = Clamp((eff - 17) / 14, 0, 0.7);
		if (Rand() < p) { const V3 w = UPV.cross(d) * (-Clamp((eff - 14) * 0.18, 1, 5)); startTumble(&w, Clamp(eff * 0.12, 1, 4)); }
	}
}

void Vehicle::crashParts(const V3& at, double impact) {
	if (!model || !def.bike.empty()) return;
	double lx, lz; worldToLocal(at.x, at.z, lx, lz);
	const bool front = lz > hz * 0.55, rear = lz < -hz * 0.55, side = std::fabs(lx) > hx * 0.7;
	const double rr = Rand();
	if (impact > 7 && model->hasHood && front && !hoodOpen && !detached.count("hood") && rr < 0.5) { hoodOpen = true; hoodAngle = -(0.15 + Rand() * 0.35); }
	if (impact > 7 && model->hasTrunk && rear && !trunkOpen && rr < 0.4) { trunkOpen = true; trunkAngle = 0.15 + Rand() * 0.3; }
	if (impact > 11 && front && rr < 0.45) detachPart("bumperF", at);
	if (impact > 11 && rear && rr < 0.45) detachPart("bumperR", at);
	if (impact > 14 && front && model->hasHood && hoodOpen && rr < 0.3) detachPart("hood", at);
	if (impact > 14 && side && !front && !rear && rr < 0.35) detachPart(lx > 0 ? "door" : "door2", at);
	if (impact > 13 && !glassBroken && rr < 0.35) shatterGlass();
}

void Vehicle::detachPart(const std::string& name, const V3& at) {
	if (!model || detached.count(name)) return;
	bool exists = false;
	for (const VPart& p : model->parts) if (p.name == name) { exists = true; break; }
	if (!exists) return;
	if (name == "door") for (auto& o : occupants) if (o && game.vehicles.isBusy(o.get())) return;
	detached.insert(name);
	if (game.effects) {
		const V3 out = V3(at.x - pos.x, 0, at.z - pos.z).normalized();
		game.effects->panelDebris(this, name, V3(vel.x * 0.8 + out.x * 3, 2 + Rand() * 3, vel.z * 0.8 + out.z * 3));
	}
	game.soundAt("metalhit", at, 0.6);
}

void Vehicle::shatterGlass() {
	if (glassBroken || !model) return;
	glassBroken = true;
	if (game.effects) game.effects->glassBurst(localToWorld(0, def.H * 0.8, 0), def.W);
	game.soundAt("glass", pos, 0.8);
}

void Vehicle::step(double h) {
	const VehInput& inp = input;
	const double m = mass;
	const double s = std::sin(yaw), c = std::cos(yaw);
	const double fx = s, fz = c, rx = -c, rz = s;
	const double vx = vel.x, vz = vel.z;
	const double vLong = vx * fx + vz * fz;
	const double vLat = vx * rx + vz * rz;
	const double wb = a + b;
	const double rr = r;
	const double wet = game.env.wet;
	const double surf = surface ? surface : 1;
	const double mu = def.grip * (1 - wet * 0.18) * surf * (driver() && driver()->isPlayer ? game.specialGrip() : 1) * (flat ? 0.6 : 1);
	if (airborne) {
		vel.x -= vx * 0.02 * h; vel.z -= vz * 0.02 * h;
		r *= 1 - 0.3 * h;
		yaw += r * h;
		pos.x += vel.x * h; pos.z += vel.z * h;
		vy -= G * h * game.gravity;
		pos.y += vy * h;
		return;
	}
	const double hcg = 0.55;
	double Nf = m * G * b / wb - m * axLong * hcg / wb;
	double Nr = m * G * a / wb + m * axLong * hcg / wb;
	Nf = Max(Nf, m * G * 0.15); Nr = Max(Nr, m * G * 0.15);
	const double dlt = steerAngle;
	const double cd = std::cos(dlt), sd = std::sin(dlt);
	const double vFlat = vLat - rr * a;
	const double fLong = vLong * cd - vFlat * sd;
	const double fLat = vFlat * cd + vLong * sd;
	const double alphaF = std::atan2(fLat, Max(std::fabs(fLong), 1.6));
	const double vRlat = vLat + rr * b;
	const double alphaR = std::atan2(vRlat, Max(std::fabs(vLong), 1.6));
	const double peak = 0.11;
	const double thr = exploded ? 0 : inp.throttle;
	const double brk = inp.brake;
	double driveF = 0;
	if (thr > 0 && vLong > -0.8) { const double k = Clamp(vLong / (flat ? def.top * 0.45 : def.top), 0, 1); driveF = thr * def.force * (flat ? 0.6 : 1) * (1 - 0.92 * k * k); }
	if (brk > 0 && vLong < 0.8 && vLong > -12) driveF = -brk * def.force * 0.55;
	double brakeF = 0;
	if (brk > 0 && vLong > 0.8) brakeF = brk * def.brake;
	if (thr > 0 && vLong < -0.8) brakeF = thr * def.brake * 0.8;
	double rearDrive = 0, frontDrive = 0;
	if (def.drive == EDrive::RWD) rearDrive = driveF;
	else if (def.drive == EDrive::FWD) frontDrive = driveF;
	else { rearDrive = driveF * 0.6; frontDrive = driveF * 0.4; }
	const double bSign = std::fabs(vLong) > 0.3 ? Sign(vLong) : 0;
	double FxF = frontDrive - bSign * brakeF * 0.6;
	double FxR = rearDrive - bSign * brakeF * 0.4;
	const double muR = mu * rearGrip;
	if (inp.handbrake) {
		FxR = -bSign * mu * Nr * 0.75;
		if (std::fabs(vLong) < 0.5) vel *= 1 - 3 * h;
	}
	double FyF = -mu * Nf * SatF(alphaF / peak);
	double FyR = -muR * Nr * SatF(alphaR / (peak * (inp.handbrake ? 1.4 : 1)));
	const double capF = mu * Nf, capR = muR * Nr;
	const double mF = Hypot(FxF, FyF);
	if (mF > capF) { FxF *= capF / mF; FyF *= capF / mF; }
	const double mR = Hypot(FxR, FyR);
	wheelspin = 0;
	if (mR > capR) {
		const double k = capR / mR;
		if (std::fabs(rearDrive) > capR * 0.9) wheelspin = Clamp((std::fabs(rearDrive) - capR * 0.9) / capR, 0, 1);
		FxR *= k; FyR *= k;
	}
	const double wfx = fx * cd - rx * sd, wfz = fz * cd - rz * sd;
	const double wrx = rx * cd + fx * sd, wrz = rz * cd + fz * sd;
	double Fx = FxF * wfx + FyF * wrx + FxR * fx + FyR * rx;
	double Fz = FxF * wfz + FyF * wrz + FxR * fz + FyR * rz;
	const double spd = Hypot(vx, vz);
	const double cdrag = 0.08 * def.force / (def.top * def.top);
	Fx -= vx * spd * cdrag + vx * 18 * (m / 1500);
	Fz -= vz * spd * cdrag + vz * 18 * (m / 1500);
	if (std::fabs(vLong) < 3 && !inp.handbrake) {
		const double k = (1 - std::fabs(vLong) / 3) * m * 4;
		Fx -= vLat * rx * k; Fz -= vLat * rz * k;
	}
	auto cross2 = [](double ux, double uz, double wx, double wz) { return uz * wx - ux * wz; };
	double tau = cross2(fx * a, fz * a, FxF * wfx + FyF * wrx, FxF * wfz + FyF * wrz) + cross2(-fx * b, -fz * b, FxR * fx + FyR * rx, FxR * fz + FyR * rz);
	tau -= rr * I * (spd < 4 ? 3.5 : 0.4);
	const double ax = Fx / m, az = Fz / m;
	vel.x += ax * h; vel.z += az * h;
	r += tau / I * h;
	axLong = Damp(axLong, ax * fx + az * fz, 12, h);
	ayLat = Damp(ayLat, ax * rx + az * rz, 12, h);
	if (thr == 0 && brk == 0 && Hypot(vel.x, vel.z) < 0.25) { vel.x *= 0.8; vel.z *= 0.8; r *= 0.8; }
	yaw += r * h;
	pos.x += vel.x * h;
	pos.z += vel.z * h;
	slipRear = std::fabs(vRlat);
	slipFront = std::fabs(fLat);
}

void Vehicle::afterPhysics(double dt) {
	const double s = std::sin(yaw), c = std::cos(yaw);
	const double hw = def.track / 2, zf = def.wheelbase / 2, zr = -def.wheelbase / 2;
	CollisionWorld& col = *game.collision;
	const double yRef = pos.y + (airborne ? 0.6 : 1.3);
	auto gh = [&](double lx, double lz) { return col.surfaceHeight(pos.x + lx * c + lz * s, pos.z - lx * s + lz * c, yRef); };
	const double h0 = gh(hw, zf), h1 = gh(-hw, zf), h2 = gh(hw, zr), h3 = gh(-hw, zr);
	const double hF = (h0 + h1) / 2, hR = (h2 + h3) / 2, hL = (h0 + h2) / 2, hRt = (h1 + h3) / 2;
	const double target = (hF + hR) / 2;
	surface = game.map.IsOnRoad(pos.x, pos.z) || std::fabs(target - 0.15) < 0.01 ? 1 : 0.85;
	const bool fwdBlock = (hF - pos.y > 0.9 && speed() > 0) || (hR - pos.y > 0.9 && speed() < 0);
	if (fwdBlock && !airborne) {
		const double vl = speed();
		vel.x -= s * vl * 1.3; vel.z -= c * vl * 1.3;
		pos.x -= s * Sign(vl) * 0.1; pos.z -= c * Sign(vl) * 0.1;
		if (std::fabs(vl) > 8) damage(std::fabs(vl) * 8);
	}
	if (airborne) {
		if (pos.y <= target) {
			pos.y = target;
			const double impact = -vy;
			airborne = false;
			bodyYV -= impact * 0.35;
			if (impact > 9) { damage((impact - 9) * 25); game.soundAt("crash", pos, Min(1, impact / 20)); }
			vy = 0;
		}
	} else {
		const double dy = target - pos.y;
		if (dy < -0.4) {
			airborne = true;
			vy = Max(groundVy, -2);
		} else {
			if (std::fabs(dy) < 0.25) groundVy = Damp(groundVy, dy / Max(dt, 1e-3), 20, dt);
			else { groundVy = 0; bodyYV += dy > 0 ? 1.5 : -0.5; }
			pos.y = target;
		}
	}
	const double tgtPitch = std::atan2(hF - hR, def.wheelbase);
	const double tgtRoll = std::atan2(hL - hRt, def.track);
	if (!airborne) { groundPitch = Damp(groundPitch, tgtPitch, 12, dt); groundRoll = Damp(groundRoll, tgtRoll, 12, dt); }
	else groundPitch = Damp(groundPitch, Clamp(vy * 0.03, -0.4, 0.3), 1.5, dt);
	col.obbContacts(pos.x, pos.z, yaw, hx, hz, pos.y, contacts);
	for (const Contact& ct : std::vector<Contact>(contacts)) resolveStatic(ct);
	common(dt);
	if (sunk) {
		vel *= 1 - dt * 1.5;
		pos.y = Max(game.map.GroundHeight(pos.x, pos.z), pos.y - dt * 0.6);
		airborne = false;
	}
	wheelRot += speed() * dt / def.wheelR;
	skid = (slipRear > 3.2 || wheelspin > 0.2 || (input.handbrake && std::fabs(speed()) > 4) || (input.brake > 0.5 && speed() > 12)) && !airborne ? 1 : 0;
}

void Vehicle::resolveStatic(const Contact& ct) {
	CollObj* o = ct.obj;
	if (o && o->kind == CollObj::Circle && o->breakable && !o->broken && speedAbs() > 2.5) {
		game.breakProp(o);
		if (game.effects) game.effects->propDebris(o, vel);
		vel *= 0.9;
		damage(speedAbs() * 1.5);
		game.soundAt("metalhit", pos, 0.6);
		if (o->prop >= 0 && game.propType(o->prop) == "hydrant" && game.effects) game.effects->hydrantSpray(o->x, game.propY(o->prop), o->z);
		return;
	}
	pos.x += ct.nx * ct.depth;
	pos.z += ct.nz * ct.depth;
	const double px = ct.px - pos.x, pz = ct.pz - pos.z;
	const double vpx = vel.x + r * pz, vpz = vel.z - r * px;
	const double vn = vpx * ct.nx + vpz * ct.nz;
	if (vn >= 0) return;
	const double e = 0.18;
	const double rn = pz * ct.nx - px * ct.nz;
	const double j = -(1 + e) * vn / (1 / mass + rn * rn / I);
	vel.x += j * ct.nx / mass;
	vel.z += j * ct.nz / mass;
	r += rn * j / I;
	const double tx = -ct.nz, tz = ct.nx;
	const double vt = vpx * tx + vpz * tz;
	const double jt = Clamp(-vt * mass * 0.25, -j * 0.4, j * 0.4);
	vel.x += jt * tx / mass; vel.z += jt * tz / mass;
	const double impact = -vn;
	if (impact > 3) {
		damage((impact - 3) * 14);
		dent(ct.px, pos.y + 0.6, ct.pz, impact);
		if (impact > 9) crashParts(V3(ct.px, pos.y + 0.5, ct.pz), impact);
		if (impact > 15 && !tb) crashTumble(V3(ct.nx, 0, ct.nz), impact, 1, true);
		if (game.time - lastHit > 0.25) {
			lastHit = game.time;
			game.soundAt("crash", pos, Clamp(impact / 18, 0.2, 1));
			if (game.effects) game.effects->sparks(V3(ct.px, pos.y + 0.5, ct.pz), impact);
			if (DrvIsPlayer(this)) game.rig.addShake(Min(0.8, impact / 25));
		}
		onCrash(impact, o);
	}
}

void Vehicle::damage(double amount, Character* source) {
	if (exploded) return;
	if (game.cheatsOn.vehGod && DrvIsPlayer(this)) return;
	if (remote) return; // (another player's: their client decides)
	health -= amount;
	if (source) lastDamager = source;
	game.events.vehicleDamaged.emit(this, amount, source);
}

void Vehicle::dent(double wx, double wy, double wz, double strength) {
	if (strength < 5) return;
	if (game.cheatsOn.vehGod && DrvIsPlayer(this)) return;
	// (in the vehicle's group frame; the renderer pushes the panels' vertices in towards the middle)
	const V3 l = groupMatrix().inverse().apply(V3(wx, wy, wz));
	dents.push_back({ l, strength });
	dentVersion++;
}

void Vehicle::explode() {
	if (exploded) return;
	exploded = true;
	onFire = false;
	health = 0;
	if (canTumble() && !sunk) {
		const V3 w((Rand() - 0.5) * 5, (Rand() - 0.5) * 3, (Rand() - 0.5) * 5);
		startTumble(&w, 6 + Rand() * 3);
		shatterGlass();
	} else {
		vy = 6 + Rand() * 3;
		airborne = true;
		r += (Rand() - 0.5) * 3;
	}
	if (game.combat) game.combat->vehicleExplosion(this, pos + V3(0, def.H * 0.45, 0));
	for (auto o : occupants) if (o) { DamageInfo di; di.type = "explosion"; di.source = lastDamager.get(); o->takeDamage(1000, di); }
	game.events.vehicleExploded.emit(this);
	wreckTime = 0;
}

void Vehicle::updateVisual(double dt) {
	const double tp = Clamp(axLong * 0.008, -0.07, 0.07);
	const double tr = Clamp(-ayLat * 0.011, -0.09, 0.09);
	const double k = 90, cdamp = 11;
	bodyPitchV += ((tp - bodyPitch) * k - bodyPitchV * cdamp) * dt;
	bodyRollV += ((tr - bodyRoll) * k - bodyRollV * cdamp) * dt;
	bodyYV += ((-bodyY) * 120 - bodyYV * 9) * dt;
	bodyPitch += bodyPitchV * dt;
	bodyRoll += bodyRollV * dt;
	bodyY += bodyYV * dt;
	if (def.hydraulics) {
		hydraulicV += (-hydraulic * 30 - hydraulicV * 2.5) * dt;
		hydraulic += hydraulicV * dt;
	}
	const bool night = game.env.night > 0.4;
	lightsOn = (night || game.env.rain > 0.3) && driver() && !isWrecked();
	braking = driver() && ((input.brake > 0.1 && speed() > 0.5) || input.handbrake);
}

void Vehicle::remove() {
	if (removed) return;
	removed = true;
	for (auto& o : occupants) {
		if (!o || o->isPlayer) continue;
		if (o->remote && !o->npcProxy) { takeOut(o.get()); continue; }
		game.removeCharacter(o.get());
	}
}

} // namespace atg
