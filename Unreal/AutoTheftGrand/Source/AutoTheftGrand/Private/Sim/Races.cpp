#include "Races.h"
#include "Game.h"
#include "Peds.h"
#include "Pickups.h"
#include "Skeleton.h"
#include "Traffic.h"

namespace atg {

namespace {
std::string Thousands(double v) {
	std::string s = std::to_string((long long)JsRound(v)), out;
	for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) out += ','; out += s[i]; }
	return out;
}
std::string Upper(std::string s) { for (char& c : s) c = (char)std::toupper((unsigned char)c); return s; }
}

std::string RaceTime(double t) {
	const int m = (int)std::floor(t / 60);
	const double s = t - m * 60;
	char b[32]; std::snprintf(b, sizeof b, "%d:%05.2f", m, s);
	return b;
}

RaceSystem::RaceSystem(Game& g) : game(g) {
	std::vector<V2> ring;
	for (int k = 0; k <= 12; k++) { const double th = -0.3 + k / 12.0 * kPi * 2; const double r = NcRingR(2, th); ring.push_back({ NCITY.x + r * std::cos(th), NCITY.z + r * std::sin(th) }); }
	const Town& hale = TownByKey("hale");
	const Town& ridge = TownByKey("ridge");
	std::vector<RaceDef> defs = {
		{ "solsprint", "Sol Sprint", 250, 2000, { "kestrel", "brawler", "zenith" }, { { XS[12], ZS[4] }, { XS[12], ZS[8] }, { XS[8], ZS[8] }, { XS[8], ZS[4] }, { XS[11], ZS[4] } } },
		{ "bayshore", "Bayshore Run", 500, 4500, { "zenith", "kestrel", "brawler" }, { { XS[17], ZS[3] }, { 960, -560 }, { hale.x, hale.z }, { 960, -1500 }, { 860, -1860 } } },
		{ "aurring", "Aurelio Ring", 400, 3500, { "kestrel", "pico", "brawler" }, ring },
		{ "cedarclimb", "Cedar Valley Climb", 600, 5000, { "summit", "zenith", "kestrel" }, { { NCITY.x - 280, NCITY.z + 300 }, { 60, -3620 }, { -10, -3420 }, { ridge.x, ridge.z }, { 160, -2830 } } },
	};
	for (auto& d : defs) plan(std::move(d));
}

// thread the waypoints through the road network into a dense line for the AI and sparse checkpoints
void RaceSystem::plan(RaceDef def) {
	const RoadNet& net = game.map.roads;
	std::vector<V2> line;
	for (size_t i = 0; i + 1 < def.pts.size(); i++) {
		const V2 a = def.pts[i], b = def.pts[i + 1];
		const auto r = net.FindRoute(a.x, a.z, b.x, b.z);
		const std::vector<V2> poly = net.RoutePolyline(r ? &*r : nullptr, a.x, a.z, b.x, b.z);
		for (size_t k = i ? 1 : 0; k < poly.size(); k++) line.push_back(poly[k]);
	}
	// snap onto the road and resample: AI points every ~18 m, checkpoints every ~140 m
	std::vector<V3> snapped;
	for (const V2& p : line) {
		const EdgeHit c = net.Closest(p.x, p.z, [](const REdge& e) { return !e.removed && e.type != ERoad::Rail && e.type != ERoad::Dirt; }, 40);
		if (c.valid()) { const EdgePoint t = net.At(net.edges[c.e], c.s); snapped.push_back(V3(t.x, t.y, t.z)); }
	}
	if (snapped.empty()) return;
	std::vector<V3> dense = { snapped[0] };
	for (const V3& p : snapped) {
		const V3 q = dense.back();
		const double d = Hypot(p.x - q.x, p.z - q.z);
		if (d > 18) { const int n = (int)std::floor(d / 18); for (int k = 1; k <= n; k++) dense.push_back(V3(q.x + (p.x - q.x) * k / n, p.y, q.z + (p.z - q.z) * k / n)); }
	}
	if (dense.size() < 6) return;
	Race race;
	double acc = 0;
	for (size_t i = 1; i < dense.size(); i++) { acc += Hypot(dense[i].x - dense[i - 1].x, dense[i].z - dense[i - 1].z); if (acc > 140 || i == dense.size() - 1) { race.cps.push_back(dense[i]); acc = 0; } }
	const V3 s = dense[0], n2 = dense[3];
	for (size_t i = 1; i < dense.size(); i++) race.len += Hypot(dense[i].x - dense[i - 1].x, dense[i].z - dense[i - 1].z);
	// grid lanes: both lanes of our direction on a wide street, single file on a two-lane road
	const EdgeHit se = net.Closest(s.x, s.z, [](const REdge& e) { return !e.removed && e.type != ERoad::Rail; });
	const bool wide = se.valid() && (net.edges[se.e].lanesF > 1 || net.edges[se.e].lanesB > 1);
	if (wide) { race.lanes[0] = 1.9; race.lanes[1] = 5.1; }
	race.sx = s.x; race.sz = s.z; race.syaw = std::atan2(n2.x - s.x, n2.z - s.z);
	race.def = std::move(def);
	race.dense = std::move(dense);
	races.push_back(std::move(race));
	const int index = (int)races.size() - 1;
	MarkerOpts o; o.color = 0xffffff; o.radius = 4; o.height = 2.2; o.icon = "flag"; o.label = races[index].def.name; o.vehicleOnly = true; o.arrow = false;
	o.onEnter = [this, index](Marker*) { offer(index); };
	races[index].marker = game.pickupsSys->addMarker(races[index].sx, races[index].sz, o);
}

