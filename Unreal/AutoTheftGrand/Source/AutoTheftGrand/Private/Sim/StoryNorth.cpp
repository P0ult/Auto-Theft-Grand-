// Chapters VII and VIII: San Aurelio (src/game/story_north.js). Voss's money went north. It's being washed
// through San Aurelio by Vincent Castell's Harbor Saints: the port, the arena, the logging trucks out of
// Timberline. His daughter Nina wants him gone.
#include "Story.h"
#include "Boats.h"
#include "Collision.h"
#include "Game.h"
#include "Hud.h"
#include "Phone.h"
#include "Police.h"

namespace atg {
namespace {

// story_north.js NCAST
Appearance NorthLook(const std::string& key) {
	Appearance a;
	if (key == "nina") { a.female = true; a.skin = 0xe0ac87; a.hair = 0x3a1f12; a.hairStyle = "long"; a.shirt = 0x151515; a.shirtType = "jacket"; a.jacketColor = 0x7a1f2b; a.pants = 0x1a1a1a; a.shoes = 0x111111; a.build = 0.94; a.height = 0.98; }
	else if (key == "kiko") { a.skin = 0xa86b4a; a.hair = 0x1a1a1a; a.hairStyle = "cap"; a.hat = 0x2a9d8f; a.shirt = 0xf4a261; a.shirtType = "tee"; a.pants = 0x264653; a.shorts = true; a.shoes = 0xffffff; a.build = 0.88; a.height = 0.94; a.jacketColor = 0x111111; }
	else if (key == "castell") { a.skin = 0xf1c7a5; a.hair = 0xb0b0b0; a.hairStyle = "short"; a.shirt = 0xffffff; a.shirtType = "jacket"; a.jacketColor = 0x1d3557; a.pants = 0x1d3557; a.shoes = 0x111111; a.build = 1.1; a.height = 1.02; a.glasses = true; a.beard = true; }
	else if (key == "padre") { a.skin = 0xc68863; a.hair = 0x777777; a.hairStyle = "short"; a.shirt = 0x111111; a.shirtType = "long"; a.pants = 0x111111; a.shoes = 0x111111; a.glasses = true; a.jacketColor = 0x111111; }
	return a;
}
MissionPedOpts NorthCast(const std::string& key, bool invincible = false) { MissionPedOpts o; o.hasAppearance = true; o.appearance = NorthLook(key); o.invincible = invincible; return o; }
MissionPedOpts NorthCastAt(const std::string& key, double y, bool invincible = false) { MissionPedOpts o = NorthCast(key, invincible); o.hasY = true; o.y = y; return o; }
Appearance NorthSaintLook() { RNG rng((uint32_t)(int64_t)std::floor(Rand() * 1e9)); return SaintLook(rng); }

// freeroam.roadSpot, or the point itself
RoadSpot NorthSpot(Game& g, double x, double z, bool foot = false) {
	RoadSpot s;
	if (FindRoadSpot(g, x, z, foot, s)) return s;
	return { x, z, g.map.GroundHeight(x, z), 0 };
}
// the nearest open water at least `depth` deep, spiralling out
V3 WaterSpot(Game& g, double x, double z, double depth = 2.5) {
	const CityMap& map = g.map;
	auto deep = [&](double px, double pz) { return map.WaterLevel(px, pz) - map.TerrainHeight(px, pz) > depth && g.collision->floorHeight(px, pz, 2) < WATER_Y + 0.3; };
	if (deep(x, z)) return V3(x, 0, z);
	for (double r = 6; r < 500; r += 6) for (int a = 0; a < 20; a++) {
		const double px = x + std::cos(a / 20.0 * 6.283) * r, pz = z + std::sin(a / 20.0 * 6.283) * r;
		if (deep(px, pz)) return V3(px, 0, pz);
	}
	return V3(x, 0, z);
}
struct SaintOpts { bool guard = true; double face = NaN(); double health = 110; double accuracy = 0.4; };
std::vector<Ped*> Saints(MissionContext& m, const std::vector<V3>& pts, const std::vector<std::string>& weapons, const SaintOpts& so = {}) {
	std::vector<Ped*> out;
	for (size_t i = 0; i < pts.size(); i++) {
		MissionEnemyOpts o; o.gang = "saints"; o.weapon = weapons[i % weapons.size()]; o.guard = so.guard; o.face = so.face; o.health = so.health;
		o.hasAppearance = true; o.appearance = NorthSaintLook(); o.accuracy = so.accuracy;
		out.push_back(m.enemy(pts[i].x, pts[i].z, o));
	}
	return out;
}
std::vector<V3> Around(double cx, double cz, int n, double r0, double r1, double ph = 0) {
	std::vector<V3> out;
	for (int i = 0; i < n; i++) { const double a = ph + (double)i / n * kPi * 2 + Rand(-0.2, 0.2), r = Rand(r0, r1); out.push_back(V3(cx + std::cos(a) * r, 0, cz + std::sin(a) * r)); }
	return out;
}
// a boat with a Saints driver, running a route. (The browser game also seats a gunner when the boat's
// definition lists seats, which no boat's does: there is never a gunner.)
struct SaintBoat { Vehicle* b; Ped* drv; };
SaintBoat MakeSaintBoat(MissionContext& m, const std::string& type, double x, double z, const std::vector<std::array<double, 2>>& route, double pace, const Appearance* drvLook = nullptr) {
	Vehicle* b = m.car(type, x, z, std::atan2(route[0][0] - x, route[0][1] - z));
	MissionPedOpts o; o.hasAppearance = true; o.appearance = drvLook ? *drvLook : NorthSaintLook(); o.brain = "script";
	Ped* drv = m.ped(x, z, o);
	b->putIn(drv, 0);
	b->ai = std::make_shared<BoatDriver>(m.game, b, &route, 0, pace);
	return { b, drv };
}
// a kerbside lane point near (x, z) whose traffic runs towards (tx, tz)
RoadSpot LaneTowards(Game& g, double x, double z, double tx, double tz) {
	const RoadNet& net = g.map.roads;
	const EdgeHit c = net.Closest(x, z, [](const REdge& e) { return !e.removed && e.type != ERoad::Rail && e.type != ERoad::Dirt; });
	if (!c.valid()) return NorthSpot(g, x, z);
	const REdge& e = net.edges[c.e];
	const EdgePoint t = net.At(e, Clamp(c.s, 8, e.len - 8));
	int dir = (tx - t.x) * t.tx + (tz - t.z) * t.tz >= 0 ? 0 : 1;
	if ((dir == 0 ? e.lanesF : e.lanesB) < 1) dir = 1 - dir;
	const double fx = dir ? -t.tx : t.tx, fz = dir ? -t.tz : t.tz;
	const double off = net.LaneOffset(e, dir, 0);
	return { t.x - fz * off, t.z + fx * off, t.y, std::atan2(fx, fz) };
}
V3 Plaza(const CityMap& map) { auto it = map.landmarks.find("aurelio"); return it != map.landmarks.end() ? V3(it->second.x, 0, it->second.z) : V3(NCITY.x, 0, NCITY.z); }
V3 LandmarkOr(const CityMap& map, const char* key, const V3& fallback) { auto it = map.landmarks.find(key); return it != map.landmarks.end() ? V3(it->second.x, 0, it->second.z) : fallback; }
struct Pontoon { double x0, x1, z; };
// the Aurelio Marina's pontoon (boats.js L.aurMarina.pontoon), or the browser's fallback beside the marina
Pontoon MarinaPontoon(const Landmark& mr) {
	auto a = mr.pts.find("pontoon0"), b = mr.pts.find("pontoon1");
	if (a != mr.pts.end() && b != mr.pts.end()) return { a->second.x, b->second.x, a->second.z };
	return { mr.x + 10, mr.x + 80, mr.z };
}
SpawnOpts Paint(uint32_t c) { SpawnOpts o; o.hasColor = true; o.color = c; return o; }
} // namespace

void AddChapterSeven(std::vector<MissionDef>& story) {
	// ================================================================ CHAPTER VII — SAN AURELIO
	MissionDef ns; ns.id = "northstar"; ns.title = "North Star"; ns.contact = "M"; ns.requiresIds = { "secosunrise" }; ns.reward = 6000;
	ns.log = "Drove Marisol up the coast to San Aurelio, shook off a Harbor Saints ambush at Gull Bay and met Nina Castell on the Plaza.";
	ns.start = [](const CityMap& map) { const auto& h = map.landmarks.at("home"); return V3(h.x - 3, 0, h.z + 2); };
	ns.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 s = StoryLandmark(game, "home", -3, 2);
		Ped* mari = m.ped(s.x - 1.6, s.z - 1, StoryCast("marisol", true));
		m.speakers = { { "Marisol", mari }, { "Dre", &m.player() } };
		co_await m.cutscene([&m, mari]() -> MissionTask {
			m.face(mari, &m.player()); m.face(&m.player(), mari);
			m.twoShot(&m.player(), mari, 1, 3.4);
			co_await m.lines({
				{ "Marisol", "Benny's ledger, page forty. Voss's money didn't stay in Puerto Seco. It went north. San Aurelio." },
				{ "Dre", "Never been. Big place?" },
				{ "Marisol", "Big enough. The port's run by a man called Vincent Castell and his Harbor Saints. They wash dirty money for half the coast." },
				{ "Marisol", "His daughter reached out. Nina. She wants to talk. Drive me up \xE2\x80\x94 Bayshore Road, then the Aurelio Highway." },
			});
		});
		const V3 home = StoryLandmark(game, "home");
		const RoadSpot r = NorthSpot(game, home.x + 10, home.z + 24);
		Vehicle* car = m.car("kestrel", r.x, r.z, r.yaw, Paint(0x20242c));
		co_await m.getIn(car, "Get in the <span class=\"b\">car</span>.");
		m.keepAlive(car, "The car was destroyed.");
		m.follower(mari, 0);
		UntilOpts board; board.timeout = 12; board.resolveTimeout = true;
		co_await m.until([mari, car]() { return mari->vehicle == car; }, board);
		if (mari->vehicle != car) car->putIn(mari, 1);
		m.failIf([&m, &game, car, mari]() { return m.player().vehicle != car && !game.vehicles.isBusy(&m.player()) && mari->vehicle == car && m.distTo(car) > 40; }, "You left Marisol behind.");
		m.help("It's a long drive. Follow the GPS line up <b>Bayshore Road</b> and onto the <b>Aurelio Highway</b>.", 7);
		const Town& G = TownByKey("gull");
		const RoadSpot g1 = NorthSpot(game, G.x, G.z + 260);
		GoToOpts go; go.vehicle = true; go.radius = 9; go.inCar = car; go.text = "Drive to <span class=\"y\">Gull Bay</span>.";
		co_await m.goTo(g1.x, g1.z, go);
		co_await m.say("Marisol", "Two cars coming up fast behind us. Navy jackets \xE2\x80\x94 that's the Saints. Somebody talked.", 3.5);
		const RoadSpot back = NorthSpot(game, G.x - 10, G.z + 420);
		std::vector<ChaseCar> chasers = { StoryChaseCar(m, "brawler", back.x, back.z, back.yaw + kPi, "saints", 1, NorthSaintLook) };
		const RoadSpot back2 = NorthSpot(game, G.x + 6, G.z + 520);
		chasers.push_back(StoryChaseCar(m, "kestrel", back2.x, back2.z, back2.yaw + kPi, "saints", 1, NorthSaintLook));
		m.objective("Shake off or wreck the <span class=\"r\">Saints</span>.");
		co_await m.until([&m, chasers]() { for (const auto& c : chasers) if (!(c.v->isWrecked() || c.driver->dead || m.distTo(c.v) > 280)) return false; return true; });
		co_await m.say("Marisol", "Welcome to San Aurelio. Nina said the Plaza \xE2\x80\x94 the fountain in the middle of town.", 3.5);
		const V3 P = Plaza(game.map);
		const RoadSpot pz = NorthSpot(game, P.x + 20, P.z + 60);
		GoToOpts go2; go2.vehicle = true; go2.radius = 8; go2.slow = true; go2.inCar = car; go2.text = "Meet Nina at the <span class=\"y\">Plaza de Aurelio</span>.";
		co_await m.goTo(pz.x, pz.z, go2);
		car->input.brake = 1;
		const RoadSpot w = NorthSpot(game, pz.x, pz.z, true);
		Ped* nina = m.ped(w.x, w.z, NorthCastAt("nina", w.y, true));
		m.speakers = { { "Nina", nina }, { "Marisol", mari }, { "Dre", &m.player() } };
		co_await m.wait(0.8);
		game.vehicles.exit(&m.player());
		UntilOpts out; out.timeout = 5; out.resolveTimeout = true;
		co_await m.until([&m, &game]() { return !m.player().vehicle && !game.vehicles.isBusy(&m.player()); }, out);
		co_await m.cutscene([&m, nina]() -> MissionTask {
			m.face(nina, &m.player()); m.face(&m.player(), nina);
			m.twoShot(&m.player(), nina, -1, 3.5);
			co_await m.lines({
				{ "Nina", "You're the one who took down Voss. You look shorter on the news." },
				{ "Dre", "You're Castell's kid. Why would you help us?" },
				{ "Nina", "Because my father's going to get me killed. The Saints bring guns in by boat, cash out through the arena, product down from Timberline on the log trucks." },
				{ "Nina", "Pull it apart one piece at a time and he'll have nothing left. Start with Kiko \xE2\x80\x94 my runner. You'll find him at Bayview Park." },
			});
		});
	};
	story.push_back(std::move(ns));

