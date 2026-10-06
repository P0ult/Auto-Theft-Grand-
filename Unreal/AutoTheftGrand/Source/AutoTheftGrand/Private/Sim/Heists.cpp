#include "Heists.h"
#include "Collision.h"
#include "Game.h"
#include "Gameplay.h"
#include "Peds.h"
#include "Pickups.h"
#include "Police.h"
#include "RoadNet.h"
#include "Traffic.h"

namespace atg {

namespace {
const double FIRST = 75;               // seconds into a session before the first van
const double EVERY[2] = { 150, 260 };  // between vans
const int DOOR_HITS = 4;               // bullets in the back doors to break the lock

// '$' + Math.round(n).toLocaleString('en-US')
std::string Money(double n) {
	long long v = (long long)std::floor(n + 0.5);
	const bool neg = v < 0; if (neg) v = -v;
	std::string s = std::to_string(v), out;
	for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) out += ','; out += s[i]; }
	return std::string("$") + (neg ? "-" : "") + out;
}
}

Appearance GuardLook(RNG& rng) {
	// (the options object is evaluated first: its two chances come off the generator before the look)
	const bool cap = rng.Chance(0.6);
	const bool glasses = rng.Chance(0.3);
	Appearance a = RandomAppearance(rng);
	a.shirt = 0x6b7480; a.shirtType = "long"; a.jacketColor = 0x2b3340; a.pants = 0x1f2633;
	a.hairStyle = cap ? "cap" : "short"; a.hat = 0x1f2633; a.shoes = 0x111111; a.shorts = false; a.bandana = -1; a.glasses = glasses;
	return a;
}

Heists::Heists(Game& g) : game(g), timer(FIRST) {
	auto& ev = g.events;
	ev.vehicleShot.on([this](Vehicle* v, Character* shooter, V3 point) {
		Van* h = van.get();
		if (!h || v != h->v.get() || !shooter || !shooter->isPlayer) return;
		alert();
		if (h->state != "open") {
			// the back doors: the last metre of the van, high enough to miss the bumper
			const double fx = std::sin(v->yaw), fz = std::cos(v->yaw);
			const double along = (point.x - v->pos.x) * fx + (point.z - v->pos.z) * fz;
			if (along < -v->def.L / 2 + 0.9 && point.y > v->pos.y + v->def.clearance + 0.25) {
				h->hits++;
				game.soundAt("metalhit", point, 0.6);
				if (h->hits >= DOOR_HITS) open();
			}
		}
	});
	ev.explosion.on([this](V3 pos, double r, Character*) {
		Van* h = van.get();
		Vehicle* v = h ? h->v.get() : nullptr;
		if (!v || v->removed) return;
		if (Hypot(pos.x - v->pos.x, pos.z - v->pos.z) < r + 3.5) { alert(); open(); }
	});
	ev.carCrash.on([this](Vehicle* A, Vehicle* B, double impact) {
		Van* h = van.get();
		Vehicle* pv = game.player->vehicle;
		if (h && pv && impact > 4 && ((A == h->v.get() && B == pv) || (B == h->v.get() && A == pv))) alert();
	});
	ev.kill.on([this](Character* killer, Character* victim, const std::string&, const std::string&) {
		if (!killer || !killer->isPlayer || !van) return;
		for (const auto& gr : van->guards) if (gr.get() == victim) { alert(); return; }
	});
	ev.carjack.on([this](Character* by, Character*, Vehicle* veh) { if (by && by->isPlayer && van && veh == van->v.get()) alert(); });
	auto off = [this]() { if (van && van->state == "open") finish(); };
	ev.playerDied.on(off);
	ev.busted.on(off);
	g.radarSources.push_back([this](std::vector<Blip>& out) { blipList(out); });
}

void Heists::blipList(std::vector<Blip>& out) const {
	const Van* h = van.get();
	const Vehicle* v = h ? h->v.get() : nullptr;
	if (!v || v->removed || v->isWrecked() || h->state == "open") return;
	Blip b; b.x = v->pos.x; b.z = v->pos.z; b.icon = "money"; b.color = 0x6fd36b;
	out.push_back(b);
}