void RaceSystem::offer(int index) {
	Race& race = races[index];
	Player& p = *game.player;
	Vehicle* v = p.vehicle;
	if (active || game.missionActive || !v || v->driver() != &p || v->def.kind == "boat" || v->def.aircraft || v->def.train) return;
	if (v->def.pedal || v->def.board) { if (game.hud) game.hud->help("You'll need something with an engine.", 3); return; }
	v->input.brake = 1; v->vel *= 0.3;
	if (!game.hud) return;
	char km[16]; std::snprintf(km, sizeof km, "%.1f", race.len / 1000);
	auto best = game.stats.raceBest.find(race.def.id);
	std::string sub = std::string("Street race \xC2\xB7 ") + km + " km \xC2\xB7 " + std::to_string(race.cps.size()) + " checkpoints \xC2\xB7 Entry $" + std::to_string(race.def.entry) +
		" \xC2\xB7 First place wins $" + Thousands(race.def.prize) + (best != game.stats.raceBest.end() ? " \xC2\xB7 Your best " + RaceTime(best->second) : "");
	StoreItem go; go.name = "Race in your " + v->def.name; go.desc = "Three locals are waiting. Traffic won't stop for you.";
	go.button = "Race \xE2\x80\x94 $" + std::to_string(race.def.entry); go.price = race.def.entry; go.closes = true;
	go.use = [this, index](Player&, Game&) { if (!active) start(races[index]); return std::string(); };
	PanelOpts o; o.inVehicle = true; o.leave = "Not now (Esc)";
	game.hud->openPanel(Upper(race.def.name), sub, { go }, o);
}

