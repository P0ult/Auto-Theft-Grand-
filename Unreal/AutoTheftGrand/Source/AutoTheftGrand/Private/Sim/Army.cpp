#include "Army.h"
#include "Aircraft.h"
#include "Collision.h"
#include "Game.h"
#include "Gameplay.h"
#include "Military.h"
#include "Peds.h"
#include "Police.h"
#include "Skeleton.h"
#include "Traffic.h"
#include "Vehicles.h"

namespace atg {

namespace {
const std::vector<std::string> RADIO = {
	"All units: the National Guard has been authorised. Military assets inbound.",
	"Command to all callsigns: suspect is armed and extremely dangerous. Weapons free.",
	"Armour rolling out of Fort Carver. Clear the streets.",
	"Gunship on station. Painting the target.",
};
}

Army::Army(Game& g) : game(g) {
	g.radarSources.push_back([this](std::vector<Blip>& out) { blipList(out); });
}

V3 Army::targetPos() const { return game.policeSys->targetPos(); }

// the army at five stars: trucks, armour and the gunship (always on the radar's edge), troops as dots (hud.js)
void Army::blipList(std::vector<Blip>& out) const {
	const bool flash = (int)std::floor(game.time * 4) % 2 != 0;
	for (const Unit& u : units) {
		const Vehicle* v = u.veh.get();
		if (!v || v->isWrecked() || u.leaving) continue;
		Blip b; b.x = v->pos.x; b.z = v->pos.z; b.color = flash ? 0xff3333 : 0xb5d334;
		b.icon = u.kind == "heli" ? "heli" : u.kind == "tank" ? "tank" : "dot";
		b.small = u.kind != "heli" && u.kind != "tank";
		b.square = u.kind == "truck" || u.kind == "jeep";
		out.push_back(b);
	}
	for (const auto& r : troops) if (const Ped* s = r.get()) if (!s->dead) {
		Blip b; b.x = s->pos.x; b.z = s->pos.z; b.color = 0xb5d334; b.icon = "dot"; b.small = true; b.noEdge = true;
		out.push_back(b);
	}
}

void Army::update(double dt) {
	Police* pol = game.policeSys;
	Player& p = *game.player;
	if (!pol || (game.gameplay && game.gameplay->state == "menu")) return;
	const bool want = enabled && pol->level >= 5 && !p.dead && !game.missionNoArmy;
	if (want && !active) activate();
	else if (!want && active) standDown();
	{ std::vector<Ref<Ped>> keep; for (auto& r : troops) if (Ped* s = r.get()) if (!s->removed) keep.push_back(r); troops = keep; }
	{ std::vector<Unit> keep; for (Unit& u : units) if (Vehicle* v = u.veh.get()) if (!v->removed) keep.push_back(u); units = keep; }
	if (active) {
		time += dt;
		spawn(dt);
		radioT -= dt;
		if (radioT <= 0) { radioT = Rand(25, 40); if (game.hud) game.hud->dispatch(RandPick(RADIO)); }
	}
	for (Unit& u : units) {
		Vehicle* v = u.veh.get();
		if (!v || !v->driver() || v->driver()->dead || v->isWrecked()) continue;
		if (u.kind == "heli") fly(u, dt);
		else if (u.kind == "tank") tank(u, dt);
		else convoy(u, dt);
	}
	despawn();
}

void Army::activate() {
	active = true;
	time = 0;
	spawnT = 2;
	radioT = 6;
	game.peds->gangAggro["army"] = true;
	if (game.hud) {
		game.hud->bigMessage("THE ARMY IS COMING", "failed", 3.5, "Five stars \xc2\xb7 Fort Carver has been mobilised");
		game.hud->dispatch(RADIO[0]);
	}
	if (game.audio) game.audio->play("alarm", 0.8);
	game.events.armyDeployed.emit();
}

// below five stars: the units head home (they're tidied up once out of sight)
void Army::standDown() {
	active = false;
	game.peds->gangAggro["army"] = game.military && game.military->alerted;
	for (const auto& r : troops) if (Ped* s = r.get()) if (!s->dead && s->state == "attack" && !game.peds->gangAggro["army"]) { s->threat = nullptr; s->setState("guard"); }
	for (Unit& u : units) {
		u.leaving = true;
		Vehicle* v = u.veh.get();
		if (!v) continue;
		if (u.kind != "heli" && v->driver() && !v->driver()->isPlayer && !v->isWrecked()) {
			v->ai = std::make_shared<LaneDriver>(game, v, nullptr);
			if (Tank* t = dynamic_cast<Tank*>(v)) t->hasAim = false;
		}
	}
}

void Army::reset() {
	for (Unit& u : units) {
		Vehicle* v = u.veh.get();
		if (!v) continue;
		bool player = false;
		for (auto& o : v->occupants) {
			if (!o) continue;
			if (o->isPlayer) { player = true; continue; }
			game.peds->remove(o.get());
			o.reset();
		}
		if (!player) game.vehicles.remove(v);
	}
	for (const auto& r : troops) if (Ped* s = r.get()) game.peds->remove(s);
	units.clear(); troops.clear();
	if (active) { active = false; game.peds->gangAggro["army"] = game.military && game.military->alerted; }
}

// ------------------------------------------------------------------ spawning
Ped* Army::soldier(double x, double z, const std::string& weapon, bool hasYaw, double yaw, bool hasY, double y) {
	RNG rng((uint32_t)(Rand() * 1e9));
	PedOpts o;
	o.hasAppearance = true; o.appearance = SoldierLook(rng);
	o.brain = "gang"; o.gang = "army"; o.state = "guard";
	static const std::vector<std::string> Guns = { "rifle", "rifle", "smg" };
	o.weapon = weapon.empty() ? RandPick(Guns) : weapon;
	o.health = 140; o.armor = 40; o.persistent = true;
	o.hasYaw = hasYaw; o.yaw = yaw; o.hasY = hasY; o.y = y;
	Ped* s = game.peds->spawnPed(x, z, o);
	if (!s) return nullptr;
	s->accuracy = 0.5;
	s->damageMul = 0.55;
	s->soldier = true;
	s->response = true; // (not the Fort Carver garrison: killing one doesn't lock the base down)
	return s;
}

Army::Unit* Army::ground(const std::string& type, int seats, const std::string& kind) {
	Police::CarOpts o;
	o.type = type; o.seats = seats; o.list = &scratch; o.siren = 0; o.hasRadius = true; o.r0 = 110; o.r1 = 230;
	o.crew = [this](Vehicle* v, int) -> Character* { return soldier(v->pos.x, v->pos.z); };
	Vehicle* v = game.policeSys->spawnCar(true, nullptr, o);
	scratch.clear();
	if (!v) return nullptr;
	v->armyUnit = true;
	v->stable = true;
	Unit u; u.veh = Ref<Vehicle>(v); u.kind = kind; u.unloaded = false; u.fireT = 3;
	units.push_back(u);
	return &units.back();
}

void Army::spawn(double dt) {
	spawnT -= dt;
	if (spawnT > 0) return;
	spawnT = 3;
	auto alive = [&](const char* k) {
		int n = 0;
		for (const Unit& u : units) { const Vehicle* v = u.veh.get(); if (v && u.kind == k && !u.leaving && !v->isWrecked() && v->driver() && !v->driver()->dead) n++; }
		return n;
	};
	auto any = [&](const char* k) { for (const Unit& u : units) { const Vehicle* v = u.veh.get(); if (v && u.kind == k && !v->isWrecked()) return true; } return false; };
	// troop trucks and jeeps (on the roads: they need a lane to spawn on)
	if (alive("truck") + alive("jeep") < 2) { if (alive("truck") <= alive("jeep")) ground("barracks", 2, "truck"); else ground("ranger", 3, "jeep"); return; }
	// the gunship after a few seconds
	if (time > 8 && alive("heli") < 1 && !any("heli")) { spawnHeli(); return; }
	// and then the armour
	if (time > 18 && alive("tank") < 1 && !any("tank")) ground("mammoth", 1, "tank");
}

void Army::spawnHeli() {
	const V3 tp = targetPos();
	const double a = Rand() * kPi * 2;
	const double x = tp.x + std::cos(a) * 240, z = tp.z + std::sin(a) * 240;
	const double y = Max(game.map.GroundHeight(x, z), 0) + 75;
	const double yaw = std::atan2(tp.x - x, tp.z - z);
	SpawnOpts so; so.hasY = true; so.y = y; so.persistent = true;
	Heli* v = dynamic_cast<Heli*>(game.vehicles.spawn("warhawk", x, z, yaw, so));
	if (!v) return;
	Ped* pilot = soldier(x, z, "pistol");
	if (pilot) { v->putIn(pilot, 0); pilot->homeCar = Ref<Vehicle>(v); }
	v->spool = 1; v->grounded = false;
	v->vel.set(std::sin(yaw) * 24, 0, std::cos(yaw) * 24); // (arrives at speed)
	v->armyUnit = true;
	Unit u; u.veh = Ref<Vehicle>(v); u.kind = "heli"; u.burst = 0; u.burstT = 4; u.rocketT = 6; u.orbit = Rand() * 6.28; u.side = Rand() < 0.5 ? 1 : -1;
	units.push_back(u);
}

// ------------------------------------------------------------------ troop trucks and jeeps
void Army::convoy(Unit& u, double dt) {
	Vehicle* v = u.veh.get();
	Player& pl = *game.player;
	if (v->ai) v->ai->update(dt);
	if (u.leaving) return;
	const V3 tp = targetPos();
	const double d = Hypot(tp.x - v->pos.x, tp.z - v->pos.z);
	// close in and slowed down (or stuck): the troops pile out of the back
	if (!u.unloaded && d < (pl.vehicle ? 26 : 38) && v->speedAbs() < 6) {
		u.unloaded = true;
		const int n = u.kind == "truck" ? 4 : 0;
		const double s = std::sin(v->yaw), c = std::cos(v->yaw);
		for (int i = 0; i < n; i++) {
			const double back = v->def.L / 2 + 1 + (i >> 1) * 1.1, side = (i & 1 ? 1 : -1) * 0.8;
			const double x = v->pos.x - s * back + c * side, z = v->pos.z - c * back - s * side;
			Ped* sol = soldier(x, z, "", true, v->yaw + kPi, true, game.collision->floorHeight(x, z, v->pos.y + 0.5));
			if (!sol) continue;
			sol->threat = Ref<Character>(&pl); sol->setState("attack");
			troops.push_back(Ref<Ped>(sol));
		}
		// the passengers too; the driver keeps the truck after you
		std::vector<std::shared_ptr<Character>> occ(std::begin(v->occupants), std::end(v->occupants));
		for (const auto& oc : occ) {
			Ped* o = dynamic_cast<Ped*>(oc.get());
			if (!o || o == v->driver() || o->isPlayer || game.vehicles.isBusy(o)) continue;
			game.vehicles.exit(o);
			o->threat = Ref<Character>(&pl); o->setState("attack");
			troops.push_back(Ref<Ped>(o));
		}
		if (game.hud) game.hud->dispatch(u.kind == "truck" ? "Troops deploying!" : "Ranger crew dismounting.");
	}
	// ready to go again once you've driven off
	if (u.unloaded && d > 120) u.unloaded = u.kind == "truck";
}

// ------------------------------------------------------------------ the tank
void Army::tank(Unit& u, double dt) {
	Tank* v = dynamic_cast<Tank*>(u.veh.get());
	Player& pl = *game.player;
	if (!v) return;
	if (u.leaving) { if (v->ai) v->ai->update(dt); return; }
	const V3 tp = targetPos();
	const double d = Hypot(tp.x - v->pos.x, tp.z - v->pos.z);
	const bool see = d < 180 && game.collision->lineOfSight(v->pos.x, v->pos.y + 2.4, v->pos.z, tp.x, tp.y + 1.2, tp.z);
	// drive towards you; hold back and shell once in range and in sight
	if (v->ai) v->ai->update(dt);
	if (see && d < 70) { v->input.throttle = 0; v->input.brake = v->speed() > 0.5 ? 1 : 0; }
	// the turret: lead a moving target a little, and miss by a few metres (it's a tank, not a sniper)
	if (!u.hasMiss || (u.missT -= dt) <= 0) { u.hasMiss = true; const double mx = Rand(-4, 4), mz = Rand(-4, 4); u.miss = V3(mx, 0, mz); u.missT = Rand(2, 4); }
	const V3 tv = pl.vehicle ? pl.vehicle->vel : pl.vel;
	const double lead = Clamp(d / 190, 0, 1.2);
	v->hasAim = true;
	v->aimAt.set(tp.x + tv.x * lead + u.miss.x, tp.y + 0.8, tp.z + tv.z * lead + u.miss.z);
	u.fireT -= dt;
	if (see && u.fireT <= 0 && v->reload <= 0 && d > 12 && !friendlyNear(v->aimAt, 11, v)) {
		const double want = WrapAngle(std::atan2(v->aimAt.x - v->pos.x, v->aimAt.z - v->pos.z) - v->yaw);
		if (std::fabs(WrapAngle(want - v->turretYaw)) < 0.07) { v->fireCannon(); u.fireT = Rand(3.5, 5.5); }
	}
}

// ------------------------------------------------------------------ the gunship's autopilot
void Army::fly(Unit& u, double dt) {
	Heli* v = dynamic_cast<Heli*>(u.veh.get());
	Player& pl = *game.player;
	if (!v) return;
	AirVehicle::Ctl& c = v->ctl;
	v->spool = Max(v->spool, 0.95);
	const V3 tp = targetPos();
	double gx, gz, alt;
	if (u.leaving) {
		// climb away towards Fort Carver
		gx = v->pos.x + std::sin(v->yaw) * 300; gz = v->pos.z + std::cos(v->yaw) * 300; alt = 140;
		u.leaveT += dt;
		if (u.leaveT > 25) { removeUnit(u); return; }
	} else {
		// circle the target at 60 m, off to one side so the chin gun has a clear line
		u.orbit += dt * 0.22 * u.side;
		gx = tp.x + std::cos(u.orbit) * 60; gz = tp.z + std::sin(u.orbit) * 60;
		alt = 38;
	}
	// altitude above whatever is higher: the ground here, or where we're going; climb over buildings ahead
	const double here = game.map.GroundHeight(v->pos.x, v->pos.z), there = game.map.GroundHeight(gx, gz);
	double targetY = Max(Max(here, there), 0) + alt;
	const double fx = std::sin(v->yaw), fz = std::cos(v->yaw);
	RayHit hit; RayOpts ro; ro.ignoreProps = true;
	if (game.collision->raycast(v->pos.x, v->pos.y + 1, v->pos.z, fx, 0, fz, 50, hit, ro)) targetY = Max(targetY, v->pos.y + 25);
	c.coll = Clamp((targetY - v->pos.y) * 0.12 - v->vel.y * 0.35, -1, 1);
	// face the target once close, otherwise the direction of travel
	const double dx = gx - v->pos.x, dz = gz - v->pos.z, dist = Hypot(dx, dz);
	const bool close = !u.leaving && Hypot(tp.x - v->pos.x, tp.z - v->pos.z) < 160;
	const double faceX = close ? tp.x - v->pos.x : dx;
	const double faceZ = close ? tp.z - v->pos.z : dz;
	const double dyaw = WrapAngle(std::atan2(faceX, faceZ) - v->yaw);
	c.yaw = Clamp(-dyaw * 1.6, -1, 1);
	// move towards the goal point in the heli's own frame (pitch forward / roll sideways)
	const double sy = std::sin(v->yaw), cy = std::cos(v->yaw);
	const double fwd = dx * sy + dz * cy, left = dx * cy - dz * sy;
	const double vF = v->vel.x * sy + v->vel.z * cy, vL = v->vel.x * cy - v->vel.z * sy;
	const double want = Min(dist * 0.5, u.leaving ? 45 : 26);
	const double wantF = dist > 1 ? fwd / dist * want : 0, wantL = dist > 1 ? left / dist * want : 0;
	c.pitch = Clamp((wantF - vF) * 0.08, -1, 1);
	c.roll = Clamp(-(wantL - vL) * 0.08, -1, 1);
	if (u.leaving) return;
	// weapons
	const double d = Hypot(tp.x - v->pos.x, tp.z - v->pos.z);
	const bool see = d < 220 && game.collision->lineOfSight(v->pos.x, v->pos.y - 0.5, v->pos.z, tp.x, tp.y + 1.2, tp.z);
	const V3 tv = pl.vehicle ? pl.vehicle->vel : pl.vel;
	const double ax = tp.x + tv.x * 0.25 + Rand(-1.5, 1.5), ay = tp.y + 1;
	const double az = tp.z + tv.z * 0.25 + Rand(-1.5, 1.5);
	v->hasAim = true; v->aimAt.set(ax, ay, az);
	u.burstT -= dt; u.rocketT -= dt;
	if (u.burst > 0) {
		u.burst -= dt;
		if (v->gunT <= 0 && see) { v->fireMinigun(); v->gunT = 0.09; }
	} else if (u.burstT <= 0 && see && d < 170) { u.burst = 1.4; u.burstT = Rand(3, 5); }
	if (u.rocketT <= 0 && see && d > 30 && d < 200 && !friendlyNear(v->aimAt, 9, v)) { v->fireRocket(); u.rocketT = Rand(5, 8); }
}

// own troops or vehicles near where a shell or rocket would land (they hold fire rather than hit them)
bool Army::friendlyNear(const V3& p, double r, const Vehicle* self) const {
	const double r2 = r * r;
	for (const auto& rs : troops) if (const Ped* s = rs.get()) if (!s->dead && !s->removed && (s->pos.x - p.x) * (s->pos.x - p.x) + (s->pos.z - p.z) * (s->pos.z - p.z) < r2) return true;
	for (const Unit& o : units) {
		const Vehicle* w = o.veh.get();
		if (w && w != self && !w->removed && !w->isWrecked() && !o.leaving && (w->pos.x - p.x) * (w->pos.x - p.x) + (w->pos.z - p.z) * (w->pos.z - p.z) < r2) return true;
	}
	return false;
}

// ------------------------------------------------------------------ tidy-up
void Army::removeUnit(Unit& u) {
	Vehicle* v = u.veh.get();
	if (!v) return;
	for (const auto& o : v->occupants) if (o && o->isPlayer) return;
	for (auto& o : v->occupants) if (o) { game.peds->remove(o.get()); o.reset(); }
	game.vehicles.remove(v);
}

void Army::despawn() {
	Player& pl = *game.player;
	const V3 pp = pl.vehicle ? pl.vehicle->pos : pl.pos;
	for (Unit& u : units) {
		Vehicle* v = u.veh.get();
		if (!v) continue;
		bool mine = false;
		for (const auto& o : v->occupants) if (o && o->isPlayer) mine = true;
		if (mine) { u.leaving = true; continue; } // stolen: it's yours now
		const double d2 = Dist2(v->pos.x, v->pos.z, pp.x, pp.z);
		const double far = u.kind == "heli" ? 420 : 280;
		if (d2 > far * far || (v->isWrecked() && d2 > 140 * 140) || (u.leaving && d2 > 200 * 200 && !game.peds->inView(v->pos.x, v->pos.z))) removeUnit(u);
	}
	for (const auto& r : troops) {
		Ped* s = r.get();
		if (!s) continue;
		const double d2 = Dist2(s->pos.x, s->pos.z, pp.x, pp.z);
		if (d2 > 220 * 220 || (s->dead && d2 > 70 * 70) || (!active && !s->dead && d2 > 90 * 90 && !game.peds->inView(s->pos.x, s->pos.z))) game.peds->remove(s);
	}
}

} // namespace atg
