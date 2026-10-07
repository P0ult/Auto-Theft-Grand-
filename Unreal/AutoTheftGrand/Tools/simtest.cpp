// Native tests for the simulation (no Unreal): Tools/native.sh simtest.exe simtest.cpp && ./simtest.exe [test ...]
// Each test sets up a game on the generated world, runs fixed 1/30 s frames and prints what it measured.
#include "Sim/Aircraft.h"
#include "Sim/Army.h"
#include "Sim/Bike.h"
#include "Sim/Boat.h"
#include "Sim/Boats.h"
#include "Sim/Combat.h"
#include "Sim/Effects.h"
#include "Sim/Game.h"
#include "Sim/Gameplay.h"
#include "Sim/Heists.h"
#include "Sim/Military.h"
#include "Sim/Missions.h"
#include "Sim/NpcCrime.h"
#include "Sim/Hud.h"
#include "Sim/Peds.h"
#include "Sim/Pets.h"
#include "Sim/Phone.h"
#include "Sim/Pickups.h"
#include "Sim/Police.h"
#include "Sim/Rail.h"
#include "Sim/Roadblocks.h"
#include "Sim/Setup.h"
#include "Sim/Shops.h"
#include "Sim/Skateboard.h"
#include "Sim/Skateparks.h"
#include "Sim/Special.h"
#include "Sim/Story.h"
#include "Sim/Traffic.h"
#include "Sim/WeaponWheel.h"
#include "Sim/Wildlife.h"
#include "WorldGen.h"
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
	const V3 hp = pol->heli->pos;
	const V3 from = hp - V3(6, 10, 0); // (from close under it: from the street a building can be in the way)
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

