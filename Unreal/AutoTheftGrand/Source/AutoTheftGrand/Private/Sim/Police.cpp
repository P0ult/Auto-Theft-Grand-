#include "Police.h"
#include "Collision.h"
#include "Combat.h"
#include "Effects.h"
#include "Game.h"
#include "Peds.h"
#include "RoadNet.h"

namespace atg {

namespace {
const double THRESH[6] = { 0, 1, 3, 7, 14, 24 };
const std::vector<std::string> COP_LINES = { "Freeze!", "Police! Hands where I can see them!", "Stop right there!", "Suspect on foot!", "Get down on the ground!", "Drop it!" };
template <typename T> const T& Pick(const std::vector<T>& v) { return v[(size_t)std::floor(Rand() * v.size())]; }
}

Appearance CopAppearance(bool swat) {
	RNG rng((uint32_t)(int64_t)std::floor(Rand() * 1e9));
	// (the options object is evaluated first: its two chances come off the generator before the look)
	const bool female = rng.Chance(0.2);
	const bool glasses = rng.Chance(0.3);
	Appearance a = RandomAppearance(rng, female ? 1 : 0);
	a.shirtType = "long"; a.hairStyle = "buzz"; a.glasses = glasses; a.shorts = false; a.bandana = -1;
	a.hasUniform = true;
	a.uniformShirt = swat ? 0x1b1b1f : 0x1f2f55;
	a.uniformPants = swat ? 0x1b1b1f : 0x1a2440;
	a.uniformHat = swat ? 0x111111 : 0x141d33;
	a.shoes = 0x0a0a0a;
	return a;
}

// ------------------------------------------------------------------ PursuitDriver
PursuitDriver::PursuitDriver(Game& g, Vehicle* v, const LaneStart* st, Police* pol) : LaneDriver(g, v, nullptr, true), police(pol) {
	fixedCruise = true;
	cruise = 30;
	ignoreLights = true;
	direct = false;
	reverseT = 0;
	if (st) start(*st); else resnap();
}

bool PursuitDriver::chooseNext(const LanePath& cur, Exit& out) {
	const std::vector<Exit> opts = options(cur);
	if (opts.empty()) return false;
	const V3 t = police ? police->targetPos() : game.player->pos;
	Exit best = opts[0];
	double bd = kInf;
	for (const Exit& o : opts) {
		const REdge& e = net.edges[o.e];
		const RNode& far = net.nodes[o.dir == 0 ? e.b : e.a];
		const double d = Hypot(far.x - t.x, far.z - t.z) + Rand() * 20;
		if (d < bd) { bd = d; best = o; }
	}
	out = best;
	return true;
}

void PursuitDriver::update(double dt) {
	Vehicle* v = veh;
	if (!v->driver() || v->isWrecked()) return;
	const V3 tp = police->targetPos();
	const double d = Hypot(tp.x - v->pos.x, tp.z - v->pos.z);
	Player& pl = *game.player;
	const bool see = d < 80;
	direct = see || !game.map.IsOnRoad(v->pos.x, v->pos.z);
	if (!direct) {
		cruise = 26 + police->level * 2;
		if (wasDirect) { wasDirect = false; resnap(); }
		LaneDriver::update(dt);
		return;
	}
	wasDirect = true;
	VehInput& inp = v->input;
	// direct chase: aim at the predicted position
	const V3 tv = pl.vehicle ? pl.vehicle->vel : pl.vel;
	const double lead = Clamp(d / 30, 0, 1.2);
	const double tx = tp.x + tv.x * lead, tz = tp.z + tv.z * lead;
	double lx, lz; v->worldToLocal(tx, tz, lx, lz);
	// obstacle feelers
	auto probe = [&](double ang) {
		const double a = v->yaw + ang;
		RayHit h; RayOpts ro; ro.ignoreProps = true;
		return game.collision->raycast(v->pos.x, v->pos.y + 0.8, v->pos.z, std::sin(a), 0, std::cos(a), 14, h, ro) ? h.t : 14.0;
	};
	const double fL = probe(0.35), fR = probe(-0.35), fC = probe(0);
	double steer = std::atan2(lx, Max(0.5, lz)) / (v->def.steer * 0.7);
	if (fC < 9) steer += fL > fR ? 1.2 : -1.2;
	else if (fL < 6) steer -= 0.6; else if (fR < 6) steer += 0.6;
	inp.steer = Clamp(steer, -1, 1);
	const bool onFoot = !pl.vehicle;
	double want = onFoot ? (d > 20 ? 20 : Clamp(d - 10, 0, 10)) : 34 + police->level * 2;
	if (std::fabs(std::atan2(lx, lz)) > 1.4 && d < 30) want = Min(want, 9);
	const double speed = v->speed();
	// reversing out of stuck spots
	if (reverseT > 0) {
		reverseT -= dt;
		inp.throttle = 0; inp.brake = 1; inp.steer = -inp.steer; inp.handbrake = false;
		return;
	}
	if (std::fabs(speed) < 1 && want > 5) { stuck += dt; if (stuck > 1.5) { reverseT = 1.2; stuck = 0; } } else stuck = 0;
	const double err = want - speed;
	inp.throttle = err > 0 ? Clamp(err * 0.3, 0.2, 1) : 0;
	inp.brake = err < -2 ? Clamp(-err * 0.2, 0.2, 1) : 0;
	inp.handbrake = std::fabs(std::atan2(lx, lz)) > 1.2 && speed > 10;
}

// ------------------------------------------------------------------ Police
Police::Police(Game& g) : game(g) {
	auto& ev = g.events;
	ev.kill.on([this](Character* killer, Character* victim, const std::string&, const std::string&) {
		Ped* vp = dynamic_cast<Ped*>(victim);
		if (!killer || !killer->isPlayer || (vp && vp->criminal)) return; // (vigilante work: suspects are fair game)
		if (vp && vp->brain == "cop") crime(3, victim->pos, true);
		else if (vp && !vp->gang.empty()) crime(0.8, victim->pos, false);
		else crime(1.4, victim->pos, false);
	});
	ev.gunshot.on([this](Character* shooter, V3 pos, const std::string&) { if (shooter && shooter->isPlayer && !game.vigilanteActive) crime(0.18, pos, false, true); });
	ev.melee.on([this](Character* att, Character* vic, const std::string&) { if (att && att->isPlayer) { Ped* p = dynamic_cast<Ped*>(vic); const bool cop = p && p->brain == "cop"; crime(cop ? 2 : 0.35, vic->pos, cop); } });
	ev.pedHitByCar.on([this](Character* c, Vehicle* v, double) { if (v->driver() && v->driver()->isPlayer) { Ped* p = dynamic_cast<Ped*>(c); const bool cop = p && p->brain == "cop"; crime(cop ? 2 : 0.5, c->pos, cop); } });
	ev.carjack.on([this](Character* by, Character*, Vehicle* veh) { if (by && by->isPlayer) crime(veh->def.police ? 2 : 0.6, veh->pos, false); });
	ev.explosion.on([this](V3 pos, double, Character* src) { if (src && src->isPlayer) crime(1.2, pos, false); });
	ev.vehicleShot.on([this](Vehicle* v, Character* s, V3) { if (s && s->isPlayer && v->def.police) crime(1.5, v->pos, true); });
	ev.enteredVehicle.on([this](Character* c, Vehicle* v) { if (c->isPlayer && v->def.police && level < 1) crime(1, v->pos, true); });
	ev.carCrash.on([this](Vehicle* A, Vehicle* B, double impact) {
		Vehicle* pv = game.player->vehicle;
		if (pv && (A == pv || B == pv) && (A->def.police || B->def.police) && impact > 5) crime(0.8, pv->pos, true);
	});
}

V3 Police::targetPos() const {
	const Player& p = *game.player;
	if (seen || level == 0) return p.vehicle ? p.vehicle->pos : p.pos;
	return lastKnown;
}

// witnessed: forces at least one star
void Police::crime(double amount, const V3& pos, bool severe, bool noise) {
	if (game.cheatsOn.neverWanted) return;
	if (!enabled || game.player->dead) return;
	bool nearCop = false;
	const double r = noise ? 45 : 35;
	for (const auto& cr : cops) if (Ped* c = cr.get()) if (!c->dead && Dist2(c->pos.x, c->pos.z, pos.x, pos.z) < r * r) { nearCop = true; break; }
	const double witnessK = nearCop ? 1 : noise ? 0.35 : 0.6;
	heat += amount * witnessK * (severe ? 1.5 : 1);
	if ((nearCop || severe) && level == 0 && amount >= 0.3) heat = Max(heat, THRESH[1]);
	if (!noise || nearCop) { lastSeen = game.time; lastKnown = game.player->pos; }
	updateLevel();
}

// at least `l` stars right now (restricted areas), still capped by the current mission
void Police::raise(int l) {
	if (!enabled || game.player->dead) return;
	if (heat < THRESH[l]) heat = THRESH[l] + 0.01;
	const Player& p = *game.player;
	lastSeen = game.time;
	lastKnown = p.vehicle ? p.vehicle->pos : p.pos;
	updateLevel();
}

void Police::setLevel(int l) {
	level = (int)Clamp(l, 0, 5);
	heat = THRESH[level];
	if (level > 0) { lastSeen = game.time; lastKnown = game.player->pos; }
}

void Police::clear() {
	level = 0; heat = 0;
	for (auto& cr : cars) if (Vehicle* c = cr.get()) if (dynamic_cast<PursuitDriver*>(c->ai.get())) c->sirenOn = false;
	game.events.wantedCleared.emit();
}

void Police::updateLevel() {
	int l = 0;
	for (int i = 1; i <= 5; i++) if (heat >= THRESH[i]) l = i;
	if (IsSet(game.missionMaxWanted)) l = (int)Min(l, game.missionMaxWanted);
	if (l > level) { level = l; game.events.wantedUp.emit(l); }
}

Ped* Police::spawnCop(double x, double z, const PedOpts* extra) {
	const bool swat = level >= 4 && Rand() < 0.6;
	PedOpts o;
	if (extra) o = *extra;
	o.hasAppearance = true; o.appearance = CopAppearance(swat);
	o.brain = "cop"; o.team = "police"; o.health = swat ? 150 : 100; o.armor = swat ? 50 : 0; o.persistent = true;
	if (extra) { o.missionTag = extra->missionTag; if (extra->hasY) { o.hasY = true; o.y = extra->y; } if (extra->hasYaw) { o.hasYaw = true; o.yaw = extra->yaw; } }
	Ped* cop = game.peds->spawnPed(x, z, o);
	const std::string w = level >= 4 ? Pick(std::vector<std::string>{ "rifle", "smg", "shotgun" }) : level >= 3 ? Pick(std::vector<std::string>{ "pistol", "shotgun", "smg" }) : "pistol";
	cop->giveWeapon(w, 999); cop->equip(w);
	cop->accuracy = 0.35 + level * 0.08;
	cop->damageMul = 0.45 + level * 0.05;
	cops.push_back(Ref<Ped>(cop));
	return cop;
}

// near: somewhere other than the player to respond to (an NPC suspect); still within the player's range.
// opts (the army uses this too): type, crew(veh, seat) -> occupant, seats, list, siren, radius
Vehicle* Police::spawnCar(bool pursuit, const V3* near, const CarOpts& opts) {
	const V3 p = near ? *near : targetPos();
	const V3 pp = game.player->vehicle ? game.player->vehicle->pos : game.player->pos;
	const double r0 = opts.hasRadius ? opts.r0 : near ? 45 : 75, r1 = opts.hasRadius ? opts.r1 : near ? 150 : 230;
	for (int tries = 0; tries < 14; tries++) {
		Traffic::Sample smp;
		if (!Traffic::SampleLane(game, p.x, p.z, r0, r1, smp)) continue;
		const double x = smp.x, z = smp.z;
		const double d2 = Dist2(x, z, p.x, p.z), dp2 = Dist2(x, z, pp.x, pp.z);
		if (d2 < (r0 - 5) * (r0 - 5) || d2 > (r1 + 10) * (r1 + 10) || dp2 > 235 * 235) continue;
		if (game.peds->inView(x, z) && dp2 < 120 * 120 && tries < 10) continue;
		bool blocked = false;
		for (const auto& o : game.vehicles.list) if (Dist2(o->pos.x, o->pos.z, x, z) < 100 && std::fabs(o->pos.y - smp.y) < 4) { blocked = true; break; }
		if (blocked) continue;
		const RoadNet& net = game.map.roads;
		const Line pts = net.LanePath(net.edges[smp.start.e], smp.start.dir, smp.start.lane);
		const int k = (int)Min((double)pts.size() - 2, Max(0, std::floor(smp.s0 / 5)));
		const double yaw = std::atan2(pts[k + 1].x - pts[k].x, pts[k + 1].z - pts[k].z);
		SpawnOpts so; so.persistent = true;
		Vehicle* v = game.vehicles.spawn(opts.type.empty() ? "police" : opts.type, x, z, yaw, so);
		if (!v) return nullptr;
		const int n = opts.seats >= 0 ? opts.seats : (pursuit && level >= 2 ? 2 : 1);
		for (int q = 0; q < n; q++) {
			Character* who = opts.crew ? opts.crew(v, q) : spawnCop(x, z);
			v->putIn(who, q);
			if (Ped* pw = dynamic_cast<Ped*>(who)) pw->homeCar = Ref<Vehicle>(v);
		}
		if (pursuit) v->ai = std::make_shared<PursuitDriver>(game, v, &smp.start, this);
		else v->ai = std::make_shared<LaneDriver>(game, v, &smp.start);
		v->sirenOn = opts.siren >= 0 ? opts.siren != 0 : pursuit;
		if (!opts.list) v->policeUnit = true;
		v->vel.set(std::sin(yaw) * 12, 0, std::cos(yaw) * 12);
		(opts.list ? *opts.list : cars).push_back(Ref<Vehicle>(v));
		return v;
	}
	return nullptr;
}

// ------------------------------------------------------------------ the cop brain (called by Ped::think)
void Police::copThink(Ped* cop, double dt) {
	Player& pl = *game.player;
	cop->animState.cower = false; cop->animState.handsUp = false;
	cop->aiming = false;
	if (level == 0 || pl.dead) {
		// on a call to an NPC suspect (npccrime.js)
		if (cop->npcTask && !pl.dead && game.npcCrime && game.npcCrime->copThink(cop, dt)) return;
		// no wanted: return to the car or patrol (not while climbing in: that made every cop getting back
		// into a car a civilian)
		if (game.vehicles.isBusy(cop)) return;
		Vehicle* home = cop->homeCar.get();
		if (home && !home->isWrecked() && !home->removed && !cop->vehicle) {
			int seat = -1;
			for (int i = 0; i < 4; i++) if (!home->occupants[i]) { seat = i; break; }
			if (seat >= 0 && seat < 2) game.vehicles.enter(cop, home, seat, true);
			else cop->stop();
		} else if (!cop->vehicle) { cop->brain = "civilian"; cop->state = "wander"; cop->persistent = false; cop->node = -1; }
		return;
	}
	const V3 tp = pl.vehicle ? pl.vehicle->pos : pl.pos;
	const double d = Hypot(tp.x - cop->pos.x, tp.z - cop->pos.z);
	// the player drove off: get back in the car
	Vehicle* home = cop->homeCar.get();
	if (pl.vehicle && d > 35 && home && !home->isWrecked() && !game.vehicles.isBusy(cop)) {
		const double hd = Hypot(home->pos.x - cop->pos.x, home->pos.z - cop->pos.z);
		if (hd < 40) {
			int seat = -1;
			for (int i = 0; i < 4; i++) if (!home->occupants[i]) { seat = i; break; }
			if (seat >= 0) { game.vehicles.enter(cop, home, seat, true); return; }
		}
	}
	const bool lethal = level >= 2 || cop->threat.get() == &pl;
	const bool hasLOS = d < (cop->hasHoldPos ? 85 : 60) && game.collision->lineOfSight(cop->pos.x, cop->pos.y + 1.6, cop->pos.z, tp.x, tp.y + 1.2, tp.z);
	if (hasLOS) { seen = true; lastSeen = game.time; lastKnown = tp; }
	if (!IsSet(cop->lineT)) cop->lineT = Rand(0, 3);
	cop->lineT -= dt;
	if (cop->lineT <= 0 && hasLOS && d < 30) { cop->lineT = Rand(6, 12); cop->say(Pick(COP_LINES)); }
	if (lethal && hasLOS && d < (cop->hasHoldPos ? 75 : 45) && cop->weaponDef().type == "gun") {
		cop->threat = Ref<Character>(&pl);
		cop->attack(dt);
		return;
	}
	// a roadblock: hold the line behind the cars until the suspect is on foot and close
	if (cop->hasHoldPos && (pl.vehicle || d > 28)) {
		const V3 h = cop->holdPos;
		if (Hypot(h.x - cop->pos.x, h.z - cop->pos.z) > 1.2) cop->goTo(h.x, h.z, 3, dt, 1.0);
		else { cop->stop(); cop->faceTowards(tp.x, tp.z, dt, 6); }
		return;
	}
	// chase to arrest
	const V3 target = hasLOS ? tp : lastKnown;
	if (d > 1.1) cop->goTo(target.x, target.z, d > 6 ? 5.8 : 3, dt, 1.0);
	else { cop->stop(); cop->faceTowards(tp.x, tp.z, dt, 10); }
}

// ------------------------------------------------------------------ update
void Police::update(double dt) {
	Player& pl = *game.player;
	{ std::vector<Ref<Ped>> keep; for (auto& c : cops) if (c.get() && !c->removed) keep.push_back(c); cops = keep; }
	{ std::vector<Ref<Vehicle>> keep; for (auto& c : cars) if (c.get() && !c->removed) keep.push_back(c); cars = keep; }
	// visibility and evading
	if (level > 0) {
		const V3 tp = pl.vehicle ? pl.vehicle->pos : pl.pos;
		bool seenNow = false;
		for (auto& cr : cops) {
			Ped* c = cr.get();
			if (c->dead) continue;
			const V3 cp = c->vehicle ? c->vehicle->pos : c->pos;
			const double d2 = Dist2(cp.x, cp.z, tp.x, tp.z);
			if (d2 < 50 * 50 && (d2 < 12 * 12 || game.collision->lineOfSight(cp.x, cp.y + 1.5, cp.z, tp.x, tp.y + 1.2, tp.z))) { seenNow = true; break; }
		}
		if (heli && !heli->down && Dist2(heli->pos.x, heli->pos.z, tp.x, tp.z) < 70 * 70) seenNow = true;
		seen = seenNow;
		if (seenNow) { lastSeen = game.time; lastKnown = tp; }
		const double evadeTime = 10 + level * 5;
		const double unseen = game.time - lastSeen;
		flash = unseen > 2;
		if (unseen > evadeTime) clear();
		heat = Max(THRESH[level], heat - dt * 0.01);
	} else { flash = false; heat = Max(0, heat - dt * 0.05); }

	// spawning response units
	const int wantCars = level;
	int pursuitCars = 0;
	for (auto& cr : cars) if (Vehicle* c = cr.get()) if (dynamic_cast<PursuitDriver*>(c->ai.get()) && !c->isWrecked() && c->driver() && !c->driver()->dead) pursuitCars++;
	spawnTimer -= dt;
	if (level > 0 && pursuitCars < wantCars && spawnTimer <= 0) { spawnTimer = 4; spawnCar(true); }
	// foot cops at low levels nearby if none
	int footCops = 0;
	for (auto& cr : cops) if (Ped* c = cr.get()) if (!c->dead && !c->vehicle) footCops++;
	if (level >= 1 && footCops < 1 && !pl.vehicle && spawnTimer <= 0 && Rand() < 0.02) {
		const double a = Rand() * 6.28;
		const double x = pl.pos.x + std::cos(a) * 45, z = pl.pos.z + std::sin(a) * 45;
		if (game.map.BlockAt(x, z) && !game.peds->inView(x, z)) spawnCop(x, z);
	}
	// patrol cars when not wanted
	patrolTimer -= dt;
	if (level == 0 && patrolTimer <= 0) {
		patrolTimer = 20;
		int live = 0;
		for (auto& cr : cars) if (Vehicle* c = cr.get()) if (!c->isWrecked()) live++;
		if (live < 2 && Rand() < 0.6) spawnCar(false);
	}
	// update the drivers, and turn patrols into pursuers when you are wanted
	const std::vector<Ref<Vehicle>> carList = cars;
	for (auto& cr : carList) {
		Vehicle* v = cr.get();
		if (!v) continue;
		if (!v->driver() || v->driver()->dead || v->driver()->isPlayer) { v->sirenOn = false; continue; }
		const bool isPursuit = dynamic_cast<PursuitDriver*>(v->ai.get()) != nullptr;
		if (level > 0 && !isPursuit) { v->ai = std::make_shared<PursuitDriver>(game, v, nullptr, this); v->sirenOn = true; }
		if (level == 0 && isPursuit) { v->ai = std::make_shared<LaneDriver>(game, v, nullptr); v->sirenOn = false; }
		if (v->ai) v->ai->update(dt);
		// cops bail out when the player is on foot nearby, or the car is stuck close by
		const V3 tp = pl.vehicle ? pl.vehicle->pos : pl.pos;
		const double d = Hypot(tp.x - v->pos.x, tp.z - v->pos.z);
		if (level > 0 && ((!pl.vehicle && d < 22) || (pl.vehicle && d < 12 && pl.vehicle->speedAbs() < 3)) && v->speedAbs() < 4) {
			std::vector<Character*> occ;
			for (auto& o : v->occupants) if (o) occ.push_back(o.get());
			for (Character* o : occ) if (!game.vehicles.isBusy(o)) game.vehicles.exit(o);
		}
	}
	// despawn far units
	const V3 pp = pl.vehicle ? pl.vehicle->pos : pl.pos;
	for (auto& cr : carList) {
		Vehicle* v = cr.get();
		if (!v || v->removed) continue;
		const double d2 = Dist2(v->pos.x, v->pos.z, pp.x, pp.z);
		if (d2 > 260 * 260 || (v->isWrecked() && d2 > 120 * 120)) {
			for (auto& o : v->occupants) if (o && !o->isPlayer) { auto keep = o; game.peds->remove(keep.get()); }
			for (auto& o : v->occupants) o.reset();
			game.vehicles.remove(v);
		}
	}
	const std::vector<Ref<Ped>> copList = cops;
	for (auto& cr : copList) {
		Ped* c = cr.get();
		if (!c || c->vehicle) continue;
		const double d2 = Dist2(c->pos.x, c->pos.z, pp.x, pp.z);
		if (d2 > 200 * 200 || (c->dead && d2 > 60 * 60)) game.peds->remove(c);
	}
	// arrest
	if (level > 0 && !pl.dead && !game.missionNoBust) {
		bool close = false;
		for (auto& cr : cops) {
			Ped* c = cr.get();
			if (!c || c->dead || c->vehicle || c->ragdolling) continue;
			const double d = Hypot(c->pos.x - pl.pos.x, c->pos.z - pl.pos.z);
			if (!pl.vehicle && d < 1.5 && Hypot(pl.vel.x, pl.vel.z) < 3.5) close = true;
			if (pl.vehicle && pl.vehicle->speedAbs() < 1.5) { const V3 dp = pl.vehicle->doorWorld(); if (Hypot(c->pos.x - dp.x, c->pos.z - dp.z) < 1.6) close = true; }
		}
		arrestTimer = close ? arrestTimer + dt : Max(0, arrestTimer - dt * 2);
		if (arrestTimer > (pl.vehicle ? 2.2 : 1.4)) { arrestTimer = 0; game.events.busted.emit(); }
	}
	// the helicopter
	if (level >= 3 && !heli && !pl.dead) {
		heli = std::make_unique<PoliceHeli>();
		const V3 p = pl.pos;
		const double a = Rand() * 6.28;
		heli->pos = V3(p.x + std::cos(a) * 180, p.y + 60, p.z + std::sin(a) * 180);
	}
	if (heli) {
		updateHeli(dt);
		if (heli->done) heli.reset();
	}
}

void Police::reset() {
	if (game.army) game.army->reset();
	if (game.roadblocks) game.roadblocks->reset();
	clear();
	for (auto& cr : cars) if (Vehicle* v = cr.get()) {
		for (auto& o : v->occupants) if (o && !o->isPlayer) { auto keep = o; game.peds->remove(keep.get()); }
		for (auto& o : v->occupants) o.reset();
		game.vehicles.remove(v);
	}
	for (auto& cr : cops) if (Ped* c = cr.get()) game.peds->remove(c);
	cars.clear(); cops.clear();
	heli.reset();
}

// ------------------------------------------------------------------ the helicopter (police.js Helicopter)
void Police::updateHeli(double dt) {
	PoliceHeli& h = *heli;
	h.rotor += dt * 30;
	h.tailRotor += dt * 40;
	if (h.down) {
		h.vel.y -= 9.8 * dt;
		h.pos = h.pos + h.vel * dt;
		h.spin += dt * 3;
		if (Rand() < 0.6 && game.effects) game.effects->fire(h.pos, 1.2);
		if (h.pos.y <= game.map.GroundHeight(h.pos.x, h.pos.z) + 1) {
			if (Combat* cb = dynamic_cast<Combat*>(game.combat)) cb->explosion(h.pos, 10, 250, game.player.get());
			h.done = true;
		}
		return;
	}
	const bool hasTp = level > 0;
	const V3 tp = hasTp ? targetPos() : V3();
	V3 target;
	if (!hasTp || level < 3) {
		// leave
		target = h.pos + V3(0, 0, -200);
		target.y = 120;
		if (!IsSet(h.leaveT)) h.leaveT = 0;
		h.leaveT += dt;
		if (h.leaveT > 12) { h.done = true; return; }
	} else {
		const double t = game.time * 0.25;
		target = V3(tp.x + std::cos(t) * 28, game.map.GroundHeight(tp.x, tp.z) + 32, tp.z + std::sin(t) * 28);
	}
	const double dx = target.x - h.pos.x, dy = target.y - h.pos.y, dz = target.z - h.pos.z;
	h.vel.x += (dx * 0.6 - h.vel.x) * dt * 0.8;
	h.vel.y += (dy * 0.8 - h.vel.y) * dt * 1.2;
	h.vel.z += (dz * 0.6 - h.vel.z) * dt * 0.8;
	const double sp = Hypot(h.vel.x, h.vel.z);
	if (sp > 30) { h.vel.x *= 30 / sp; h.vel.z *= 30 / sp; }
	h.pos = h.pos + h.vel * dt;
	// face the player
	if (hasTp) h.yaw += WrapAngle(std::atan2(tp.x - h.pos.x, tp.z - h.pos.z) - h.yaw) * Min(1, dt * 1.5);
	h.pitch = Clamp(sp * 0.012, 0, 0.3);
	// the searchlight at night
	const double night = game.env.night;
	if (hasTp) {
		h.lightAt = tp;
		h.coneOpacity = 0.05 * night;
		h.coneOn = night > 0.2;
		h.spotIntensity = night * 400;
	}
	// a sniper at four stars and up
	h.fireT -= dt;
	if (hasTp && level >= 4 && h.fireT <= 0) {
		h.fireT = 0.9;
		const V3 from = h.pos + V3(0, -1.2, 0);
		const V3 to(tp.x + Rand(-2.5, 2.5), tp.y + 1, tp.z + Rand(-2.5, 2.5));
		const V3 dir = (to - from).normalized();
		if (Combat* cb = dynamic_cast<Combat*>(game.combat)) {
			CombatHit hit;
			const bool hh = cb->raycast(from.x, from.y, from.z, dir.x, dir.y, dir.z, 120, nullptr, hit);
			if (game.effects) game.effects->tracer(from, hh ? hit.point : from + dir * 120);
			if (hh) { WeaponDef def = *FindWeapon("smg"); def.damage = 10; cb->applyHit(hit, def, nullptr, dir, 0.9); }
		}
		game.soundAt("smg", from, 0.7);
	}
}

void Police::radarCones(std::vector<RadarCone>& out) const {
	for (const auto& cr : cops) {
		Ped* c = cr.get();
		if (!c || c->dead) continue;
		Vehicle* v = c->vehicle;
		out.push_back({ v ? v->pos.x : c->pos.x, v ? v->pos.z : c->pos.z, v ? v->yaw : c->yaw, v ? 50.0 : 32.0, 0.5 });
	}
	if (heli && !heli->down) out.push_back({ heli->pos.x, heli->pos.z, 0, 70, kPi });
}

void Police::radarCops(std::vector<V3>& out) const {
	for (const auto& cr : cops) if (Ped* c = cr.get()) if (!c->dead) out.push_back(c->vehicle ? c->vehicle->pos : c->pos);
}

bool Police::heliRay(double ox, double oy, double oz, double dx, double dy, double dz, double maxT, double& t) const {
	if (!heli || heli->done || heli->down) return false;
	// utils.js raySphere (radius 2.6)
	const V3& c = heli->pos;
	const double lx = ox - c.x, ly = oy - c.y, lz = oz - c.z;
	const double b = lx * dx + ly * dy + lz * dz;
	const double cc = lx * lx + ly * ly + lz * lz - 2.6 * 2.6;
	const double h = b * b - cc;
	if (h < 0) return false;
	double tt = -b - std::sqrt(h);
	tt = tt >= 0 ? tt : (cc < 0 ? 0 : -1);
	if (tt < 0 || tt >= maxT) return false;
	t = tt;
	return true;
}

void Police::heliHit(double dmg) {
	if (!heli) return;
	heli->health -= dmg;
	if (heli->health <= 0 && !heli->down) { heli->down = true; heli->vel.y = 0; game.events.heliDown.emit(); }
}

bool Police::heliAlive(V3& pos) const { if (!heli || heli->down || heli->done) return false; pos = heli->pos; return true; }

} // namespace atg
