// Native tests for the simulation (no Unreal): Tools/native.sh simtest.exe simtest.cpp && ./simtest.exe [test ...]
// Each test sets up a game on the generated world, runs fixed 1/30 s frames and prints what it measured.
#include "Sim/Combat.h"
#include "Sim/Effects.h"
#include "Sim/Game.h"
#include "Sim/Gameplay.h"
#include "Sim/NpcCrime.h"
#include "Sim/Hud.h"
#include "Sim/Peds.h"
#include "Sim/Pickups.h"
#include "Sim/Police.h"
#include "Sim/Rail.h"
#include "Sim/Roadblocks.h"
#include "Sim/Setup.h"
#include "Sim/Special.h"
#include "Sim/Traffic.h"
#include "Sim/WeaponWheel.h"
#include "crashtrace.h"
#include <chrono>
#include <cstdio>
#include <cstring>
using namespace atg;

struct World {
	CityMap map;
	RoadMeshes roads;
	std::vector<PropInstance> props;
	std::map<std::string, PropTemplate> defs;
	World() : roads(BuildRoadMeshes(map)), props(PlaceProps(map)), defs(BuildPropTemplates()) {}
	std::unique_ptr<Game> game(bool systems = false) {
		WorldData w; w.map = &map; w.roadPrims = roads.prims; w.roadDecks = roads.decks; w.props = props; w.propDefs = &defs;
		auto g = std::make_unique<Game>(w);
		if (systems) { InstallSystems(*g); StartGame(*g); }
		const Landmark& home = map.landmarks.at("home");
		g->respawnPlayer(home.x, home.z, 0);
		return g;
	}
};

// a point in the middle of the north-south street nearest the safehouse (an open stretch to walk and drive)
static void ToStreet(World& w, Game& g, double dz = 0) {
	const Landmark& home = w.map.landmarks.at("home");
	const double x = XS[w.map.NearestX(home.x)];
	g.respawnPlayer(x + 2, home.z + dz, 0);
}