void RaceSystem::start(Race& race) {
	Player& p = *game.player;
	Vehicle* v = p.vehicle;
	if (!v) return;
	const double fx = std::sin(race.syaw), fz = std::cos(race.syaw);
	// (right of travel is (-fz, fx))
	auto slot = [&](int i) { const double back = 4 + (race.lanes[0] == race.lanes[1] ? i * 9 : (i >> 1) * 9), lat = race.lanes[i % 2]; return V2(race.sx - fx * back - fz * lat, race.sz - fz * back + fx * lat); };
	auto place = [&](Vehicle* veh, int i) { const V2 q = slot(i); veh->pos.set(q.x, game.map.GroundHeight(q.x, q.z) + 0.05, q.z); veh->yaw = race.syaw; veh->vel = V3(); veh->r = 0; veh->input.throttle = 0; veh->input.brake = 1; };
	if (game.hud) game.hud->fade(0.4, []() {});
	place(v, 0);
	game.rig.yaw = race.syaw + kPi;
	// clear the grid of traffic
	std::vector<Vehicle*> clear;
	for (const auto& o : game.vehicles.list) if (o.get() != v && o->ai && !o->persistent && Hypot(o->pos.x - race.sx, o->pos.z - race.sz) < 40) clear.push_back(o.get());
	if (game.traffic) for (Vehicle* o : clear) game.traffic->despawn(o);
	auto a = std::make_unique<Active>();
	a->race = &race; a->car = Ref<Vehicle>(v);
	for (int i = 1; i <= 3; i++) {
		const std::string& type = race.def.cars[(i - 1) % race.def.cars.size()];
		const V2 q = slot(i);
		SpawnOpts so; so.persistent = true;
		Vehicle* car = game.vehicles.spawn(type, q.x, q.z, race.syaw, so);
		if (!car) continue;
		place(car, i);
		// randomAppearance(this.rng, { female, shirtType, glasses }): the options are drawn first
		const bool female = rng.Chance(0.3);
		const std::string shirt = RandPick(std::vector<std::string>{ "jacket", "tee", "long" });
		const bool glasses = rng.Chance(0.5);
		PedOpts po; po.persistent = true; po.hasAppearance = true; po.appearance = RandomAppearance(rng, female ? 1 : 0);
		po.appearance.shirtType = shirt; po.appearance.glasses = glasses;
		Ped* d = game.peds->spawnPed(q.x, q.z, po);
		if (!d) { game.vehicles.remove(car); continue; }
		car->putIn(d, 0);
		auto r = std::make_unique<Rival>();
		r->car = Ref<Vehicle>(car); r->driver = Ref<Ped>(d);
		r->ai = std::make_unique<RaceDriver>(game, car, race.dense, 0.7 + i * 0.06 + Rand(-0.04, 0.04));
		car->ai.reset();
		Blip b; b.x = q.x; b.z = q.z; b.color = 0xff9f1c; b.icon = "dot"; b.small = true;
		r->blip = game.addBlip(b);
		a->rivals.push_back(std::move(r));
	}
	game.disableAmbient = true;
	active = std::move(a);
	if (game.hud) game.hud->help("<b>" + race.def.name + "</b> \xE2\x80\x94 first to the line takes $" + Thousands(race.def.prize) + ".", 3);
}

void RaceSystem::marker(Active& a) {
	if (a.marker) game.pickupsSys->removeMarker(a.marker);
	a.marker = nullptr;
	if (a.idx >= (int)a.race->cps.size()) return;
	const V3 cp = a.race->cps[a.idx];
	const bool last = a.idx == (int)a.race->cps.size() - 1;
	MarkerOpts o; o.radius = 9; o.height = 6; o.color = last ? 0xffffff : 0xffd23f; o.icon = last ? "flag" : "dot";
	a.marker = game.pickupsSys->addMarker(cp.x, cp.z, o);
	if (game.hud) game.hud->routeTo(V2(cp.x, cp.z));
}

