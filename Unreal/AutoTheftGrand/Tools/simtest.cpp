// Native tests for the simulation (no Unreal): Tools/native.sh simtest.exe simtest.cpp && ./simtest.exe [test ...]
// Each test sets up a game on the generated world, runs fixed 1/30 s frames and prints what it measured.
#include "Sim/Game.h"
#include "Sim/Gameplay.h"
#include "Sim/Hud.h"
#include "Sim/Peds.h"
#include "Sim/Rail.h"
#include "Sim/Setup.h"
#include "Sim/Traffic.h"
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
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