void Heists::update(double dt) {
	Player& p = *game.player;
	timer -= dt;
	if (!van) {
		if (timer <= 0 && enabled && !game.missionActive && !p.dead && (game.police ? game.police->wantedLevel() : 0) == 0 && game.gameplay && game.gameplay->state == "playing") {
			timer = 20; // (retry if no road)
			if (spawn()) timer = Rand(EVERY[0], EVERY[1]);
		}
		return;
	}
	Van& h = *van;
	Vehicle* v = h.v.get();
	if (!v) { van.reset(); timer = Max(timer, Rand(EVERY[0], EVERY[1])); return; }
	h.t += dt;
	// the doors swing open
	for (double& d : v->rearDoorOpen) d += ((h.state == "open" ? 1 : 0) - d) * Min(1, dt * 4);
	const V3 pp = p.vehicle ? p.vehicle->pos : p.pos;
	const double d = std::sqrt(Dist2(v->pos.x, v->pos.z, pp.x, pp.z));
	// the first sighting
	if (!h.told && d < 120 && h.state == "rounds") {
		h.told = true;
		if (game.hud) game.hud->help("An <b style=\"color:#6fd36b\">armoured van</b> is doing its rounds nearby. Shoot the back doors open to rob it.", 6);
	}
	// a run for it, until it's stopped: then the guards get out and fight
	if (h.state == "fleeing") {
		if (auto* ai = dynamic_cast<LaneDriver*>(v->ai.get())) { ai->panic = 20; ai->ignoreLights = true; }
		h.still = v->speedAbs() < 1.5 ? h.still + dt : 0;
		if (h.still > 1.6 || !v->driver() || v->driver()->dead || v->health < v->maxHealth * 0.35 || (d < 8 && v->speedAbs() < 4)) guardsOut();
	}
	if (h.state == "alerted" && h.t > h.outT) guardsOut();
	// the cash: count what's been picked up
	if (!h.cash.empty() && game.pickupsSys) {
		auto listed = [&](const Pickup* pk) { for (const auto& q : game.pickupsSys->list) if (q.get() == pk) return true; return false; };
		int left = 0;
		for (const Cash& c : h.cash) if (listed(c.pk.get())) left++;
		for (Cash& c : h.cash) if (!c.got && !listed(c.pk.get()) && c.pk->life > 0) { c.got = true; h.taken += c.amount; }
		if (!left && !h.done) {
			h.done = true;
			robbed++;
			if (h.taken > 0) { if (game.hud) game.hud->bigMessage("ARMORED VAN ROBBED", "passed", 3.5, "+" + Money(h.taken)); game.sound("passed", 0.8); }
		}
	}
	// gone: out of range, wrecked and left behind, or robbed and left behind
	const bool seen = game.peds->inView(v->pos.x, v->pos.z, 3);
	if (v->removed || (d > 360 && !seen) || ((v->isWrecked() || h.done) && d > 140 && !seen)) finish();
}

bool Heists::spawn() {
	const Player& p = *game.player;
	const V3 pp = p.vehicle ? p.vehicle->pos : p.pos;
	if (!game.traffic) return false;
	for (int k = 0; k < 8; k++) {
		Traffic::Sample smp;
		if (!Traffic::SampleLane(game, pp.x, pp.z, 130, 240, smp, [](const REdge& e) { return e.type != ERoad::Freeway && e.type != ERoad::Ramp && e.type != ERoad::Rail; })) continue;
		if (game.peds->inView(smp.x, smp.z, 4)) continue;
		bool near = false;
		for (const auto& o : game.vehicles.list) if (Dist2(o->pos.x, o->pos.z, smp.x, smp.z) < 12 * 12) { near = true; break; }
		if (near) continue;
		Vehicle* v = game.traffic->spawnCar(smp.start, smp.s0, "stockade");
		if (!v) continue;
		v->persistent = true;
		v->heistVan = true;
		v->locked = true; // (you can still pull the driver out once the guards are out)
		// swap the civilians for guards
		for (auto& o : v->occupants) if (o && !o->isPlayer) { auto keep = o; o.reset(); keep->vehicle = nullptr; game.peds->remove(keep.get()); }
		RNG rng((uint32_t)(int64_t)std::floor(Rand() * 1e9));
		auto h = std::make_unique<Van>();
		h->v = Ref<Vehicle>(v);
		for (int s = 0; s < 2; s++) {
			PedOpts po; po.hasAppearance = true; po.appearance = GuardLook(rng); po.brain = "civilian"; po.health = 130; po.armor = 50; po.persistent = true; po.hasY = true; po.y = v->pos.y;
			Ped* q = game.peds->spawnPed(v->pos.x, v->pos.z, po);
			const std::string w = s == 0 ? "pistol" : rng.Pick(std::vector<std::string>{ "smg", "shotgun", "pistol" });
			q->giveWeapon(w, 400); q->equip(w);
			q->accuracy = 0.55; q->damageMul = 0.6;
			v->putIn(q, s);
			h->guards.push_back(Ref<Ped>(q));
		}
		van = std::move(h);
		return true;
	}
	return false;
}