void RaceSystem::update(double dt) {
	if (!active || game.paused) return;
	Active& a = *active;
	Player& p = *game.player;
	IHud* hud = game.hud;
	a.t += dt;
	// countdown: everyone held on the brakes
	if (a.t < 0) {
		const int n = (int)std::ceil(-a.t - 0.5);
		if (n < a.count && n >= 1) { a.count = n; if (hud) hud->bigMessage(std::to_string(n), "hint", 0.9); game.sound("ui"); }
		for (auto& r : a.rivals) if (Vehicle* c = r->car.get()) { c->input.throttle = 0; c->input.brake = 1; }
		if (Vehicle* c = a.car.get()) { c->input.brake = Max(c->input.brake, 1); c->vel *= 0.9; }
		return;
	}
	if (!a.go) { a.go = true; if (hud) hud->bigMessage("GO!", "passed", 1.2); game.sound("checkpoint"); marker(a); }
	for (auto& r : a.rivals) {
		Vehicle* c = r->car.get();
		if (!c) continue;
		if (!r->done) { r->ai->update(dt); if (r->ai->done) { r->done = true; r->t = a.t; a.finished.push_back(r.get()); } }
		if (r->blip) { r->blip->x = c->pos.x; r->blip->z = c->pos.z; }
	}
	// player checkpoints
	const V3 pp = p.vehicle ? p.vehicle->pos : p.pos;
	if (a.idx < (int)a.race->cps.size() && p.vehicle) {
		const V3 cp = a.race->cps[a.idx];
		if (Hypot(pp.x - cp.x, pp.z - cp.z) < 11) {
			a.idx++;
			game.sound("checkpoint");
			if (a.idx >= (int)a.race->cps.size()) { finish(); return; }
			marker(a);
		}
	}
	// standings: progress along the dense line
	const auto& dense = a.race->dense;
	int bi = a.pi; double bd = kInf;
	for (int i = Max(0, a.pi - 5); i < Min((int)dense.size(), a.pi + 40); i++) { const double d = Dist2(dense[i].x, dense[i].z, pp.x, pp.z); if (d < bd) { bd = d; bi = i; } }
	a.pi = bi;
	int ahead = 0;
	for (auto& r : a.rivals) if (r->done || r->ai->idx > a.pi + 1) ahead++;
	if (hud) {
		hud->setCounter("POSITION", std::to_string(1 + ahead) + "/4");
		const std::string label = "CHECKPOINTS";
		hud->setBar(&label, (double)a.idx / a.race->cps.size(), "#ffd23f");
	}
	// out of the car, wrecked or dead: the race is off
	if (!p.vehicle || p.vehicle->isWrecked()) a.out += dt; else a.out = 0;
	Vehicle* car = a.car.get();
	if (a.out > 8 || p.dead || !car || car->isWrecked()) end("RACE ABANDONED", "failed", "You left the race");
}

void RaceSystem::finish() {
	Active& a = *active;
	Player& p = *game.player;
	const int place = 1 + (int)a.finished.size();
	const double time = a.t;
	const std::string id = a.race->def.id;
	auto prevIt = game.stats.raceBest.find(id);
	const bool best = place == 1 && (prevIt == game.stats.raceBest.end() || time < prevIt->second);
	if (best) game.stats.raceBest[id] = time;
	const int pay = place == 1 ? a.race->def.prize : place == 2 ? a.race->def.entry * 2 : place == 3 ? a.race->def.entry : 0;
	if (pay) { p.money += pay; if (game.hud) game.hud->moneyFlash(pay); game.sound("cash"); }
	static const char* PLACES[] = { "", "1ST", "2ND", "3RD", "4TH" };
	const std::string title = place == 1 ? "RACE WON!" : std::string(PLACES[(int)Min(place, 4)]) + " PLACE";
	end(title, place == 1 ? "passed" : "hint", RaceTime(time) + (pay ? " \xC2\xB7 $" + Thousands(pay) : "") + (best ? " \xC2\xB7 New best!" : ""));
	if (place == 1) { game.stats.racesWon++; game.events.raceWon.emit(id); }
}

void RaceSystem::end(const std::string& title, const std::string& kind, const std::string& sub) {
	if (!active) return;
	std::unique_ptr<Active> a = std::move(active);
	if (game.hud) {
		game.hud->bigMessage(title, kind, 4, sub);
		game.hud->routeTo(std::nullopt); game.hud->setCounter("", ""); game.hud->setBar(nullptr);
	}
	if (a->marker) game.pickupsSys->removeMarker(a->marker);
	game.disableAmbient = false;
	for (auto& r : a->rivals) {
		if (r->blip) game.removeBlip(r->blip);
		// the rivals cruise off and become ordinary traffic that despawns in time
		Vehicle* c = r->car.get();
		if (Ped* d = r->driver.get()) d->persistent = false;
		if (!c) continue;
		c->persistent = false;
		if (!c->isWrecked() && c->driver()) { c->ai.reset(); c->input.throttle = 0; c->input.brake = 1; }
	}
}

} // namespace atg
