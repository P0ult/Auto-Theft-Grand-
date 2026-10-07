// Chapter VI: Out of Town (src/game/story.js, faregame through secosunrise).
#include "Story.h"
#include "Game.h"
#include "Hud.h"
#include "Phone.h"
#include "Police.h"
#include "Rail.h"

namespace atg {
namespace {
RoadSpot Spot(Game& g, double x, double z, bool foot = false) { RoadSpot s; if (!FindRoadSpot(g, x, z, foot, s)) throw MissionFail("Something went wrong."); return s; }
Appearance SecoLook() {
	RNG rng((uint32_t)(Rand() * 1e9));
	const uint32_t shirt = RandPick(std::vector<uint32_t>{ 0xc2a878, 0xe9d8a6, 0x8d6e4a });
	const auto type = RandPick(std::vector<std::string>{ "long", "jacket", "tee" });
	const uint32_t pants = RandPick(std::vector<uint32_t>{ 0x4e4a45, 0x6d5c43, 0x3b2f2a });
	const auto hair = RandPick(std::vector<std::string>{ "cap", "short", "buzz" }); const int64_t bandana = Rand() < 0.4 ? 0x7a1f1f : -1; const bool glasses = Rand() < 0.5;
	Appearance a = RandomAppearance(rng, 0); a.shirt = shirt; a.shirtType = type; a.jacketColor = 0x5a4632; a.pants = pants; a.hairStyle = hair; a.hat = 0xc2a878; a.bandana = bandana; a.glasses = glasses; return a;
}
std::vector<Ped*> Secos(MissionContext& m, const std::vector<V3>& points, const std::vector<std::string>& weapons, MissionEnemyOpts opts = {}, bool explicitY = false) {
	std::vector<Ped*> out;
	for (size_t i = 0; i < points.size(); i++) { auto o = opts; o.gang = "cuervos"; o.weapon = weapons[i % weapons.size()]; o.hasAppearance = true; o.appearance = SecoLook(); auto* e = m.enemy(points[i].x, points[i].z, o); if (explicitY) e->setPosition(points[i].x, points[i].y, points[i].z); out.push_back(e); }
	return out;
}
int StopIndex(const Train& train, const std::string& key) { for (int i = 0; i < (int)train.stops.size(); i++) if (train.stops[i].key == key) return i; return -1; }
}

void AddChapterSix(std::vector<MissionDef>& story) {
	MissionDef fare; fare.id = "faregame"; fare.title = "Fare Game"; fare.contact = "M"; fare.requiresIds = { "finale" }; fare.reward = 4000;
	fare.log = "Drove a cab out to Mirador, picked up Benny the bookkeeper and got him to a boat at Port Hale with Los Secos on our tail."; fare.start = story[19].start;
	fare.after = [](Game& g) { g.setTimeout(1.5, [&g]() { g.hud->help("Rico's next job is out at <b>Dry Wells station</b>. It's a long way \xE2\x80\x94 whistle for a cab with <b>H</b> and skip the ride with <b>Space</b>.", 9); }); };
	fare.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 s = StoryLandmark(game, "home", -3, 2); Ped* mari = m.ped(s.x - 1.6, s.z - 1, StoryCast("marisol", true)); m.speakers = { {"Marisol", mari}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, mari]() -> MissionTask {
			m.face(mari, &m.player()); m.face(&m.player(), mari); m.twoShot(&m.player(), mari, 1, 3.4);
			co_await m.lines({ {"Marisol", "Voss had partners. A cartel out of Puerto Seco. Los Secos. They want his money, and they want his bookkeeper."}, {"Dre", "Benny? The guy with the glasses?"}, {"Marisol", "He's hiding up in Mirador. Take a cab \xE2\x80\x94 nobody looks twice at a cab \xE2\x80\x94 and get him to the boat at Port Hale."} });
		});
		const V3 h = StoryLandmark(game, "home"); const auto r = StoryRoadNear(game, h.x + 10, h.z + 24); Vehicle* cab = m.car("taxi", r.x, r.z, r.yaw); co_await m.getIn(cab, "Get in the <span class=\"b\">cab</span>."); m.keepAlive(cab, "The cab was destroyed.");
		const auto& t = TownByKey("mirador"); const auto pick = Spot(game, t.x + 25, t.z + 10), walk = Spot(game, pick.x, pick.z, true); auto look = StoryCast("benny"); look.hasY = true; look.y = walk.y;
		Ped* benny = m.ped(walk.x, walk.z, look); benny->health = benny->maxHealth = 300; benny->setState("guard"); benny->hasGuardFace = true; benny->guardFace = pick.yaw + kPi / 2;
		m.blipEntity(benny, 0x4a90ff, "person"); m.keepAlive(benny, "Benny is dead.");
		GoToOpts go; go.vehicle = true; go.radius = 6; go.slow = true; go.inCar = cab; go.inCarMsg = "Benny will only get in the cab."; go.text = "Pick up <span class=\"b\">Benny</span> in <span class=\"y\">Mirador</span>."; co_await m.goTo(pick.x, pick.z, go);
		m.speakers = { {"Benny", benny}, {"Dre", &m.player()} }; m.follower(benny, 0); UntilOpts board; board.timeout = 12; board.resolveTimeout = true; co_await m.until([benny, cab]() { return benny->vehicle == cab; }, board); if (benny->vehicle != cab) cab->putIn(benny, 2);
		co_await m.say("Benny", "Drive! They've been parked outside the diner all morning \xE2\x80\x94 tan pickup!", 3.5);
		m.failIf([&m, &game, cab, benny]() { return m.player().vehicle != cab && !game.vehicles.isBusy(&m.player()) && benny->vehicle == cab; }, "You left Benny behind.");
		const auto back = Spot(game, t.x - 120, t.z - 60); StoryChaseCar(m, "hauler", back.x, back.z, back.yaw, "cuervos", 2, SecoLook); std::weak_ptr<MissionContext> weak = m.shared_from_this();
		game.setTimeout(9, [weak, &game]() { if (auto ctx = weak.lock()) if (game.missions->active == ctx) game.hud->subtitle("They're shooting at a TAXI! Who shoots at a taxi?!", "Benny", 3); });
		V3 pier; auto it = game.map.landmarks.find("halePier"); if (it != game.map.landmarks.end()) pier = V3(it->second.x, 0, it->second.z); else { const auto& hale = TownByKey("hale"); pier = V3(hale.x + 130, 0, hale.z); }
		const auto dock = Spot(game, pier.x - 70, pier.z); go.radius = 7; go.slow = false; go.text = "Get Benny to the boat at <span class=\"y\">Port Hale</span>."; co_await m.goTo(dock.x, dock.z, go); cab->input.brake = 1;
		co_await m.say("Benny", "I owe you, Dre. Here \xE2\x80\x94 Voss's ledger. Page forty. That's where the cartel's money goes.", 4); game.vehicles.exit(benny);
	}; story.push_back(std::move(fare));

	MissionDef sol; sol.id = "solline"; sol.title = "Sol Line Express"; sol.contact = "R"; sol.requiresIds = { "faregame" }; sol.reward = 6000;
	sol.log = "Took the Los Secos gun train at Dry Wells, rammed through their roadblock and brought it into Union Station.";
	sol.start = [](const CityMap& map) { const auto it = map.landmarks.find("station_dry"); const auto& st = it == map.landmarks.end() ? map.landmarks.at("home") : it->second; const double rot = st.vals.count("rot") ? st.vals.at("rot") : 0; return V3(st.x - std::cos(rot) * 26, 0, st.z + std::sin(rot) * 26); };
	sol.run = [](MissionContext& m, Game& game) -> MissionTask {
		Train* train = game.rail ? dynamic_cast<Train*>(game.rail->train.get()) : nullptr; if (!train && game.rail) train = game.rail->spawnTrain();
		const int dryIndex = game.rail ? game.rail->station("dry") : -1, fernIndex = game.rail ? game.rail->station("fern") : -1, unionIndex = game.rail ? game.rail->station("union") : -1;
		if (!train || dryIndex < 0 || unionIndex < 0) throw MissionFail("The Sol Line is closed today.");
		const auto dry = game.rail->stations[dryIndex], dest = game.rail->stations[unionIndex]; const auto& lm = game.map.landmarks.at("station_dry"); const double rot = lm.vals.at("rot");
		train->s = dry.s + train->len / 2; train->v = 0; train->dwell = 1e9; train->atStation = StopIndex(*train, "dry"); train->dirS = 1; train->place(); game.rail->clearLineFor(train);
		const V3 s0(lm.x - std::cos(rot) * 24, 0, lm.z + std::sin(rot) * 24); Ped* rico = m.ped(s0.x + 1.5, s0.z + 1, StoryCast("rico", true)); m.speakers = { {"Rico", rico}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, rico]() -> MissionTask {
			m.face(rico, &m.player()); m.face(&m.player(), rico); m.twoShot(&m.player(), rico, -1, 3.4);
			co_await m.lines({ {"Rico", "Benny's ledger checks out. Los Secos move their guns into the city on the Sol Line. That train right there."}, {"Rico", "Three of their boys on the platform. Take them out, climb in the cab up front and drive her into Union Station. I'll meet you there."}, {"Dre", "I've never driven a train."}, {"Rico", "Push the lever forward. Try not to hit anything. Well \xE2\x80\x94 hit whatever they put in your way."} });
		});
		rico->setPosition(rico->pos.x, rico->pos.z - 400); std::vector<V3> pts; for (double a : { -22., 0., 24. }) pts.push_back(V3(lm.x + std::sin(rot) * a, lm.y, lm.z + std::cos(rot) * a));
		MissionEnemyOpts guard; guard.guard = true; guard.face = rot + kPi / 2; const auto guards = Secos(m, pts, { "rifle", "shotgun", "pistol" }, guard, true); AggroWhenNear(m, guards, 40); m.player().giveWeapon("smg", 90);
		co_await m.killAll(guards, "Take out the <span class=\"r\">cartel guards</span> on the platform."); co_await m.getIn(train, "Climb into the <span class=\"b\">train cab</span> (at the front)."); train->dwell = 0;
		m.failIf([&m, &game, train]() { return m.player().vehicle != train && !game.vehicles.isBusy(&m.player()); }, "You abandoned the train."); auto stop = m.timer(240, "Los Secos got their guns back.");
		Blip b; b.x = dest.x; b.z = dest.z; b.color = 0xffd23f; b.icon = "train"; m.blips.push_back(game.addBlip(b)); m.objective("Drive the train to <span class=\"y\">Union Station</span>. Hold <b>W</b> to accelerate, <b>S</b> to brake.");
		bool ambushed = false; const double fernS = fernIndex >= 0 ? game.rail->stations[fernIndex].s : 0; std::optional<RailCrossing> crossing;
		for (const auto& c : game.map.roadInfo.rail.crossings) if (c.kind == "level" && c.s > fernS) { crossing = c; break; }
		m.tick([&m, &game, train, fernIndex, fernS, crossing, &ambushed](double) {
			if (ambushed || fernIndex < 0 || train->s < fernS - 700) return; ambushed = true;
			if (crossing) { SpawnOpts paint; paint.hasColor = true; paint.color = 0xc2a878; auto* car = m.car("hauler", crossing->x, crossing->z, Rand() * 6, paint); car->parked = true; }
			const auto& fl = game.map.landmarks.at("station_fern"); const double fr = fl.vals.at("rot"); std::vector<V3> points;
			for (double a : { -30., -10., 10., 30. }) points.push_back(V3(fl.x + std::sin(fr) * a, fl.y, fl.z + std::cos(fr) * a));
			MissionEnemyOpts attack; attack.guard = false; attack.accuracy = 0.3; Secos(m, points, { "rifle", "smg", "rifle", "shotgun" }, attack, true);
			game.hud->subtitle("Rico here \xE2\x80\x94 they know you're coming. Fern Creek's crawling with them. Don't stop!", "Rico", 4.5);
		});
		co_await m.until([train, dest]() { return train->centreS() > dest.s - 30 && std::fabs(train->v) < 1.5; }); stop(); co_await m.say("Rico", "Ha! Right on the platform. Two crates of rifles the cartel will never see again.", 3.5);
		train->atStation = StopIndex(*train, "union"); train->dwell = 14; train->dirS = -1; game.rail->releaseLine();
	}; story.push_back(std::move(sol));

	MissionDef dust; dust.id = "dustoff"; dust.title = "Dust Off"; dust.contact = "R"; dust.requiresIds = { "solline" }; dust.reward = 5000;
	dust.log = "Flew the Skipper low through the canyons and buzzed the Los Secos compound so Rico's spotter could map it.";
	dust.start = [](const CityMap&) { return V3(AIRFIELD.x - 30, 0, AIRFIELD.z - 130); };
	dust.run = [](MissionContext& m, Game& game) -> MissionTask {
		Ped* rico = m.ped(AIRFIELD.x - 27, AIRFIELD.z - 128, StoryCast("rico", true)); m.speakers = { {"Rico", rico}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, rico]() -> MissionTask {
			m.face(rico, &m.player()); m.face(&m.player(), rico); m.twoShot(&m.player(), rico, 1, 3.4);
			co_await m.lines({ {"Rico", "Their compound's in Puerto Seco. Walls, lookouts, the works. We need eyes on it before we hit it."}, {"Rico", "Take the Skipper. Stay low through the rings so their lookouts don't spot you, buzz the compound, and bring her home in one piece."} });
		});
		SpawnOpts paint; paint.hasColor = true; paint.color = 0xe8e8e8; Vehicle* plane = m.car("skipper", AIRFIELD.x + 230, AIRFIELD.z, -kPi / 2, paint); co_await m.getIn(plane, "Get in the <span class=\"b\">Skipper</span>."); m.keepAlive(plane, "The Skipper was destroyed.");
		m.help("W / S: throttle. Pull back (mouse or \xE2\x86\x93) to climb, A / D to roll into turns. Fly through the rings.", 8); auto stop = m.timer(300, "Their lookouts spotted you. Too slow.");
		struct Ring { double x, z, alt; bool buzz; }; const std::vector<Ring> route = { {AIRFIELD.x-620,AIRFIELD.z+20,60,false}, {-3700,680,70,false}, {-4150,820,60,false}, {-4000,470,40,true}, {-3700,180,70,false}, {AIRFIELD.x-420,AIRFIELD.z,45,false} };
		for (size_t i = 0; i < route.size(); i++) {
			const auto& r = route[i]; if (r.buzz) { const auto& town = TownByKey("seco"); std::vector<V3> p; for (const auto& d : std::vector<V2>{ {-40,10},{30,-25},{0,45},{55,30} }) p.push_back(V3(town.x + d.x, 0, town.z + d.z)); MissionEnemyOpts attack; attack.guard = false; attack.accuracy = 0.25; Secos(m, p, { "rifle", "rifle", "smg", "rifle" }, attack); }
			const V3 next = i + 1 < route.size() ? V3(route[i + 1].x, 0, route[i + 1].z) : V3(AIRFIELD.x, 0, AIRFIELD.z);
			co_await m.airRing(r.x, r.z, r.alt, 16, next, r.buzz ? "Buzz the <span class=\"r\">Los Secos compound</span>!" : "Fly through the rings (" + std::to_string(i + 1) + "/" + std::to_string(route.size()) + ").");
		}
		GoToOpts go; go.vehicle = true; go.radius = 16; go.slow = true; go.inCar = plane; go.text = "Land the Skipper on the <span class=\"y\">runway</span>."; co_await m.goTo(AIRFIELD.x + 40, AIRFIELD.z, go); stop(); co_await m.say("Rico", "Smooth. My guy got every wall and every window. Tomorrow, we go in.", 3);
	}; story.push_back(std::move(dust));

	MissionDef seco; seco.id = "secosunrise"; seco.title = "Seco Sunrise"; seco.contact = "R"; seco.requiresIds = { "dustoff" }; seco.reward = 15000; seco.chapterEnd = { "CHAPTER VII", "San Aurelio" };
	seco.log = "Took a Warhawk gunship to Puerto Seco at dawn, burned the Los Secos trucks and ran El Seco down in the desert."; seco.start = story[24].start;
	seco.after = [](Game& g) { g.setTimeout(1.5, [&g]() { g.hud->help("Marisol has news from up north. Meet her at the <b>safehouse</b>.", 7); }); };
	seco.run = [](MissionContext& m, Game& game) -> MissionTask {
		game.env.setTime(6.2); Ped* rico = m.ped(AIRFIELD.x - 27, AIRFIELD.z - 128, StoryCast("rico", true)), *mari = m.ped(AIRFIELD.x - 33, AIRFIELD.z - 127, StoryCast("marisol", true)); m.speakers = { {"Rico", rico}, {"Marisol", mari}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, rico, mari]() -> MissionTask {
			m.face(rico, &m.player()); m.face(mari, &m.player()); m.face(&m.player(), rico); m.twoShot(&m.player(), rico, 1, 3.6);
			co_await m.lines({ {"Marisol", "Army base says one of their gunships is \"in for maintenance\". It's behind the hangar."}, {"Rico", "Three trucks in the compound. That's the money, the guns, everything. Burn them."}, {"Dre", "And El Seco?"}, {"Marisol", "He'll run. They always run."} });
		});
		Vehicle* heli = m.car("warhawk", AIRFIELD.x - 70, AIRFIELD.z - 110, 0); co_await m.getIn(heli, "Get in the <span class=\"b\">Warhawk</span>."); m.help("Space / Shift: climb & descend. W / S: nose down / up. A / D: turn. Left mouse: minigun, right mouse: rockets.", 9);
		const auto& c = TownByKey("seco"); co_await m.airRing(c.x + 260, c.z - 40, 70, 26, V3(c.x, 0, c.z), "Fly to <span class=\"y\">Puerto Seco</span>.");
		std::vector<Vehicle*> trucks; SpawnOpts paint; paint.hasColor = true; paint.color = 0xc2a878;
		for (const auto& d : std::vector<V2>{ {-30,25},{35,-20},{10,60} }) { const auto sp = Spot(game, c.x + d.x, c.z + d.z); Vehicle* t = m.car("hauler", sp.x, sp.z, sp.yaw, paint); t->parked = true; t->locked = true; m.blipEntity(t, 0xff3030, "car"); trucks.push_back(t); }
		std::vector<V3> points; for (const auto& d : std::vector<V2>{ {-50,0},{-20,40},{25,20},{50,-35},{0,-45},{60,55},{-45,60},{15,90} }) points.push_back(V3(c.x + d.x, 0, c.z + d.z));
		MissionEnemyOpts attack; attack.guard = false; attack.accuracy = 0.3; const auto guns = Secos(m, points, { "rifle", "rifle", "smg", "rpg", "rifle", "shotgun", "rifle", "rpg" }, attack);
		m.objective("Destroy the three <span class=\"r\">cartel trucks</span>."); co_await m.until([trucks]() { return std::all_of(trucks.begin(), trucks.end(), [](const auto* t) { return t->isWrecked() || t->exploded; }); });
		game.hud->subtitle("That's the last of their money going up. Now where's the old man?", "Dre", 3); const auto esc = Spot(game, c.x - 80, c.z + 30); paint.color = 0xe9d8a6;
		Vehicle* car = m.car("summit", esc.x, esc.z, esc.yaw, paint); Ped* boss = m.ped(esc.x, esc.z, StoryCast("elseco")); car->putIn(boss); car->health = 1500;
		const auto& dry = TownByKey("dry"); RouteDriverOpts drive; drive.speed = 24; auto drv = std::make_shared<RouteDriver>(game, car, V3(dry.x, 0, dry.z), drive); car->ai = drv; m.blipEntity(boss, 0xff3030, "skull");
		m.failIf([boss, car]() { auto ai = std::dynamic_pointer_cast<RouteDriver>(car->ai); return !boss->dead && ai && ai->arrived; }, "El Seco got away."); m.objective("<span class=\"r\">El Seco</span> is running. Stop him!"); co_await m.until([boss]() { return boss->dead; });
		for (auto* g : guns) if (!g->dead) g->setState("flee"); co_await m.wait(1.5); co_await m.say("Rico", "That's it, D. No more Voss, no more Secos. The whole state's yours.", 4); co_await m.say("Dre", "Tino would've loved this. Bring it home.", 3);
	}; story.push_back(std::move(seco));
}
} // namespace atg
