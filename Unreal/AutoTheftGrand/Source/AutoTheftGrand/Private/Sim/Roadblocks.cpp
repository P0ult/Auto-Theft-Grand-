#include "Roadblocks.h"
#include "Collision.h"
#include "Game.h"
#include "Peds.h"
#include "Police.h"
#include "RoadNet.h"
#include "Traffic.h"

namespace atg {

namespace {
const size_t MAX = 2;                    // roadblocks standing at once
const double DIST[2] = { 90, 165 };      // how far ahead (m), by speed
const double SPIKE_AHEAD = 15;           // the strip lies this far in front of the cars
const std::vector<std::string> LINES = { "All units, roadblock in position. Suspect inbound.", "Roadblock set. Spike strip deployed.", "Units in position, we have the road closed." };
}

Roadblocks::Roadblocks(Game& g) : game(g) {
	g.events.playerDied.on([this]() { timer = 10; });
	g.events.busted.on([this]() { timer = 10; });
	g.radarSources.push_back([this](std::vector<Blip>& out) { blipList(out); });
}

void Roadblocks::blipList(std::vector<Blip>& out) const {
	const bool flash = (int)std::floor(game.time * 4) % 2 != 0;
	for (const Block& b : blocks) for (const auto& r : b.cars) if (Vehicle* v = r.get()) if (!v->removed && !v->isWrecked()) {
		Blip bl; bl.x = v->pos.x; bl.z = v->pos.z; bl.color = flash ? 0x3355ff : 0xff3333; bl.icon = "dot"; bl.small = true; bl.noEdge = true; bl.square = true;
		out.push_back(bl);
	}
}

void Roadblocks::update(double dt) {
	Police* pol = game.policeSys;
	Player& p = *game.player;
	Vehicle* v = p.vehicle;
	// a new block ahead of a driving suspect
	timer -= dt;
	if (enabled && pol && pol->enabled && pol->level >= 3 && v && v->def.kind.empty() && !p.dead && !game.missionNoRoadblocks && blocks.size() < MAX && timer <= 0) {
		timer = 3; // (retry soon if there was nowhere to put it)
		const double T[6] = { 0, 0, 0, 24, 17, 14 };
		if (v->speedAbs() > 11 && place(v)) timer = T[pol->level];
	}
	// spike strips, burst tyres
	for (Block& b : blocks) if (b.hasSpike) stinger(b);
	for (const auto& op : game.vehicles.list) {
		Vehicle* o = op.get();
		if (!o->flat || o->removed || o->isWrecked() || o->speedAbs() < 7) continue;
		if (Rand() < dt * 6 && Dist2(o->pos.x, o->pos.z, game.rig.camPos.x, game.rig.camPos.z) < 80 * 80) {
			const double s = Rand() < 0.5 ? 1 : -1, fx = std::sin(o->yaw), fz = std::cos(o->yaw);
			const double z = (Rand() < 0.5 ? 1 : -1) * o->def.wheelbase / 2, x = s * o->def.track / 2;
			if (game.effects) game.effects->sparks(V3(o->pos.x + fz * x + fx * z, o->pos.y + 0.05, o->pos.z - fx * x + fz * z), 3);
		}
	}
	// tidy up the ones left behind
	const V3 pp = v ? v->pos : p.pos;
	for (int i = (int)blocks.size() - 1; i >= 0; i--) {
		Block& b = blocks[i];
		b.t += dt;
		{ std::vector<Ref<Vehicle>> keep; for (auto& c : b.cars) if (Vehicle* cv = c.get()) if (!cv->removed && !(cv->driver() && cv->driver()->isPlayer)) keep.push_back(c); b.cars = keep; }
		const double d = std::sqrt(Dist2(b.x, b.z, pp.x, pp.z));
		const double ahead = (pp.x - b.x) * b.tx + (pp.z - b.z) * b.tz; // > 0: you're past it
		const bool seen = game.peds->inView(b.x, b.z, 4);
		if (d > 300 || (b.t > 4 && ((ahead > 60 && d > 110) || (pol->level == 0 && d > 70)) && !seen) || (b.t > 240 && !seen)) remove(i);
	}
}

// find a stretch of road ahead of the car and close it
bool Roadblocks::place(Vehicle* v) {
	const RoadNet& net = game.map.roads;
	Police* pol = game.policeSys;
	const double sp = v->speedAbs();
	double hx = v->vel.x, hz = v->vel.z;
	double hl = Hypot(hx, hz); if (!hl) hl = 1;
	hx /= hl; hz /= hl;
	for (const double dist : { Clamp(sp * 5, DIST[0], DIST[1]), DIST[0] + 10, DIST[1] - 10 }) {
		const double px = v->pos.x + hx * dist, pz = v->pos.z + hz * dist;
		const EdgeHit c = net.Closest(px, pz, [](const REdge& e) { return !e.removed && e.type != ERoad::Rail && e.type != ERoad::Ramp; }, 18, v->pos.y);
		if (!c.valid()) continue;
		const REdge& e = net.edges[c.e];
		if (c.s < 16 || c.s > e.len - 16) continue;
		if (std::fabs(c.y - v->pos.y) > 10) continue;
		const EdgePoint at = net.At(e, c.s);
		double tx = at.tx, tz = at.tz;
		const double dot = tx * hx + tz * hz;
		if (std::fabs(dot) < 0.8) continue; // a side road
		// carriageway width each side of the centre line (u = (-tz, tx) is the forward-lane side)
		const double wU = Min(e.wL, e.T->off0 + e.T->laneW * Max(1, e.lanesF) + 0.4);
		const double wD = Min(e.wR, e.T->off0 + e.T->laneW * Max(1, e.lanesB) + 0.4);
		const double ux = -tz, uz = tx;
		if (dot < 0) { tx = -tx; tz = -tz; } // facing the way the suspect is coming
		const double cx = at.x, cz = at.z, y = c.y;
		for (const Block& b : blocks) if (Dist2(b.x, b.z, cx, cz) < 140 * 140) return false;
		if (game.peds->inView(cx, cz, 2) && Hypot(cx - v->pos.x, cz - v->pos.z) < 95) continue;
		const double W = wU + wD;
		const int n = (int)Clamp(std::round(W / 4.9), 2, 5);
		const double step = W / n;
		// clear the traffic out of the way
		{
			std::vector<Vehicle*> all; for (auto& o : game.vehicles.list) all.push_back(o.get());
			for (Vehicle* o : all) {
				if (o->persistent || (o->driver() && o->driver()->isPlayer) || o->removed) continue;
				if (Dist2(o->pos.x, o->pos.z, cx, cz) < 16 * 16 && o->traffic && game.traffic) game.traffic->despawn(o);
			}
		}
		const std::string district = game.map.DistrictAt(cx, cz);
		const bool rural = district == "country" || district == "desert" || district == "forest";
		Block b{ cx, cz, y, tx, tz, ux, uz, wU, wD };
		for (int i = 0; i < n; i++) {
			const double lat = -wD + (i + 0.5) * step;
			const double x = cx + ux * lat, z = cz + uz * lat;
			const std::string type = pol->level >= 4 && i == n / 2 ? "enforcer" : rural ? "sheriff" : "police";
			const double yawRoad = std::atan2(tx, tz);
			const double yaw = yawRoad + (i % 2 ? 1 : -1) * 1.2;
			SpawnOpts so; so.persistent = true; so.parked = true; so.hasY = true; so.y = y;
			Vehicle* car = game.vehicles.spawn(type, x, z, yaw, so);
			car->sirenOn = true; car->sirenMute = true; car->roadblock = true;
			b.cars.push_back(Ref<Vehicle>(car));
			// officers in cover behind it
			const int nc = n <= 2 ? 2 : i % 2 ? 1 : 2;
			for (int k = 0; k < nc; k++) {
				const double hx2 = x + tx * 3.4 + ux * (k ? 1.1 : -1.1), hz2 = z + tz * 3.4 + uz * (k ? 1.1 : -1.1);
				PedOpts po; po.hasY = true; po.y = game.collision->floorHeight(hx2, hz2, y + 1.5);
				Ped* cop = pol->spawnCop(hx2, hz2, &po);
				if (type == "enforcer" && cop->weapon != "rifle") { cop->giveWeapon("rifle", 999); cop->equip("rifle"); }
				cop->hasHoldPos = true; cop->holdPos = V3(hx2, 0, hz2);
				cop->setYaw(yawRoad + kPi);
				b.cops.push_back(Ref<Ped>(cop));
			}
		}
		// the stinger, always at four stars, sometimes at three
		if (pol->level >= 4 || Rand() < 0.45) {
			const double sx = cx - tx * SPIKE_AHEAD, sz = cz - tz * SPIKE_AHEAD;
			const double mid = (wU - wD) / 2;
			b.hasSpike = true;
			// on the road surface (the ground under a road can sit a few centimetres below it and bury the strip)
			const EdgeHit rs = net.Closest(sx + ux * mid, sz + uz * mid, [](const REdge& e2) { return !e2.removed && e2.type != ERoad::Rail; }, 18, y);
			const double sy = Max(game.collision->surfaceHeight(sx, sz, y + 0.6), rs.valid() ? rs.y + 0.035 : -kInf);
			b.spike = { sx + ux * mid, sz + uz * mid, sy, W / 2, std::atan2(ux, uz) - kPi / 2, W };
		}
		blocks.push_back(b);
		if (game.hud) game.hud->dispatch(LINES[(size_t)std::floor(Rand() * LINES.size())]);
		return true;
	}
	return false;
}

// anything rolling over the strip loses its tyres (not the police: they know where it is)
void Roadblocks::stinger(Block& b) {
	const Spike& s = b.spike;
	for (const auto& op : game.vehicles.list) {
		Vehicle* o = op.get();
		if (o->flat || o->removed || !o->def.kind.empty() || o->def.police || o->speedAbs() < 2) continue;
		const double rx = o->pos.x - s.x, rz = o->pos.z - s.z;
		if (rx * rx + rz * rz > 30 * 30) continue;
		const double along = rx * b.tx + rz * b.tz, lat = rx * b.ux + rz * b.uz;
		if (std::fabs(along) < o->def.L / 2 && std::fabs(lat) < s.half + 0.4 && std::fabs(o->pos.y - s.y) < 2) {
			o->flat = true;
			game.soundAt("pop", o->pos, 1);
			if (o->driver() && o->driver()->isPlayer) {
				game.sound("pop", 0.6);
				if (game.hud) game.hud->help("Spike strip! Your tyres are shredded: get them fixed at <b>Los Soles Customs</b>.", 4);
			}
		}
	}
}

void Roadblocks::remove(int i) {
	Block& b = blocks[i];
	for (auto& r : b.cars) if (Vehicle* c = r.get()) if (!c->removed && !(c->driver() && c->driver()->isPlayer)) {
		for (auto& o : c->occupants) if (o && !o->isPlayer) { auto keep = o; game.peds->remove(keep.get()); }
		for (auto& o : c->occupants) o.reset();
		game.vehicles.remove(c);
	}
	for (auto& r : b.cops) if (Ped* c = r.get()) if (!c->removed && !c->vehicle) game.peds->remove(c);
	blocks.erase(blocks.begin() + i);
}

void Roadblocks::reset() {
	for (int i = (int)blocks.size() - 1; i >= 0; i--) remove(i);
	timer = 6;
}

} // namespace atg
