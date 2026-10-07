#include "NpcCrime.h"
#include "Collision.h"
#include "Game.h"
#include "Gameplay.h"
#include "Peds.h"
#include "Police.h"
#include "Ragdoll.h"
#include "RoadLayout.h"
#include "RoadNet.h"
#include <algorithm>
#include <typeinfo>

namespace atg {

namespace {
struct CrimeDef { const char* name; int stars; bool ticket; int add; };
const std::map<std::string, CrimeDef>& CRIMES() {
	static const std::map<std::string, CrimeDef> m = {
		{ "jaywalk", { "Jaywalking", 1, true, 0 } },
		{ "speeding", { "Reckless driving", 1, true, 0 } },
		{ "hitrun", { "Hit and run", 1, false, 0 } },
		{ "hitped", { "Hit and run", 2, false, 0 } },
		{ "assault", { "Assault", 1, false, 0 } },
		{ "mugging", { "Mugging", 2, false, 0 } },
		{ "gta", { "Grand theft auto", 2, false, 0 } },
		{ "evading", { "Evading police", 2, false, 1 } },
		{ "resisting", { "Resisting arrest", 2, false, 1 } },
		{ "copAssault", { "Attacking an officer", 3, false, 0 } },
		{ "copKill", { "Officer down", 4, false, 0 } },
	};
	return m;
}
const CrimeDef& Crime(const std::string& k) { return CRIMES().at(k); }
int CrimeStars(const std::string& k) { auto it = CRIMES().find(k); return it == CRIMES().end() ? 0 : it->second.stars; }

const std::vector<std::string>& SAY(const std::string& k) {
	static const std::map<std::string, std::vector<std::string>> m = {
		{ "ticket", { "Crosswalk's right there, pal.", "That's a ticket.", "Use the crossing next time.", "Do you know how fast you were going?", "License and registration." } },
		{ "ticketed", { "Yeah, yeah...", "For THAT? Seriously?", "Fine. Whatever.", "I was in a hurry, officer!" } },
		{ "stop", { "Police! Stop!", "Freeze!", "Stop right there!", "On the ground!", "Pull over!" } },
		{ "surrender", { "Okay, okay! Don't shoot!", "I give up!", "It wasn't me!", "Alright, alright!" } },
		{ "flee", { "Not today!", "Can't catch me!", "Screw this!", "I'm outta here!" } },
		{ "cuffed", { "This is harassment!", "I want my lawyer!", "Ow! Watch the arm!", "You got the wrong guy!" } },
		{ "mug", { "Wallet. Now.", "Give me your money!", "Don't make this hard.", "Empty your pockets!" } },
		{ "mugged", { "Help! Police! He took my wallet!", "Thief! Somebody stop him!", "Help! I've been robbed!" } },
		{ "rage", { "Are you blind?!", "Look what you did to my car!", "Learn to drive, idiot!", "You wanna go?!", "Get out of the car!" } },
		{ "sorry", { "Sorry! Sorry!", "My bad!", "Whoa, are you okay?!", "I didn't see you!" } },
	};
	return m.at(k);
}
const std::string& Pick(const std::vector<std::string>& v) { return v[(size_t)std::floor(Rand() * v.size())]; }
template <typename T> T* PickPtr(const std::vector<T*>& v) { return v[(size_t)std::floor(Rand() * v.size())]; }

double Rough(const std::string& d) {
	static const std::map<std::string, double> m = { { "hood", 1.8 }, { "corona", 1.6 }, { "docks", 1.4 }, { "westside", 1.2 }, { "midtown", 1 }, { "downtown", 0.9 }, { "beach", 0.8 }, { "hills", 0.4 } };
	auto it = m.find(d);
	return it == m.end() ? 0.7 : it->second;
}

// where someone is: their car, their ragdoll, or them
V3 P(const Character* c) { return c->vehicle ? c->vehicle->pos : c->ragdolling && c->ragdoll ? c->ragdoll->center() : c->pos; }

int SeatCount(const Vehicle* v) { return v->layout.seats.empty() ? 2 : (int)v->layout.seats.size(); }
bool IsPlainLaneDriver(const VehicleAI* ai) { return ai && typeid(*ai) == typeid(LaneDriver); }
}

// ------------------------------------------------------------------ SuspectDriver
SuspectDriver::SuspectDriver(Game& g, Vehicle* v, std::shared_ptr<NpcCase> r) : LaneDriver(g, v, nullptr, true), rec(r) {
	fixedCruise = true;
	cruise = 20;
	ignoreLights = true;
	reverseT = 0;
	resnap();
}

V3 SuspectDriver::tp() const {
	auto r = rec.lock();
	Ped* p = r ? r->ped.get() : nullptr;
	return p ? P(p) : veh->pos;
}

bool SuspectDriver::chooseNext(const LanePath& cur, Exit& out) {
	const std::vector<Exit> opts = options(cur);
	if (opts.empty()) return false;
	const V3 t = tp();
	Exit best = opts[0];
	double bd = kInf;
	for (const Exit& o : opts) {
		const REdge& e = net.edges[o.e];
		const RNode& far = net.nodes[o.dir == 0 ? e.b : e.a];
		const double d = Hypot(far.x - t.x, far.z - t.z) + Rand() * 15;
		if (d < bd) { bd = d; best = o; }
	}
	out = best;
	return true;
}

void SuspectDriver::update(double dt) {
	Vehicle* v = veh;
	auto r = rec.lock();
	if (!v->driver() || v->isWrecked() || !r || !r->ped.get()) return;
	const V3 tpos = tp();
	const double d = Hypot(tpos.x - v->pos.x, tpos.z - v->pos.z);
	Vehicle* sv = r->ped->vehicle;
	const bool chase = r->phase == "chase" && sv;
	VehInput& inp = v->input;
	dist = d;
	auto stop = [&]() { inp.throttle = 0; inp.steer = 0; inp.brake = v->speedAbs() > 0.4 ? 1 : 0; inp.handbrake = v->speedAbs() <= 0.4; };
	// close enough to get out: stop (behind a pulled-over car, or by a suspect on foot)
	if (!chase && (d < (sv ? 11 : 16) || (r->park && d < 45))) { stop(); return; }
	// a car pulled over nearby: drive straight up behind it (turning round if we came the other way)
	if (sv && r->phase == "pulled" && sv->speedAbs() < 2 && d < 70) {
		const double tx = sv->pos.x - std::sin(sv->yaw) * 9, tz = sv->pos.z - std::cos(sv->yaw) * 9;
		const double dd = Hypot(tx - v->pos.x, tz - v->pos.z);
		if (dd < 3) { stop(); return; }
		double lx, lz; v->worldToLocal(tx, tz, lx, lz);
		const bool behind = lz < 0;
		if (reverseT > 0 || (behind && dd < 12)) {
			// just past it: back up
			reverseT = Max(0, reverseT - dt);
			inp.throttle = 0; inp.brake = v->speed() > -3 ? 0.6 : 0; inp.handbrake = false; inp.steer = Clamp(-std::atan2(lx, -lz) / (v->def.steer * 0.7), -1, 1);
			return;
		}
		inp.steer = Clamp(std::atan2(lx, Max(0.5, lz)) / (v->def.steer * 0.7), -1, 1);
		const double want = Clamp(dd * 0.5, 2.5, 9), err = want - v->speed();
		inp.throttle = err > 0 ? Clamp(err * 0.3, 0.15, 0.8) : 0;
		inp.brake = err < -1 ? Clamp(-err * 0.25, 0.2, 1) : 0;
		inp.handbrake = false;
		if (std::fabs(v->speed()) < 0.5 && want > 3) { stuck += dt; if (stuck > 2) { reverseT = 1.2; stuck = 0; } } else stuck = 0;
		return;
	}
	// a suspect driving: close in along the roads, then tail them (lights on: time to pull over) or run them down
	const bool tail = sv && !chase && r->phase == "respond" && sv->speedAbs() > 2;
	if (!(chase || tail) || d > 55) {
		cruise = sv ? 34 : r->phase == "respond" ? 24 : 16;
		if (direct) { direct = false; resnap(); }
		LaneDriver::update(dt);
		return;
	}
	direct = true;
	const double lead = Clamp(d / 30, 0, 1.2);
	const double tx = tpos.x + sv->vel.x * lead, tz = tpos.z + sv->vel.z * lead;
	double lx, lz; v->worldToLocal(tx, tz, lx, lz);
	auto probe = [&](double ang) {
		const double a = v->yaw + ang;
		RayHit h; RayOpts ro; ro.ignoreProps = true;
		return game.collision->raycast(v->pos.x, v->pos.y + 0.8, v->pos.z, std::sin(a), 0, std::cos(a), 14, h, ro) ? h.t : 14.0;
	};
	const double fL = probe(0.35), fR = probe(-0.35), fC = probe(0);
	double steer = std::atan2(lx, Max(0.5, lz)) / (v->def.steer * 0.7);
	if (fC < 9) steer += fL > fR ? 1.2 : -1.2; else if (fL < 6) steer -= 0.6; else if (fR < 6) steer += 0.6;
	inp.steer = Clamp(steer, -1, 1);
	if (reverseT > 0) { reverseT -= dt; inp.throttle = 0; inp.brake = 1; inp.steer = -inp.steer; inp.handbrake = false; return; }
	double want = chase ? 36 : Clamp(sv->speedAbs() + (d - 12) * 0.8, 0, 38);
	if (std::fabs(std::atan2(lx, lz)) > 1.4 && d < 30) want = Min(want, 9);
	const double speed = v->speed();
	if (std::fabs(speed) < 1 && want > 5) { stuck += dt; if (stuck > 1.5) { reverseT = 1.2; stuck = 0; } } else stuck = 0;
	const double err = want - speed;
	inp.throttle = err > 0 ? Clamp(err * 0.3, 0.2, 1) : 0;
	inp.brake = err < -2 ? Clamp(-err * 0.2, 0.2, 1) : 0;
	inp.handbrake = std::fabs(std::atan2(lx, lz)) > 1.2 && speed > 10;
}

// ------------------------------------------------------------------ FleeDriver
FleeDriver::FleeDriver(Game& g, Vehicle* v, std::function<V3()> f) : LaneDriver(g, v, nullptr, true), from(std::move(f)) {
	fixedCruise = true;
	cruise = Rand(24, 30);
	ignoreLights = true;
	resnap();
}

bool FleeDriver::chooseNext(const LanePath& cur, Exit& out) {
	const std::vector<Exit> opts = options(cur);
	if (opts.empty()) return false;
	const V3 f = from();
	Exit best = opts[0];
	double bd = -kInf;
	for (const Exit& o : opts) {
		const REdge& e = net.edges[o.e];
		const RNode& far = net.nodes[o.dir == 0 ? e.b : e.a];
		const double d = Hypot(far.x - f.x, far.z - f.z) + Rand() * 60;
		if (d > bd) { bd = d; best = o; }
	}
	out = best;
	return true;
}

// ------------------------------------------------------------------ PullOverDriver
PullOverDriver::PullOverDriver(Game& g, Vehicle* v) : LaneDriver(g, v, nullptr, true) { jam = 99; }

void PullOverDriver::update(double dt) {
	Vehicle* v = veh;
	VehInput& inp = v->input;
	t += dt;
	inp.throttle = 0; inp.steer = t < 1.2 && v->speedAbs() > 2 ? 0.3 : 0;
	inp.brake = v->speedAbs() > 0.4 ? 0.75 : 0; inp.handbrake = v->speedAbs() <= 0.4;
}

// ------------------------------------------------------------------ the system
NpcCrime::NpcCrime(Game& g) : game(g) {
	mugT = Rand(45, 90);
	theftT = Rand(70, 130);
	g.events.carCrash.on([this](Vehicle* A, Vehicle* B, double impact) { onCrash(A, B, impact); });
	g.events.pedHitByCar.on([this](Character* c, Vehicle* v, double spd) { onPedHit(c, v, spd); });
	g.events.melee.on([this](Character* att, Character* vic, const std::string&) { onViolence(att, vic, false); });
	g.events.kill.on([this](Character* killer, Character* victim, const std::string&, const std::string&) { onViolence(killer, victim, true); });
	g.events.wantedUp.on([this](int) { const auto cs = cases; for (const auto& rec : cs) release(rec); });
	// the radar: suspects (orange, flashing while the police are on them) and the units answering the call
	g.radarSources.push_back([this](std::vector<Blip>& out) {
		const bool flash = (int)std::floor(game.time * 4) % 2 != 0;
		std::vector<NpcTag> tags;
		tagged(tags);
		for (const NpcTag& t : tags) { const V3 q = t.c->vehicle ? t.c->vehicle->pos : t.c->pos; Blip b; b.x = q.x; b.z = q.z; b.color = t.hot && flash ? 0xff3333 : 0xff9a1f; b.icon = "dot"; b.small = true; b.noEdge = true; out.push_back(b); }
		if (Police* pol = game.policeSys) if (pol->level == 0) for (const auto& r : pol->cars) if (Vehicle* car = r.get()) if (car->npcJob && !car->removed) {
			Blip b; b.x = car->pos.x; b.z = car->pos.z; b.color = flash ? 0x3355ff : 0xdde4ff; b.icon = "dot"; b.small = true; b.noEdge = true; b.square = true; out.push_back(b);
		}
	});
}

double NpcCrime::level() const {
	const std::string& s = game.settings.npcCrime;
	return s == "off" ? 0 : s == "high" ? 2.2 : 1;
}

bool NpcCrime::running() const {
	return level() > 0 && !game.disableAmbient && !game.cutscene && !(game.gameplay && game.gameplay->state == "menu");
}

void NpcCrime::after(double sec, std::function<void()> fn) { later.push_back({ game.time + sec, std::move(fn) }); }

bool NpcCrime::policeFree() const { const Police* p = game.policeSys; return p && p->enabled && p->level == 0 && !game.player->dead; }
V3 NpcCrime::pp() const { const Player& p = *game.player; return p.vehicle ? p.vehicle->pos : p.pos; }

// Think calls come unevenly (twice in a frame for people out of view), so timers run on the game clock:
// the time since this character's last call, not the dt handed in.
double NpcCrime::clockDt(Ped* c) {
	const double t = game.time, d = Clamp(t - (IsSet(c->crimeClock) ? c->crimeClock : t), 0, 0.25);
	c->crimeClock = t;
	return d;
}

bool NpcCrime::civ(const Ped* p) const {
	return p && !p->removed && !p->dead && !p->isPlayer && p->brain == "civilian" && !p->persistent && p->missionTag.empty() && !p->shopClerk &&
		!p->npcCase && !p->crimeTask && p->state != "follow" && !p->ragdolling;
}

Ped* NpcCrime::driverOf(Vehicle* v) const {
	if (!v) return nullptr;
	Ped* d = dynamic_cast<Ped*>(v->driver());
	return d && !v->removed && !v->policeUnit && !v->def.police && !v->persistent && !v->isWrecked() && civ(d) && v->ai ? d : nullptr;
}

// ------------------------------------------------------------------ witnesses
// the nearest police unit that can see a spot: a patrol car (or its crew on foot)
Vehicle* NpcCrime::policeSees(double x, double y, double z, double R) const {
	const Police* pol = game.policeSys;
	if (!pol) return nullptr;
	Vehicle* best = nullptr;
	double bd = R * R;
	auto look = [&](double ux, double uy, double uz, Vehicle* unit) {
		const double d2 = Dist2(ux, uz, x, z);
		if (d2 >= bd) return;
		if (d2 > 144 && !game.collision->lineOfSight(ux, uy + 1.5, uz, x, y + 1.2, z)) return;
		bd = d2; best = unit;
	};
	for (const auto& r : pol->cars) if (Vehicle* v = r.get()) {
		Ped* d = dynamic_cast<Ped*>(v->driver());
		if (!v->removed && !v->isWrecked() && d && d->brain == "cop" && !d->dead) look(v->pos.x, v->pos.y, v->pos.z, v);
	}
	for (const auto& r : pol->cops) if (Ped* c = r.get()) {
		Vehicle* home = c->homeCar.get();
		if (!c->dead && !c->removed && !c->vehicle && home && !home->removed) look(c->pos.x, c->pos.y, c->pos.z, home);
	}
	return best;
}

// ------------------------------------------------------------------ crimes
// An NPC broke the law. Seen by the police it counts at once; otherwise somebody may call it in.
NpcCase* NpcCrime::commit(Ped* ped, const std::string& key, const CommitOpts& opts) {
	if (!running() || !ped || ped->dead || ped->removed || ped->isPlayer) return nullptr;
	const CrimeDef& C = Crime(key);
	std::shared_ptr<NpcCase> rec = ped->npcCase;
	if (!rec) {
		if ((int)cases.size() >= (level() > 1 ? 7 : 4) && !opts.force) return nullptr;
		rec = std::make_shared<NpcCase>();
		rec->ped = Ref<Ped>(ped); rec->crime = key; rec->t = game.time; rec->seenT = game.time;
		rec->scene = P(ped);
		ped->npcCase = rec;
		cases.push_back(rec);
	}
	if (C.add) rec->stars = (int)Min(5, rec->stars + C.add);
	if (C.stars > CrimeStars(rec->crime) || C.add) rec->crime = key;
	rec->stars = (int)Max(rec->stars, C.stars);
	if (opts.car) rec->car = Ref<Vehicle>(opts.car);
	rec->pos = P(ped);
	const V3 pos = P(ped);
	Vehicle* unit = opts.hasWitness ? opts.witness : policeSees(pos.x, pos.y, pos.z, opts.seeR);
	if (unit || rec->known) know(rec, unit);
	else if (opts.call && Rand() < opts.call) rec->callAt = Min(IsSet(rec->callAt) ? rec->callAt : kInf, game.time + Rand(opts.delay0, opts.delay1));
	else if (!IsSet(rec->callAt)) rec->expire = game.time + 25; // nobody saw: it only counts if a patrol spots them soon
	return rec.get();
}

void NpcCrime::know(const std::shared_ptr<NpcCase>& rec, Vehicle* unit) {
	const bool first = !rec->known;
	Ped* ped = rec->ped.get();
	rec->known = true; rec->callAt = NaN(); rec->expire = NaN();
	rec->seenT = game.time; rec->pos = P(ped);
	ped->npcWanted = rec->stars;
	// (kept around while wanted: a chase can run further than pedestrians normally live from the player)
	if (first) { rec->pedPersistent = ped->persistent ? 1 : 0; ped->persistent = true; }
	if (Vehicle* car = rec->car.get()) if (!car->removed) { if (rec->carPersistent < 0) rec->carPersistent = car->persistent ? 1 : 0; car->persistent = true; }
	if (first || rec->stars != rec->shownStars) {
		rec->shownStars = rec->stars;
		const V3 p = pp();
		if (Dist2(rec->pos.x, rec->pos.z, p.x, p.z) < 260 * 260 && game.hud) {
			std::string s;
			for (int i = 0; i < rec->stars; i++) s += "\xe2\x98\x85";
			game.hud->dispatch(s + " " + Crime(rec->crime).name, game.map.ZoneName(rec->pos.x, rec->pos.z));
		}
	}
	if (unit && !rec->unit && policeFree() && !unit->npcJob) assign(rec, unit);
}

// ------------------------------------------------------------------ police response
void NpcCrime::dispatchUnit(const std::shared_ptr<NpcCase>& rec) {
	Police* pol = game.policeSys;
	if (!policeFree() || rec->unit) return;
	const V3 sp = P(rec->ped.get());
	Vehicle* best = nullptr;
	double bd = 300 * 300;
	for (const auto& r : pol->cars) if (Vehicle* v = r.get()) {
		Ped* d = dynamic_cast<Ped*>(v->driver());
		if (v->removed || v->isWrecked() || v->npcJob || !d || d->brain != "cop" || d->dead) continue;
		const double d2 = Dist2(v->pos.x, v->pos.z, sp.x, sp.z);
		if (d2 < bd) { bd = d2; best = v; }
	}
	const V3 p = pp();
	if (!best && Dist2(sp.x, sp.z, p.x, p.z) < 220 * 220 && game.time - spawnedT > 5) {
		best = pol->spawnCar(false, &sp);
		spawnedT = game.time;
	}
	if (best) assign(rec, best);
}

void NpcCrime::assign(const std::shared_ptr<NpcCase>& rec, Vehicle* car) {
	car->npcJob = rec;
	rec->unit = std::make_shared<NpcCase::Unit>();
	rec->unit->car = Ref<Vehicle>(car);
	for (const auto& o : car->occupants) if (Ped* c = dynamic_cast<Ped*>(o.get())) if (c->brain == "cop") rec->unit->cops.push_back(Ref<Ped>(c));
	for (const auto& r : rec->unit->cops) if (Ped* c = r.get()) c->homeCar = Ref<Vehicle>(car);
	car->ai = std::make_shared<SuspectDriver>(game, car, rec);
	car->sirenOn = true;
	if (rec->phase == "open") rec->phase = "respond";
}

void NpcCrime::release(const std::shared_ptr<NpcCase>& rec) {
	auto u = rec->unit;
	rec->unit.reset(); rec->park = false;
	if (rec->phase != "closed" && rec->phase != "chase") rec->phase = "open";
	if (!u) return;
	for (const auto& r : u->cops) if (Ped* c = r.get()) {
		if (c->npcTask == rec) c->npcTask.reset();
		if (c->hasNpcAim) { c->accuracy = c->npcAimAccuracy; c->damageMul = c->npcAimDamageMul; c->hasNpcAim = false; }
		if (c->threat.get() == rec->ped.get()) c->threat = nullptr;
	}
	Vehicle* car = u->car.get();
	if (!car) return;
	if (car->npcJob == rec) car->npcJob.reset();
	if (!car->removed && dynamic_cast<SuspectDriver*>(car->ai.get())) { car->ai = std::make_shared<LaneDriver>(game, car, nullptr); car->sirenOn = false; }
}

void NpcCrime::close(const std::shared_ptr<NpcCase>& rec, const std::string& why) {
	if (rec->phase == "closed") return;
	Ped* ped = rec->ped.get();
	release(rec);
	rec->phase = "closed"; rec->why = why;
	for (size_t i = 0; i < cases.size(); i++) if (cases[i] == rec) { cases.erase(cases.begin() + i); break; }
	if (ped) {
		if (ped->npcCase == rec) { ped->npcCase.reset(); ped->npcWanted = 0; ped->hasFleeSpeed = false; }
		if (rec->pedPersistent == 0 && !ped->removed) ped->persistent = false;
		if (!(ped->crimeTask && ped->crimeTask->kind == "escort")) ped->crimeTask.reset();
	}
	Vehicle* car = rec->car.get();
	if (car && !car->removed) {
		car->persistent = rec->carPersistent > 0;
		// a getaway car still under its driver goes back to normal traffic (which also tidies it away later)
		if (ped && car->driver() == ped && !ped->dead && (dynamic_cast<FleeDriver*>(car->ai.get()) || dynamic_cast<PullOverDriver*>(car->ai.get()))) car->ai = std::make_shared<LaneDriver>(game, car, nullptr);
		bool inTraffic = false;
		for (const auto& r : game.traffic->cars) if (r.get() == car) { inTraffic = true; break; }
		if (!car->policeUnit && !inTraffic && !car->ownedByPlayer && game.player->vehicle != car) { car->traffic = true; game.traffic->cars.push_back(Ref<Vehicle>(car)); }
	}
	if (rec->known && why != "gone") {
		const V3 p = pp(), sp = rec->pos;
		if (Dist2(sp.x, sp.z, p.x, p.z) < 200 * 200 && game.hud) {
			const char* msg = why == "arrested" ? "Suspect in custody" : why == "ticketed" ? "Ticket issued" : why == "escaped" ? "Suspect got away" : why == "dead" ? "Suspect down" : nullptr;
			if (msg) game.hud->dispatch(msg);
		}
	}
}

// ------------------------------------------------------------------ per frame
void NpcCrime::update(double dt) {
	if (!running()) { const auto cs = cases; for (const auto& rec : cs) close(rec, "gone"); later.clear(); return; }
	for (int i = (int)later.size() - 1; i >= 0; i--) if (i < (int)later.size() && game.time >= later[i].first) { auto fn = later[i].second; later.erase(later.begin() + i); fn(); }
	// people with something on who are now in a car (their own think stops once they're in a seat)
	{
		std::vector<std::shared_ptr<Ped>> peds = game.peds->list;
		for (const auto& p : peds) {
			auto t = p->crimeTask;
			if (!t || !p->vehicle) continue;
			if (t->kind == "steal" && p->vehicle == t->car.get()) stolen(p.get(), *t);
			else if (t->kind == "rejoin" || t->kind == "argue") {
				p->crimeTask.reset();
				if (p->vehicle->driver() == p.get()) if (auto* ld = dynamic_cast<LaneDriver*>(p->vehicle->ai.get())) ld->resnap();
			}
		}
	}
	stageTick(dt);
	scanT -= dt;
	if (scanT <= 0) { scanT = 0.4; scan(); }
	const auto cs = cases;
	for (const auto& rec : cs) updateCase(rec, dt);
}

// ongoing offences a patrol can catch in the act: jaywalking, speeding
void NpcCrime::scan() {
	const V3 p0 = pp();
	const std::vector<std::shared_ptr<Ped>> peds = game.peds->list;
	for (const auto& sp : peds) {
		Ped* p = sp.get();
		auto t = p->crimeTask;
		if (!t || t->kind != "jaywalk" || p->dead || p->npcCase) continue;
		if (!game.map.IsOnRoad(p->pos.x, p->pos.z)) continue;
		Vehicle* unit = policeSees(p->pos.x, p->pos.y, p->pos.z, 36);
		if (unit) { CommitOpts o; o.hasWitness = true; o.witness = unit; commit(p, "jaywalk", o); }
		// drivers lean on the horn
		for (const auto& r : game.traffic->cars) if (Vehicle* v = r.get()) {
			if (v->removed || !v->driver() || v->speedAbs() < 2 || v->hornT > game.time) continue;
			double lx, lz; v->worldToLocal(p->pos.x, p->pos.z, lx, lz);
			if (lz > 0 && lz < 16 && std::fabs(lx) < 3) { v->hornT = game.time + 6; game.soundAt("horn", v->pos, 0.8); }
		}
	}
	for (const auto& r : game.traffic->cars) if (Vehicle* v = r.get()) {
		LaneDriver* ai = dynamic_cast<LaneDriver*>(v->ai.get());
		Ped* d = dynamic_cast<Ped*>(v->driver());
		if (!ai || !ai->reckless || v->removed || v->isWrecked() || !v->driver() || (d && d->npcCase) || v->driver()->dead) continue;
		if (Dist2(v->pos.x, v->pos.z, p0.x, p0.z) > 300 * 300) continue;
		const LanePath* lane = ai->paths.empty() ? nullptr : ai->paths[0].get();
		const double limit = !lane ? 14 : !lane->turn ? lane->speed : lane->to ? lane->to->speed : 14;
		if (v->speedAbs() < Max(13, limit * 1.15)) continue;
		Vehicle* unit = policeSees(v->pos.x, v->pos.y, v->pos.z, 48);
		if (unit && d) { CommitOpts o; o.hasWitness = true; o.witness = unit; o.car = v; commit(d, "speeding", o); }
	}
}

void NpcCrime::updateCase(const std::shared_ptr<NpcCase>& rec, double dt) {
	Ped* ped = rec->ped.get();
	if (!ped || ped->removed) { close(rec, "gone"); return; }
	if (ped->dead) { close(rec, "dead"); return; }
	const V3 sp = P(ped), p0 = pp();
	if (Dist2(sp.x, sp.z, p0.x, p0.z) > 330 * 330) { close(rec, "gone"); return; }
	if (!rec->known) {
		if (IsSet(rec->callAt) && game.time >= rec->callAt) know(rec);
		else if (IsSet(rec->expire) && game.time > rec->expire) { close(rec, "gone"); return; }
		else if (Rand() < dt * 2) { if (Vehicle* u = policeSees(sp.x, sp.y, sp.z, 30)) know(rec, u); }
		if (!rec->known) return;
	}
	ped->npcWanted = rec->stars;
	// in sight of any unit?
	if (Rand() < dt * 3) {
		Vehicle* u = policeSees(sp.x, sp.y, sp.z, 50);
		bool near = false;
		if (!u && rec->unit) for (const auto& r : rec->unit->cops) if (Ped* c = r.get()) if (!c->dead && !c->vehicle && Dist2(c->pos.x, c->pos.z, sp.x, sp.z) < 30 * 30) { near = true; break; }
		if (u || near) { rec->seenT = game.time; rec->pos = sp; }
	}
	const double lost = game.time - rec->seenT;
	if (lost > 22 + rec->stars * 6) { close(rec, "escaped"); return; }
	if (!policeFree()) { if (rec->unit) release(rec); return; }
	// the unit: still a working police car with its crew?
	if (auto u = rec->unit) {
		std::vector<Ref<Ped>> keep;
		for (const auto& r : u->cops) if (Ped* c = r.get()) if (!c->removed && !c->dead) keep.push_back(r);
		u->cops = keep;
		Vehicle* car = u->car.get();
		Ped* d = car ? dynamic_cast<Ped*>(car->driver()) : nullptr;
		if (!car || car->removed || car->isWrecked() || u->cops.empty() || (car->driver() && (!d || d->brain != "cop"))) release(rec);
	}
	if (!rec->unit) { rec->dispatchT -= dt; if (rec->dispatchT <= 0) { rec->dispatchT = 2; dispatchUnit(rec); } return; }
	direct(rec, dt);
}

// what happens between a unit and its suspect
void NpcCrime::direct(const std::shared_ptr<NpcCase>& rec, double dt) {
	Ped* ped = rec->ped.get();
	Vehicle* car = rec->unit->car.get();
	const std::vector<Ref<Ped>> cops = rec->unit->cops;
	const V3 sp = P(ped);
	const double dCar = Hypot(car->pos.x - sp.x, car->pos.z - sp.z);
	Vehicle* sv = ped->vehicle;
	const CrimeDef& C = Crime(rec->crime);
	// first contact (the cruiser pulls up, or its crew gets close on foot): comply, run, or (armed and desperate) fight
	double dCop = kInf;
	for (const auto& r : cops) if (Ped* c = r.get()) if (!c->vehicle) dCop = Min(dCop, Hypot(c->pos.x - sp.x, c->pos.z - sp.z));
	if (rec->phase == "respond" && (dCar < (sv ? 30 : 34) || dCop < 22)) {
		rec->phase = "confront";
		// (only someone holding a gun takes on armed police)
		const bool armed = ped->weaponDef().type == "gun";
		const double r = Rand();
		rec->reaction = !rec->force.empty() ? rec->force : (C.ticket ? (r < 0.78 ? "comply" : "flee") : armed && rec->stars >= 2 && r < 0.06 ? "fight" : r < 0.42 ? "comply" : "flee");
		if (sv) {
			if (!rec->car.get()) { rec->car = Ref<Vehicle>(sv); if (rec->carPersistent < 0) rec->carPersistent = sv->persistent ? 1 : 0; sv->persistent = true; }
			if (rec->reaction == "comply") { sv->ai = std::make_shared<PullOverDriver>(game, sv); rec->phase = "pulled"; }
			else carChase(rec, sv);
		} else if (rec->reaction == "flee") { rec->phase = "chase"; commit(ped, "resisting"); ped->say(Pick(SAY("flee"))); }
		else if (rec->reaction == "fight") { rec->phase = "chase"; commit(ped, "copAssault"); }
		else ped->say(Pick(SAY("surrender")));
	}
	// a car chase ends when the getaway car can't go on: out they get, on foot
	if (rec->phase == "chase" && sv) {
		// stopped, or pinned against the cruiser
		rec->stopT = sv->speedAbs() < 1.2 || (sv->speedAbs() < 4 && dCar < 8) ? rec->stopT + dt : Max(0, rec->stopT - dt);
		if ((sv->isWrecked() || sv->health < sv->maxHealth * 0.45 || (rec->stopT > 2.5 && dCar < 30)) && !game.vehicles.isBusy(ped)) {
			game.vehicles.exit(ped);
			rec->reaction = Rand() < 0.55 ? "flee" : "comply";
			if (rec->reaction == "comply") { rec->phase = "confront"; ped->say(Pick(SAY("surrender"))); } else ped->say(Pick(SAY("flee")));
		}
	}
	// suspect on foot and close: park up and let the crew out
	rec->park = !sv && dCar < 45;
	// crew out when the suspect is close on foot, or pulled over and stopped; back in for a car chase
	const bool out = rec->phase != "escort" && ((!sv && dCar < 40) || (rec->phase == "pulled" && sv && sv->speedAbs() < 1 && dCar < 20));
	if (out && car->speedAbs() < 2) for (const auto& r : cops) if (Ped* c = r.get()) if (c->vehicle == car && !game.vehicles.isBusy(c)) { game.vehicles.exit(c); c->npcTask = rec; }
	if ((rec->phase == "chase" && sv) || rec->phase == "escort") {
		for (const auto& r : cops) if (Ped* c = r.get()) if (!c->vehicle && !game.vehicles.isBusy(c)) {
			int seat = -1;
			for (int s : { 0, 1 }) if (!car->occupants[s]) { seat = s; break; }
			if (seat >= 0) game.vehicles.enter(c, car, seat, true);
		}
	}
	for (const auto& r : cops) if (Ped* c = r.get()) if (!c->vehicle) c->npcTask = rec;
	// booked: suspect in the back, crew in the front: drive off
	if (rec->phase == "escort") {
		rec->escortT += dt;
		if (!ped->vehicle && !game.vehicles.isBusy(ped) && rec->escortT > 12) {
			int seat = -1;
			for (int s : { 2, 3, 1 }) if (s < SeatCount(car) && !car->occupants[s]) { seat = s; break; }
			if (seat >= 0) game.vehicles.seatNow(ped, car, seat);
		}
		bool allIn = true;
		for (const auto& r : cops) if (Ped* c = r.get()) if (c->vehicle != car) { allIn = false; break; }
		if (ped->vehicle == car && allIn) { ped->crimeTask = std::make_shared<CrimeTask>(); ped->crimeTask->kind = "escort"; close(rec, "arrested"); }
	}
}

void NpcCrime::carChase(const std::shared_ptr<NpcCase>& rec, Vehicle* sv) {
	rec->phase = "chase";
	commit(rec->ped.get(), "evading");
	Ref<Vehicle> car = rec->unit ? rec->unit->car : Ref<Vehicle>();
	std::weak_ptr<NpcCase> wr = rec;
	const V3 scene = rec->scene;
	sv->ai = std::make_shared<FleeDriver>(game, sv, [car, wr, scene]() { Vehicle* c = car.get(); if (c && !c->removed) return c->pos; auto r = wr.lock(); return r ? r->scene : scene; });
	rec->car = Ref<Vehicle>(sv);
	if (rec->carPersistent < 0) rec->carPersistent = sv->persistent ? 1 : 0;
	sv->persistent = true;
}

// ------------------------------------------------------------------ cops on a job (called from Police::copThink)
bool NpcCrime::copThink(Ped* cop, double dtIn) {
	(void)dtIn;
	const double dt = clockDt(cop);
	std::shared_ptr<NpcCase> rec = cop->npcTask;
	if (!rec || rec->phase == "closed" || !rec->unit || rec->unit->car.get() != cop->homeCar.get()) { cop->npcTask.reset(); return false; }
	Ped* ped = rec->ped.get();
	if (!ped) return false;
	if (rec->phase == "escort" || (rec->phase == "chase" && ped->vehicle)) return false; // back to the car
	Vehicle* sv = ped->vehicle;
	if (sv) {
		// walk up to the driver's window
		if (rec->phase != "pulled") return false;
		const V3 door = sv->doorWorld();
		const double d = Hypot(door.x - cop->pos.x, door.z - cop->pos.z);
		if (d > 1.6) { cop->goTo(door.x, door.z, 2.6, dt, 1.2); return true; }
		cop->stop(); cop->faceTowards(sv->pos.x, sv->pos.z, dt, 6); cop->animState.talking = true;
		rec->talkT += dt;
		if (!rec->said) { rec->said = true; cop->say(Pick(SAY("ticket"))); }
		if (rec->talkT > 4.5) {
			if (Crime(rec->crime).ticket) { ped->say(Pick(SAY("ticketed"))); if (sv->driver() == ped) sv->ai = std::make_shared<LaneDriver>(game, sv, nullptr); close(rec, "ticketed"); }
			else if (!game.vehicles.isBusy(ped)) { game.vehicles.exit(ped); rec->phase = "confront"; rec->reaction = "comply"; rec->talkT = 0; }
		}
		return true;
	}
	const V3 sp = P(ped);
	const double dx = sp.x - cop->pos.x, dz = sp.z - cop->pos.z, d = Hypot(dx, dz);
	if (rec->reaction == "fight" && cop->weaponDef().type == "gun" && d < 45) {
		// (the police hold back against the player when you're not wanted; not against an armed suspect)
		if (!cop->hasNpcAim) { cop->hasNpcAim = true; cop->npcAimAccuracy = cop->accuracy; cop->npcAimDamageMul = cop->damageMul; }
		cop->accuracy = Max(cop->accuracy, 0.6); cop->damageMul = Max(cop->damageMul, 1);
		cop->threat = Ref<Character>(ped); cop->attack(dt);
		return true;
	}
	cop->aiming = false;
	if ((IsSet(cop->lineT) ? cop->lineT : 0) < game.time && d < 26 && rec->phase == "chase") { cop->lineT = game.time + Rand(5, 9); cop->say(Pick(SAY("stop"))); }
	const bool running = ped->state == "flee" || rec->reaction == "flee";
	if (d > (running ? 1.4 : 1.5) || ped->ragdolling) {
		// (a runner gets chased flat out right up to the tackle; someone waiting gets walked up to)
		if (d > 5 || (running && !ped->ragdolling)) run(cop, sp.x, sp.z, running ? 7.3 : 6, dt); else cop->goTo(sp.x, sp.z, 2.8, dt, 1.3);
		return true;
	}
	cop->stop(); cop->faceTowards(sp.x, sp.z, dt, 10);
	if (running) {
		// tackle
		const double dd = d ? d : 1;
		ped->knockDown(V3(dx / dd * 3.5, 1.2, dz / dd * 3.5));
		rec->reaction = "comply"; rec->phase = "arrest"; rec->cuffT = -1.5;
		ped->setState("wander");
		return true;
	}
	if (Crime(rec->crime).ticket) {
		cop->animState.talking = true;
		rec->talkT += dt;
		if (!rec->said) { rec->said = true; cop->say(Pick(SAY("ticket"))); }
		if (rec->talkT > 4) { ped->say(Pick(SAY("ticketed"))); close(rec, "ticketed"); }
		return true;
	}
	rec->phase = "arrest";
	rec->cuffT += dt;
	if (rec->cuffT > 2.5 && !ped->ragdolling) {
		rec->phase = "escort";
		ped->say(Pick(SAY("cuffed")));
		Vehicle* car = rec->unit->car.get();
		int seat = -1;
		for (int s : { 2, 3, 1 }) if (s < SeatCount(car) && !car->occupants[s]) { seat = s; break; }
		if (seat >= 0) game.vehicles.enter(ped, car, seat, true);
		else { ped->crimeTask = std::make_shared<CrimeTask>(); ped->crimeTask->kind = "escort"; close(rec, "arrested"); ped->remove(); }
	}
	return true;
}

// ------------------------------------------------------------------ NPCs with something on (called from Ped::think)
bool NpcCrime::pedThink(Ped* ped, double dtIn) {
	(void)dtIn;
	const double dt = clockDt(ped);
	std::shared_ptr<CrimeTask> t = ped->crimeTask;
	if (t) {
		const std::string& k = t->kind;
		if (k == "jaywalk") {
			t->t += dt;
			// caught in the act: carry on across until the police get here, then the case decides
			if (ped->npcCase && ped->npcCase->phase != "open" && ped->npcCase->phase != "respond") ped->crimeTask.reset();
			else {
				if (t->t < 0.6) { ped->stop(); ped->faceTowards(t->x, t->z, dt, 6); return true; }
				if (ped->goTo(t->x, t->z, t->sp, dt, 0.8) || t->t > 25) {
					const auto& nodes = game.map.walkNodes;
					int best = -1; double bd = kInf;
					for (int i : t->nodeIds) { const WalkNode& n = nodes[i]; const double d = Dist2(n.x, n.z, ped->pos.x, ped->pos.z); if (d < bd) { bd = d; best = i; } }
					ped->node = best; ped->prevNode = -1; ped->crimeTask.reset(); ped->setState("wander");
					return false;
				}
				return true;
			}
		} else if (k == "mug") return mugThink(ped, *t, dt);
		else if (k == "victim") {
			t->t += dt;
			Ped* m = t->by.get();
			if (!m || m->dead || m->removed || t->t > 6 || ped->ragdolling) {
				ped->crimeTask.reset();
				if (m) { ped->threat = Ref<Character>(m); ped->threatPos = m->pos; }
				ped->setState("flee");
				return false;
			}
			ped->stop(); ped->animState.handsUp = true; ped->faceTowards(m->pos.x, m->pos.z, dt, 6);
			return true;
		} else if (k == "steal") {
			t->t += dt;
			Vehicle* car = t->car.get();
			if (!car || car->removed || car->isWrecked() || t->t > 14 || (car->driver() && car->driver()->isPlayer)) { ped->crimeTask.reset(); return false; }
			if (!game.vehicles.isBusy(ped) && t->t > 0.2 && !t->tried) { t->tried = true; if (!game.vehicles.enter(ped, car, 0)) ped->crimeTask.reset(); }
			return true;
		} else if (k == "argue") return argueThink(ped, *t, dt);
		else if (k == "rejoin") {
			if (!game.vehicles.isBusy(ped)) { ped->crimeTask.reset(); return false; }
			return true;
		} else if (k == "escort") return true;
	}
	std::shared_ptr<NpcCase> rec = ped->npcCase;
	if (!rec || !rec->known || !rec->unit || ped->vehicle) return false;
	Ped* cop = nearestCop(*rec, ped);
	// waiting on a ticket / hands up for the cuffs / being walked to the car
	if (rec->phase == "escort") return true;
	if (rec->reaction == "comply" && (rec->phase == "confront" || rec->phase == "arrest")) {
		ped->stop();
		if (cop) ped->faceTowards(cop->pos.x, cop->pos.z, dt, 5);
		if (Crime(rec->crime).ticket) ped->animState.talking = true; else ped->animState.handsUp = true;
		return true;
	}
	if (rec->reaction == "flee" && rec->phase == "chase" && cop) {
		if (ped->state != "flee") ped->setState("flee");
		ped->threat = Ref<Character>(cop); ped->stateTime = Min(ped->stateTime, 5);
		// flat out at first, then tiring
		if (!IsSet(rec->fleeAt)) rec->fleeAt = game.time;
		ped->hasFleeSpeed = true; ped->fleeSpeed = Clamp(6.3 - (game.time - rec->fleeAt) * 0.07, 5, 6.3);
		return false;
	}
	if (rec->reaction == "fight" && cop) {
		if (ped->weaponDef().type != "gun" && ped->weapons.count("pistol")) ped->equip("pistol");
		ped->threat = Ref<Character>(cop);
		if (ped->state != "attack") ped->setState("attack");
		return false;
	}
	return false;
}

// run at a point, sliding round walls on the way (a straight line pinned a cop to a building mid-chase)
void NpcCrime::run(Ped* c, double x, double z, double sp, double dt) {
	(void)dt;
	double dx = x - c->pos.x, dz = z - c->pos.z;
	double l = Hypot(dx, dz); if (!l) l = 1;
	dx /= l; dz /= l;
	const auto pr = game.collision->resolveCircle(c->pos.x + dx * 1.6, c->pos.z + dz * 1.6, 0.4, c->pos.y + 0.3, 1.4);
	if (pr.hit) {
		dx += (pr.x - (c->pos.x + dx * 1.6)) * 2; dz += (pr.z - (c->pos.z + dz * 1.6)) * 2;
		double m = Hypot(dx, dz); if (!m) m = 1;
		dx /= m; dz /= m;
	}
	c->moveTargetX = dx * sp; c->moveTargetZ = dz * sp;
	c->yaw = std::atan2(dx, dz);
}

Ped* NpcCrime::nearestCop(const NpcCase& rec, Ped* ped) const {
	Ped* best = nullptr; double bd = kInf;
	if (rec.unit) for (const auto& r : rec.unit->cops) if (Ped* c = r.get()) {
		if (c->dead) continue;
		const double d = Dist2(c->pos.x, c->pos.z, ped->pos.x, ped->pos.z);
		if (d < bd) { bd = d; best = c; }
	}
	return best;
}

// ------------------------------------------------------------------ jaywalking (asked by Ped::wander now and then)
// straight across the nearest street, well away from the corner: that's what the crossings are for
bool NpcCrime::jaywalk(Ped* ped, bool force) {
	if (!running() || ped->brain != "civilian" || ped->persistent || ped->npcCase || ped->crimeTask) return false;
	if (ped->jaywalker < 0) ped->jaywalker = Rand() < 0.2 * Min(2, level()) ? 1 : 0;
	if (!force && !ped->jaywalker) return false;
	const CityMap& map = game.map;
	const double x = ped->pos.x, z = ped->pos.z;
	if (!map.BlockAt(x, z)) return false;
	const int i = map.NearestX(x), j = map.NearestZ(z);
	const double dx = std::fabs(x - XS[i]), dz = std::fabs(z - ZS[j]);
	double tx, tz;
	if (dx < dz) {
		if (dx > HALF_ROAD + 6 || dz < 24 || !map.IsOnCityStreet(XS[i], z)) return false;
		tx = 2 * XS[i] - x + Rand(-0.8, 0.8); tz = z + Rand(-6, 6);
	} else {
		if (dz > HALF_ROAD + 6 || dx < 24 || !map.IsOnCityStreet(x, ZS[j])) return false;
		tz = 2 * ZS[j] - z + Rand(-0.8, 0.8); tx = x + Rand(-6, 6);
	}
	// map.walkAreaAt: the block there, or a town's walk area
	const std::vector<int>* ids = nullptr;
	if (const Block* b = map.BlockAt(tx, tz)) ids = &b->nodeIds;
	else for (const TownArea& ta : map.townAreas) if (Hypot(tx - ta.x, tz - ta.z) < ta.r && !ta.nodeIds.empty()) { ids = &ta.nodeIds; break; }
	if (!ids || ids->empty() || map.IsOnRoad(tx, tz)) return false;
	auto t = std::make_shared<CrimeTask>();
	t->kind = "jaywalk"; t->x = tx; t->z = tz; t->nodeIds = *ids; t->sp = Rand() < 0.35 ? 3.3 : 1.8; t->t = 0;
	ped->crimeTask = t;
	return true;
}

// ------------------------------------------------------------------ staged crimes
void NpcCrime::stageTick(double dt) {
	if (game.missionActive) return; // (a mission's set pieces come first)
	const double lvl = level();
	const V3 p0 = pp();
	const double night = game.env.night > 0.5 ? 1.7 : 1;
	const double rough = Rough(game.map.DistrictAt(p0.x, p0.z));
	const double k = lvl * night * rough;
	// a few drivers in a hurry
	recklessT -= dt;
	if (recklessT <= 0) {
		recklessT = 6;
		const int want = (int)std::round((lvl > 1 ? 3 : 1) * Min(1.5, night));
		int n = 0;
		std::vector<Vehicle*> cands;
		for (const auto& r : game.traffic->cars) if (Vehicle* v = r.get()) {
			if (v->removed || !(Dist2(v->pos.x, v->pos.z, p0.x, p0.z) < 250 * 250)) continue;
			LaneDriver* ai = dynamic_cast<LaneDriver*>(v->ai.get());
			if (ai && ai->reckless) n++;
			else if (driverOf(v) && IsPlainLaneDriver(v->ai.get()) && v->type != "taxi" && v->def.bike.empty() && Dist2(v->pos.x, v->pos.z, p0.x, p0.z) > 50 * 50) cands.push_back(v);
		}
		if (n < want && !cands.empty()) makeReckless(PickPtr(cands));
	}
	mugT -= dt * k;
	if (mugT <= 0) { mugT = Rand(60, 120); stage("mug"); }
	theftT -= dt * k;
	if (theftT <= 0) { theftT = Rand(80, 150); stage("steal"); }
}

void NpcCrime::makeReckless(Vehicle* v) {
	LaneDriver* ai = dynamic_cast<LaneDriver*>(v->ai.get());
	if (!ai) return;
	ai->reckless = true;
	ai->cruiseFactor = Rand(1.35, 1.65);
	ai->ignoreLights = Rand() < 0.65;
}

// set up a crime near the player (the director, and the admin "crime now" command)
bool NpcCrime::stage(const std::string& kind) {
	const V3 p0 = pp();
	std::vector<Ped*> peds;
	for (const auto& p : game.peds->list) if (civ(p.get()) && !p->vehicle && !p->walkedDog && (p->state == "wander" || p->state == "idle")) peds.push_back(p.get());
	auto within = [&](const V3& p, double r0, double r1) { const double d2 = Dist2(p.x, p.z, p0.x, p0.z); return d2 > r0 * r0 && d2 < r1 * r1; };
	// (the browser game sorts with a random comparator: a shuffle)
	auto shuffled = [](std::vector<Ped*> v) { for (int i = (int)v.size() - 1; i > 0; i--) std::swap(v[i], v[(size_t)std::floor(Rand() * (i + 1))]); return v; };
	if (kind == "mug") {
		std::vector<Ped*> ms; for (Ped* p : peds) if (within(p->pos, 18, 75)) ms.push_back(p);
		for (Ped* m : shuffled(ms)) {
			Ped* v = nullptr;
			for (Ped* p : peds) { const double d2 = Dist2(p->pos.x, p->pos.z, m->pos.x, m->pos.z); if (p != m && d2 < 30 * 30 && d2 > 3 * 3) { v = p; break; } }
			if (!v) continue;
			const std::string w = Rand() < 0.55 ? "knife" : "pistol";
			m->giveWeapon(w, 24);
			auto t = std::make_shared<CrimeTask>();
			t->kind = "mug"; t->victim = Ref<Ped>(v); t->w = w; t->t = 0; t->stage = "approach";
			m->crimeTask = t;
			m->talkPartner = nullptr; m->setState("wander");
			return true;
		}
		return false;
	}
	if (kind == "steal") {
		std::vector<Ped*> ts; for (Ped* p : peds) if (within(p->pos, 20, 90)) ts.push_back(p);
		for (Ped* th : shuffled(ts)) {
			Vehicle* best = nullptr; double bd = 32 * 32;
			for (const auto& vp : game.vehicles.list) {
				Vehicle* v = vp.get();
				if (v->removed || v->isWrecked() || v->persistent || v->def.police || v->def.aircraft || v->def.train || !v->def.boat.empty() || v->policeUnit || v->ownedByPlayer || v->locked) continue;
				Ped* d = dynamic_cast<Ped*>(v->driver());
				if (v->driver() && (v->driver()->isPlayer || !d || d->brain != "civilian" || v->speedAbs() > 0.8)) continue;
				bool player = false; for (const auto& o : v->occupants) if (o && o->isPlayer) player = true;
				if (player) continue;
				const double d2 = Dist2(v->pos.x, v->pos.z, th->pos.x, th->pos.z);
				if (d2 < bd) { bd = d2; best = v; }
			}
			if (!best) continue;
			auto t = std::make_shared<CrimeTask>();
			t->kind = "steal"; t->car = Ref<Vehicle>(best); t->t = 0; t->occupied = best->driver() != nullptr; t->from = th->pos;
			th->crimeTask = t;
			th->talkPartner = nullptr; th->setState("wander");
			return true;
		}
		return false;
	}
	if (kind == "speed") {
		std::vector<Vehicle*> cands;
		for (const auto& r : game.traffic->cars) if (Vehicle* v = r.get()) {
			LaneDriver* ai = dynamic_cast<LaneDriver*>(v->ai.get());
			if (driverOf(v) && IsPlainLaneDriver(v->ai.get()) && ai && !ai->reckless && within(v->pos, 30, 200)) cands.push_back(v);
		}
		if (cands.empty()) return false;
		makeReckless(PickPtr(cands));
		return true;
	}
	if (kind == "jaywalk") {
		for (Ped* p : peds) if (within(p->pos, 8, 60) && jaywalk(p, true)) return true;
		return false;
	}
	return false;
}

// the thief is behind the wheel: off they go
void NpcCrime::stolen(Ped* ped, const CrimeTask& tIn) {
	const CrimeTask t = tIn;
	Vehicle* car = t.car.get();
	ped->crimeTask.reset();
	const V3 from = t.from;
	car->ai = std::make_shared<FleeDriver>(game, car, [from]() { return from; });
	car->parked = false;
	bool inTraffic = false;
	for (const auto& r : game.traffic->cars) if (r.get() == car) { inTraffic = true; break; }
	if (!inTraffic) { car->traffic = true; game.traffic->cars.push_back(Ref<Vehicle>(car)); }
	// (a jacked driver already shouts about it; a parked car's owner is nowhere to be seen)
	CommitOpts o; o.car = car; o.call = t.occupied ? 0.95 : 0.55; o.delay0 = 3; o.delay1 = 7;
	commit(ped, "gta", o);
}

bool NpcCrime::mugThink(Ped* m, CrimeTask& t, double dt) {
	Ped* v = t.victim.get();
	t.t += dt;
	if (m->npcCase || !v || v->dead || v->removed || v->vehicle || t.t > 40) {
		m->crimeTask.reset();
		if (v && v->crimeTask && v->crimeTask->kind == "victim") v->crimeTask.reset();
		return false;
	}
	const double dx = v->pos.x - m->pos.x, dz = v->pos.z - m->pos.z, d = Hypot(dx, dz);
	if (t.stage == "approach") {
		if (d > 2.2) { m->goTo(v->pos.x, v->pos.z, d > 12 ? 2.6 : 3.6, dt, 1.8); return true; }
		t.stage = "rob"; t.rt = 0;
		m->equip(t.w);
		m->say(Pick(SAY("mug")));
		auto vt = std::make_shared<CrimeTask>();
		vt->kind = "victim"; vt->by = Ref<Ped>(m); vt->t = 0;
		v->crimeTask = vt;
		if (v->state == "idle") v->talkPartner = nullptr;
		v->setState("wander");
	}
	if (t.stage == "rob") {
		t.rt += dt;
		m->stop(); m->faceTowards(v->pos.x, v->pos.z, dt, 10);
		if (m->weaponDef().type == "gun") { m->aiming = true; m->aimPitch = 0; }
		if (t.rt > 2.6) {
			m->loot = RandInt(40, 260);
			m->aiming = false;
			m->crimeTask.reset();
			m->threat = Ref<Character>(v); m->threatPos = v->pos; m->setState("flee");
			if (v->crimeTask && v->crimeTask->kind == "victim") v->crimeTask.reset();
			v->threat = Ref<Character>(m); v->threatPos = m->pos; v->setState("flee");
			v->say(Pick(SAY("mugged")));
			CommitOpts o; o.call = 1; o.delay0 = 2; o.delay1 = 5;
			commit(m, "mugging", o);
			return false;
		}
	}
	return true;
}

bool NpcCrime::argueThink(Ped* ped, CrimeTask& t, double dt) {
	t.t += dt;
	if (game.vehicles.isBusy(ped)) return true;
	Vehicle* own = t.car.get();
	Vehicle* other = t.other.get();
	Ped* foe = t.foe.get();
	const bool done = t.t > t.dur || !other || other->removed || (t.hasFoe && (!foe || foe->dead || foe->ragdolling || foe->removed));
	if (done || ped->npcCase) {
		// back in the car and on your way (or leave it, if it's wrecked)
		if (ped->state == "attack") ped->setState("wander");
		ped->threat = nullptr;
		if (!ped->npcCase && own && !own->removed && !own->isWrecked() && !own->driver() && Dist2(own->pos.x, own->pos.z, ped->pos.x, ped->pos.z) < 30 * 30) {
			if (game.vehicles.enter(ped, own, 0)) { auto rt = std::make_shared<CrimeTask>(); rt->kind = "rejoin"; rt->car = Ref<Vehicle>(own); ped->crimeTask = rt; }
			else ped->crimeTask.reset();
			return true;
		}
		ped->crimeTask.reset();
		return false;
	}
	if (foe) {
		// fists out
		ped->threat = Ref<Character>(foe);
		if (ped->state != "attack") ped->setState("attack");
		return false;
	}
	const V3 door = other->doorWorld();
	const double d = Hypot(door.x - ped->pos.x, door.z - ped->pos.z);
	if (d > 1.8) { ped->goTo(door.x, door.z, 2.4, dt, 1.4); return true; }
	ped->stop(); ped->faceTowards(other->pos.x, other->pos.z, dt, 6); ped->animState.talking = true;
	if ((IsSet(t.sayT) ? t.sayT : 0) <= t.t) {
		t.sayT = t.t + 2.6;
		ped->say(Pick(SAY("rage")));
		Ped* od = dynamic_cast<Ped*>(other->driver());
		if (od && !od->isPlayer && Rand() < 0.5) { Ref<Ped> r(od); after(0.9, [r]() { Ped* o = r.get(); if (o && !o->removed) o->say(Pick(SAY("sorry"))); }); }
	}
	if (t.fight && t.t > 4 && !t.hasFoe) {
		Ped* od = dynamic_cast<Ped*>(other->driver());
		if (od && !od->isPlayer && od->brain == "civilian" && !game.vehicles.isBusy(od)) {
			game.vehicles.exit(od);
			auto ot = std::make_shared<CrimeTask>();
			ot->kind = "argue"; ot->car = Ref<Vehicle>(other); ot->other = Ref<Vehicle>(own); ot->t = 0; ot->dur = t.dur - t.t + 2; ot->foe = Ref<Ped>(ped); ot->hasFoe = true;
			od->crimeTask = ot;
			t.foe = Ref<Ped>(od); t.hasFoe = true;
		} else t.fight = false;
	}
	return true;
}

// ------------------------------------------------------------------ events
void NpcCrime::onCrash(Vehicle* A, Vehicle* B, double impact) {
	if (!running() || impact < 4.5) return;
	Ped* da = driverOf(A);
	Ped* db = driverOf(B);
	if (!da && !db) return;
	if (A->crashT > game.time - 12 || B->crashT > game.time - 12) return;
	A->crashT = B->crashT = game.time;
	// who ran into whom: the one heading at the other harder (a reckless driver or a getaway car, always)
	auto into = [](Vehicle* X, Vehicle* Y) { const double dx = Y->pos.x - X->pos.x, dz = Y->pos.z - X->pos.z; double l = Hypot(dx, dz); if (!l) l = 1; return (X->vel.x * dx + X->vel.z * dz) / l; };
	auto reckless = [](Vehicle* v) { LaneDriver* ai = dynamic_cast<LaneDriver*>(v->ai.get()); return ai && ai->reckless; };
	auto onCase = [](Vehicle* v) { Ped* d = dynamic_cast<Ped*>(v->driver()); return d && d->npcCase; };
	Vehicle* bad = into(A, B) >= into(B, A) ? A : B;
	if (reckless(A) || onCase(A)) bad = A; else if (reckless(B) || onCase(B)) bad = B;
	Vehicle* good = bad == A ? B : A;
	Ped* bd = driverOf(bad);
	Ped* gd = driverOf(good);
	const V3 p0 = pp();
	if (Dist2(bad->pos.x, bad->pos.z, p0.x, p0.z) > 200 * 200) return;
	const double r = Rand();
	if (bd && r < (reckless(bad) ? 0.7 : 0.35)) {
		// hit and run
		Ref<Vehicle> g(good);
		const V3 gpos = good->pos;
		bad->ai = std::make_shared<FleeDriver>(game, bad, [g, gpos]() { Vehicle* v = g.get(); return v ? v->pos : gpos; });
		CommitOpts o; o.car = bad; o.call = 0.6; o.delay0 = 5; o.delay1 = 10;
		commit(bd, "hitrun", o);
		if (gd) { Ref<Ped> rg(gd); after(0.6, [rg]() { Ped* p = rg.get(); if (p && !p->removed && !p->dead) p->say(Pick(SAY("rage"))); }); }
		return;
	}
	if (gd && r < 0.8 && good->speedAbs() < 6 && good->def.bike.empty()) {
		// road rage: the other driver gets out and has words (and sometimes more)
		Ref<Ped> rg(gd); Ref<Vehicle> rgood(good), rbad(bad);
		after(0.9, [this, rg, rgood, rbad]() {
			Ped* g = rg.get(); Vehicle* gv = rgood.get();
			if (!g || g->removed || g->dead || !gv || g->vehicle != gv || game.vehicles.isBusy(g) || g->crimeTask || g->npcCase) return;
			game.vehicles.exit(g);
			auto t = std::make_shared<CrimeTask>();
			t->kind = "argue"; t->car = rgood; t->other = rbad; t->t = 0; t->dur = Rand(8, 13); t->fight = Rand() < 0.4;
			g->crimeTask = t;
		});
	}
}

void NpcCrime::onPedHit(Character* c, Vehicle* v, double spd) {
	Ped* d = driverOf(v);
	if (!running() || !d || spd < 5 || v->pedHitT > game.time - 8) return;
	v->pedHitT = game.time;
	LaneDriver* ai = dynamic_cast<LaneDriver*>(v->ai.get());
	if (Rand() < 0.6 || (ai && ai->reckless)) {
		Ref<Character> rc(c);
		const V3 cpos = c->pos;
		v->ai = std::make_shared<FleeDriver>(game, v, [rc, cpos]() { Character* x = rc.get(); return x ? x->pos : cpos; });
		CommitOpts o; o.car = v; o.call = 0.85; o.delay0 = 3; o.delay1 = 6;
		commit(d, "hitped", o);
	} else { Ref<Ped> rd(d); after(0.5, [rd]() { Ped* p = rd.get(); if (p && !p->removed && !p->dead) p->say(Pick(SAY("sorry"))); }); }
}

void NpcCrime::onViolence(Character* att, Character* vic, bool kill) {
	Ped* a = dynamic_cast<Ped*>(att);
	if (!a || a->isPlayer || a->brain != "civilian" || a->dead) return;
	Ped* vp = dynamic_cast<Ped*>(vic);
	if (vp && vp->brain == "cop") { CommitOpts o; o.call = 1; o.delay0 = 1; o.delay1 = 2; commit(a, kill ? "copKill" : "copAssault", o); return; }
	if (vic && !vic->isPlayer && ((a->crimeTask && a->crimeTask->kind == "argue") || a->npcCase || a->state == "attack")) { CommitOpts o; o.call = 0.5; o.delay0 = 4; o.delay1 = 8; commit(a, "assault", o); }
}

// ------------------------------------------------------------------ for the HUD and the radar
void NpcCrime::tagged(std::vector<NpcTag>& out) const {
	for (const auto& rec : cases) { Ped* p = rec->ped.get(); if (rec->known && p && !p->removed && rec->phase != "closed") out.push_back({ p, rec->stars, rec->unit != nullptr }); }
}

} // namespace atg