	MissionDef bm; bm.id = "boardmeeting"; bm.title = "Board Meeting"; bm.contact = "K"; bm.requiresIds = { "northstar" }; bm.reward = 4500;
	bm.log = "Ran Nina's messages across San Aurelio on Kiko's skateboard against the clock, then saved Kiko from a Saints beating at Bayview Park.";
	bm.start = [](const CityMap& map) { const V3 p = LandmarkOr(map, "aurPark", Plaza(map)); return V3(p.x + 4, 0, p.z + 4); };
	bm.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 P = LandmarkOr(game.map, "aurPark", Plaza(game.map));
		Ped* kiko = m.ped(P.x + 6, P.z + 3, NorthCast("kiko"));
		kiko->health = kiko->maxHealth = 400;
		m.speakers = { { "Kiko", kiko }, { "Dre", &m.player() } };
		co_await m.cutscene([&m, kiko]() -> MissionTask {
			m.face(kiko, &m.player()); m.face(&m.player(), kiko);
			m.twoShot(&m.player(), kiko, 1, 3.2);
			co_await m.lines({
				{ "Kiko", "You're Nina's guy? Cool. The Saints listen to every phone in town, so we do it old school. On wheels." },
				{ "Kiko", "Four drops. Cathedral, the Plaza, the harbour, Northgate. Clock's tight \xE2\x80\x94 but the crew pays extra for style." },
				{ "Kiko", "Ollie with Space, flip with A or D, shove-it with S. Land clean and I'll give you time back." },
			});
		});
		const RoadSpot sp = NorthSpot(game, P.x, P.z, true);
		Vehicle* board = m.car("skateboard", sp.x, sp.z, sp.yaw);
		co_await m.getIn(board, "Get on the <span class=\"b\">skateboard</span>.");
		m.failIf([board]() { return board->removed; }, "You lost the board.");
		// a stretchable clock: each clean trick buys a few seconds
		double end = m.t + 80;
		auto stopClock = m.tick([&m, &game, &end](double) { game.hud->setTimer(end - m.t); });
		m.failIf([&m, &end]() { return m.t > end; }, "Too slow. The message didn't get through.");
		std::weak_ptr<MissionContext> weak = m.shared_from_this();
		const int trickId = game.events.skateTrick.on([weak, &game, &end](const std::string& name, double, Vehicle*) {
			auto ctx = weak.lock();
			if (!ctx || game.missions->active != ctx) return;
			end += 4; game.hud->help("<b>" + name + "</b> \xE2\x80\x94 +4 seconds", 1.4);
		});
		m.onCleanup([&game, trickId]() { game.events.skateTrick.off(trickId); });
		struct Drop { std::string name; V3 p; };
		const std::vector<Drop> drops = {
			{ "the <span class=\"y\">Cathedral</span>", LandmarkOr(game.map, "aurCathedral", V3(NCITY.x - 220, 0, NCITY.z)) },
			{ "the <span class=\"y\">Plaza</span>", V3(NCITY.x + 30, 0, NCITY.z + 38) },
			{ "the <span class=\"y\">harbour</span>", LandmarkOr(game.map, "aurHarbor", V3(NCITY.x + 540, 0, NCITY.z)) },
			{ "<span class=\"y\">Northgate</span>", V3(NCITY.x + 40, 0, NCITY.z - 330) },
		};
		for (size_t i = 0; i < drops.size(); i++) {
			const RoadSpot d = NorthSpot(game, drops[i].p.x, drops[i].p.z, true);
			GoToOpts go; go.vehicle = true; go.radius = 3.5;
			go.text = "Skate to " + drops[i].name + " (" + std::to_string(i + 1) + "/" + std::to_string(drops.size()) + ").";
			go.condition = [&m, board]() { return m.player().vehicle == board || !m.player().vehicle; };
			co_await m.goTo(d.x, d.z, go);
			end += 45;
			m.cash(150);
		}
		stopClock(); game.hud->setTimer(NaN());
		game.hud->subtitle("Dre! They followed me to the park \xE2\x80\x94 help!", "Kiko", 3.5);
		const V3 kp(P.x + Rand(-8, 8), 0, P.z + Rand(-8, 8));
		kiko->setPosition(kp.x, kp.z);
		kiko->setState("cower"); kiko->cowerTime = 999;
		m.keepAlive(kiko, "Kiko is dead.");
		m.blipEntity(kiko, 0x4a90ff, "person");
		SaintOpts so; so.guard = false;
		const auto thugs = Saints(m, Around(kp.x, kp.z, 5, 4, 9), { "bat", "knife", "pistol", "bat", "pistol" }, so);
		for (Ped* t : thugs) t->threat = &m.player();
		m.player().giveWeapon("pistol", 60);
		co_await m.killAll(thugs, "Save <span class=\"b\">Kiko</span> from the <span class=\"r\">Saints</span>.");
		kiko->setState("idle");
		co_await m.say("Kiko", "Man, you can ride. Tell Nina the drops are done. I owe you one.", 3.5);
	};
	story.push_back(std::move(bm));

	MissionDef lt; lt.id = "lowtide"; lt.title = "Low Tide"; lt.contact = "N"; lt.requiresIds = { "northstar" }; lt.reward = 7000;
	lt.log = "Took a speedboat out from Aurelio Marina at dusk, ran down the Saints' smuggling boat off the coast, fished out the package and lost the coast guard.";
	lt.start = [](const CityMap& map) { const V3 mr = LandmarkOr(map, "aurMarina", Plaza(map)); return V3(mr.x - 4, 0, mr.z + 3); };
	lt.run = [](MissionContext& m, Game& game) -> MissionTask {
		auto it = game.map.landmarks.find("aurMarina");
		if (it == game.map.landmarks.end()) throw MissionFail("The marina is closed today.");
		const Landmark mr = it->second;
		game.env.setTime(19.4);
		Ped* nina = m.ped(mr.x - 2, mr.z + 4, NorthCast("nina", true));
		m.speakers = { { "Nina", nina }, { "Dre", &m.player() } };
		co_await m.cutscene([&m, nina]() -> MissionTask {
			m.face(nina, &m.player()); m.face(&m.player(), nina);
			m.twoShot(&m.player(), nina, 1, 3.4);
			co_await m.lines({
				{ "Nina", "Every Thursday a boat comes down the coast from the north with a package for my father. Tonight it doesn't arrive." },
				{ "Nina", "Take the speedboat on the end of the pontoon. Catch it before it gets to the dock at Gull Bay." },
				{ "Dre", "And the coast guard?" },
				{ "Nina", "Oh, they'll come. They always come. Don't let them catch you with it." },
			});
		});
		const Pontoon pt = MarinaPontoon(mr);
		const V3 bs = WaterSpot(game, pt.x1 - 20, pt.z - 9, 1.6);
		Vehicle* boat = m.car("speedboat", bs.x, bs.z, kPi / 2, Paint(0xf4f4f0));
		co_await m.getIn(boat, "Take the <span class=\"b\">speedboat</span> at the end of the pontoon.");
		m.keepAlive(boat, "Your boat sank.");
		// the smugglers: out of the north-east, hugging the coast down towards Gull Bay
		const std::vector<std::array<double, 2>> route = { { 1500, -4300 }, { 1470, -3900 }, { 1440, -3500 }, { 1390, -3100 }, { 1330, -2880 }, { 1300, -2820 } };
		const V3 start = WaterSpot(game, 1540, -4700, 4);
		const SaintBoat sm = MakeSaintBoat(m, "cruiser", start.x, start.z, route, 0.72);
		sm.b->health = sm.b->maxHealth = 900;
		m.blipEntity(sm.b, 0xff3030, "boat");
		m.failIf([sm]() { return !sm.drv->dead && Hypot(sm.b->pos.x - 1300, sm.b->pos.z + 2820) < 70; }, "The package reached the Saints.");
		m.objective("Intercept the <span class=\"r\">Saints' boat</span> before it reaches Gull Bay. Shoot the driver or sink it.");
		co_await m.until([sm]() { return sm.drv->dead || sm.b->isWrecked() || sm.b->exploded; });
		sm.b->ai.reset();
		const V3 pk(sm.b->pos.x + Rand(-4, 4), 0, sm.b->pos.z + Rand(-4, 4));
		GoToOpts grab; grab.vehicle = true; grab.radius = 7; grab.y = WATER_Y + 0.1; grab.color = 0x6cff6c; grab.text = "Grab the floating <span class=\"g\">package</span>.";
		co_await m.goTo(pk.x, pk.z, grab);
		m.cash(1500);
		m.wanted(2);
		game.hud->subtitle("Coast guard! Cut your lights and run!", "Nina", 3);
		co_await m.loseWanted("Lose the <span class=\"r\">coast guard</span>.");
		const V3 home = WaterSpot(game, (pt.x0 + pt.x1) / 2, pt.z + 10, 1.4);
		GoToOpts back; back.vehicle = true; back.radius = 9; back.y = WATER_Y + 0.1; back.text = "Bring the package back to <span class=\"y\">Aurelio Marina</span>.";
		co_await m.goTo(home.x, home.z, back);
		co_await m.say("Nina", "That's his whole week's shipment in the bottom of the bay. He'll feel that.", 3.5);
	};
	story.push_back(std::move(lt));

	MissionDef sa; sa.id = "sanctuary"; sa.title = "Sanctuary"; sa.contact = "N"; sa.requiresIds = { "boardmeeting", "lowtide" }; sa.reward = 8000;
	sa.log = "Held off three waves of Saints at the Cathedral of San Aurelio and drove Padre Ignacio and his ledger to safety in Timberline.";
	sa.start = [](const CityMap& map) { const V3 c = LandmarkOr(map, "aurCathedral", Plaza(map)); return V3(c.x + 6, 0, c.z + 6); };
	sa.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 C = LandmarkOr(game.map, "aurCathedral", Plaza(game.map));
		const RoadSpot sp = NorthSpot(game, C.x, C.z, true);
		Ped* padre = m.ped(sp.x, sp.z, NorthCastAt("padre", sp.y));
		padre->health = padre->maxHealth = 600;
		Ped* nina = m.ped(sp.x + 1.5, sp.z + 1, NorthCastAt("nina", sp.y, true));
		m.speakers = { { "Padre", padre }, { "Nina", nina }, { "Dre", &m.player() } };
		co_await m.cutscene([&m, padre, nina]() -> MissionTask {
			m.face(padre, &m.player()); m.face(&m.player(), padre); m.face(nina, &m.player());
			m.twoShot(&m.player(), padre, 1, 3.6);
			co_await m.lines({
				{ "Padre", "Your father's men made their confessions here for twenty years, Nina. I wrote down the ones that mattered." },
				{ "Nina", "He knows, Padre. They're on their way. Dre \xE2\x80\x94 keep him alive, then get him out of the city." },
				{ "Padre", "I haven't held a gun since the seminary. I trust you have." },
			});
		});
		nina->setPosition(nina->pos.x, nina->pos.z - 300);
		m.keepAlive(padre, "Padre Ignacio is dead.");
		m.blipEntity(padre, 0x4a90ff, "person");
		padre->setState("cower"); padre->cowerTime = 999;
		m.player().giveWeapon("rifle", 180); m.player().giveWeapon("shotgun", 30); m.player().switchTo("rifle");
		SaintOpts so; so.guard = false;
		game.hud->bigMessage("WAVE 1", "hint", 2);
		co_await m.killAll(Saints(m, Around(sp.x, sp.z, 5, 38, 55), { "pistol", "smg", "pistol", "shotgun", "smg" }, so), "Defend the <span class=\"b\">Padre</span> from the <span class=\"r\">Saints</span>.", "SAINTS");
		co_await m.wait(2);
		std::vector<Ped*> wave2;
		for (int k : { 0, 1 }) {
			const RoadSpot cs = NorthSpot(game, sp.x + (k ? 120 : -120), sp.z + (k ? -60 : 70));
			const ChaseCar c = StoryChaseCar(m, "brawler", cs.x, cs.z, cs.yaw, "saints", 1, NorthSaintLook);
			wave2.push_back(c.driver); wave2.insert(wave2.end(), c.guns.begin(), c.guns.end());
		}
		const auto foot = Saints(m, Around(sp.x, sp.z, 3, 40, 50, 1), { "rifle", "smg", "shotgun" }, so);
		wave2.insert(wave2.end(), foot.begin(), foot.end());
		game.hud->bigMessage("WAVE 2", "hint", 2);
		co_await m.killAll(wave2, "They brought cars. Take them out.", "SAINTS");
		co_await m.wait(2);
		SaintOpts tough = so; tough.health = 130;
		game.hud->bigMessage("WAVE 3", "hint", 2);
		co_await m.killAll(Saints(m, Around(sp.x, sp.z, 6, 40, 60, 2), { "rifle", "smg", "shotgun", "rifle", "rpg", "smg" }, tough), "Last push. Hold them off!", "SAINTS");
		padre->setState("idle");
		co_await m.say("Padre", "God forgive me, that was exhilarating. Now \xE2\x80\x94 the car. Timberline, there's a lodge up there.", 3.5);
		const RoadSpot cr = NorthSpot(game, sp.x + 20, sp.z + 20);
		Vehicle* car = m.car("summit", cr.x, cr.z, cr.yaw, Paint(0x3a4a3a));
		co_await m.getIn(car, "Get in the <span class=\"b\">car</span>.");
		m.keepAlive(car, "The car was destroyed.");
		m.follower(padre, 0);
		UntilOpts board; board.timeout = 14; board.resolveTimeout = true;
		co_await m.until([padre, car]() { return padre->vehicle == car; }, board);
		if (padre->vehicle != car) car->putIn(padre, 1);
		m.failIf([&m, &game, car, padre]() { return m.player().vehicle != car && !game.vehicles.isBusy(&m.player()) && padre->vehicle == car && m.distTo(car) > 40; }, "You left the Padre behind.");
		bool chased = false;
		m.tick([&m, &game, car, &chased](double) {
			if (chased) return;
			const V3 p = car->pos;
			if (Hypot(p.x - NCITY.x, p.z - NCITY.z) < 560) return;
			chased = true;
			const RoadSpot b = NorthSpot(game, p.x + 150, p.z + 40);
			StoryChaseCar(m, "kestrel", b.x, b.z, b.yaw, "saints", 1, NorthSaintLook);
			game.hud->subtitle("Behind us! They're not giving up!", "Padre", 3);
		});
		const Town& T = TownByKey("timber");
		const RoadSpot lodge = NorthSpot(game, T.x + 30, T.z + 20);
		GoToOpts go; go.vehicle = true; go.radius = 8; go.slow = true; go.inCar = car; go.text = "Drive the Padre up the Timber Road to <span class=\"y\">Timberline</span>.";
		co_await m.goTo(lodge.x, lodge.z, go);
		car->input.brake = 1;
		co_await m.say("Padre", "Here. Every name, every payment. Give it to Nina \xE2\x80\x94 and tell her I'll pray for her father. Someone should.", 4.5);
		game.vehicles.exit(padre);
	};
	story.push_back(std::move(sa));

	MissionDef bo; bo.id = "boxoffice"; bo.title = "Box Office"; bo.contact = "N"; bo.requiresIds = { "sanctuary" }; bo.reward = 12000;
	bo.chapterEnd = { "CHAPTER VIII", "Harbor Saints" };
	bo.log = "Hit the Aurelio Arena box office on fight night, cracked the cash room, outran a three-star manhunt and laid low in Cedar Ridge.";
	bo.start = [](const CityMap& map) { const V3 a = LandmarkOr(map, "aurArena", Plaza(map)); return V3(a.x + 60, 0, a.z + 10); };
	bo.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 A = LandmarkOr(game.map, "aurArena", Plaza(game.map));
		game.env.setTime(21.5);
		const RoadSpot ns = NorthSpot(game, A.x + 60, A.z + 10, true);
		Ped* nina = m.ped(ns.x + 1.2, ns.z + 1, NorthCastAt("nina", ns.y, true));
		m.speakers = { { "Nina", nina }, { "Dre", &m.player() } };
		co_await m.cutscene([&m, nina]() -> MissionTask {
			m.face(nina, &m.player()); m.face(&m.player(), nina);
			m.twoShot(&m.player(), nina, -1, 3.4);
			co_await m.lines({
				{ "Nina", "Fight night. Every dollar the Saints wash goes through that box office tonight \xE2\x80\x94 cash, no questions." },
				{ "Nina", "Six guards outside, a strongroom by the south doors. Crack it, grab it, and get out of town. There's a garage in Cedar Ridge." },
				{ "Dre", "The cops?" },
				{ "Nina", "Half of them are at the fight. The other half will be very, very angry." },
			});
		});
		nina->setPosition(nina->pos.x, nina->pos.z - 300);
		m.player().giveWeapon("smg", 180); m.player().giveWeapon("grenade", 4);
		const auto guards = Saints(m, Around(A.x, A.z, 6, 34, 46), { "smg", "pistol", "shotgun", "smg", "rifle", "pistol" });
		AggroWhenNear(m, guards, 32);
		co_await m.killAll(guards, "Take out the <span class=\"r\">arena guards</span>.", "GUARDS");
		// the strongroom: stand at it while the lock gives
		const RoadSpot sr = NorthSpot(game, A.x, A.z + 30, true);
		MarkerOpts mo; mo.radius = 1.2; mo.color = 0x6cff6c; mo.label = "Strongroom"; mo.footOnly = true; mo.icon = "money";
		Marker* mk = m.marker(sr.x, sr.z, mo);
		m.objective("Crack the <span class=\"g\">strongroom</span>. Stay put while you work the lock.");
		double k = 0;
		co_await m.until([&m, &game, sr, &k](double dt) {
			const V3 p = m.player().pos;
			if (Hypot(p.x - sr.x, p.z - sr.z) < 1.8 && !m.player().vehicle) {
				k = Min(1.0, k + dt / 7);
				const std::string label = "CRACKING"; game.hud->setBar(&label, k, "#6cff6c");
				if (Rand() < dt * 4) game.soundAt("clink", p, 0.3);
			}
			return k >= 1;
		});
		game.hud->setBar(nullptr); m.removeMarker(mk);
		m.cash(6000);
		game.hud->bigMessage("CASH GRABBED", "passed", 2.5, "$6,000 up front \xE2\x80\x94 the rest is in the bags");
		// a second wave while the alarm goes, and every cop in town
		SaintOpts so; so.guard = false;
		Saints(m, Around(sr.x, sr.z, 4, 30, 40), { "smg", "shotgun", "rifle", "smg" }, so);
		m.wanted(3);
		const RoadSpot gc = NorthSpot(game, A.x + 50, A.z + 60);
		Vehicle* car = m.car("zenith", gc.x, gc.z, gc.yaw, Paint(0x111111));
		m.blipEntity(car, 0x4aa3ff, "car");
		m.objective("Get to a car and <span class=\"r\">lose the cops</span>.");
		co_await m.until([&game]() { return game.policeSys->level == 0; });
		const Town& R = TownByKey("ridge");
		const RoadSpot gar = NorthSpot(game, R.x + 40, R.z + 10);
		GoToOpts go; go.vehicle = true; go.radius = 8; go.slow = true; go.text = "Lay low at the garage in <span class=\"y\">Cedar Ridge</span>.";
		go.condition = [&game]() { return game.policeSys->level == 0; };
		co_await m.goTo(gar.x, gar.z, go);
		co_await m.say("Dre", "Nina? It's done. The arena's dry. Your father's going to notice.", 3);
		co_await m.say("Nina", "He's already noticed. That's the point. Next: the ship.", 3);
	};
	story.push_back(std::move(bo));
}

} // namespace atg