// the walk-in shops: a clerk turns up behind each counter, the Gun Barn's counter opens the gun menu, a gun in a
// clerk's face empties the till (or, at the Gun Barn, starts a shoot-out), drinks make the camera sway, and a
// dead clerk's shop opens again once you've been away a while
static void TestShops(World& w) {
	printf("shops\n");
	auto g = w.game(true);
	Player& p = *g->player;
	ShopSystem* S = g->shops;
	HudModel& hud = *g->hudModel;
	printf("  %zu shops:", S->shops.size());
	for (auto& s : S->shops) printf(" %s", s.it->key.c_str());
	printf("\n");
	Check(S->shops.size() == 8, "a clerk for each of the eight shops");
	auto shop = [&](const std::string& key) -> ShopSystem::Shop& { for (auto& s : S->shops) if (s.it->key == key) return s; return S->shops[0]; };
	auto at = [&](const P3& q) { g->respawnPlayer(q.x, q.z, 0); };
	// the Gun Barn: step up to the counter
	ShopSystem::Shop& gun = shop("gunshop");
	at(gun.it->center);
	Run(*g, 1);
	Check(gun.clerk && gun.state == "calm" && gun.marker->visible, "the Gun Barn's clerk is behind the counter");
	printf("  clerk at (%.2f, %.2f) yaw %.2f, said '%s'\n", gun.clerk->pos.x, gun.clerk->pos.z, gun.clerk->yaw, hud.speeches.empty() ? "" : hud.speeches.back().text.c_str());
	Check(!hud.speeches.empty(), "and says hello");
	p.money = 1000;
	at(gun.it->service);
	g->frame(1.0 / 30); g->frame(1.0 / 30);
	printf("  menu '%s' (%s), %zu rows, paused %d\n", hud.menu.title.c_str(), hud.menu.kind.c_str(), hud.menu.rows.size(), (int)g->paused);
	Check(hud.menu.kind == "gunshop" && hud.menu.rows.size() == 13 && g->paused, "the gun menu (11 weapons, armour, leave), the game paused");
	for (auto& r : hud.menu.rows) printf("    %-14s %-40s [%s]%s\n", r.name.c_str(), r.desc.c_str(), r.button.c_str(), r.enabled ? "" : " (disabled)");
	hud.menuPress(2);
	printf("  bought a pistol: money %.0f, holding %s, ammo %.0f\n", p.money, p.weapon.c_str(), p.weapons.count("pistol") ? p.weapons["pistol"].ammo + p.weapons["pistol"].clip : 0);
	Check(p.weapon == "pistol" && p.money == 1000 - FindWeapon("pistol")->price, "a pistol, paid for");
	Check(hud.menu.rows[2].button.rfind("Ammo $", 0) == 0, "its button now sells ammo");
	hud.menuPress(11);
	Check(p.armor == 100, "body armour");
	p.money = 10;
	hud.menuPress(7);
	Check(hud.menu.rows[7].button == "Not enough cash" && !p.weapons.count("minigun"), "not enough cash for the minigun");
	hud.menuPress((int)hud.menu.rows.size() - 1);
	Check(hud.menu.kind.empty() && !g->paused, "leaving closes it");
	// Ray's Liquor: a gun in the clerk's face
	ShopSystem::Shop& liq = shop("liquor");
	at(liq.it->center);
	Run(*g, 1.5);
	Check(liq.clerk && liq.state == "calm", "Ray is behind the counter");
	const double m0 = p.money;
	auto aimAt = [&](Ped* c) {
		const double a = std::atan2(c->pos.x - p.pos.x, c->pos.z - p.pos.z);
		p.yaw = a; g->rig.yaw = a + kPi; g->rig.pitch = -0.05;
		g->input.mouse.right = true;
	};
	for (int i = 0; i < 30 && liq.state == "calm"; i++) { aimAt(liq.clerk.get()); g->frame(1.0 / 30); }
	printf("  aimed: state %s, aiming %d\n", liq.state.c_str(), (int)p.aiming);
	Check(liq.state == "handsup", "hands up");
	g->input.mouse.right = false;
	Run(*g, 3);
	Run(*g, 0.5);
	printf("  after the till: state %s, money %.0f -> %.0f, wanted %d\n", liq.state.c_str(), m0, p.money, g->policeSys->level);
	Check(liq.state == "robbed" && g->policeSys->level >= 1, "the till handed over, the cops called");
	at(liq.it->service);
	Run(*g, 0.5);
	printf("  at the till: money %.0f\n", p.money);
	Check(p.money > m0 + 140, "the cash on the counter");
	g->policeSys->setLevel(0);
	// the Gun Barn doesn't hand over the till
	at(gun.it->center);
	Run(*g, 0.5);
	for (int i = 0; i < 30 && gun.state == "calm"; i++) { aimAt(gun.clerk.get()); g->frame(1.0 / 30); }
	g->input.mouse.right = false;
	printf("  Gun Barn: state %s, clerk %s with %s, state %s\n", gun.state.c_str(), gun.clerk->brain.c_str(), gun.clerk->weapon.c_str(), gun.clerk->state.c_str());
	Check(gun.state == "hostile" && gun.clerk->weapon == "shotgun" && gun.clerk->state == "attack", "the owner pulls a shotgun");
	S->calmDown();
	Check(!gun.clerk && gun.state == "closed", "and calms down after a respawn");
	g->policeSys->setLevel(0);
	// the bar: two whiskeys
	ShopSystem::Shop& bar = shop("bar");
	at(bar.it->center);
	Run(*g, 1);
	p.money = 100;
	at(bar.it->service);
	g->frame(1.0 / 30); g->frame(1.0 / 30);
	Check(hud.menu.kind == "store" && hud.menu.title == "THE RUSTY ANCHOR", "the bar's menu");
	hud.menuPress(1); hud.menuPress(1);
	printf("  two whiskeys: drunk %.2f, note '%s', money %.0f\n", p.drunk, hud.menu.note.c_str(), p.money);
	Check(std::fabs(p.drunk - 1.2) < 1e-9 && hud.menu.note == "The room is starting to spin..." && p.money == 76, "the room sways");
	hud.closeOverlay();
	const double y0 = g->rig.yaw;
	Run(*g, 2);
	Check(g->rig.yaw != y0 && p.drunk < 1.2, "the camera sways and it wears off");
	// kill Ray: the shop shuts until you've been away
	at(liq.it->center);
	Run(*g, 0.5);
	Ped* ray = liq.clerk.get();
	DamageInfo di; di.source = &p; di.weapon = "pistol";
	ray->takeDamage(500, di);
	Run(*g, 0.5);
	at(liq.it->service);
	Run(*g, 0.2);
	printf("  dead clerk: state %s, help '%s'\n", liq.state.c_str(), hud.helpLine.text.c_str());
	Check(liq.state == "dead" && hud.menu.kind.empty() && hud.helpLine.text == "There's nobody behind the counter.", "a dead clerk: nobody serves");
	g->respawnPlayer(liq.it->center.x + 150, liq.it->center.z, 0);
	Run(*g, 61);
	Check(!liq.clerk && liq.state == "closed", "gone once you've been away a minute");
	at(liq.it->center);
	Run(*g, 0.5);
	Check(liq.clerk && liq.state == "calm" && liq.clerk.get() != ray, "and a new clerk turns up");
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

// armoured vans: a Stockade with two armed guards does its rounds; four shots in the back doors open them, the
// cash falls out, the guards come out fighting and the police come; picking up the bags robs the van
static void TestHeists(World& w) {
	printf("heists\n");
	auto g = w.game(true);
	ToStreet(w, *g);
	Player& p = *g->player;
	p.invincible = true;
	g->missionNoBust = true;
	Heists* hs = dynamic_cast<Heists*>(g->system("heists"));
	Check(hs && hs->timer == 75, "the first van after 75 s");
	if (!hs) return;
	Check(hs->spawn(), "a van spawns on a lane nearby");
	if (!hs->van) return;
	Vehicle* v = hs->van->v.get();
	int seated = 0; for (auto& q : hs->van->guards) if (q.get() && q->vehicle == v) seated++;
	std::vector<Blip> bl; hs->blipList(bl);
	printf("  %s at %.0f m, %d guards inside, blip %zu\n", v->def.id.c_str(), Hypot(v->pos.x - p.pos.x, v->pos.z - p.pos.z), seated, bl.size());
	Check(v->def.id == "stockade" && seated == 2 && bl.size() == 1, "a Stockade with two guards, on the radar");
	// slow it, then shoot the back doors
	v->vel = V3(); // (its driver keeps the wheel: the guards bail out of a van that still has one)
	const double fx = std::sin(v->yaw), fz = std::cos(v->yaw);
	const V3 back(v->pos.x - fx * (v->def.L / 2 - 0.3), v->pos.y + v->def.clearance + 0.8, v->pos.z - fz * (v->def.L / 2 - 0.3));
	g->respawnPlayer(v->pos.x - fx * 12, v->pos.z - fz * 12, v->yaw); // (behind it, close enough to fight)
	for (int i = 0; i < 4; i++) g->events.vehicleShot.emit(v, &p, back);
	Run(*g, 3);
	int out = 0; for (auto& q : hs->van->guards) if (q.get() && !q->vehicle && q->state == "attack") out++;
	printf("  state %s, %zu bags, %d guards out fighting, wanted %d, doors %.2f\n", hs->van->state.c_str(), hs->van->cash.size(), out, g->police->wantedLevel(), v->rearDoorOpen[0]);
	Check(hs->van->state == "open" && hs->van->cash.size() == 4, "four shots open the doors and the cash falls out");
	Check(out == 2 && g->police->wantedLevel() >= 2, "the guards come out fighting and the police come");
	Check(v->rearDoorOpen[0] > 0.9, "the doors swing open");
	// pick up the bags
	const double m0 = p.money;
	for (auto& c : std::vector<Heists::Cash>(hs->van->cash)) { g->respawnPlayer(c.pk->pos.x, c.pk->pos.z, 0); g->frame(1.0 / 30); g->frame(1.0 / 30); }
	g->frame(1.0 / 30);
	printf("  money +%.0f, robbed %d, message '%s' %s\n", p.money - m0, hs->robbed, g->hudModel->big.text.c_str(), g->hudModel->big.sub.c_str());
	Check(hs->robbed == 1 && p.money - m0 >= 3000 && g->hudModel->big.text == "ARMORED VAN ROBBED", "grabbing the bags robs the van");
}

// the phone: I takes it out; the arrows and Enter work the apps; a cheat code raises the wanted level, another
// makes you invincible for five minutes; Lester clears the stars for $500 a star
static void TestPhone(World& w) {
	printf("phone\n");
	auto g = w.game(true);
	ToStreet(w, *g);
	Player& p = *g->player;
	Phone* ph = g->phone;
	auto tap = [&](const char* k) { g->input.KeyDown(k); g->frame(1.0 / 30); g->input.KeyUp(k); g->frame(1.0 / 30); };
	tap("KeyI");
	Check(ph->open && ph->screen == "home" && ph->items.size() == 6 && g->phoneOpen, "I takes it out on the home screen");
	const V3 p0 = p.pos;
	tap("ArrowRight");
	Check(ph->sel == 1, "the arrows move round the apps");
	Check(Hypot(p.pos.x - p0.x, p.pos.z - p0.z) < 0.5, "the arrows don't move you");
	tap("Enter");
	Check(ph->screen == "cheats" && ph->items.size() == 15, "Enter opens Cheats");
	for (int i = 0; i < 4; i++) tap("ArrowDown");
	printf("  selected %s\n", ph->items[ph->sel].name.c_str());
	tap("Enter");
	printf("  wanted %d, phone open %d\n", g->police->wantedLevel(), ph->open ? 1 : 0);
	Check(g->police->wantedLevel() == 1 && !ph->open, "FUGITIVE raises the wanted level and puts the phone away");
	ph->cheat("PAINKILLER");
	g->frame(1.0 / 30);
	Check(g->cheatsOn.god && p.invincible, "PAINKILLER: invincible");
	ph->timed["god"] = g->time + 0.5;
	Run(*g, 1);
	Check(!g->cheatsOn.god && !p.invincible, "and it wears off");
	// Lester
	p.money = 2000;
	tap("KeyI"); tap("Enter");
	Check(ph->screen == "contacts" && ph->items.size() == 4, "Contacts");
	tap("ArrowDown"); tap("ArrowDown");
	printf("  %s: %s\n", ph->items[ph->sel].name.c_str(), ph->items[ph->sel].sub.c_str());
	tap("Enter");
	Run(*g, 2);
	printf("  after the call: money %.0f, wanted %d\n", p.money, g->police->wantedLevel());
	Check(p.money == 1500, "Lester takes $500");
	Run(*g, 5.5);
	Check(g->police->wantedLevel() == 0, "and the stars go five seconds later");
	// Benny delivers a car
	tap("KeyI"); tap("Enter"); tap("ArrowDown"); tap("Enter");
	Run(*g, 8);
	printf("  Benny: delivered %s, money %.0f\n", ph->delivered.get() ? ph->delivered->def.id.c_str() : "nothing", p.money);
	Check(ph->delivered.get() && ph->delivered->ownedByPlayer && p.money == 1300, "Benny leaves a car at the kerb for $200");
}

// bikes: a motorbike accelerates and leans into a turn, a bicycle's pedals go round, a hard hit throws the rider
static void TestBikes(World& w) {
	printf("bikes\n");
	auto g = w.game();
	ToStreet(w, *g);
	Player& p = *g->player;
	Vehicle* v = g->vehicles.spawn("razor", p.pos.x + 2, p.pos.z, 0);
	Bike* b = dynamic_cast<Bike*>(v);
	Check(b != nullptr, "a Razor is a bike");
	if (!b) return;
	g->vehicles.seatNow(&p, b, 0);
	g->input.KeyDown("KeyW");
	Run(*g, 3);
	printf("  3 s throttle: %.1f m/s\n", b->speed());
	Check(b->speed() > 10, "it accelerates");
	g->input.KeyDown("KeyA");
	Run(*g, 0.8);
	printf("  turning: lean %.2f rad\n", b->lean);
	Check(std::fabs(b->lean) > 0.1, "it leans into the turn");
	g->input.KeyUp("KeyA"); g->input.KeyUp("KeyW");
	b->throwRiders(12);
	g->frame(1.0 / 30);
	Check(!p.vehicle && p.ragdolling, "a hard hit throws the rider off");
	Run(*g, 4);
	printf("  parked lean %.2f\n", b->lean);
	Check(std::fabs(b->lean - 0.2) < 0.05, "left alone it rests on its stand");
	// a bicycle
	auto g2 = w.game();
	ToStreet(w, *g2);
	Player& p2 = *g2->player;
	Bike* bmx = dynamic_cast<Bike*>(g2->vehicles.spawn("bmx", p2.pos.x + 2, p2.pos.z, 0));
	g2->vehicles.seatNow(&p2, bmx, 0);
	g2->input.KeyDown("KeyW");
	Run(*g2, 2);
	printf("  BMX: %.1f m/s, pedals %.1f rad\n", bmx->speed(), bmx->pedalPhase);
	Check(bmx->pedalPhase > 5, "a bicycle's pedals go round");
}

static void TestAircraft(World& w) {
	printf("aircraft\n");
	// a light plane takes off from the airfield's runway
	{
		auto g = w.game();
		Player& p = *g->player;
		const Landmark& af = w.map.landmarks.at("airfield");
		g->respawnPlayer(af.x + 4, af.z - 260, 0);
		Plane* pl = dynamic_cast<Plane*>(g->vehicles.spawn("skipper", af.x, af.z - 260, 0));
		Check(pl != nullptr, "a Skipper is a plane");
		if (!pl) return;
		g->vehicles.seatNow(&p, pl, 0);
		g->input.KeyDown("KeyW");
		double t = 0;
		while (t < 30 && pl->forwardSpeed() < pl->def.vRotate + 2) { Run(*g, 0.1); t += 0.1; }
		printf("  rotate speed %.1f m/s after %.1f s (vRotate %.1f), spool %.2f\n", pl->forwardSpeed(), t, pl->def.vRotate, pl->spool);
		Check(pl->forwardSpeed() > pl->def.vRotate, "it reaches rotation speed on the runway");
		g->input.KeyDown("ArrowDown");
		Run(*g, 0.5);
		g->input.KeyUp("ArrowDown");
		printf("  after rotating: grounded %d, alt %.1f m, climb %.1f m/s\n", (int)pl->grounded, pl->altitude(), pl->vel.y);
		Check(!pl->grounded, "pulling back lifts it off");
		Run(*g, 6);
		printf("  6 s later: alt %.1f m, speed %.1f m/s, gear %.2f, health %.0f\n", pl->altitude(), pl->forwardSpeed(), pl->gearK, pl->health);
		Check(pl->altitude() > 20 && !pl->exploded, "it climbs out");
		Check(pl->gearK < 1, "the gear retracts once climbing out");
		// bank and turn
		const double yaw0 = pl->yaw;
		g->input.KeyDown("KeyA");
		Run(*g, 0.5);
		g->input.KeyUp("KeyA");
		Run(*g, 4);
		printf("  bank: heading change %.2f rad\n", WrapAngle(pl->yaw - yaw0));
		Check(std::fabs(WrapAngle(pl->yaw - yaw0)) > 0.1, "a banked wing turns it");
		// nose down into the ground
		g->input.KeyDown("ArrowUp");
		Run(*g, 1);
		g->input.KeyUp("ArrowUp");
		t = 0;
		while (t < 30 && !pl->exploded) { Run(*g, 0.1); t += 0.1; }
		printf("  dive: exploded %d after %.1f s\n", (int)pl->exploded, t);
		Check(pl->exploded, "diving into the ground wrecks it");
		Run(*g, 3);
		Check(pl->grounded && p.dead, "the wreck lies on the ground and the pilot is dead");
	}
	// a helicopter lifts off, hovers and flies forward
	{
		auto g = w.game();
		Player& p = *g->player;
		const Landmark& af = w.map.landmarks.at("airfield");
		g->respawnPlayer(af.x + 6, af.z, 0);
		Heli* h = dynamic_cast<Heli*>(g->vehicles.spawn("skylark", af.x, af.z, 0));
		Check(h != nullptr, "a Skylark is a helicopter");
		if (!h) return;
		g->vehicles.seatNow(&p, h, 0);
		Run(*g, 3);
		printf("  spool after 3 s %.2f, grounded %d\n", h->spool, (int)h->grounded);
		g->input.KeyDown("Space");
		Run(*g, 4);
		g->input.KeyUp("Space");
		printf("  after 4 s climb: alt %.1f m, vy %.1f\n", h->altitude(), h->vel.y);
		Check(h->altitude() > 5, "Space climbs");
		Run(*g, 4);
		const double a0 = h->altitude();
		Run(*g, 2);
		printf("  hands off: alt %.1f then %.1f m, vy %.2f, drift %.2f m/s\n", a0, h->altitude(), h->vel.y, Hypot(h->vel.x, h->vel.z));
		Check(std::fabs(h->altitude() - a0) < 2.5, "hands off it holds its height");
		g->input.KeyDown("KeyW");
		Run(*g, 4);
		g->input.KeyUp("KeyW");
		printf("  nose down 4 s: %.1f m/s, tilt %.2f\n", h->forwardSpeed(), h->tiltP);
		Check(h->forwardSpeed() > 10, "tilting the disc forward flies forward");
		// shot down: it spins and falls
		h->damage(99999);
		double t = 0;
		while (t < 30 && !h->exploded) { Run(*g, 0.1); t += 0.1; }
		printf("  shot down: exploded %d after %.1f s\n", (int)h->exploded, t);
		Check(h->exploded, "with no health it falls and blows up");
	}
	// the tank drives and fires its cannon
	{
		auto g = w.game();
		ToStreet(w, *g);
		Player& p = *g->player;
		Tank* tk = dynamic_cast<Tank*>(g->vehicles.spawn("mammoth", p.pos.x + 3, p.pos.z, 0));
		Check(tk != nullptr, "a Mammoth is a tank");
		if (!tk) return;
		g->vehicles.seatNow(&p, tk, 0);
		Check(p.hiddenInVehicle, "the crew is hidden inside");
		g->input.KeyDown("KeyW");
		Run(*g, 3);
		g->input.KeyUp("KeyW");
		printf("  3 s throttle: %.1f m/s, tracks %.1f %.1f\n", tk->speed(), tk->trackL, tk->trackR);
		Check(tk->speed() > 3, "it drives");
		Run(*g, 2);
		g->input.mouse.left = true;
		g->frame(1.0 / 30);
		g->input.mouse.left = false;
		const V3 m = tk->muzzleWorld();
		printf("  fired: reload %.2f, recoil %.2f, muzzle %.1f m above the hull\n", tk->reload, tk->recoil, m.y - tk->pos.y);
		Check(tk->reload > 1.5 && tk->recoil > 0.9, "the cannon fires and recoils");
		Run(*g, 2);
		tk->explode();
		Run(*g, 0.5);
		printf("  blown up: turret at %.2f m\n", tk->turretY);
		Check(tk->turretY > 1.78, "the turret is blown off its ring");
		Run(*g, 4);
		Check(tk->turretY == 1.78 && tk->turretTilt == 0.25, "it lands tilted");
	}
}

static void TestArmy(World& w) {
	printf("army\n");
	// Fort Carver: the hardware streams in, the garrison stands at its posts, trespassing puts the base on alert
	{
		auto g = w.game(true);
		Player& p = *g->player;
		p.invincible = true;
		g->missionNoBust = true;
		Military* mil = g->military;
		Check(mil != nullptr, "Fort Carver is installed");
		if (!mil) return;
		g->respawnPlayer(BASE.maxX + 60, BASE.gateZ, -kPi / 2);
		Run(*g, 2);
		int parked = 0, soldiers = 0;
		for (const auto& e : mil->fixed) if (e.veh.get()) parked++;
		for (const auto& r : mil->soldiers) if (r.get() && !r->dead) soldiers++;
		printf("  at the gate: %d parked aircraft and vehicles, %d soldiers, inside %d\n", parked, soldiers, (int)mil->inside);
		Check(parked >= 8, "the hardware is parked out");
		Check(soldiers == 15, "the garrison is at its posts");
		Check(!mil->inside && !mil->alerted, "outside the fence nothing happens");
		g->respawnPlayer(BASE.maxX - 40, BASE.gateZ, -kPi / 2);
		Run(*g, 1);
		printf("  inside: inside %d, alerted %d, stars %d\n", (int)mil->inside, (int)mil->alerted, g->policeSys->level);
		Check(mil->inside && !mil->alerted, "a warning on the way in");
		Run(*g, 8);
		int attacking = 0;
		for (const auto& r : mil->soldiers) if (r.get() && r->state == "attack") attacking++;
		printf("  8 s later: alerted %d, army hostile %d, stars %d, %d soldiers attacking\n", (int)mil->alerted, (int)g->peds->gangAggro["army"], g->policeSys->level, attacking);
		Check(mil->alerted && g->peds->gangAggro["army"] && g->policeSys->level >= 3, "staying puts the base on alert and brings three stars");
		// the hardware: take a jet and it is replaced later
		Military::Fixed* jet = nullptr;
		for (auto& e : mil->fixed) if (e.type == "raptor" && e.veh.get()) { jet = &e; break; }
		Check(jet != nullptr, "a Raptor on the flight line");
		if (jet) {
			Vehicle* v = jet->veh.get();
			v->pos.x += 40;
			Run(*g, 1);
			printf("  moved the Raptor: slot empty %d, respawn in %.0f s\n", (int)!jet->hasVeh, jet->timer);
			Check(!jet->hasVeh && jet->timer > 100, "a taken jet is respawned later");
		}
	}
	// five stars: trucks and jeeps, then the gunship, then the tank; they pull out below five
	{
		auto g = w.game(true);
		ToStreet(w, *g);
		Player& p = *g->player;
		p.invincible = true;
		g->missionNoBust = true;
		Army* army = dynamic_cast<Army*>(g->army);
		Check(army != nullptr, "the army is installed");
		if (!army) return;
		g->policeSys->setLevel(5);
		int deployed = 0;
		g->events.armyDeployed.on([&]() { deployed++; });
		std::map<std::string, int> seen;
		bool heliFired = false;
		for (int i = 0; i < 30; i++) {
			p.health = 100;
			g->policeSys->raise(5);
			Run(*g, 1);
			for (const auto& u : army->units) if (u.veh.get()) seen[u.kind]++;
			for (const auto& u : army->units) if (u.kind == "heli") if (Heli* h = dynamic_cast<Heli*>(u.veh.get())) if (h->gunT > 0 || h->missileT > 0) heliFired = true;
		}
		std::map<std::string, int> now;
		double heliD = -1, heliAlt = -1, tankD = -1;
		for (const auto& u : army->units) if (Vehicle* v = u.veh.get()) {
			now[u.kind]++;
			const double d = Hypot(v->pos.x - p.pos.x, v->pos.z - p.pos.z);
			if (u.kind == "heli") { heliD = d; heliAlt = v->altitude(); }
			if (u.kind == "tank") tankD = d;
		}
		printf("  after 30 s at five stars: active %d, deployed %d, trucks %d, jeeps %d, gunship %d (%.0f m away, %.0f m up), tank %d (%.0f m away), troops %zu\n",
			(int)army->active, deployed, now["truck"], now["jeep"], now["heli"], heliD, heliAlt, now["tank"], tankD, army->troops.size());
		Check(army->active && deployed == 1, "five stars mobilise the army");
		Check(seen["truck"] + seen["jeep"] > 0, "troop trucks and jeeps come");
		Check(seen["heli"] > 0 && seen["tank"] > 0, "then the gunship and the tank");
		Check(heliD >= 0 && heliD < 140, "the gunship circles the target");
		Check(heliFired, "the gunship opens fire");
		g->policeSys->setLevel(2);
		Run(*g, 1);
		bool leaving = true;
		for (const auto& u : army->units) if (u.veh.get() && !u.leaving) leaving = false;
		printf("  two stars: active %d, all leaving %d\n", (int)army->active, (int)leaving);
		Check(!army->active && leaving, "below five stars they pull out");
	}
}

static void TestBoats(World& w) {
	printf("boats\n");
	auto g = w.game(true);
	Player& p = *g->player;
	p.invincible = true;
	g->missionNoBust = true;
	BoatSystem* bs = dynamic_cast<BoatSystem*>(g->system("boats"));
	Check(bs != nullptr, "the boat system is installed");
	if (!bs) return;
	printf("  %zu marinas, %zu cruising routes, %zu pontoons\n", bs->marinas.size(), bs->routes.size(), bs->pontoons.size());
	Check(bs->marinas.size() >= 4 && bs->routes.size() >= 4, "marinas and routes are planned");
	// the Santa Luz marina: boats tied up along the pontoon
	g->respawnPlayer(62, 700, 0);
	Run(*g, 2);
	const BoatSystem::Marina* sl = nullptr;
	for (const auto& m : bs->marinas) if (m.name == "Santa Luz Marina") sl = &m;
	int moored = 0;
	if (sl) for (const auto& r : sl->boats) if (Boat* b = dynamic_cast<Boat*>(r.get())) if (b->moored) moored++;
	printf("  Santa Luz Marina: active %d, %d boats moored\n", sl ? (int)sl->active : -1, moored);
	Check(sl && sl->active && moored >= 4, "boats are tied up at the marina");
	// out at sea: a speedboat gets onto the plane
	Boat* b = dynamic_cast<Boat*>(g->vehicles.spawn("speedboat", -200, 900, 0));
	Check(b != nullptr, "a speedboat is a Boat");
	if (!b) return;
	g->vehicles.seatNow(&p, b, 0);
	g->input.KeyDown("KeyW");
	Run(*g, 5);
	printf("  5 s full throttle: %.1f m/s, pitch %.3f, floating %d, %.2f m below the swell line\n", b->speed(), b->pitch, (int)b->floating, b->waterLevel() - b->pos.y);
	Check(b->speed() > 20 && b->floating, "it planes across the water");
	g->input.KeyDown("KeyA");
	Run(*g, 2);
	g->input.KeyUp("KeyA");
	printf("  turning: roll %.3f, yaw %.2f\n", b->roll, b->yaw);
	Check(std::fabs(b->yaw) > 0.5, "it turns");
	g->input.KeyUp("KeyW");
	// the police come out on the water
	g->policeSys->setLevel(2);
	int police = 0; double nearest = 1e9; bool shot = false;
	for (int i = 0; i < 25; i++) {
		g->policeSys->raise(2);
		Run(*g, 1);
		police = 0;
		for (const auto& r : bs->police) if (Boat* pb = dynamic_cast<Boat*>(r.get())) { police++; nearest = Min(nearest, Hypot(pb->pos.x - b->pos.x, pb->pos.z - b->pos.z)); if (pb->gunT > 0) shot = true; }
	}
	printf("  25 s at two stars at sea: %d police boats, nearest %.0f m, gunner fired %d\n", police, nearest, (int)shot);
	Check(police >= 1 && nearest < 120, "police boats come after you");
	// holed: it burns, blows up and goes down
	g->policeSys->clear();
	g->vehicles.exit(&p);
	Run(*g, 1);
	b->explode();
	Run(*g, 14);
	printf("  exploded: sinking %d, sunk %d, %.1f m under\n", (int)b->sinking, (int)b->sunk, b->waterLevel() - b->pos.y);
	Check(b->sinking && b->sunk, "a wrecked boat sinks");
	// cruising traffic off Santa Luz
	auto g2 = w.game(true);
	BoatSystem* bs2 = dynamic_cast<BoatSystem*>(g2->system("boats"));
	g2->respawnPlayer(-100, 760, 0);
	Run(*g2, 3);
	int cruising = 0;
	for (const auto& r : bs2->routes) for (const auto& rb : r.boats) if (rb.get() && rb->driver()) cruising++;
	printf("  off the beach: %d boats cruising\n", cruising);
	Check(cruising >= 2, "boats cruise the coast");
}

// the same runs as tools/browser-test/tests/aircmp.mjs (mode: plane, heli or tank), printed the same way
static void AirCompare(World& w, const std::string& mode) {
	auto g = w.game();
	const Landmark& af = w.map.landmarks.at("airfield");
	const double ax = mode == "boat" ? -200 : af.x, az = mode == "heli" ? af.z : mode == "boat" ? 900 : af.z - 260;
	g->env.setWeather("clear", true);
	g->respawnPlayer(ax + 4, az, 0);
	Run(*g, 0.5);
	Vehicle* v = g->vehicles.spawn(mode == "plane" ? "skipper" : mode == "heli" ? "skylark" : mode == "skate" ? "skateboard" : mode == "boat" ? "speedboat" : "mammoth", ax, az, 0);
	g->vehicles.seatNow(&*g->player, v, 0);
	Plane* pl = dynamic_cast<Plane*>(v);
	Heli* hl = dynamic_cast<Heli*>(v);
	Tank* tk = dynamic_cast<Tank*>(v);
	Skateboard* sk = dynamic_cast<Skateboard*>(v);
	Boat* bt = dynamic_cast<Boat*>(v);
	auto st = [&](double t) {
		printf("%.1f x %.2f y %.2f z %.2f yaw %.3f fwd %.2f hp %.0f ex %d", t, v->pos.x, v->pos.y, v->pos.z, v->yaw, v->forwardSpeed(), v->health, v->exploded ? 1 : 0);
		if (pl) { const V3 f = pl->quat.rotate(V3(0, 0, 1)); printf(" alt %.2f pitch %.3f gr %d gear %.2f spool %.3f", pl->altitude(), std::asin(f.y), pl->grounded ? 1 : 0, pl->gearK, pl->spool); }
		if (hl) printf(" alt %.2f tilt %.3f %.3f gr %d spool %.3f", hl->altitude(), hl->tiltP, hl->tiltR, hl->grounded ? 1 : 0, hl->spool);
		if (tk) printf(" tracks %.2f %.2f body %.4f %.4f", tk->trackL, tk->trackR, tk->bodyPitch, tk->bodyRoll);
		if (bt) printf(" pitch %.4f roll %.4f float %d t %.2f", bt->pitch, bt->roll, bt->floating ? 1 : 0, g->time);
		if (sk) printf(" air %d flip %.3f push %.3f crouch %.3f lean %.3f on %d", sk->airborne ? 1 : 0, sk->flip, sk->pushK, sk->crouch, sk->lean, sk->driver() ? 1 : 0);
		printf("\n");
	};
	double t = 0;
	auto phase = [&](std::vector<const char*> keys, double secs) {
		for (const char* k : keys) g->input.KeyDown(k);
		for (int i = 0; i < secs * 2; i++) { Run(*g, 0.5); t += 0.5; st(t); }
		for (const char* k : keys) g->input.KeyUp(k);
	};
	if (pl) {
		g->input.KeyDown("KeyW");
		while (t < 30 && pl->forwardSpeed() < pl->def.vRotate + 2) { Run(*g, 0.1); t += 0.1; }
		st(t);
		phase({ "ArrowDown" }, 0.5); phase({}, 6); phase({ "KeyA" }, 0.5); phase({}, 4); phase({ "ArrowUp" }, 1); phase({}, 12);
	} else if (hl) {
		phase({}, 3); phase({ "Space" }, 4); phase({}, 6); phase({ "KeyW" }, 4); phase({}, 3); phase({ "KeyD", "KeyW" }, 2); phase({ "ShiftLeft" }, 3);
	} else if (bt) {
		phase({ "KeyW" }, 4); phase({ "KeyW", "KeyA" }, 2); phase({}, 2); phase({ "KeyS" }, 2); phase({ "KeyD", "KeyW" }, 2);
	} else if (sk) {
		phase({ "KeyW" }, 3); phase({ "Space" }, 0.5); phase({ "KeyA" }, 0.5); phase({}, 2); phase({ "KeyW", "KeyD" }, 2); phase({ "KeyS" }, 1.5);
	} else {
		phase({ "KeyW" }, 3); phase({ "KeyW", "KeyA" }, 2); phase({}, 2); phase({ "KeyS" }, 2); phase({ "KeyD" }, 2);
	}
}

static void TestSkate(World& w) {
	printf("skate\n");
	auto g = w.game();
	ToStreet(w, *g);
	Player& p = *g->player;
	Skateboard* b = dynamic_cast<Skateboard*>(g->vehicles.spawn("skateboard", p.pos.x + 1, p.pos.z, 0));
	Check(b != nullptr, "a skateboard is a Skateboard");
	if (!b) return;
	g->vehicles.seatNow(&p, b, 0);
	Check(b->layout.stand && p.vehicle == b, "the rider stands on the deck");
	g->input.KeyDown("KeyW");
	double maxPush = 0;
	for (int i = 0; i < 90; i++) { g->frame(1.0 / 30); maxPush = Max(maxPush, b->pushK); }
	g->input.KeyUp("KeyW");
	printf("  3 s pushing: %.1f m/s, push %.2f, crouch %.2f\n", b->speed(), maxPush, b->crouch);
	Check(b->speed() > 3 && maxPush > 0.5, "pushing with the back foot gets it rolling");
	// an ollie with a kickflip
	const double cash0 = p.money;
	int tricks = 0; std::string trick;
	g->events.skateTrick.on([&](const std::string& t, double, Vehicle*) { tricks++; trick = t; });
	g->input.KeyDown("Space");
	g->frame(1.0 / 30);
	g->input.KeyUp("Space");
	printf("  ollie: airborne %d, vy %.2f\n", (int)b->airborne, b->vy);
	Check(b->airborne && b->vy > 4, "Space pops an ollie");
	g->input.KeyDown("KeyA");
	g->frame(1.0 / 30);
	g->input.KeyUp("KeyA");
	printf("  flip speed %.1f rad/s (%s)\n", b->flipV, b->trickName.c_str());
	int n = 0;
	while (b->airborne && n++ < 90) g->frame(1.0 / 30);
	printf("  landed after %d frames: tricks %d (%s), cash +%.0f, still on %d\n", n, tricks, trick.c_str(), p.money - cash0, (int)(p.vehicle == b));
	Check(tricks == 1 && trick == "KICKFLIP" && p.money > cash0 && p.vehicle == b, "a clean kickflip pays");
	// a crooked landing: flip and land before it comes round
	Run(*g, 1);
	g->input.KeyDown("Space"); g->frame(1.0 / 30); g->input.KeyUp("Space");
	b->vy = 1.0; // (a short hop: the flip can't finish)
	g->input.KeyDown("KeyD"); g->frame(1.0 / 30); g->input.KeyUp("KeyD");
	n = 0;
	while (b->airborne && n++ < 90) g->frame(1.0 / 30);
	printf("  short hop with a heelflip: flipped %.2f, rider off %d\n", b->flip, (int)(p.vehicle != b));
	Check(p.vehicle != b, "land it crooked and you bail");
	// off the pavement it digs in
	auto g2 = w.game();
	Player& p2 = *g2->player;
	const Landmark& af = w.map.landmarks.at("airfield");
	const double gx = af.x + 60, gz = af.z;
	g2->respawnPlayer(gx, gz, 0);
	Skateboard* b2 = dynamic_cast<Skateboard*>(g2->vehicles.spawn("skateboard", gx + 1, gz, 0));
	g2->vehicles.seatNow(&p2, b2, 0);
	b2->vel = V3(0, 0, 6);
	Run(*g2, 1);
	printf("  on the grass by the runway: %.2f m/s after 1 s\n", b2->speedAbs());
	Check(b2->speedAbs() < 1.5, "on sand or grass it stops");
	// the skatepark: boards lying about, locals skating laps, ramps you can ride up
	auto g3 = w.game(true);
	Skateparks* sp = dynamic_cast<Skateparks*>(g3->system("skateparks"));
	Check(sp && sp->parks.size() == 1, "the skatepark system is installed");
	if (!sp) return;
	Skateparks::Park& park = sp->parks[0];
	printf("  %s: %zu concrete and %zu steel vertices\n", park.name.c_str(), park.concrete.Count(), park.metal.Count());
	g3->respawnPlayer(park.x, park.z - park.hz - 30, 0);
	Run(*g3, 2);
	int lying = 0;
	for (const auto& r : park.boards) if (r.get() && !r->driver()) lying++;
	const V3 s0 = park.skaters.empty() || !park.skaters[0].v.get() ? V3() : park.skaters[0].v->pos;
	double ollies = 0;
	for (int i = 0; i < 300; i++) { g3->frame(1.0 / 30); for (const auto& sk : park.skaters) if (sk.v.get() && sk.v->airborne) ollies += 1.0 / 30; }
	const V3 s1 = park.skaters.empty() || !park.skaters[0].v.get() ? V3() : park.skaters[0].v->pos;
	printf("  active %d, %d boards lying about, %zu locals; one rode %.0f m, %.1f s in the air between them\n", (int)park.active, lying, park.skaters.size(), Hypot(s1.x - s0.x, s1.z - s0.z), ollies);
	Check(park.active && lying == 4 && park.skaters.size() == 2, "boards lie about and two locals skate");
	Check(ollies > 0.3, "the locals ollie");
	// up the funbox ramp: x from -6.1 to -3.5 rises 0.7 m (park axes)
	const double gy = g3->collision->floorHeight(park.x - 4.0, park.z, park.y + 2);
	printf("  the funbox ramp at x -4: floor %.2f m above the pad\n", gy - park.y);
	Check(gy - park.y > 0.4, "the ramps are solid");
}

static void TestWildlife(World& w) {
	printf("wildlife\n");
	auto g = w.game(true);
	g->disableAmbient = true;
	g->cheats.god = true;
	ToStreet(w, *g);
	auto* wild = dynamic_cast<Wildlife*>(g->wildlife);
	Player& p = *g->player;
	wild->clear();
	auto make = [&](const std::string& breed, double dx, double dz) {
		auto a = std::make_shared<Animal>(*g, breed, p.pos.x + dx, p.pos.z + dz, true, p.pos.y, true, 0);
		a->state = a->sp.bird ? "peck" : "graze"; wild->list.push_back(a); return a;
	};
	auto pigeon = make("pigeon", 0, 3), deer = make("deer", 0, 10);
	wild->update(1.0 / 30);
	Check(pigeon->flying && pigeon->state == "fly", "a close player sends birds into flight");
	Check(deer->state == "flee", "a close player makes deer flee");
	const double birdY = pigeon->pos.y, deerZ = deer->pos.z;
	for (int i = 0; i < 30; i++) wild->update(1.0 / 30);
	Check(pigeon->pos.y > birdY + 1 && deer->pos.z > deerZ + 1, "birds climb and deer run away");
	auto cat = make("tabby", 0, 30);
	g->events.gunshot.emit(&p, p.pos, "pistol");
	Check(cat->state == "flee", "gunfire scares animals within 55 m");
	cat->pos.z = p.pos.z + 80; cat->state = "graze";
	g->events.explosion.emit(p.pos, 5, &p);
	Check(cat->state == "flee", "blast noise scares animals within 90 m");
	wild->clear();
	auto rabbit = make("rabbit", 0, 5);
	Combat* combat = dynamic_cast<Combat*>(g->combat);
	CombatHit hit;
	const double h = rabbit->P.hipY + rabbit->sp.bh * rabbit->scale * 0.3;
	Check(combat->raycast(p.pos.x, p.pos.y + h, p.pos.z + 1, 0, 0, 1, 8, &p, hit) && hit.kind == CombatHit::Animal_ && hit.animal == rabbit.get(), "bullets hit the animal's body sphere");
	if (hit.kind == CombatHit::Animal_) combat->applyHit(hit, *FindWeapon("sniper"), &p, V3(0, 0, 1));
	Check(rabbit->dead, "gunshots damage and kill animals");
	auto cow = make("cow", 0, 8);
	combat->explosion(cow->pos + V3(0, 0.5, 0), 3, 180, &p);
	Check(cow->dead, "explosions kill nearby animals");
	auto roadCat = make("blackcat", 0, 20);
	Vehicle* car = g->vehicles.spawn("kestrel", roadCat->pos.x, roadCat->pos.z, 0);
	car->vel.set(0, 0, 10); wild->roadkill();
	Check(roadCat->dead, "cars kill animals they hit");
	wild->clear();
	PedOpts o; o.persistent = true;
	Ped* owner = g->peds->spawnPed(p.pos.x + 4, p.pos.z + 25, o);
	wild->addWalkedDog(owner);
	Check((bool)owner->walkedDog && wild->count() == 0, "walked dogs belong to a pedestrian, outside the ambient cap");
	auto dog = wild->list.back(); owner->dead = true;
	wild->update(1.0 / 30);
	Check(dog->stray && !dog->owner && dog->state == "flee", "a dog flees after its owner dies");
	dog->pos.x += 200; wild->update(1.0 / 30);
	Check(dog->removed, "distant animals are removed");
	wild->clear();
	g->disableAmbient = false;
	Run(*g, 10);
	Check(!wild->list.empty(), "wildlife spawns around the player on valid ground");
	bool finite = true; for (const auto& a : wild->list) finite = finite && a->pos.finite();
	Check(finite, "spawned animals remain finite");
}

// Compare every breed's rig to animals.js at the same explicit speed, phase and clock.
static void TestPets(World& w) {
	printf("pets\n");
	auto g = w.game(true); g->disableAmbient = true;
	ToStreet(w, *g); Player& p = *g->player;
	PetSystem& s = *g->pets;
	int adopted = 0; g->events.petAdopted.on([&](Animal*) { adopted++; });
	Check(s.adopt("missing") == nullptr, "unknown breeds are rejected");
	Animal* a = s.adopt("lab", "Buddy");
	Check(a && a->pet && a->owner == &p && a->petName == "Buddy" && adopted == 1, "adoption creates a named pet and emits the event");
	Check(a->maxHealth == AnimalSpeciesTable().at("dog").hp * 3, "pets have triple health");
	a->pos = p.pos + V3(0, 0, -12); const V3 start = a->pos;
	Run(*g, 2);
	Check(a->pos.distanceTo(start) > 3 && a->state == "follow", "the pet catches up");
	a->pos = p.pos + V3(a->id % 2 ? 0.9 : -0.9, 0, -1.5); a->speed = 0;
	Run(*g, 3);
	Check(a->state == "sit" && a->sitK > 0.5, "it sits when the owner stops");
	g->input.KeyDown("KeyK"); g->frame(1.0 / 30); g->input.KeyUp("KeyK");
	Check(s.stay, "K tells it to stay");
	const V3 st = a->pos; p.pos.z += 10; Run(*g, 1);
	Check(a->pos.distanceTo(st) < 0.05, "stay keeps it put");
	s.command(); Check(!s.stay, "another whistle calls it back");
	a->pos = p.pos + V3(1, 0, -1);
	Vehicle* v = g->vehicles.spawn("kestrel", p.pos.x + 3, p.pos.z, 0);
	g->vehicles.seatNow(&p, v, 0);
	Check(a->inVehicle == v && v->petSeat == 1 && !v->occupants[1], "it rides in a free passenger seat");
	const auto ride = a->rootMatrix().position();
	Check(ride.distanceTo(v->seatWorld(1)) < 1, "its pose follows the car's seat");
	g->vehicles.exit(&p); Run(*g, 1);
	Check(!a->inVehicle && v->petSeat == -1, "it gets out alongside the car");
	a->pos = p.pos + V3(1, 0, -1); Vehicle* bike = g->vehicles.spawn("razor", p.pos.x + 3, p.pos.z, 0);
	g->vehicles.seatNow(&p, bike, 0);
	Check(!a->inVehicle, "pets run alongside bikes");
	g->vehicles.exit(&p); Run(*g, 0.5);
	PedOpts o; o.brain = "script"; o.persistent = true;
	Ped* enemy = g->peds->spawnPed(p.pos.x, p.pos.z + 6, o);
	DamageInfo hurt; hurt.source = enemy; hurt.type = "bullet"; p.takeDamage(1, hurt);
	a->pos = enemy->pos + V3(0, 0, -0.8); s.update(1.0 / 30);
	Check(s.target == enemy && enemy->health < enemy->maxHealth && enemy->lastAnimalDamager == a, "dogs attack whoever hurt the owner; bites keep their animal source");
	s.target = nullptr; p.lastHitTime = -99; p.aiming = true;
	enemy->recoverFrom(); enemy->setPosition(p.pos.x, p.pos.y, p.pos.z + 10); enemy->update(0);
	g->rig.camPos = p.pos + V3(0, 1.2, -3);
	g->rig.setCinematic(p.pos + V3(0, 1.2, -3), enemy->chestPos(), 50);
	g->rig.update(0, nullptr, p);
	s.command(); Check(s.target == enemy, "aiming and whistling sends the dog at the target");
	p.aiming = false; enemy->removed = true; s.update(1.0 / 30);
	Check(!s.target, "removed targets are dropped");
	a->pos.x += 1000; s.update(1.0 / 30);
	Check(a->pos.distanceTo(p.pos) < 5, "a far-behind pet catches up on valid ground");
	const auto net = s.netState(); s.poseRemote("friend", net, 1.0 / 30);
	Check(net.size() == 7 && s.remote.count("friend"), "the pet has a compact state and a remote pose");
	s.poseRemote("friend", { 0, NaN(), 0, 0, 0, 0, 0 }, 1.0 / 30);
	Check(!s.remote.count("friend"), "invalid remote states remove the stand-in");
	auto old = s.pet; s.adopt("tabby", "Cleo");
	Check(old->removed && s.info.name == "Cleo", "adopting replaces the previous pet");
	const InteriorShell* it = nullptr;
	for (const auto& room : g->map.interiors) if (room.key == "petshop") it = &room;
	g->respawnPlayer(it->door.x, it->door.z, it->YawIn()); Run(*g, 0.1);
	auto* shop = g->shops->shopAt(it->center.x, it->center.z);
	Check(shop && shop->pets.size() == 6, "Pet Palace's pens contain their dogs and cats");
	p.money = 5000; g->shops->serve(*shop);
	Check(g->hudModel->menu.kind == "store" && g->hudModel->menu.rows[0].enabled, "the pet menu offers adoption");
	g->hudModel->menuPress(0);
	Check(s.info.breed == "lab" && p.money == 3800, "buying a Labrador charges its browser price");
	s.pet->health = 1; g->hudModel->menuPress(9);
	Check(s.pet->health == s.pet->maxHealth && p.money == 3785, "pet treats heal for $15");
	g->hudModel->closeOverlay();
	s.pet->die(); Run(*g, 26);
	Check(!s.pet && s.info.breed.empty(), "a dead pet is mourned and removed after 25 seconds");
	s.adopt("husky", "Blue"); s.pet->pos = p.pos;
	Vehicle* doomed = g->vehicles.spawn("kestrel", p.pos.x + 1, p.pos.z, 0);
	s.board(doomed); doomed->exploded = true; s.update(1.0 / 30);
	Check(s.pet->dead && !s.pet->inVehicle && doomed->petSeat == -1, "a car explosion kills a riding pet and clears its seat");
	s.release(); Check(!s.pet && s.info.name.empty(), "release clears the pet");
}

static void TestMissions(World& w) {
	printf("missions\n");
	auto g = w.game(true); g->disableAmbient = true; g->cheats.god = true;
	ToStreet(w, *g); Player& p = *g->player; Missions& e = *g->missions;
	Ped* enemy = nullptr; Ped* companion = nullptr; Vehicle* car = nullptr; int ticks = 0, after = 0, passes = 0, failures = 0;
	g->events.missionPassed.on([&](const std::string&) { passes++; });
	g->events.missionFailed.on([&](const std::string&) { failures++; });
	MissionDef first; first.id = "fixture"; first.title = "Mission engine check"; first.contact = "T"; first.reward = 250; first.log = "Fixture passed.";
	const V3 origin = p.pos;
	first.start = [origin](const CityMap&) { return origin + V3(0, 0, 25); };
	first.after = [&](Game&) { after++; };
	first.run = [&](MissionContext& m, Game&) -> MissionTask {
		companion = m.ped(origin.x + 3, origin.z + 2); m.keepAlive(companion, "Companion died.");
		car = m.car("kestrel", origin.x + 6, origin.z + 5); car->locked = true; m.lockedCars.push_back(car);
		MissionEnemyOpts opt; opt.guard = true; enemy = m.enemy(origin.x + 6, origin.z + 20, opt);
		m.speakers["Guide"] = companion; m.tick([&](double) { ticks++; });
		co_await m.cutscene([&m, companion]() -> MissionTask {
			m.twoShot(&m.player(), companion);
			co_await m.say("Guide", "This checks dialogue and cleanup.", 0.1);
			co_await m.wait(0.1);
		});
		auto stop = m.timer(10);
		GoToOpts o; o.onFoot = true; o.text = "Walk to the checkpoint.";
		co_await m.goTo(origin.x, origin.z + 25, o);
		stop(); co_await m.killAll({ enemy }, "Take out the target.", "Targets");
	};
	MissionDef second; second.id = "followup"; second.title = "Unlocked follow-up"; second.contact = "F"; second.requiresIds = { first.id };
	second.start = first.start; second.run = [](MissionContext& m, Game&) -> MissionTask { co_await m.wait(1); };
	e.story = { first, second };
	e.refreshContacts(); Check(e.available().size() == 1 && e.contactMarkers.size() == 1, "contacts follow prerequisites");
	g->policeSys->setLevel(1); Check(!e.start("fixture"), "wanted players cannot start ordinary missions"); g->policeSys->clearWanted();
	const double cash = p.money; Check(e.start("fixture"), "mission starts");
	Check(g->cutscene && g->hudModel->letterboxed && !g->policeSys->enabled, "cutscenes lock controls, letterbox and suspend police");
	Check(!e.canEnterVehicle(car) && enemy->targetArrow, "locked mission cars and target arrows");
	Run(*g, 0.5);
	Check(!g->cutscene && !g->hudModel->letterboxed && g->policeSys->enabled, "dialogue and waits resume the script and restore controls");
	Check(!g->hudModel->objectiveText.empty() && g->hudModel->gpsTarget.has_value(), "checkpoint objective and GPS");
	g->respawnPlayer(origin.x, origin.z + 25, 0); Run(*g, 0.1);
	Check(g->hudModel->counterLabel == "Targets", "checkpoint advances to the target counter");
	enemy->die(DamageInfo()); Run(*g, 0.1);
	Check(!e.active && e.completed.count("fixture") && p.money == cash + 250 && passes == 1 && g->stats.missions == 1, "completing objectives pays once and records progress");
	Check(!companion->persistent && !car->locked && !enemy->targetArrow && !g->hudModel->gpsTarget && !Finite(g->hudModel->timerSeconds), "cleanup releases entities, arrows, GPS and timers");
	g->respawnPlayer(origin.x, origin.z, 0);
	Run(*g, 5.6); Check(after == 1 && e.available().size() == 1 && e.contactMarkers.size() == 1, "delayed after callback and unlocked contacts");
	e.abortActive(); if (e.active) Run(*g, 0.1);
	MissionDef timeout; timeout.id = "timeout"; timeout.title = "Timeout";
	timeout.run = [](MissionContext& m, Game&) -> MissionTask { UntilOpts o; o.timeout = 0.1; o.timeoutReason = "Too slow."; co_await m.until([]() { return false; }, o); };
	e.story.push_back(timeout); e.start("timeout"); Run(*g, 0.2);
	Check(!e.active && g->hudModel->big.style == "failed" && g->hudModel->big.sub == "Too slow." && !e.completed.count("timeout"), "timed waits propagate MissionFail without rewards");
	MissionDef cancel; cancel.id = "cancel"; cancel.title = "Cancel";
	cancel.run = [](MissionContext& m, Game&) -> MissionTask { co_await m.cutscene([&m]() -> MissionTask { co_await m.wait(100); }); };
	e.story.push_back(cancel); e.start("cancel"); e.abortActive(); Run(*g, 0.1);
	Check(!e.active && !g->cutscene && g->policeSys->enabled && failures == 1, "aborting unwinds nested cutscenes without a failure message");
	MissionDef fail; fail.id = "fail"; fail.title = "Fail";
	fail.run = [](MissionContext& m, Game&) -> MissionTask { auto* ped = m.ped(m.player().pos.x + 5, m.player().pos.z); m.keepAlive(ped, "Friend down."); co_await m.wait(100); };
	e.story.push_back(fail); Check(e.start("fail"), "failure fixture starts"); if (e.active && !e.active->peds.empty()) e.active->peds[0]->die(DamageInfo()); Run(*g, 0.1);
	Check(!e.active && g->hudModel->big.sub == "Friend down.", "keepAlive fails an active mission");
	// A route driver follows a real city street and stops at the destination.
	Traffic::Sample at; Check(Traffic::SampleLane(*g, origin.x, origin.z, 25, 100, at), "route test has a lane");
	Vehicle* v = g->vehicles.spawn("kestrel", at.x, at.z, 0); PedOpts po; po.brain = "script"; po.persistent = true;
	Ped* driver = g->peds->spawnPed(at.x, at.z, po); v->putIn(driver);
	const auto& edge = g->map.roads.edges[at.start.e]; const auto end = g->map.roads.At(edge, Clamp(at.s0 + (at.start.dir ? -50 : 50), 10, edge.len - 10));
	auto route = std::make_shared<RouteDriver>(*g, v, V3(end.x, end.y, end.z)); v->ai = route;
	for (int i = 0; i < 1200 && !route->arrived; i++) g->frame(1.0 / 30);
	Check(route->arrived && v->input.brake == 1, "mission route driver reaches its destination and brakes");
	{
		auto quit = w.game(true); quit->missions->story = { cancel }; quit->missions->start("cancel");
	}
	Check(true, "quitting during a cutscene destroys coroutines before their game systems");
}

static void TestStory(World& w) {
	printf("story: Chapters I-V\n");
	const auto definitions = BuildStory();
	Check(definitions.size() == 22, "twenty-two Chapter I-V missions in browser order");
	for (const auto& def : definitions) {
		auto g = w.game(true); g->disableAmbient = true; g->player->invincible = true;
		Missions& e = *g->missions; Player& p = *g->player;
		e.completed = std::set<std::string>(def.requiresIds.begin(), def.requiresIds.end());
		const V3 start = def.start(g->map); g->respawnPlayer(start.x, start.z, 0);
		std::string result, reason;
		g->events.missionPassed.on([&](const std::string& id) { if (id == def.id) result = "PASSED"; });
		g->events.missionFailed.on([&](const std::string& id) { if (id == def.id) { result = "FAILED"; reason = g->hudModel->big.sub; } });
		e.start(def.id);
		for (int i = 0; i < 1800 && result.empty(); i++) {
			auto ctx = e.active;
			if (!ctx) { Run(*g, 0.1); continue; }
			if (g->cutscene) { g->input.KeyDown("Space"); Run(*g, 0.1); g->input.KeyUp("Space"); Run(*g, 0.2); continue; }
			g->policeSys->clearWanted();
			if (def.id == "toolingup" && !p.weapons.count("pistol") && g->hudModel->objectiveText.find("buy a <b>pistol") != std::string::npos) {
				for (auto& shop : g->shops->shops) if (shop.it->key == "gunshop") {
					g->respawnPlayer(shop.it->door.x, shop.it->door.z, shop.it->YawIn()); Run(*g, 0.1);
					g->shops->serve(shop); g->hudModel->menuPress(2); g->hudModel->closeOverlay();
				}
			}
			// Get in the car indicated by the live blue blip, as the browser mission test does.
			for (const auto& b : ctx->blips) if (def.id != "tail" && b->color == 0x4aa3ff && b->icon == "car" && std::find(g->blips.begin(), g->blips.end(), b) != g->blips.end()) {
				for (const auto& v : ctx->cars) if (!v->isWrecked() && Hypot(v->pos.x - b->x, v->pos.z - b->z) < 1 && p.vehicle != v.get()) {
					if (p.vehicle) p.vehicle->takeOut(&p); g->vehicles.seatNow(&p, v.get());
				}
			}
			if (def.id == "tail" && ctx->cars.size() > 1) {
				const auto& target = ctx->cars[1];
				if (!p.vehicle) { auto* own = g->vehicles.spawn("meridian", target->pos.x, target->pos.z - 40, 0); g->vehicles.seatNow(&p, own); }
				p.vehicle->pos = target->pos - target->fwd() * 40; p.vehicle->vel = V3(); p.vehicle->yaw = target->yaw;
			}
			for (auto it = ctx->markers.rbegin(); it != ctx->markers.rend(); ++it) if (!(*it)->removed) {
				const auto& m = **it;
				(*it)->inside = false;
				if (m.vehicleOnly && !p.vehicle) { auto* v = g->vehicles.spawn("meridian", p.pos.x + 3, p.pos.z, 0); g->vehicles.seatNow(&p, v); }
				if (m.footOnly && p.vehicle) p.vehicle->takeOut(&p);
				if (p.vehicle) { p.vehicle->pos = m.pos; p.vehicle->vel = V3(); } else p.setPosition(m.pos.x, m.pos.y, m.pos.z);
				break;
			}
			if (g->hudModel->gpsTarget && def.id == "cleansweep") { const auto t = *g->hudModel->gpsTarget; g->respawnPlayer(t.x, t.z, 0); }
			for (const auto& ped : ctx->peds) if (ped->state == "follow" && !ped->dead && p.vehicle && !ped->vehicle) {
				for (int s : { 1, 2, 3 }) if (s < (int)p.vehicle->layout.seats.size() && !p.vehicle->occupants[s]) { p.vehicle->putIn(ped.get(), s); break; }
			}
			for (const auto& ped : ctx->peds) if (ped->missionEnemy && !ped->dead) { DamageInfo d; d.source = &p; ped->takeDamage(9999, d); }
			for (const auto& b : ctx->blips) if (b->color == 0xff3030) {
				if (auto* c = b->character.get()) if (!c->dead) { DamageInfo d; d.source = &p; c->takeDamage(9999, d); }
				if (auto* v = b->vehicle.get()) if (!v->isWrecked()) v->explode();
			}
			Run(*g, 0.5);
		}
		printf("  %s: %s, %.1f sim seconds, cash %.0f, reason '%s'\n", def.id.c_str(), result.empty() ? "TIMEOUT" : result.c_str(), g->time, p.money, reason.c_str());
		Check(result == "PASSED" && e.completed.count(def.id) && g->stats.missions == 1, "actual story script reaches its reward and cleanup");
		Check(!g->cutscene && g->policeSys->enabled, "story leaves controls and police running");
		if (def.id == "finale") {
			Check(Finite(g->hudModel->creditsT), "the finale starts the credits");
			g->hudModel->update(42); Check(!Finite(g->hudModel->creditsT), "credits end after 42 real seconds");
		}
	}
	// The no-guns rule must fail the mission, and its event listener must be detached on cleanup.
	auto g = w.game(true); g->disableAmbient = true; g->player->invincible = true;
	g->missions->completed = { "oldfriends" }; const auto& d = g->missions->story[2]; const V3 start = d.start(g->map);
	g->respawnPlayer(start.x, start.z, 0); g->missions->start("cleansweep");
	for (int i = 0; i < 200 && g->cutscene; i++) { g->input.KeyDown("Space"); Run(*g, 0.1); g->input.KeyUp("Space"); Run(*g, 0.2); }
	g->events.gunshot.emit(g->player.get(), g->player->pos, "pistol"); Run(*g, 0.1);
	Check(!g->missions->active && g->hudModel->big.sub == "Kings rules: no guns on our own block!", "Clean Sweep fails if the player fires a gun");
	g->events.gunshot.emit(g->player.get(), g->player->pos, "pistol"); Run(*g, 0.1);
	Check(!g->missions->active, "the failed mission's listener is safely removed");
	// Leaving the race car must fail and restore ambient traffic and people.
	{
		auto race = w.game(true); race->disableAmbient = true; race->player->invincible = true;
		const auto& r = race->missions->story[5]; const V3 at = r.start(race->map); race->respawnPlayer(at.x, at.z, 0); race->missions->start("race");
		for (int i = 0; i < 500; i++) {
			auto ctx = race->missions->active;
			if (!ctx) break;
			if (race->cutscene) { race->input.KeyDown("Space"); Run(*race, 0.1); race->input.KeyUp("Space"); Run(*race, 0.2); }
			else if (race->hudModel->counterLabel == "POSITION") { race->player->vehicle->takeOut(race->player.get()); Run(*race, 0.1); break; }
			else { for (const auto& b : ctx->blips) if (b->color == 0x4aa3ff && b->vehicle && !race->player->vehicle) race->vehicles.seatNow(race->player.get(), b->vehicle.get()); Run(*race, 0.1); }
		}
		Check(!race->missions->active && race->hudModel->big.sub == "You left your car." && !race->disableAmbient, "leaving the race car fails and restores the living city");
	}
	{
		auto hot = w.game(true); hot->disableAmbient = true; hot->player->invincible = true;
		const auto& d = hot->missions->story[6]; const V3 at = d.start(hot->map); hot->respawnPlayer(at.x, at.z, 0); hot->missions->start("hotwheels");
		for (int i = 0; i < 200 && hot->cutscene; i++) { hot->input.KeyDown("Space"); Run(*hot, 0.1); hot->input.KeyUp("Space"); Run(*hot, 0.2); }
		for (const auto& b : hot->missions->active->blips) if (b->vehicle && b->color == 0x4aa3ff) hot->vehicles.seatNow(hot->player.get(), b->vehicle.get());
		Run(*hot, 0.1); hot->player->vehicle->health = 449; Run(*hot, 0.1);
		Check(!hot->missions->active && hot->hudModel->big.sub.find("too damaged") != std::string::npos, "Hot Wheels refuses a damaged delivery car");
	}
	{
		auto snitch = w.game(true); snitch->disableAmbient = true; snitch->player->invincible = true;
		const auto& d = snitch->missions->story[8]; const V3 at = d.start(snitch->map); snitch->respawnPlayer(at.x, at.z, 0); snitch->missions->start("snitch");
		for (int i = 0; i < 200 && snitch->cutscene; i++) { snitch->input.KeyDown("Space"); Run(*snitch, 0.1); snitch->input.KeyUp("Space"); Run(*snitch, 0.2); }
		auto drv = std::dynamic_pointer_cast<RouteDriver>(snitch->missions->active->cars[0]->ai); drv->arrived = true; Run(*snitch, 0.1);
		Check(!snitch->missions->active && snitch->hudModel->big.sub == "Benny made it to the police station.", "The Snitch fails if Benny reaches the police station");
	}
	{
		auto tail = w.game(true); tail->disableAmbient = true; tail->player->invincible = true;
		const auto& d = tail->missions->story[12]; const V3 at = d.start(tail->map); tail->respawnPlayer(at.x, at.z, 0); tail->missions->start("tail");
		for (int i = 0; i < 200 && tail->cutscene; i++) { tail->input.KeyDown("Space"); Run(*tail, 0.1); tail->input.KeyUp("Space"); Run(*tail, 0.2); }
		const auto target = tail->missions->active->cars[1]; auto* own = tail->vehicles.spawn("meridian", target->pos.x, target->pos.z - 40, 0); tail->vehicles.seatNow(tail->player.get(), own);
		own->pos = target->pos - target->fwd() * 40; Run(*tail, 0.1);
		tail->events.gunshot.emit(tail->player.get(), tail->player->pos, "pistol"); Run(*tail, 0.1);
		Check(!tail->missions->active && tail->hudModel->big.sub == "Deacon noticed you.", "the stealth tail fails on nearby player gunfire");
	}
}

static void StoryMeta(World& w) {
	for (const auto& d : BuildStory()) {
		const V3 s = d.start(w.map); std::string req;
		for (const auto& r : d.requiresIds) { if (!req.empty()) req += ","; req += r; }
		printf("story|%s|%s|%s|%.0f|%s|%.2f,%.2f|%s\n", d.id.c_str(), d.title.c_str(), d.contact.c_str(), d.reward, req.c_str(), s.x, s.z, d.log.c_str());
	}
}

static void AnimalCompare(World& w) {
	auto g = w.game();
	for (const auto& breed : AnimalBreedOrder()) {
		auto a = std::make_shared<Animal>(*g, breed, 0, 0, true, 0, true, 0);
		a->id = 7; a->phase = 0.37; a->t = 2.1;
		for (int i = 0; i < 90; i++) {
			a->speed = i < 30 ? a->sp.walk : i < 60 ? (a->sp.bird ? a->sp.fly : a->sp.run) : 0;
			a->state = i >= 60 ? "sit" : "wander"; a->flying = a->sp.bird && i < 60;
			a->t += 1.0 / 30; a->animate(1.0 / 30);
			if (i % 30 == 29) printf("rig %s %d %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n", breed.c_str(), i, a->bodyY, a->bodyRotX, a->legRotX[0], a->legRotX[2], a->headRotX, a->tailRotY, a->wingRotZ[0]);
		}
	}
}

int main(int argc, char** argv) {
	InstallCrashTrace();
	setvbuf(stdout, nullptr, _IONBF, 0);
	SeedRand(12345);
	const auto t0 = std::chrono::steady_clock::now();
	World w;
	printf("world %.1f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
	if (argc > 1 && !std::strcmp(argv[1], "vehcompare")) { VehCompare(w); return 0; }
	if (argc > 1 && !std::strcmp(argv[1], "aircmp")) { AirCompare(w, argc > 2 ? argv[2] : "plane"); return 0; }
	if (argc > 1 && !std::strcmp(argv[1], "animalcmp")) { AnimalCompare(w); return 0; }
	if (argc > 1 && !std::strcmp(argv[1], "storymeta")) { StoryMeta(w); return 0; }
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
	if (want("heists")) TestHeists(w);
	if (want("phone")) TestPhone(w);
	if (want("bikes")) TestBikes(w);
	if (want("aircraft")) TestAircraft(w);
	if (want("army")) TestArmy(w);
	if (want("skate")) TestSkate(w);
	if (want("boats")) TestBoats(w);
	if (want("shops")) TestShops(w);
	if (want("wildlife")) TestWildlife(w);
	if (want("pets")) TestPets(w);
	if (want("missions")) TestMissions(w);
	if (want("story")) TestStory(w);
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