// shots fired, a ram, a guard down: the van's on alert
void Heists::alert() {
	Van* h = van.get();
	if (!h || h->state != "rounds") return;
	Vehicle* v = h->v.get();
	if (!v) return;
	if (game.policeSys) game.policeSys->crime(1.2, v->pos, true);
	Ped* dr = dynamic_cast<Ped*>(v->driver());
	if (v->speedAbs() > 4 && v->driver() && !v->driver()->dead && Rand() < 0.6) {
		h->state = "fleeing";
		if (dr) dr->say("Code red! Code red!");
	} else { h->state = "alerted"; h->outT = h->t + 0.6; }
}

void Heists::guardsOut() {
	Van& h = *van;
	Player& p = *game.player;
	h.state = h.state == "open" ? "open" : "alerted";
	h.outT = kInf;
	Vehicle* v = h.v.get();
	if (v->ai) { v->ai.reset(); v->traffic = false; }
	v->locked = false;
	for (const auto& r : h.guards) {
		Ped* q = r.get();
		if (!q || q->dead || q->removed) continue;
		if (q->vehicle && !game.vehicles.isBusy(q)) game.vehicles.exit(q);
		q->threat = Ref<Character>(&p);
		q->setState("attack");
	}
}

// the doors give: the cash bags fall out of the back
void Heists::open() {
	Van* h = van.get();
	if (!h || h->state == "open") return;
	Vehicle* v = h->v.get();
	const bool wasRounds = h->state == "rounds";
	h->state = "open";
	game.soundAt("metalhit", v->pos, 1);
	game.sound("alarm", 0.5);
	if (game.policeSys) game.policeSys->raise(2);
	if (wasRounds || v->ai) guardsOut();
	const double fx = std::sin(v->yaw), fz = std::cos(v->yaw), rx = -fz, rz = fx;
	const double total = std::round(Rand(3000, 7500) / 50) * 50;
	const int n = 4;
	for (int i = 0; i < n; i++) {
		const double back = v->def.L / 2 + 0.9 + Rand(0, 1.6), side = Rand(-1.2, 1.2);
		const double x = v->pos.x - fx * back + rx * side, z = v->pos.z - fz * back + rz * side;
		const double amount = std::round(total / n);
		if (!game.pickupsSys) continue;
		PickupData d; d.amount = amount; d.life = 120; d.hasY = true; d.y = game.collision->floorHeight(x, z, v->pos.y + 1); d.blip = true;
		game.pickupsSys->spawn("money", x, z, d);
		h->cash.push_back({ game.pickupsSys->list.back(), amount, false });
	}
	if (game.hud) game.hud->help("The doors are open: <b style=\"color:#6fd36b\">grab the cash</b> and lose the cops.", 5);
}

void Heists::finish() {
	Van* h = van.get();
	if (!h) return;
	Vehicle* v = h->v.get();
	const Player& pl = *game.player;
	const V3 pp = pl.vehicle ? pl.vehicle->pos : pl.pos;
	for (const auto& r : h->guards) {
		Ped* q = r.get();
		if (!q || q->removed || !(!q->vehicle || q->vehicle == v)) continue;
		const bool far = Hypot(q->pos.x - pp.x, q->pos.z - pp.z) > 60;
		if (far || q->dead) game.peds->remove(q); else q->persistent = false;
	}
	if (v && !v->removed && !(v->driver() && v->driver()->isPlayer)) {
		const bool seen = game.peds->inView(v->pos.x, v->pos.z, 3);
		if (!seen || Hypot(v->pos.x - pp.x, v->pos.z - pp.z) > 150) {
			for (auto& o : v->occupants) if (o && !o->isPlayer) { auto keep = o; game.peds->remove(keep.get()); }
			for (auto& o : v->occupants) o.reset();
			game.vehicles.remove(v);
		} else v->persistent = false;
	}
	van.reset();
	timer = Max(timer, Rand(EVERY[0], EVERY[1]));
}

void Heists::reset() {
	if (van) {
		Van& h = *van;
		if (game.pickupsSys) for (const Cash& c : h.cash) {
			auto& list = game.pickupsSys->list;
			for (size_t i = 0; i < list.size(); i++) if (list[i] == c.pk) { c.pk->remove(); list.erase(list.begin() + i); break; }
		}
		for (const auto& r : h.guards) if (Ped* q = r.get()) if (!q->removed) game.peds->remove(q);
		Vehicle* v = h.v.get();
		if (v && !v->removed && !(v->driver() && v->driver()->isPlayer)) {
			for (auto& o : v->occupants) if (o && !o->isPlayer) { auto keep = o; game.peds->remove(keep.get()); }
			for (auto& o : v->occupants) o.reset();
			game.vehicles.remove(v);
		}
		van.reset();
	}
	timer = FIRST;
}

} // namespace atg
