#include "Boats.h"
#include "Boat.h"
#include "Character.h"
#include "Collision.h"
#include "Game.h"
#include "Peds.h"
#include "Player.h"
#include "Police.h"
#include "RoadLayout.h"
#include "Vehicles.h"
#include "WorldGen.h"
#include "WorldMeshes.h"

namespace atg {

// ------------------------------------------------------------------ the driver
BoatDriver::BoatDriver(Game& g, Vehicle* veh, const std::vector<std::array<double, 2>>* points, int i, double p) : game(g), v(veh), idx(i), pace(p) {
	if (points) pts = *points;
}

void BoatDriver::update(double dt) {
	const CityMap& map = game.map;
	Boat* b = dynamic_cast<Boat*>(v);
	double tx, tz, want;
	if (hasTarget) {
		tx = target.x; tz = target.z;
		const double d = Hypot(tx - v->pos.x, tz - v->pos.z);
		// slow when right on top of it
		want = d < 12 ? v->def.top * 0.5 : v->def.top * 0.95;
	} else if (!pts.empty()) {
		const auto& tp = pts[idx];
		tx = tp[0]; tz = tp[1];
		if (Hypot(tx - v->pos.x, tz - v->pos.z) < 25) idx = (idx + 1) % (int)pts.size();
		want = v->def.top * pace;
	} else { v->input.throttle = 0; v->input.brake = v->speed() > 1 ? 0.5 : 0; v->input.steer = 0; return; }
	double lx, lz; v->worldToLocal(tx, tz, lx, lz);
	double ang = std::atan2(lx, Max(0.5, lz));
	// shallows ahead: steer away (feel 22 m out, and left and right)
	auto feel = [&](double a, double dist) {
		const double yw = v->yaw + a;
		const double x = v->pos.x + std::sin(yw) * dist, z = v->pos.z + std::cos(yw) * dist;
		return map.WaterLevel(x, z) - map.TerrainHeight(x, z) > v->def.draft + 0.6;
	};
	if (!feel(0, 22)) { const bool l = feel(0.6, 20), r = feel(-0.6, 20); ang = l && !r ? 0.9 : r && !l ? -0.9 : (ang >= 0 ? 1 : -1); want *= 0.5; }
	// piers, the quay, the ship: look ahead and go round
	auto probe = [&](double a) {
		RayHit hit; RayOpts o; o.ignoreProps = true;
		return game.collision->raycast(v->pos.x, v->pos.y + 0.6, v->pos.z, std::sin(v->yaw + a), 0, std::cos(v->yaw + a), 26, hit, o) ? hit.t : 26.0;
	};
	const double f0 = probe(0);
	if (f0 < 22) { const double fl = probe(0.5), fr = probe(-0.5); ang = fl > fr ? 1 : -1; want *= f0 < 10 ? 0.3 : 0.6; }
	v->input.steer = Clamp(ang * 1.6, -1, 1);
	if (std::fabs(ang) > 1.2) want *= 0.45;
	v->input.throttle = v->speed() < want ? 1 : 0.2;
	v->input.brake = v->speed() > want + 4 ? 0.4 : 0;
	// aground: back off
	const bool floating = b ? b->floating : true;
	if (!floating || (v->speedAbs() < 0.8 && v->input.throttle > 0)) stuckT += dt; else stuckT = Max(0, stuckT - dt);
	if (stuckT > 2) { v->input.throttle = 0; v->input.brake = 1; v->input.steer = -v->input.steer; if (stuckT > 4.5) stuckT = 0; }
}

// ------------------------------------------------------------------ the system
BoatSystem::BoatSystem(Game& g) : game(g) { plan(); }

// a floating pontoon: planks on floats, walkable, boats moor alongside
void BoatSystem::pontoon(double x0, double z0, double x1, double z1, double y) {
	Part g;
	g.color(0.52, 0.4, 0.28);
	const double minX = Min(x0, x1), maxX = Max(x0, x1), minZ = Min(z0, z1), maxZ = Max(z0, z1);
	const bool alongZ = maxZ - minZ > maxX - minX;
	const double len = alongZ ? maxZ - minZ : maxX - minX;
	for (int k = 0; k < len / 0.5; k++) {
		const double a = (alongZ ? minZ : minX) + k * 0.5;
		if (alongZ) g.box(minX, y - 0.12, a + 0.03, maxX, y, a + 0.47); else g.box(a + 0.03, y - 0.12, minZ, a + 0.47, y, maxZ);
	}
	g.color(0.85, 0.85, 0.82);
	for (int k = 0; k < len / 3; k++) {
		const double a = (alongZ ? minZ : minX) + k * 3 + 1.5;
		if (alongZ) g.box(minX + 0.2, y - 0.5, a - 0.6, maxX - 0.2, y - 0.12, a + 0.6); else g.box(a - 0.6, y - 0.5, minZ + 0.2, a + 0.6, y - 0.12, maxZ - 0.2);
		// cleats
		g.color(0.3, 0.3, 0.32);
		for (int s : { 0, 1 }) {
			const double e = s ? (alongZ ? maxX - 0.15 : maxZ - 0.15) : (alongZ ? minX + 0.15 : minZ + 0.15);
			if (alongZ) g.box(e - 0.05, y, a - 0.12, e + 0.05, y + 0.08, a + 0.12); else g.box(a - 0.12, y, e - 0.05, a + 0.12, y + 0.08, e + 0.05);
		}
		g.color(0.85, 0.85, 0.82);
	}
	pontoons.push_back(std::move(g.m));
	// a walkable top (a floor for people) that boats can't cross
	game.collision->addBox(minX, y - 3, minZ, maxX, y, maxZ, "pontoon");
}

bool BoatSystem::deep(double x, double z, double d) const { return game.map.WaterLevel(x, z) - game.map.TerrainHeight(x, z) > d; }

void BoatSystem::plan() {
	// (the boats add their landmarks to the map, as boats.js does; the map is only read once the game runs)
	CityMap& map = const_cast<CityMap&>(game.map);
	auto& L = map.landmarks;
	auto spot = [](double x, double z, double yaw, std::vector<std::string> types, bool police = false) { Spot s; s.x = x; s.z = z; s.yaw = yaw; s.types = std::move(types); s.police = police; return s; };
	auto marina = [&](const std::string& name, double x, double z, std::vector<Spot> spots) { Marina m; m.name = name; m.x = x; m.z = z; m.spots = std::move(spots); marinas.push_back(std::move(m)); };
	// Santa Luz: a pontoon off the beach, west of the pier
	{
		const double x = 62, zA = 692, zB = 752;
		pontoon(x - 1.6, zA, x + 1.6, zB, WATER_Y + 0.6);
		marina("Santa Luz Marina", x, 720, {
			spot(x - 4.2, 716, 0, { "speedboat", "dinghy" }), spot(x - 4.2, 734, 0, { "speedboat" }),
			spot(x + 3.8, 712, 0, { "jetski" }), spot(x + 3.8, 722, 0, { "jetski", "dinghy" }), spot(x + 4.6, 740, 0, { "cruiser", "speedboat" }),
		});
		Landmark lm; lm.x = x - 0.5; lm.z = 698; lm.name = "Santa Luz Marina";
		L["marina"] = lm;
	}
	// Port Morena: along the quay north of the cargo ship (the harbour patrol berths here)
	{
		const double qx = CityMap::CITY_MAXX + 44;
		marina("Port Morena", qx, 110, {
			spot(qx + 4, 70, kPi, { "policeboat" }, true), spot(qx + 4, 95, kPi, { "speedboat", "cruiser" }),
			spot(qx + 4, 130, kPi, { "dinghy", "speedboat" }),
		});
	}
	// the model boats tied up at Port Hale and on the lake become real ones
	std::vector<CollObj*> propBoats;
	for (CollObj* c : game.propColliders) if (c && c->prop >= 0 && game.propType(c->prop) == "boat") propBoats.push_back(c);
	auto take = [&](const std::function<bool(const PropInstance&)>& pred) {
		std::vector<PropInstance> out;
		for (CollObj* c : propBoats) {
			const PropInstance& p = game.props[c->prop];
			if (pred(p)) { game.breakProp(c); c->gone = true; out.push_back(p); }
		}
		return out;
	};
	if (L.count("halePier")) {
		const Landmark& hp = L.at("halePier");
		const auto was = take([](const PropInstance& p) { return p.y > -2 && p.y < 0; });
		std::vector<Spot> spots;
		for (size_t i = 0; i < was.size(); i++) spots.push_back(spot(was[i].x, was[i].z, was[i].rot, i == 1 ? std::vector<std::string>{ "cruiser" } : std::vector<std::string>{ "speedboat", "dinghy" }));
		marina("Port Hale", hp.x, hp.z, spots);
	}
	// Lake Mirador: dinghies and jet skis by the floating docks
	{
		const auto was = take([](const PropInstance& p) { return p.y > LAKE.y - 2; });
		std::vector<Spot> spots;
		for (const PropInstance& p : was) spots.push_back(spot(p.x, p.z, p.rot, { "dinghy" }));
		spots.push_back(spot(-1318, -1860, kPi / 2, { "jetski" }));
		spots.push_back(spot(-1316, -1910, kPi / 2, { "jetski" }));
		marina("Lake Mirador", LAKE.x + 200, LAKE.z + 10, spots);
	}
	// San Aurelio: a pontoon out from Aurelio Beach
	{
		const double z = NCITY.z + 40, cx = CoastX(z);
		pontoon(cx + 14, z - 1.6, cx + 84, z + 1.6, WATER_Y + 0.6);
		marina("Aurelio Marina", cx + 50, z, {
			spot(cx + 50, z - 4.4, kPi / 2, { "speedboat", "jetski" }), spot(cx + 70, z - 4.6, kPi / 2, { "cruiser", "speedboat" }),
			spot(cx + 56, z + 4.3, kPi / 2, { "jetski", "dinghy" }), spot(cx + 76, z + 4.6, kPi / 2, { "policeboat" }, true),
		});
		Landmark lm; lm.x = cx + 4; lm.z = z; lm.name = "Aurelio Marina";
		lm.pts["pontoon0"] = P3{ cx + 14, z, 0 }; lm.pts["pontoon1"] = P3{ cx + 84, z, 0 };
		L["aurMarina"] = lm;
	}
	// cruising routes (loops out at sea and on the lake)
	struct R { const char* name; bool lake; std::vector<std::string> types; int n; std::vector<std::array<double, 2>> pts; };
	const std::vector<R> all = {
		{ "santaluz", false, { "speedboat", "cruiser", "dinghy" }, 2, { { -600, 880 }, { -200, 940 }, { 300, 900 }, { 700, 1020 }, { 300, 1150 }, { -300, 1100 }, { -800, 1000 } } },
		{ "beach", false, { "jetski" }, 2, { { -320, 765 }, { -120, 790 }, { 40, 775 }, { -60, 835 }, { -260, 820 } } },
		{ "coast", false, { "speedboat", "cruiser" }, 1, { { 1050, 500 }, { 1150, 0 }, { 1300, -500 }, { 1350, -1000 }, { 1250, -1400 }, { 1400, -900 }, { 1300, 200 } } },
		{ "aurelio", false, { "speedboat", "cruiser", "jetski" }, 2, { { 1420, -3300 }, { 1480, -3800 }, { 1430, -4300 }, { 1540, -4550 }, { 1565, -4000 }, { 1545, -3500 } } },
		{ "lake", true, { "dinghy", "jetski" }, 1, { { -1400, -1800 }, { -1520, -1760 }, { -1620, -1880 }, { -1520, -2020 }, { -1380, -1960 } } },
	};
	for (const R& r : all) {
		Route rt; rt.name = r.name; rt.lake = r.lake; rt.types = r.types; rt.n = r.n;
		for (const auto& p : r.pts) if (deep(p[0], p[1], 2)) rt.pts.push_back(p);
		if (rt.pts.size() >= 3) routes.push_back(std::move(rt));
	}
}

V3 BoatSystem::playerPos() const { const Player& p = *game.player; return p.vehicle ? p.vehicle->pos : p.pos; }

void BoatSystem::update(double dt) {
	t -= dt;
	if (t <= 0) { t = 1; stream(); }
	policeUpdate(dt);
}

void BoatSystem::stream() {
	VehicleManager& vm = game.vehicles;
	const V3 pp = playerPos();
	// moored boats
	for (Marina& mr : marinas) {
		{ std::vector<Ref<Vehicle>> keep; for (auto& b : mr.boats) if (b.get() && !b->removed) keep.push_back(b); mr.boats = keep; }
		const double d = Hypot(pp.x - mr.x, pp.z - mr.z);
		if (d < 260 && !mr.active) {
			mr.active = true;
			for (const Spot& s : mr.spots) {
				bool taken = false;
				for (const auto& v : vm.list) if (Dist2(v->pos.x, v->pos.z, s.x, s.z) < 9) { taken = true; break; }
				if (taken) continue;
				SpawnOpts o; o.parked = true;
				Boat* b = dynamic_cast<Boat*>(vm.spawn(RandPick(s.types), s.x, s.z, s.yaw, o));
				if (!b) continue;
				b->moored = true;
				if (s.police) b->locked = false;
				mr.boats.push_back(Ref<Vehicle>(b));
			}
		} else if (d > 380 && mr.active) {
			mr.active = false;
			for (auto& r : mr.boats) if (Boat* b = dynamic_cast<Boat*>(r.get())) if (!b->driver() && b->moored && !b->persistent) vm.remove(b);
			mr.boats.clear();
		}
		// moored boats stay put (lines out) until someone takes them
		for (auto& r : mr.boats) if (Boat* b = dynamic_cast<Boat*>(r.get())) if (b->moored) { if (b->driver()) b->moored = false; else { b->vel *= 0.8; b->r *= 0.8; } }
	}
	// cruising traffic
	for (Route& r : routes) {
		{ std::vector<Ref<Vehicle>> keep; for (auto& b : r.boats) if (Vehicle* v = b.get()) if (!v->removed && v->driver() && !v->driver()->dead && !v->isWrecked()) keep.push_back(b); r.boats = keep; }
		double dmin = 1e18;
		for (const auto& p : r.pts) dmin = Min(dmin, Hypot(pp.x - p[0], pp.z - p[1]));
		if (dmin < 500 && (int)r.boats.size() < r.n) spawnCruiser(r);
		if (dmin > 750) { for (auto& b : r.boats) if (Vehicle* v = b.get()) if (!(v->driver() && v->driver()->isPlayer)) despawn(v); r.boats.clear(); }
	}
}

void BoatSystem::spawnCruiser(Route& r) {
	const V3 pp = playerPos();
	// start at a route point out of sight-ish (not right next to the player)
	struct Cand { size_t i; double d; };
	std::vector<Cand> cands;
	for (size_t i = 0; i < r.pts.size(); i++) {
		const auto& p = r.pts[i];
		const double d = Hypot(pp.x - p[0], pp.z - p[1]);
		if (d <= 90) continue;
		bool busy = false;
		for (const auto& v : game.vehicles.list) if (Dist2(v->pos.x, v->pos.z, p[0], p[1]) < 30 * 30) { busy = true; break; }
		if (!busy) cands.push_back({ i, d });
	}
	std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
	if (cands.empty()) return;
	const Cand c = cands[0];
	const auto& p = r.pts[c.i];
	const auto& nx = r.pts[(c.i + 1) % r.pts.size()];
	Vehicle* b = game.vehicles.spawn(RandPick(r.types), p[0], p[1], std::atan2(nx[0] - p[0], nx[1] - p[1]));
	if (!b) return;
	PedOpts o; o.persistent = true;
	Ped* ped = game.peds ? game.peds->spawnPed(p[0], p[1], o) : nullptr;
	if (!ped) { game.vehicles.remove(b); return; }
	b->putIn(ped, 0);
	b->persistent = true;
	b->ai = std::make_shared<BoatDriver>(game, b, &r.pts, (int)((c.i + 1) % r.pts.size()), Rand(0.45, 0.75));
	r.boats.push_back(Ref<Vehicle>(b));
}

void BoatSystem::despawn(Vehicle* b) {
	for (auto& o : b->occupants) if (o && !o->isPlayer && game.peds) game.peds->remove(o.get());
	for (auto& o : b->occupants) if (o && !o->isPlayer) o.reset();
	game.vehicles.remove(b);
}

// ------------------------------------------------------------------ police on the water
bool BoatSystem::playerAtSea() const {
	const Player& p = *game.player;
	return (p.vehicle && dynamic_cast<const Boat*>(p.vehicle)) || (p.swimming && !p.vehicle);
}

void BoatSystem::policeUpdate(double dt) {
	Police* pol = game.policeSys;
	if (!pol) return;
	{ std::vector<Ref<Vehicle>> keep; for (auto& b : police) if (b.get() && !b->removed) keep.push_back(b); police = keep; }
	const V3 pp = playerPos();
	const Player& pl = *game.player;
	const int want = pol->level > 0 && playerAtSea() && !pl.dead ? (pol->level >= 3 ? 3 : pol->level >= 2 ? 2 : 1) : 0;
	int active = 0;
	for (auto& r : police) { Vehicle* b = r.get(); if (b && !b->isWrecked() && b->driver() && !b->driver()->dead) active++; }
	policeT -= dt;
	if (active < want && policeT <= 0) { policeT = 6; spawnPolice(); }
	const std::vector<Ref<Vehicle>> list = police;
	for (const auto& r : list) {
		Boat* b = dynamic_cast<Boat*>(r.get());
		if (!b || b->removed || b->isWrecked() || !b->driver() || b->driver()->dead) continue;
		const bool chase = pol->level > 0 && !pl.dead;
		if (BoatDriver* ai = dynamic_cast<BoatDriver*>(b->ai.get())) { ai->hasTarget = chase; ai->target = pp; }
		b->sirenOn = chase;
		// the bow gunner (from two stars)
		Character* gunner = b->occupants[1] ? b->occupants[1].get() : b->occupants[0].get();
		const double d = Hypot(pp.x - b->pos.x, pp.z - b->pos.z);
		b->gunT2 -= dt;
		if (chase && pol->level >= 2 && gunner && !gunner->dead && d < 90 && b->gunT2 <= 0) {
			b->gunT2 = Rand(0.16, 0.3);
			b->burst = (b->burst + 1) % 14;
			const bool burst = b->burst < 5; // short bursts, then a pause
			if (burst && game.collision->lineOfSight(b->pos.x, b->pos.y + 2, b->pos.z, pp.x, pp.y + 1, pp.z)) {
				const double miss = Clamp(d / 25, 0.6, 3.5) * (1.4 - pol->level * 0.12);
				const double tx = pp.x + Rand(-miss, miss), ty = pp.y + 0.8 + Rand(-0.5, 0.8), tz = pp.z + Rand(-miss, miss);
				const V3 tgt(tx, ty, tz);
				b->fireGun(&tgt);
			}
		}
		// off duty and far away: back to base (gone)
		if (!chase && d > 300) despawn(b);
	}
}

Vehicle* BoatSystem::spawnPolice() {
	Police* pol = game.policeSys;
	const V3 pp = playerPos();
	const bool onLake = game.map.WaterLevel(pp.x, pp.z) > WATER_Y + 1;
	// open water 110-200 m away, out of the player's view if possible
	for (int k = 0; k < 24; k++) {
		const double a = Rand() * kPi * 2, r = Rand(onLake ? 70 : 110, onLake ? 140 : 200);
		const double x = pp.x + std::cos(a) * r, z = pp.z + std::sin(a) * r;
		if (!deep(x, z, 3)) continue;
		if (k < 16 && game.peds && game.peds->inView(x, z)) continue;
		SpawnOpts o; o.persistent = true;
		Boat* b = dynamic_cast<Boat*>(game.vehicles.spawn("policeboat", x, z, std::atan2(pp.x - x, pp.z - z), o));
		if (!b) return nullptr;
		const int n = pol->level >= 2 ? 2 : 1;
		for (int q = 0; q < n; q++) { Ped* cop = pol->spawnCop(x, z); if (!cop) continue; b->putIn(cop, q); cop->homeCar = Ref<Vehicle>(b); }
		auto ai = std::make_shared<BoatDriver>(game, b, nullptr, 0, 1);
		ai->ram = true;
		b->ai = ai;
		b->policeBoat = true;
		b->sirenOn = true;
		police.push_back(Ref<Vehicle>(b));
		return b;
	}
	return nullptr;
}

} // namespace atg
