// Native tests for the simulation (no Unreal): Tools/native.sh simtest.exe simtest.cpp && ./simtest.exe [test ...]
// Each test sets up a game on the generated world, runs fixed 1/30 s frames and prints what it measured.
#include "Sim/Game.h"
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
	std::unique_ptr<Game> game() {
		WorldData w; w.map = &map; w.roadPrims = roads.prims; w.roadDecks = roads.decks; w.props = props; w.propDefs = &defs;
		auto g = std::make_unique<Game>(w);
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

int main(int argc, char** argv) {
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
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