static void Run(Game& g, double seconds) { const int n = (int)std::round(seconds * 30); for (int i = 0; i < n; i++) g.frame(1.0 / 30); }
static int fails = 0;
static void Check(bool ok, const char* what) { printf("  %s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

static void TestWalk(World& w) {
	printf("walk\n");
	auto g = w.game();
	ToStreet(w, *g);
	Player& p = *g->player;
	const V3 start = p.pos;
	g->input.KeyDown("KeyW");
	Run(*g, 2);
	g->input.KeyUp("KeyW");
	const double moved = Hypot(p.pos.x - start.x, p.pos.z - start.z);
	printf("  start (%.2f, %.2f, %.2f) end (%.2f, %.2f, %.2f) moved %.2f m\n", start.x, start.y, start.z, p.pos.x, p.pos.y, p.pos.z, moved);
	Check(moved > 3, "walks forward");
	g->input.KeyDown("ShiftLeft"); g->input.KeyDown("KeyW");
	const V3 s2 = p.pos;
	Run(*g, 1);
	g->input.KeyUp("KeyW"); g->input.KeyUp("ShiftLeft");
	printf("  sprint 1 s: %.2f m, stamina %.2f\n", Hypot(p.pos.x - s2.x, p.pos.z - s2.z), p.stamina);
	g->input.KeyDown("Space");
	g->frame(1.0 / 30);
	g->input.KeyUp("Space");
	Run(*g, 0.2);
	printf("  jumping: y %.2f vy %.2f grounded %d\n", p.pos.y, p.vel.y, p.grounded ? 1 : 0);
	Check(!p.grounded, "jumps");
}

static void TestDrive(World& w) {
	printf("drive\n");
	auto g = w.game();
	ToStreet(w, *g);
	Player& p = *g->player;
	Vehicle* v = g->vehicles.spawn("zenith", p.pos.x + 3, p.pos.z, 0);
	g->vehicles.seatNow(&p, v, 0);
	Check(p.vehicle == v, "seated");
	const V3 s = v->pos;
	g->input.KeyDown("KeyW");
	Run(*g, 3);
	printf("  3 s full throttle: %.1f m/s, moved %.1f m, y %.2f\n", v->speed(), Hypot(v->pos.x - s.x, v->pos.z - s.z), v->pos.y);
	Check(v->speed() > 10, "accelerates");
	g->input.KeyUp("KeyW");
	g->input.KeyDown("KeyS");
	Run(*g, 3);
	g->input.KeyUp("KeyS");
	printf("  after braking: %.1f m/s health %.0f\n", v->speed(), v->health);
	g->input.KeyDown("KeyF");
	g->frame(1.0 / 30);
	g->input.KeyUp("KeyF");
	Run(*g, 1.5);
	Check(!p.vehicle, "gets out");
	printf("  on foot at (%.2f, %.2f, %.2f)\n", p.pos.x, p.pos.y, p.pos.z);
	g->input.KeyDown("KeyF");
	g->frame(1.0 / 30);
	g->input.KeyUp("KeyF");
	Run(*g, 3);
	Check(p.vehicle == v, "walks to the door and gets back in");
}

static void TestCrash(World& w) {
	printf("crash\n");
	auto g = w.game();
	ToStreet(w, *g, -60);
	Player& p = *g->player;
	Vehicle* v = g->vehicles.spawn("meridian", p.pos.x, p.pos.z + 5, 0);
	Vehicle* other = g->vehicles.spawn("pico", p.pos.x, p.pos.z + 45, kPi / 2);
	g->vehicles.seatNow(&p, v, 0);
	g->input.KeyDown("KeyW");
	double maxHit = 0;
	for (int i = 0; i < 150; i++) { g->frame(1.0 / 30); maxHit = Max(maxHit, 1000 - other->health); }
	printf("  pico damage %.0f, meridian health %.0f, pico moved to (%.1f, %.1f)\n", maxHit, v->health, other->pos.x, other->pos.z);
	Check(maxHit > 0, "car-car collision does damage");
}

static void TestParked(World& w) {
	printf("parked\n");
	auto g = w.game();
	Run(*g, 3);
	printf("  %zu vehicles, %zu parked\n", g->vehicles.list.size(), g->vehicles.parked.size());
	Check(g->vehicles.list.size() > 3, "parked cars stream in");
}

// the same runs as vehcompare.mjs: a car with fixed inputs on the map's and the roads' collision only
static void VehCompare(World& w) {
	WorldData wd; wd.map = &w.map; wd.roadPrims = w.roads.prims; wd.roadDecks = w.roads.decks;
	Game g(wd);
	const Landmark& home = w.map.landmarks.at("home");
	const double x0 = XS[w.map.NearestX(home.x)] + 2;
	struct In { double t, s, b; bool hb; };
	auto straight = [](int) { return In{ 1, 0, 0, false }; };
	auto turn = [](int f) { return In{ 1, f > 45 ? 0.6 : 0.0, 0, false }; };
	auto drift = [](int f) { return In{ 1, f > 50 ? 1.0 : 0.0, 0, f > 50 && f < 60 }; };
	std::vector<std::pair<const char*, std::function<In(int)>>> scripts = { { "straight", straight }, { "turn", turn }, { "drift", drift } };
	auto dummy = g.makeCharacter<Character>(RandomAppearance());
	for (auto& sc : scripts) {
		SpawnOpts o; o.hasColor = true; o.color = 0xffffff;
		Vehicle* v = g.vehicles.spawn("kestrel", x0, home.z - 120, 0, o);
		v->occupants[0] = dummy;
		for (int f = 0; f < 120; f++) {
			const In in = sc.second(f);
			v->input.throttle = in.t; v->input.steer = in.s; v->input.brake = in.b; v->input.handbrake = in.hb;
			v->update(1.0 / 30);
			g.time += 1.0 / 30;
			if (f % 10 == 9) printf("%s %d %.4f %.4f %.4f %.5f %.4f %.4f %.2f\n", sc.first, f + 1, v->pos.x, v->pos.y, v->pos.z, v->yaw, v->vel.x, v->vel.z, v->health);
		}
		v->occupants[0].reset();
		g.vehicles.remove(v);
	}
}

// the living city: traffic drives its lanes, people walk the pavements
static void TestCity(World& w) {
	printf("city\n");
	auto g = w.game(true);
	ToStreet(w, *g);
	PopulateWorld(*g);
	printf("  populated: %zu traffic cars, %zu people\n", g->traffic->cars.size(), g->peds->list.size());
	Check(g->traffic->cars.size() > 5, "traffic populates");
	Check(g->peds->list.size() > 5, "people populate");
	std::map<Vehicle*, V3> start;
	for (auto& c : g->traffic->cars) if (Vehicle* v = c.get()) start[v] = v->pos;
	std::map<Character*, V3> pstart;
	for (auto& p : g->peds->list) pstart[p.get()] = p->pos;
	double maxLat = 0;
	int crashes = 0;
	g->events.carCrash.on([&](Vehicle*, Vehicle*, double) { crashes++; });
	for (int i = 0; i < 30 * 20; i++) {
		g->frame(1.0 / 30);
		for (auto& c : g->traffic->cars) if (Vehicle* v = c.get()) if (auto* ai = dynamic_cast<LaneDriver*>(v->ai.get())) maxLat = Max(maxLat, ai->lat);
	}
	int moved = 0, total = 0;
	double dist = 0;
	for (auto& e : start) {
		bool alive = false;
		for (auto& c : g->traffic->cars) if (c.get() == e.first) alive = true;
		if (!alive) continue;
		total++;
		const double d = Hypot(e.first->pos.x - e.second.x, e.first->pos.z - e.second.z);
		dist += d;
		if (d > 20) moved++;
	}
	int walked = 0, ptotal = 0;
	for (auto& p : g->peds->list) { auto it = pstart.find(p.get()); if (it == pstart.end()) continue; ptotal++; if (Hypot(p->pos.x - it->second.x, p->pos.z - it->second.z) > 3) walked++; }
	printf("  after 20 s: %d / %d cars moved over 20 m (mean %.0f m), max lane offset %.1f m, %d crashes\n", moved, total, total ? dist / total : 0, maxLat, crashes);
	printf("  %d / %d people walked over 3 m; now %zu cars, %zu people\n", walked, ptotal, g->traffic->cars.size(), g->peds->list.size());
	Check(total > 0 && moved * 2 > total, "most traffic drives");
	// (the browser game has about a quarter of them walking that far in 20 s: the rest stand guard or idle)
	Check(ptotal > 0 && walked * 5 > ptotal, "people walk");
	int red = 0, green = 0;
	for (int t = 0; t < 34; t++) { if (SignalState(t, 0) == ESignal::Red) red++; if (SignalState(t, 1) == ESignal::Green) green++; }
	printf("  signal cycle: axis 0 red %d s, axis 1 green %d s\n", red, green);
	Check(red == 18 && green == 13, "signal cycle matches shaders.js");
}

// the Sol Line: both trains appear at their stations and run the timetable
static void TestRail(World& w) {
	printf("rail\n");
	auto g = w.game(true);
	Run(*g, 0.1);
	std::vector<Train*> ts = g->rail->trains();
	printf("  %zu trains, %zu stations, %zu level crossings, line %.0f m\n", ts.size(), g->rail->stations.size(), g->rail->crossings.size(), g->rail->rail ? g->rail->rail->length : 0);
	Check(ts.size() == 2, "passenger and freight trains spawn");
	if (ts.size() < 2) return;
	Train* pass = static_cast<Train*>(g->rail->train.get());
	Train* fr = static_cast<Train*>(g->rail->freight.get());
	printf("  passenger at s %.0f (station %d), %zu cars; freight at s %.0f, %zu wagons\n", pass->s, pass->atStation, pass->cars.size(), fr->s, fr->cars.size());
	const double s0 = pass->s, f0 = fr->s;
	int arrivals = 0;
	g->events.trainArrived.on([&](Vehicle*, int st) { arrivals++; printf("  arrived at %s, t %.0f s\n", g->rail->stations[st].name.c_str(), g->time); });
	double vmax = 0;
	bool crossing = false;
	for (int i = 0; i < 30 * 200; i++) {
		g->frame(1.0 / 30);
		vmax = Max(vmax, std::fabs(pass->v));
		for (auto& c : g->rail->crossings) if (c.active) crossing = true;
	}
	printf("  after 200 s: passenger s %.0f (moved %.0f m, top %.1f m/s), freight s %.0f (moved %.0f m)\n", pass->s, std::fabs(pass->s - s0), vmax, fr->s, std::fabs(fr->s - f0));
	Check(std::fabs(pass->s - s0) > 300, "passenger train runs");
	Check(std::fabs(fr->s - f0) > 100, "freight train runs");
	Check(vmax > 15 && vmax < 24.5, "cruises at up to 24 m/s");
	Check(arrivals > 0, "stops at a station");
	Check(crossing, "a level crossing closes");
}

// boarding: F on the platform next to a stopped train takes a carriage seat; the cab door gives the controls
static void TestBoard(World& w) {
	printf("board\n");
	auto g = w.game(true);
	g->frame(1.0 / 30);
	Train* t = static_cast<Train*>(g->rail->train.get());
	Player& p = *g->player;
	const RailStop& st = g->rail->stations[t->atStation];
	g->respawnPlayer(st.x, st.z, 0);
	int seat;
	const V3 d = t->nearestDoor(p.pos, seat);
	printf("  at %s, nearest door %.1f m away (seat %d), train dwell %.1f s\n", st.name.c_str(), Hypot(d.x - p.pos.x, d.z - p.pos.z), seat, t->dwell);
	t->dwell = 30;
	g->input.KeyDown("KeyF"); g->frame(1.0 / 30); g->input.KeyUp("KeyF");
	Run(*g, 4);
	printf("  in %s, seat %d, hidden %d\n", p.vehicle ? p.vehicle->type.c_str() : "nothing", p.seat, p.hiddenInVehicle ? 1 : 0);
	Check(p.vehicle == t && p.seat > 0, "boards a carriage");
	g->input.KeyDown("KeyF"); g->frame(1.0 / 30); g->input.KeyUp("KeyF");
	Run(*g, 3);
	Check(!p.vehicle && !p.hiddenInVehicle, "gets off onto the platform");
	printf("  off at (%.1f, %.1f, %.1f), %.1f m from the platform point\n", p.pos.x, p.pos.y, p.pos.z, Hypot(p.pos.x - st.x, p.pos.z - st.z));
	// the cab
	const V3 cab = t->localToWorld(2.2, 0, LOCO_L / 2 - 3.4);
	g->respawnPlayer(cab.x, cab.z, 0);
	g->input.KeyDown("KeyF"); g->frame(1.0 / 30); g->input.KeyUp("KeyF");
	Run(*g, 4);
	Check(p.vehicle == t && p.seat == 0, "climbs into the cab");
	const double s0 = t->s;
	g->input.KeyDown("KeyW");
	Run(*g, 5);
	g->input.KeyUp("KeyW");
	printf("  driving: %.1f m/s after 5 s of throttle, moved %.1f m\n", t->v, t->s - s0);
	Check(t->v > 3, "the player drives it");
}

// WASTED and BUSTED: slow motion, the shard, then the respawn at the hospital or the police station
static void TestWasted(World& w) {
	printf("wasted\n");
	auto g = w.game(true);
	ToStreet(w, *g);
	Player& p = *g->player;
	p.money = 500;
	p.giveWeapon("pistol", 50);
	DamageInfo di; di.type = "fall";
	p.takeDamage(1000, di);
	g->frame(1.0 / 30);
	printf("  state %s, time scale %.2f, wasted %d\n", g->gameplay->state.c_str(), g->timeScale, (int)g->stats.wasted);
	Check(g->gameplay->state == "dead" && g->stats.wasted == 1, "dies: WASTED");
	Check(std::fabs(g->timeScale - 0.28) < 1e-9, "slow motion 0.28");
	Run(*g, 1.5);
	printf("  after 1.5 s: shard '%s', desat %.2f, flash %.2f\n", g->hudModel->shard.c_str(), g->post.desat, g->post.flash);
	Check(g->hudModel->shard == "wasted", "the shard comes up (no stinger: after 1.1 s)");
	Run(*g, 6.5);
	const Landmark& H = w.map.landmarks.at("hospital");
	const P3 sp = H.pts.at("respawn");
	printf("  after 8 s: state %s, at (%.1f, %.1f), hospital respawn (%.1f, %.1f), money %.0f, health %.0f, weapons %zu\n", g->gameplay->state.c_str(), p.pos.x, p.pos.z, sp.x, sp.z, p.money, p.health, p.weapons.size());
	Check(g->gameplay->state == "playing" && !p.dead, "respawns");
	Check(Hypot(p.pos.x - sp.x, p.pos.z - sp.z) < 1, "at the hospital");
	Check(p.money == 400 && p.weapons.size() == 1, "the hospital bill ($100) and the weapons gone");
	Check(g->timeScale == 1 && !g->hudModel->dead, "back to normal");
	printf("busted\n");
	g->events.busted.emit();
	g->frame(1.0 / 30);
	Check(g->gameplay->state == "busted" && g->stats.busted == 1 && p.animState.handsUp, "BUSTED: hands up");
	Run(*g, 8);
	const P3 ps = w.map.landmarks.at("police").pts.at("respawn");
	printf("  state %s at (%.1f, %.1f), police respawn (%.1f, %.1f), money %.0f\n", g->gameplay->state.c_str(), p.pos.x, p.pos.z, ps.x, ps.z, p.money);
	Check(g->gameplay->state == "playing" && Hypot(p.pos.x - ps.x, p.pos.z - ps.z) < 1, "out at the police station");
}

// effects: an explosion fills the pools, leaves a scorch and lingering fire, then clears
static void TestEffects(World& w) {
	printf("effects\n");
	auto g = w.game(true);
	ToStreet(w, *g);
	Effects* fx = dynamic_cast<Effects*>(g->effects);
	Check(fx != nullptr, "effects installed first");
	if (!fx) return;
	const V3 at = g->player->pos + V3(0, 0.5, 12);
	const double foot[4] = { 1, 2.3, 0, 1 };
	fx->explosion(at, 6.75, foot);
	g->frame(1.0 / 30);
	int decals = 0; for (auto& d : fx->decals) if (d.live) decals++;
	printf("  after the blast: %zu smoke, %zu fire, %zu dots, %zu emitters, %d decals, flash %.0f\n", fx->alphaPool.parts.size(), fx->addPool.parts.size(), fx->dotAlpha.parts.size(), fx->emitters.size(), decals, fx->lights[0].intensity + fx->lights[1].intensity + fx->lights[2].intensity);
	Check(fx->addPool.parts.size() > 40 && fx->alphaPool.parts.size() > 30 && decals == 1, "fireball, smoke and a scorch mark");
	Run(*g, 20);
	printf("  after 20 s: %zu smoke, %zu fire, %zu emitters\n", fx->alphaPool.parts.size(), fx->addPool.parts.size(), fx->emitters.size());
	Check(fx->emitters.empty() && fx->addPool.parts.size() < 20, "burns out");
	fx->skidAdd("k", 0, 0, 0, 0.24, 1);
	g->frame(1.0 / 30);
	const int v0 = fx->skidVersion;
	fx->skidAdd("k", 0.5, 0, 0, 0.24, 1);
	Check(fx->skidVersion == v0 + 1, "skid marks join up frame to frame");
}

// combat: the player shoots a pedestrian in front of them; a grenade hurts the people round it
static void TestCombat(World& w) {
	printf("combat\n");
	auto g = w.game(true);
	ToStreet(w, *g);
	Player& p = *g->player;
	g->cheats.god = true;
	for (auto& q : g->peds->list) g->peds->remove(q.get());
	Ped* v = g->peds->spawnPed(p.pos.x, p.pos.z + 6);
	v->yaw = kPi; v->setState("idle");
	g->frame(1.0 / 30);
	p.giveWeapon("pistol", 100); p.equip("pistol");
	int kills = 0; std::string part;
	g->events.kill.on([&](Character* k, Character*, const std::string&, const std::string& pt) { if (k == &p) { kills++; part = pt; } });
	Combat* cb = dynamic_cast<Combat*>(g->combat);
	const double h0 = v->health;
	for (int i = 0; i < 20 && !v->dead; i++) {
		const V3 from = p.pos + V3(0, 1.5, 0);
		const V3 to = v->pos + V3(0, 1.1, 0);
		cb->fireWeapon(&p, *FindWeapon("pistol"), from, (to - from).normalized());
		g->frame(1.0 / 30);
	}
	printf("  health %.0f -> %.0f, dead %d, kills %d (%s)\n", h0, v->health, v->dead ? 1 : 0, kills, part.c_str());
	Check(v->health < h0, "the shot hurts");
	Check(v->dead && kills == 1, "a kill event when they die");
	Ped* a = g->peds->spawnPed(p.pos.x + 8, p.pos.z + 20);
	Ped* b = g->peds->spawnPed(p.pos.x + 9, p.pos.z + 21);
	g->frame(1.0 / 30);
	const double ha = a->health, hb = b->health;
	cb->explosion(V3(p.pos.x + 8.5, a->pos.y + 0.3, p.pos.z + 20.5), 7, 170, &p);
	g->frame(1.0 / 30);
	printf("  grenade: %.0f -> %.0f, %.0f -> %.0f\n", ha, a->health, hb, b->health);
	Check(a->health < ha && b->health < hb, "an explosion hurts the people round it");
	cb->throwGrenade(&p, p.forward(), "grenade");
	Run(*g, 0.5);
	Check(cb->projectiles.size() == 1, "a thrown grenade flies");
	Run(*g, 3);
	Check(cb->projectiles.empty(), "and goes off after 3 s");
}

// police: witnessed crimes raise the stars, pursuit cars come one per star, you lose them by staying out of
// sight for 10 + 5 x level seconds, the helicopter comes at three stars, and a cop next to you arrests you
static void TestPolice(World& w) {
	printf("police\n");
	auto g = w.game(true);
	ToStreet(w, *g);
	Player& p = *g->player;
	p.invincible = true; // (the god cheat needs the admin tools)
	Police* pol = g->policeSys;
	g->missionNoBust = true; // (no arrests until the end)
	int ups = 0;
	g->events.wantedUp.on([&](int) { ups++; });
	pol->crime(1, p.pos, true);
	printf("  severe crime: heat %.2f, level %d\n", pol->heat, pol->level);
	Check(pol->level == 1 && ups == 1, "a severe crime gives one star");
	pol->crime(3.5, p.pos, false);
	printf("  more crime: heat %.2f, level %d\n", pol->heat, pol->level);
	Check(pol->level == 2, "heat over 3 gives two stars");
	Run(*g, 10);
	auto pursuers = [&]() { int n = 0; for (auto& c : pol->cars) if (Vehicle* v = c.get()) if (dynamic_cast<PursuitDriver*>(v->ai.get()) && !v->isWrecked()) n++; return n; };
	double nearest = 1e9;
	for (auto& c : pol->cars) if (Vehicle* v = c.get()) nearest = Min(nearest, Hypot(v->pos.x - p.pos.x, v->pos.z - p.pos.z));
	printf("  after 10 s: %d pursuit cars, %zu cops, nearest car %.0f m, level %d\n", pursuers(), pol->cops.size(), nearest, pol->level);
	int driven = 0; for (auto& c : pol->cars) if (Vehicle* v = c.get()) if (dynamic_cast<PursuitDriver*>(v->ai.get()) && v->driver()) driven++;
	Check(pol->cars.size() >= 2 && driven <= 2, "pursuit cars come, two at a time at two stars");
	for (int k = 0; k < 3; k++) {
		Run(*g, 5);
		std::string cs;
		for (auto& c : pol->cars) if (Vehicle* v = c.get()) { char b[96]; snprintf(b, sizeof b, " [%.0f m %.1f m/s%s%s]", Hypot(v->pos.x - p.pos.x, v->pos.z - p.pos.z), v->speed(), v->driver() ? "" : " empty", v->isWrecked() ? " wrecked" : ""); cs += b; }
		double nc = 1e9; for (auto& c : pol->cops) if (Ped* q = c.get()) if (!q->vehicle) nc = Min(nc, Hypot(q->pos.x - p.pos.x, q->pos.z - p.pos.z));
		printf("   t+%d: cars%s, nearest cop on foot %.0f m\n", 10 + 5 * (k + 1), cs.c_str(), nc);
	}
	nearest = 1e9;
	for (auto& c : pol->cars) if (Vehicle* v = c.get()) nearest = Min(nearest, Hypot(v->pos.x - p.pos.x, v->pos.z - p.pos.z));
	printf("  after 25 s: nearest car %.0f m, seen %d\n", nearest, pol->seen ? 1 : 0);
	Check(nearest < 40, "they close in");
	// run: far away and out of sight
	const Landmark& H = w.map.landmarks.at("hospital");
	g->respawnPlayer(H.x, H.z, 0);
	Run(*g, 5);
	printf("  5 s unseen: level %d, flashing %d\n", pol->level, pol->flash ? 1 : 0);
	Check(pol->level == 2 && pol->flash, "the stars flash once they lose you");
	Run(*g, 16);
	printf("  21 s unseen: level %d, %zu cars left\n", pol->level, pol->cars.size());
	Check(pol->level == 0, "lost after 10 + 5 x 2 seconds");
	// three stars: the helicopter
	pol->setLevel(3);
	Run(*g, 0.1);
	Check(pol->heli != nullptr, "the helicopter comes at three stars");
	double hd0 = pol->heli ? Hypot(pol->heli->pos.x - p.pos.x, pol->heli->pos.z - p.pos.z) : 0;
	Run(*g, 20);
	double hd = pol->heli ? Hypot(pol->heli->pos.x - p.pos.x, pol->heli->pos.z - p.pos.z) : 0;
	printf("  helicopter %.0f m -> %.0f m away, %.0f m up\n", hd0, hd, pol->heli ? pol->heli->pos.y - p.pos.y : 0);
	Check(pol->heli && hd < 45, "it circles over you");
	int downs = 0;
	g->events.heliDown.on([&]() { downs++; });
	Combat* cb = dynamic_cast<Combat*>(g->combat);
	CombatHit hit;
	const V3 from = p.pos + V3(0, 1.5, 0);
	const V3 hp = pol->heli->pos;
	const V3 dir = (hp - from).normalized();
	const bool hitHeli = cb->raycast(from.x, from.y, from.z, dir.x, dir.y, dir.z, 200, &p, hit) && hit.kind == CombatHit::Heli;
	printf("  shot at it: hit kind %d at %.1f m (it is %.1f m away)\n", (int)hit.kind, hit.t, (hp - from).length());
	Check(hitHeli, "a shot at it hits it");
	pol->heliHit(9999);
	Run(*g, 15);
	const bool fresh = !pol->heli || (!pol->heli->down && pol->heli->health == 700);
	printf("  shot down: %d heliDown, %s\n", downs, !pol->heli ? "gone" : fresh ? "a new one on its way" : "still falling");
	Check(downs == 1 && fresh, "it comes down and burns");
	// arrest
	p.invincible = false;
	g->missionNoBust = false;
	pol->reset();
	pol->setLevel(1);
	Ped* cop = pol->spawnCop(p.pos.x + 1, p.pos.z);
	(void)cop;
	int busted = 0;
	g->events.busted.on([&]() { busted++; });
	Run(*g, 2.5);
	printf("  a cop at arm's length: busted %d, state %s\n", busted, g->gameplay->state.c_str());
	Check(busted == 1, "a cop next to you arrests you");
}

// pickups: health, armour, cash, a weapon and a hidden package; the Spray Shack loses the police
static void TestPickups(World& w) {
	printf("pickups\n");
	auto g = w.game(true);
	Player& p = *g->player;
	Pickups* pk = g->pickupsSys;
	printf("  %zu pickups, %zu package spots, %zu markers\n", pk->list.size(), pk->packageSpots.size(), pk->markers.size());
	Check(pk->packageSpots.size() == 30 && pk->packages.size() == 30, "30 hidden packages");
	for (auto& m : pk->markers) printf("  marker at (%.1f, %.1f, %.1f) r %.1f h %.1f\n", m->pos.x, m->pos.y, m->pos.z, m->radius, m->height);
	auto find = [&](const std::string& kind, const std::string& weapon = "") -> Pickup* {
		for (auto& q : pk->list) if (q->kind == kind && (weapon.empty() || q->data.weapon == weapon) && q->respawn) return q.get();
		return nullptr;
	};
	auto walkTo = [&](const V3& at) { g->respawnPlayer(at.x, at.z, 0); g->frame(1.0 / 30); g->frame(1.0 / 30); };
	Pickup* h = find("health");
	p.health = 40;
	walkTo(h->pos);
	printf("  health pickup at (%.1f, %.1f, %.1f): health %.0f, hidden for %.0f s\n", h->pos.x, h->pos.y, h->pos.z, p.health, h->hiddenUntil - pk->t);
	Check(p.health == p.maxHealth && !h->visible && h->hiddenUntil - pk->t > 89, "health: topped up, back in 90 s");
	Pickup* a = find("armor");
	walkTo(a->pos);
	Check(p.armor == 100, "armour");
	Pickup* wp = find("weapon", "pistol");
	walkTo(wp->pos);
	printf("  pistol: %d, ammo %.0f, holding %s, help '%s'\n", (int)p.weapons.count("pistol"), p.weapons.count("pistol") ? p.weapons["pistol"].ammo + p.weapons["pistol"].clip : 0, p.weapon.c_str(), g->hudModel->helpLine.text.c_str());
	Check(p.weapons.count("pistol") && p.weapon == "pistol", "a weapon, and you hold it");
	const double m0 = p.money;
	pk->dropMoney(p.pos, 75);
	g->frame(1.0 / 30);
	Check(p.money == m0 + 75, "dropped cash");
	const Pickups::Spot sp = pk->packageSpots[0];
	walkTo(V3(sp.x, 0, sp.z));
	printf("  package: %zu found, money %.0f, message '%s'\n", pk->collectedPackages.size(), p.money, g->hudModel->big.text.c_str());
	Check(pk->collectedPackages.size() == 1 && p.money == m0 + 175, "a hidden package");
	Check(pk->packages.size() == 30 && pk->packages[0]->removed, "and it's gone");
	Run(*g, 70);
	Check(find("health") && find("weapon", "smg"), "the world's pickups don't expire");
	// the Spray Shack
	const Landmark& S = w.map.landmarks.at("spray");
	Vehicle* v = g->vehicles.spawn("zenith", S.x + 20, S.z, 0);
	g->vehicles.seatNow(&p, v, 0);
	g->policeSys->setLevel(2);
	g->missionNoBust = true;
	const uint32_t c0 = v->color;
	const double m1 = p.money;
	v->pos.x = S.x; v->pos.z = S.z;
	Run(*g, 1.5);
	printf("  spray: money %.0f -> %.0f, colour %06x -> %06x, wanted %d\n", m1, p.money, c0, v->color, g->policeSys->level);
	Check(p.money == m1 - 100 && v->color != c0 && v->painted, "a new paint job for $100");
	Check(g->policeSys->level == 0, "and the cops lose you");
}

// roadblocks: at four stars a driving suspect meets a line of cruisers ahead with cops in cover and a spike
// strip in front that bursts the tyres; the block is cleared away once left behind
static void TestRoadblocks(World& w) {
	printf("roadblocks\n");
	auto g = w.game(true);
	Player& p = *g->player;
	p.invincible = true;
	g->missionNoBust = true;
	Roadblocks* rb = dynamic_cast<Roadblocks*>(g->roadblocks);
	Check(rb != nullptr, "installed");
	if (!rb) return;
	// a long straight avenue
	const RoadNet& net = w.map.roads;
	int best = -1;
	for (int i = 0; i < (int)net.edges.size(); i++) {
		const REdge& e = net.edges[i];
		if (e.removed || e.type == ERoad::Rail || e.type == ERoad::Ramp || e.type == ERoad::Freeway || e.len < 320) continue;
		const EdgePoint a = net.At(e, 40), b = net.At(e, 240);
		if (a.tx * b.tx + a.tz * b.tz > 0.999) { best = i; break; }
	}
	Check(best >= 0, "a long straight road");
	if (best < 0) return;
	const REdge& e = net.edges[best];
	const EdgePoint a = net.At(e, 40);
	const double yaw = std::atan2(a.tx, a.tz);
	printf("  on the road at (%.1f, %.1f) heading %.3f\n", a.x, a.z, yaw);
	g->respawnPlayer(a.x, a.z, yaw);
	Vehicle* v = g->vehicles.spawn("zenith", a.x, a.z, yaw);
	g->vehicles.seatNow(&p, v, 0);
	g->policeSys->setLevel(4);
	v->vel = V3(a.tx * 25, 0, a.tz * 25);
	rb->timer = 0;
	rb->update(1.0 / 30);
	printf("  %zu roadblocks, next in %.0f s\n", rb->blocks.size(), rb->timer);
	Check(rb->blocks.size() == 1, "a roadblock goes up ahead");
	if (rb->blocks.empty()) return;
	const Roadblocks::Block& b = rb->blocks[0];
	const double ahead = (b.x - v->pos.x) * a.tx + (b.z - v->pos.z) * a.tz;
	int enforcer = 0, holding = 0;
	for (auto& c : b.cars) if (c.get() && c->def.id == "enforcer") enforcer++;
	for (auto& c : b.cops) if (c.get() && c->hasHoldPos) holding++;
	printf("  %.0f m ahead, %zu cars (%d Enforcer), %zu cops (%d holding), spike %d, %.1f m wide\n", ahead, b.cars.size(), enforcer, b.cops.size(), holding, b.hasSpike ? 1 : 0, b.spike.len);
	Check(ahead > 80 && ahead < 180, "90 to 165 m ahead");
	Check(b.cars.size() >= 2 && enforcer == 1 && (int)b.cops.size() == holding && holding >= 3, "cruisers, the Enforcer and cops in cover");
	Check(b.hasSpike && rb->timer == 17, "a stinger at four stars; the next in 17 s");
	// over the stinger
	v->pos = V3(b.spike.x, b.spike.y, b.spike.z);
	v->vel = V3(-b.tx * 20, 0, -b.tz * 20);
	rb->update(1.0 / 30);
	Check(v->flat, "the stinger bursts the tyres");
	// left behind
	const Landmark& H = w.map.landmarks.at("hospital");
	g->respawnPlayer(H.x, H.z, 0);
	Run(*g, 6);
	printf("  far away: %zu roadblocks\n", rb->blocks.size());
	Check(rb->blocks.empty(), "cleared away once you're gone");
}

// street crime: a crime a patrol sees puts stars on the suspect and the unit responds; a suspect who complies
// is cuffed and driven off; muggers, car thieves and jaywalkers get staged near the player
static void TestNpcCrime(World& w) {
	printf("npccrime\n");
	auto g = w.game(true);
	PopulateWorld(*g);
	ToStreet(w, *g);
	Player& p = *g->player;
	p.invincible = true;
	NpcCrime* nc = dynamic_cast<NpcCrime*>(g->npcCrime);
	Check(nc != nullptr, "installed");
	if (!nc) return;
	Run(*g, 1);
	// a suspect next to a patrol car
	Ped* s = g->peds->spawnPed(p.pos.x + 30, p.pos.z + 6);
	Vehicle* car = g->policeSys->spawnCar(false, &s->pos);
	printf("  patrol car %s, %.0f m from the suspect\n", car ? "spawned" : "missing", car ? Hypot(car->pos.x - s->pos.x, car->pos.z - s->pos.z) : 0.0);
	if (!car) { Check(false, "a patrol car"); return; }
	NpcCrime::CommitOpts o; o.hasWitness = true; o.witness = car;
	NpcCase* rec = nc->commit(s, "assault", o);
	Check(rec && rec->known && rec->stars == 1 && s->npcWanted == 1, "a witnessed assault: one star");
	Check(rec && rec->unit && rec->phase == "respond" && car->npcJob, "the unit responds");
	if (!rec) return;
	rec->force = "comply";
	std::vector<NpcTag> tags; nc->tagged(tags);
	Check(tags.size() == 1 && tags[0].c == s, "tagged for the HUD");
	std::string why; double tArr = 0;
	auto keep = s->npcCase;
	for (int i = 0; i < 30 * 90 && keep->phase != "closed"; i++) g->frame(1.0 / 30);
	why = keep->why; tArr = g->time;
	printf("  case %s after %.0f s (phase seen: %s), suspect in the car %d\n", why.c_str(), tArr, keep->phase.c_str(), s->vehicle == car ? 1 : 0);
	Check(why == "arrested" && s->vehicle == car, "a suspect who complies is cuffed and put in the cruiser");
	bool listed = false; for (auto& c : nc->cases) if (c == keep) listed = true;
	Check(!car->npcJob && !listed && !s->npcCase, "the case is closed");
	// staged crimes
	int mug = 0, steal = 0, jay = 0;
	for (int k = 0; k < 6; k++) { if (nc->stage("mug")) mug++; if (nc->stage("steal")) steal++; if (nc->stage("jaywalk")) jay++; Run(*g, 0.5); }
	printf("  staged: %d muggings, %d car thefts, %d jaywalkers\n", mug, steal, jay);
	Check(mug > 0 && steal > 0 && jay > 0, "muggings, car thefts and jaywalking get staged");
	int robbed = 0, stolen = 0;
	for (int i = 0; i < 30 * 40; i++) {
		g->frame(1.0 / 30);
		for (auto& c : nc->cases) { if (c->crime == "mugging") robbed = 1; if (c->crime == "gta") stolen = 1; }
	}
	printf("  within 40 s: a mugging %d, a car theft %d; %zu cases open\n", robbed, stolen, nc->cases.size());
	Check(robbed + stolen > 0, "a mugging or a car theft happens");
	Run(*g, 50);
	printf("  after 90 s: %zu cases, %zu people, %zu cars\n", nc->cases.size(), g->peds->list.size(), g->vehicles.list.size());
	Check(true, "runs for 90 s");
}

// the weapon wheel: Tab slows the game to 0.18 and fans out the weapons; the mouse picks one and letting go draws it
static void TestWheel(World& w) {
	printf("wheel\n");
	auto g = w.game(true);
	ToStreet(w, *g);
	Player& p = *g->player;
	p.giveWeapon("pistol", 60); p.giveWeapon("smg", 120); p.giveWeapon("bat", 1);
	p.equip("pistol");
	g->frame(1.0 / 30);
	WeaponWheel* ww = g->wheel;
	g->input.KeyDown("Tab");
	g->frame(1.0 / 30);
	std::string ring; for (auto& id : ww->list) ring += id + " ";
	printf("  open %d, ring: %s, selected %s, slow motion %.2f\n", ww->open ? 1 : 0, ring.c_str(), ww->sel >= 0 ? ww->list[ww->sel].c_str() : "-", g->fxScale());
	Check(ww->open && g->fxScale() == 0.18, "Tab opens it in slow motion");
	Check(ww->sel >= 0 && ww->list[ww->sel] == "pistol", "the weapon in hand is highlighted");
	// point at the slot of the smg
	int k = -1; for (int i = 0; i < (int)ww->list.size(); i++) if (ww->list[i] == "smg") k = i;
	const double a = k * kTau / ww->list.size() - kPi / 2;
	g->input.mouse.dx = std::cos(a) * 80; g->input.mouse.dy = std::sin(a) * 80;
	g->frame(1.0 / 30);
	Check(ww->sel == k, "the mouse picks a slot");
	g->input.KeyUp("Tab");
	g->frame(1.0 / 30);
	Run(*g, 1);
	printf("  closed %d, holding %s, slow motion %.2f\n", ww->open ? 0 : 1, p.weapon.c_str(), g->fxScale());
	Check(!ww->open && p.weapon == "smg" && g->fxScale() == 1, "letting go draws it and time runs again");
}

// the special ability: Caps Lock slows the world to 0.38 on foot; the meter drains in real time (about nine
// seconds from full) and stops it; a kill refills a little
static void TestSpecial(World& w) {
	printf("special\n");
	auto g = w.game(true);
	ToStreet(w, *g);
	Special* sp = g->special;
	auto tap = [&](const char* k) { g->input.KeyDown(k); g->frame(1.0 / 30); g->input.KeyUp(k); g->frame(1.0 / 30); };
	tap("CapsLock");
	printf("  on %d, slow motion %.2f, meter %.3f\n", sp->active ? 1 : 0, g->fxScale(), sp->meter);
	Check(sp->active && g->fxScale() == 0.38, "Caps Lock: bullet time on foot");
	Run(*g, 3);
	printf("  after 3 s: meter %.3f, desaturation %.2f\n", sp->meter, g->post.desat);
	Check(std::fabs(sp->meter - (1 - 0.11 * 3.07)) < 0.02, "drains 0.11 a second of real time");
	Check(g->post.desat > 0.35, "the colour drains");
	tap("KeyZ");
	Check(!sp->active && g->fxScale() == 1, "Z stops it early");
	const double m0 = sp->meter;
	g->events.kill.emit(g->player.get(), g->player.get(), "pistol", "head");
	Check(std::fabs(sp->meter - m0 - 0.1) < 0.001, "a headshot kill refills 0.1");
	tap("CapsLock");
	Run(*g, 10);
	printf("  run dry: on %d, meter %.3f\n", sp->active ? 1 : 0, sp->meter);
	Check(!sp->active && g->fxScale() == 1, "it stops when the meter runs dry");
	sp->meter = 0.05;
	tap("CapsLock");
	Check(!sp->active, "not with less than 0.12 in the meter");
}

int main(int argc, char** argv) {
	InstallCrashTrace();
	setvbuf(stdout, nullptr, _IONBF, 0);
	SeedRand(12345);
	const auto t0 = std::chrono::steady_clock::now();
	World w;
	printf("world %.1f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
	if (argc > 1 && !std::strcmp(argv[1], "vehcompare")) { VehCompare(w); return 0; }
	auto want = [&](const char* n) { if (argc < 2) return true; for (int i = 1; i < argc; i++) if (!std::strcmp(argv[i], n)) return true; return false; };
	if (want("walk")) TestWalk(w);
	if (want("drive")) TestDrive(w);
	if (want("crash")) TestCrash(w);
	if (want("parked")) TestParked(w);
	if (want("city")) TestCity(w);
	if (want("rail")) TestRail(w);
	if (want("board")) TestBoard(w);
	if (want("wasted")) TestWasted(w);
	if (want("effects")) TestEffects(w);
	if (want("combat")) TestCombat(w);
	if (want("police")) TestPolice(w);
	if (want("pickups")) TestPickups(w);
	if (want("roadblocks")) TestRoadblocks(w);
	if (want("npccrime")) TestNpcCrime(w);
	if (want("wheel")) TestWheel(w);
	if (want("special")) TestSpecial(w);
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
