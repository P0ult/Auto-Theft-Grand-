#include "Rail.h"
#include "Game.h"
#include "RoadLayout.h"

namespace atg {

RailSystem::RailSystem(Game& g) : game(g) {
	if (game.map.roadInfo.hasRail) rail = &game.map.roadInfo.rail;
	if (rail) for (const RailCrossing& c : rail->crossings) if (c.kind == "level") crossings.push_back({ c.s, c.x, c.z, false });
	stations = RailStops(game.map);
	if (rail) for (const RailStop& st : stations) {
		Blip b; b.x = st.x; b.z = st.z; b.icon = "train"; b.color = 0x9fe3ff; b.small = true; b.noEdge = true;
		game.addBlip(b);
	}
}

std::vector<Train*> RailSystem::trains() const {
	std::vector<Train*> out;
	for (const Ref<Vehicle>* r : { &train, &freight }) if (Vehicle* v = r->get()) if (!v->removed) out.push_back(static_cast<Train*>(v));
	return out;
}

int RailSystem::station(const std::string& key) const {
	for (int i = 0; i < (int)stations.size(); i++) if (stations[i].key == key) return i;
	return -1;
}

Train* RailSystem::spawnTrain() {
	TrainOpts o; o.persistent = true; o.s = 0; o.dwell = 8; o.dirS = -1; o.track = 0;
	auto tmp = std::make_shared<Train>(game, "train", 0, 0, 0, o);
	game.vehicles.add(tmp);
	// centre the train on Union Station's platform
	const int u = station("union");
	if (u >= 0) { tmp->s = stations[u].s + tmp->len / 2; tmp->place(); tmp->atStation = u; }
	tmp->persistent = true;
	train = Ref<Vehicle>(tmp);
	return tmp.get();
}

Train* RailSystem::spawnFreight() {
	const RailLoop& loop = rail->loop;
	// as many wagons as the loop can hold
	const double room = loop.edge >= 0 ? loop.m1 - loop.m0 - 17 - 14 : 100;
	const int wagons = (int)Max(3, Min(7, std::floor(room / 15.7)));
	TrainOpts o; o.persistent = true; o.s = 0; o.dwell = 20; o.dirS = 1; o.track = 1; o.wagons = wagons; o.seed = 3;
	auto tmp = std::make_shared<Train>(game, "freight", 0, 0, 0, o);
	game.vehicles.add(tmp);
	const int dry = station("dry");
	if (dry >= 0) { tmp->s = Max(tmp->len + 3, stations[dry].s + tmp->len / 2); tmp->place(); tmp->atStation = dry; }
	tmp->persistent = true;
	freight = Ref<Vehicle>(tmp);
	return tmp.get();
}

// Park every other train in the Fern Creek loop and hold it there while `t` has the line (missions)
void RailSystem::clearLineFor(Train* t) {
	held = Ref<Vehicle>(t);
	const RailLoop& loop = rail->loop;
	for (Train* o : trains()) {
		if (o == t || loop.edge < 0) continue;
		if (o->driver() && o->driver()->isPlayer) continue;
		o->track = 1;
		o->s = loop.m0 + 6 + o->len; o->v = 0; o->dirS = 1; o->dwell = 1e9; o->atStation = -1; o->place();
	}
	tokenW.reset(); tokenE.reset();
}

void RailSystem::releaseLine() {
	held.reset();
	for (Train* t : trains()) if (t->dwell > 1e6) t->dwell = 4;
}

void RailSystem::update(double dt) {
	if (!rail) return;
	if (!train.get() || train->removed) spawnTrain();
	if (!freight.get() || freight->removed) spawnFreight();
	const std::vector<Train*> ts = trains();
	dispatch(ts);
	// crossings close while a train is on them or closing in
	for (Crossing& c : crossings) {
		c.active = false;
		for (Train* tr : ts) {
			const double head = tr->s, tailS = tr->s - tr->len;
			const bool on = c.s > tailS - 12 && c.s < head + 12;
			const bool approaching = tr->v > 0.5 ? c.s > head && c.s - head < 160 : tr->v < -0.5 ? c.s < tailS && tailS - c.s < 160 : false;
			if (on || approaching) { c.active = true; break; }
		}
	}
	// moving blips on the map for the trains
	for (Train* tr : ts) {
		std::shared_ptr<Blip> b;
		for (auto& e : blips) if (e.first.get() == tr) { b = e.second; break; }
		if (!b) {
			Blip nb; nb.icon = "train"; nb.color = tr->freight ? 0xffa14a : 0x6fd3ff; nb.small = true; nb.noEdge = true;
			b = game.addBlip(nb);
			blips.push_back({ Ref<Vehicle>(tr), b });
		}
		const V3 mid = tr->cars.empty() ? tr->pos : tr->cars[tr->cars.size() / 2].pos;
		b->x = mid.x; b->z = mid.z;
	}
	for (int i = (int)blips.size() - 1; i >= 0; i--) {
		Vehicle* v = blips[i].first.get();
		if (!v || v->removed) { game.removeBlip(blips[i].second); blips.erase(blips.begin() + i); }
	}
	// "press F to board" when standing on a platform next to a stopped passenger train
	hintT -= dt;
	Player& p = *game.player;
	Train* tr = static_cast<Train*>(train.get());
	if (tr && !p.vehicle && !p.dead && std::fabs(tr->v) < 0.5 && hintT <= 0) {
		int seat;
		const V3 d = tr->nearestDoor(p.pos, seat);
		if (Hypot(d.x - p.pos.x, d.z - p.pos.z) < 6) {
			hintT = 12;
			if (game.hud) game.hud->help("Press <b>F</b> to board the train. Ride it along the Sol Line, or climb into the cab at the front to drive it.", 5);
		}
	}
}

// Signals. W = the single track west of the loop (to Dry Wells), E = east of it (to Union Station).
// A train holds a section while any part of it is on it; an autopilot train needs the section before
// it may leave the loop onto it, and never closes within 25 m of another train on the same track.
// (The loop is longer than either train, so a train is always wholly inside it before its signal.)
void RailSystem::dispatch(const std::vector<Train*>& ts) {
	const RailLoop& loop = rail->loop;
	for (Train* t : ts) { t->limitLo = -kInf; t->limitHi = kInf; t->hold = false; }
	if (loop.edge < 0) return;
	const double M0 = loop.m0, M1 = loop.m1;
	auto inW = [&](const Train* t) { return t->s - t->len < M0; };
	auto inE = [&](const Train* t) { return t->s > M1; };
	auto onSec = [&](char k, const Train* t) { return k == 'W' ? inW(t) : inE(t); };
	auto token = [&](char k) -> Ref<Vehicle>& { return k == 'W' ? tokenW : tokenE; };
	for (char k : { 'W', 'E' }) {
		Vehicle* h = token(k).get();
		if (h && (h->removed || !onSec(k, static_cast<Train*>(h)))) token(k).reset();
	}
	for (Train* t : ts) for (char k : { 'W', 'E' }) if (onSec(k, t) && !token(k).get()) token(k) = Ref<Vehicle>(t);
	auto grant = [&](char k, Train* t) {
		Vehicle* h = token(k).get();
		if (h == t) return true;
		if (h) return false;
		for (Train* o : ts) if (o != t && onSec(k, o)) return false;
		token(k) = Ref<Vehicle>(t);
		return true;
	};
	Vehicle* heldV = held.get();
	for (Train* t : ts) {
		if (t->driver() && t->driver()->isPlayer) continue;
		if (heldV && t != heldV) { t->hold = true; continue; }
		// a train asks for the next section only once it's wholly inside the loop (asking from the far
		// single-track section would deadlock: each train waiting for the section the other stands on)
		if (t->dirS > 0 && !inE(t) && !(!inW(t) && grant('E', t))) { t->limitHi = M1 - 4; t->hold = !inW(t); }
		if (t->dirS < 0 && !inW(t) && !(!inE(t) && grant('W', t))) { t->limitLo = M0 + 4; t->hold = !inE(t); }
		// spacing: another train ahead on the same track (on the single-track sections everyone shares it)
		for (Train* o : ts) {
			if (o == t) continue;
			auto shared = [&](double s) { return s < M0 || s > M1 || o->track == t->track; };
			if (t->dirS > 0) { const double nearS = o->s - o->len; if (nearS > t->s - 1 && shared(nearS)) t->limitHi = Min(t->limitHi, nearS - 25); }
			else { const double nearS = o->s; if (nearS < t->s - t->len + 1 && shared(nearS)) t->limitLo = Max(t->limitLo, nearS + 25); }
		}
	}
	// hard stop: trains never pass through each other (a player at the controls can still bump one)
	for (Train* a : ts) for (Train* b : ts) {
		if (a == b || a->s > b->s) continue; // a is behind b along +s
		const double gap = (b->s - b->len) - a->s;
		auto shared = [&](double s) { return s < M0 || s > M1 || a->track == b->track; };
		if (gap < 0.5 && shared(a->s)) {
			const double push = 0.5 - gap;
			Train* mover = std::fabs(a->v) >= std::fabs(b->v) ? a : b;
			if (Max(std::fabs(a->v), std::fabs(b->v)) > 4) game.rig.addShake(0.5);
			if (mover == a) a->s -= push; else b->s += push;
			a->v = Min(a->v, 0); b->v = Max(b->v, 0);
			a->place(); b->place();
		}
	}
}

double RailSystem::crossingAhead(double x, double z, double fx, double fz, double maxD) const {
	double best = kInf;
	for (const Crossing& c : crossings) {
		if (!c.active) continue;
		const double dx = c.x - x, dz = c.z - z;
		const double along = dx * fx + dz * fz;
		if (along < 0 || along > maxD) continue;
		const double lat = std::fabs(dx * fz - dz * fx);
		if (lat > 9) continue;
		best = Min(best, along - 7);
	}
	return best;
}

const RailStop* RailSystem::nearestStation(double x, double z) const {
	const RailStop* best = nullptr; double bd = kInf;
	for (const RailStop& st : stations) { const double d = Dist2(st.x, st.z, x, z); if (d < bd) { bd = d; best = &st; } }
	return best;
}

} // namespace atg
