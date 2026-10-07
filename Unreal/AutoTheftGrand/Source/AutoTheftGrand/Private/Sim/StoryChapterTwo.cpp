// Chapter II: Streets on Fire (src/game/story.js, missions race through familyties).
#include "Story.h"
#include "Game.h"
#include "Hud.h"
#include "Police.h"

namespace atg {
namespace {
MissionTask Countdown(MissionContext& m) {
	for (const auto& n : { "3", "2", "1" }) { m.game.hud->bigMessage(n, "hint", 0.9); m.game.sound("ui"); co_await m.wait(1); }
	m.game.hud->bigMessage("GO!", "passed", 1.2); m.game.sound("checkpoint");
}
}

void AddChapterTwo(std::vector<MissionDef>& story) {
	MissionDef race; race.id = "race"; race.title = "Burning Rubber"; race.contact = "R"; race.requiresIds = { "driveby" }; race.reward = 1500;
	race.log = "Won Rico's street race around Los Soles and earned a Kestrel GT.";
	race.start = [](const CityMap& map) { const auto& p = map.landmarks.at("garage").pts.at("entry"); return V3(p.x, 0, p.z - 2); };
	race.run = [](MissionContext& m, Game& game) -> MissionTask {
		const auto e = StoryPoint(game, "garage", "entry"); Ped* rico = m.ped(e.x + 2, e.z - 3, StoryCast("rico", true)); m.speakers = { {"Rico", rico}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, rico]() -> MissionTask {
			m.face(rico, &m.player()); m.face(&m.player(), rico); m.twoShot(&m.player(), rico, 1, 3.3);
			co_await m.lines({ {"Rico", "So you're the famous Dre. Lou says you can drive."}, {"Rico", "Out here, driving's how you earn respect. Race tonight, me and my boys. One lap around the city."}, {"Rico", "Win, and I got real work for you. Lose, and you walk home."} });
		});
		game.env.setTime(std::fmod(Max(game.env.hours, 20.5), 24));
		const double xE = XS[13], xW = XS[7], zN = ZS[3], zS = ZS[9];
		const std::vector<V3> points = { {xE,0,ZS[5]}, {xE,0,ZS[7]}, {xE,0,zS}, {XS[10],0,zS}, {xW,0,zS}, {xW,0,ZS[7]}, {xW,0,ZS[5]}, {xW,0,zN}, {XS[10],0,zN}, {xE-30,0,zN} };
		const V3 start(xE - 60, 0, zN); const double grid[4][2] = { {0,1.9}, {0,5.4}, {-9,1.9}, {-9,5.4} };
		SpawnOpts paint; paint.hasColor = true; paint.color = 0xd00000; Vehicle* playerCar = m.car("kestrel", start.x, start.z + 1.9, kPi / 2, paint);
		co_await FadeTeleport(m, start.x - 4, start.z + 4.9, kPi / 2);
		game.disableAmbient = true;
		m.onCleanup([&game]() { game.disableAmbient = false; });
		std::vector<std::shared_ptr<RaceDriver>> racers; const std::vector<std::string> types = { "brawler", "zenith", "kestrel" };
		for (int i = 1; i < 4; i++) { Vehicle* v = m.car(types[i - 1], start.x + grid[i][0], start.z + grid[i][1], kPi / 2); Ped* d = m.ped(v->pos.x, v->pos.z, StoryCast("king")); v->putIn(d); v->ai.reset(); racers.push_back(std::make_shared<RaceDriver>(game, v, points, 0.72 + i * 0.05)); }
		m.lockedCars.clear(); co_await m.getIn(playerCar, "Get in your <span class=\"b\">Kestrel GT</span> \xE2\x80\x94 it's on the starting grid.");
		for (const auto& r : racers) r->veh->input.brake = 1;
		m.objective("Win the race! Follow the <span class=\"y\">checkpoints</span>."); game.cutscene = true; co_await Countdown(m); game.cutscene = false;
		int idx = 0; MarkerOpts marker; marker.radius = 9; marker.height = 6; Marker* mk = m.marker(points[0].x, points[0].z, marker); m.gps(points[0].x, points[0].z);
		bool finished = false; const std::string label = "CHECKPOINTS";
		m.tick([&m, &game, racers, points, &idx, &mk, &finished, &label, marker](double dt) mutable {
			for (const auto& r : racers) r->update(dt);
			if (finished) return;
			const V3 pp = m.player().vehicle ? m.player().vehicle->pos : m.player().pos;
			const V3 tp = points[idx];
			if (Hypot(pp.x - tp.x, pp.z - tp.z) < 12) {
				idx++; game.sound("checkpoint"); m.removeMarker(mk);
				if (idx >= (int)points.size()) { finished = true; return; }
				marker.color = idx == (int)points.size() - 1 ? 0xffffff : 0xffd23f;
				mk = m.marker(points[idx].x, points[idx].z, marker); m.gps(points[idx].x, points[idx].z);
			}
			const V3 goal = points[Min(idx, (int)points.size() - 1)]; const double progress = idx * 1000 - Hypot(pp.x - goal.x, pp.z - goal.z);
			int place = 1; for (const auto& r : racers) if (r->done || r->progress() > progress) place++;
			game.hud->setCounter("POSITION", std::to_string(place) + "/4"); game.hud->setBar(&label, (double)idx / points.size(), "#ffd23f");
		});
		m.failIf([racers, &finished]() { return !finished && std::any_of(racers.begin(), racers.end(), [](const auto& r) { return r->done; }); }, "You lost the race.");
		m.failIf([&m, &game, playerCar]() { return m.player().vehicle != playerCar && !game.vehicles.isBusy(&m.player()); }, "You left your car.");
		co_await m.until([&finished]() { return finished; }); game.disableAmbient = false; game.hud->setBar(nullptr); playerCar->missionKeep = true; playerCar->persistent = true;
		co_await m.say("Rico", "(radio) Ha! Nobody beats my boys on their home streets. Keep the Kestrel, you earned it.", 4);
	}; race.after = [](Game& game) { game.disableAmbient = false; }; story.push_back(std::move(race));

	MissionDef hot; hot.id = "hotwheels"; hot.title = "Hot Wheels"; hot.contact = "R"; hot.requiresIds = { "race" }; hot.reward = 1500;
	hot.log = "Stole three cars to order for Rico's mysterious client."; hot.start = story[5].start;
	hot.run = [](MissionContext& m, Game& game) -> MissionTask {
		const auto e = StoryPoint(game, "garage", "entry"); Ped* rico = m.ped(e.x + 2, e.z - 3, StoryCast("rico", true)); m.speakers = { {"Rico", rico}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, rico]() -> MissionTask {
			m.face(rico, &m.player()); m.face(&m.player(), rico); m.twoShot(&m.player(), rico, -1, 3.3);
			co_await m.lines({ {"Rico", "Got an order from a client. Three rides."}, {"Rico", "A Zenith up in Vistawood, a Kestrel down by the beach, and a Brawler over in Rosewood."}, {"Rico", "Bring 'em to my lock-up. Scratch the paint too bad and the client scratches you."} });
		});
		const Block* hills = nullptr; for (int i : game.map.blocks) { const auto& b = game.map.blockStore[i]; if (b.district == "hills" && b.i == 8) { hills = &b; break; } }
		if (!hills) throw MissionFail("Something went wrong.");
		const V3 pier = StoryLandmark(game, "pierfront", 20, -10), spray = StoryLandmark(game, "spray", -40, 60);
		const std::vector<StoryRoadSpot> spots = { StoryRoadNear(game, hills->cx, hills->cz), StoryRoadNear(game, pier.x, pier.z), StoryRoadNear(game, spray.x, spray.z) };
		const std::vector<std::string> types = { "zenith", "kestrel", "brawler" }; std::vector<Vehicle*> cars; SpawnOpts parked; parked.parked = true;
		for (int i = 0; i < 3; i++) { const auto& s = spots[i]; auto* v = m.car(types[i], s.x, s.z, s.yaw, parked); v->ai.reset(); cars.push_back(v); m.keepAlive(v, "One of the cars was destroyed."); }
		std::set<Vehicle*> delivered;
		while (delivered.size() < 3) {
			std::vector<Vehicle*> left; for (auto* v : cars) if (!delivered.count(v)) left.push_back(v);
			game.hud->setCounter("DELIVERED", std::to_string(delivered.size()) + "/3"); const V3 pp = m.player().pos;
			std::stable_sort(left.begin(), left.end(), [pp](const auto* a, const auto* b) { return a->pos.distanceTo(pp) < b->pos.distanceTo(pp); }); auto* car = left[0];
			co_await m.getIn(car, "Steal the <span class=\"b\">" + car->def.name + "</span>.");
			if (Rand() < 0.6) { m.wanted(1); m.help("The car alarm went off! The cops are coming.", 4); }
			const V3 garage = StoryLandmark(game, "garage"); auto unfail = m.failIf([car]() { return car->health < 450; }, "The " + car->def.name + " is too damaged. The client won't take it.");
			const std::string label = "CAR CONDITION"; game.hud->setBar(&label, car->health / 1000, "#7dff8a");
			auto stopBar = m.tick([&game, car, label](double) { game.hud->setBar(&label, car->health / 1000, car->health < 600 ? "#ff5252" : "#7dff8a"); });
			GoToOpts go; go.vehicle = true; go.radius = 4; go.inCar = car; go.inCarMsg = "Bring the " + car->def.name + ".";
			go.text = "Deliver the " + car->def.name + " to Rico's <span class=\"y\">lock-up</span>. Lose any cops first."; go.condition = [&game]() { return game.policeSys->level == 0; };
			co_await m.goTo(garage.x, garage.z, go); stopBar(); unfail(); game.hud->setBar(nullptr);
			game.vehicles.exit(&m.player()); co_await m.wait(1.4); delivered.insert(car); car->locked = true; m.cash(500); game.sound("cash");
		}
		game.hud->setCounter("", ""); co_await m.say("Rico", "(phone) Client's thrilled. Says you've got an eye for fine machinery. Here's your cut.", 4);
	}; story.push_back(std::move(hot));

	MissionDef blood; blood.id = "bloodmoney"; blood.title = "Blood Money"; blood.contact = "D"; blood.requiresIds = { "driveby" }; blood.reward = 3000;
	blood.log = "Robbed the Vipers' stash house with Deacon's tip-off and escaped a three-star manhunt."; blood.start = story[2].start;
	blood.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 s = StoryLandmark(game, "projects", 10, 0); Ped* deacon = m.ped(s.x + 1.8, s.z - 1, StoryCast("deacon", true)); m.speakers = { {"Deacon", deacon}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, deacon]() -> MissionTask {
			m.face(deacon, &m.player()); m.face(&m.player(), deacon); m.twoShot(&m.player(), deacon, 1, 3.3);
			co_await m.lines({ {"Deacon", "Vipers stash their cash at a house in El Corona. My guy says it's lightly guarded tonight."}, {"Deacon", "Hit it, grab the money, and meet me back here. We split it down the middle."}, {"Dre", "Lightly guarded. Right."} });
		});
		const V3 v = StoryLandmark(game, "vipers"); MissionEnemyOpts guard; guard.guard = true; guard.health = 110;
		const auto guards = StoryCrew(m, "vipers", StoryRing(v.x, v.z, 8, 12, 0.2), { "smg", "pistol", "shotgun", "pistol", "smg", "pistol", "bat", "pistol" }, guard); AggroWhenNear(m, guards, 35); m.gps(v.x, v.z);
		co_await m.killAll(guards, "Clear out the <span class=\"r\">stash house guards</span>.", "GUARDS");
		int got = 0; MarkerOpts bag; bag.radius = 0.9; bag.color = 0x44ff44; bag.footOnly = true; bag.icon = "money";
		for (const auto& p : StoryRing(v.x, v.z, 3, 4, 1)) { auto* mk = m.marker(p.x, p.z, bag); mk->onEnter = [&m, &game, &got](Marker* entered) { got++; game.sound("cash"); m.removeMarker(entered); game.hud->moneyFlash(1000); }; }
		m.objective("Grab the <span class=\"g\">cash bags</span>."); co_await m.until([&got]() { return got >= 3; });
		game.policeSys->setLevel(3); m.help("Three stars! Break line of sight with the cops and stay hidden until the stars stop flashing. A <b>Spray Shack</b> also clears your wanted level.", 9);
		co_await m.loseWanted("The whole LSPD is after you. Lose your wanted level!"); GoToOpts go; go.radius = 2; go.text = "Bring the money back to <span class=\"y\">Deacon</span> at the projects."; co_await m.goTo(s.x, s.z, go);
		co_await m.say("Deacon", "Damn, Dre. Half for you, half for... the cause. You're a natural.", 4);
	}; story.push_back(std::move(blood));

	MissionDef snitch; snitch.id = "snitch"; snitch.title = "The Snitch"; snitch.contact = "V"; snitch.requiresIds = { "bloodmoney" }; snitch.reward = 2000;
	snitch.log = "Voss blackmailed Dre into silencing Benny Tran before he reached LSPD Central.";
	snitch.start = [](const CityMap& map) { const auto& p = map.landmarks.at("hospital"); return V3(p.x - 30, 0, p.z - 4); };
	snitch.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 s = StoryLandmark(game, "hospital", -30, -4); Ped* voss = m.ped(s.x + 1.8, s.z - 1.2, StoryCast("voss", true)); m.speakers = { {"Voss", voss}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, voss]() -> MissionTask {
			m.face(voss, &m.player()); m.face(&m.player(), voss); m.twoShot(&m.player(), voss, 1, 3.4);
			co_await m.lines({ {"Voss", "Castillo. You've been busy. Stash houses, drive-bys. Very entrepreneurial."}, {"Voss", "A little bird named Benny Tran is about to make a statement about a shooting at Pershing Plaza. Your name's in it."}, {"Dre", "I wasn't even in town!"}, {"Voss", "Tell it to a jury. Or make sure Benny never reaches LSPD Central. He's leaving the docks right now."} });
		});
		const V3 w = StoryLandmark(game, "warehouse", -40, 0); const auto r = StoryRoadNear(game, w.x, w.z); SpawnOpts paint; paint.hasColor = true; paint.color = 0x3a5a40;
		Vehicle* car = m.car("meridian", r.x, r.z, r.yaw, paint); car->health = 1300; Ped* benny = m.ped(r.x, r.z, StoryCast("benny")); car->putIn(benny);
		RouteDriverOpts o; o.speed = 18; o.ignoreLights = false; auto drv = std::make_shared<RouteDriver>(game, car, StoryLandmark(game, "police", 0, -12), o); car->ai = drv;
		m.blipEntity(benny, 0xff3030, "target"); game.missionMaxWanted = 2;
		m.tick([&m, car, benny, drv](double) { if (!benny->dead && car->driver() == benny && car->ai == drv && m.distTo(benny) < 50) { drv->cruise = 28; drv->ignoreLights = true; } if (car->driver() != benny) car->ai.reset(); });
		m.failIf([car, drv, benny]() { return drv->arrived && !benny->dead && car->driver() == benny; }, "Benny made it to the police station.");
		m.objective("Stop <span class=\"r\">Benny Tran</span> before he reaches LSPD Central."); co_await m.until([car, benny]() { return benny->dead || car->isWrecked(); });
		if (!benny->dead) { DamageInfo d; d.source = &m.player(); benny->takeDamage(1000, d); }
		game.missionMaxWanted = NaN(); co_await m.loseWanted(); co_await m.say("Voss", "(phone) Benny Tran, tragic accident. You and me are going to get along fine, Castillo.", 4);
	}; story.push_back(std::move(snitch));

	MissionDef family; family.id = "familyties"; family.title = "Family Ties"; family.contact = "L"; family.requiresIds = { "snitch" }; family.reward = 2500;
	family.log = "The Vipers kidnapped Marisol. Dre stormed the Pier 9 warehouse and brought her home."; family.chapterEnd = { "CHAPTER III", "Betrayal" };
	family.start = [](const CityMap& map) { const auto& p = map.landmarks.at("home"); return V3(p.x + 4, 0, p.z + 2); };
	family.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 s = StoryLandmark(game, "home", 4, 2); Ped* lou = m.ped(s.x + 1.6, s.z - 1, StoryCast("lou", true)); m.speakers = { {"Lou", lou}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, lou]() -> MissionTask {
			m.face(lou, &m.player()); m.face(&m.player(), lou); m.twoShot(&m.player(), lou, -1, 3.3);
			co_await m.lines({ {"Lou", "Dre! They took Mari! Vipers and Cuervos, they grabbed her right outside the house!"}, {"Dre", "Where?"}, {"Lou", "Word is they're holding her at the Pier 9 warehouse down in Port Morena. Go! I'll round up the boys."} });
		});
		m.player().giveWeapon("pistol", 34); const V3 w = StoryLandmark(game, "warehouse"); MissionEnemyOpts guard; guard.guard = true;
		auto enemies = StoryCrew(m, "cuervos", StoryRing(w.x - 18, w.z + 5, 5, 6), { "smg", "shotgun", "pistol", "smg", "pistol" }, guard);
		const auto vipers = StoryCrew(m, "vipers", StoryRing(w.x + 8, w.z + 8, 5, 7, 0.5), { "pistol", "smg", "bat", "pistol", "shotgun" }, guard); enemies.insert(enemies.end(), vipers.begin(), vipers.end()); AggroWhenNear(m, enemies, 40);
		Ped* mari = m.ped(w.x + 26, w.z + 2, StoryCast("marisol")); mari->crouching = true; mari->animState.cower = true; m.keepAlive(mari, "Marisol was killed."); m.gps(w.x, w.z);
		co_await m.killAll(enemies, "Storm the <span class=\"r\">Pier 9 warehouse</span>.", "ENEMIES"); m.blipEntity(mari, 0x4aa3ff, "dot");
		GoToOpts go; go.radius = 1.6; go.text = "Get to <span class=\"b\">Marisol</span>."; co_await m.goTo(mari->pos.x, mari->pos.z + 1.2, go);
		m.speakers = { {"Marisol", mari}, {"Dre", &m.player()} }; mari->animState.cower = false; mari->crouching = false;
		co_await m.lines({ {"Marisol", "Dre! I knew you'd come. They kept saying Deacon would pay for me... Deacon, Dre!"}, {"Dre", "Deacon? We'll talk at home. Stay close."} });
		m.follower(mari, 0); const V3 home = StoryLandmark(game, "home"); go.radius = 5; go.text = "Take <span class=\"b\">Marisol</span> home.";
		go.condition = [&m, mari]() { return mari->vehicle == m.player().vehicle || (!m.player().vehicle && Hypot(mari->pos.x - m.player().pos.x, mari->pos.z - m.player().pos.z) < 8); };
		co_await m.goTo(home.x, home.z + 14, go); if (mari->vehicle) game.vehicles.exit(mari);
		co_await m.say("Marisol", "Thank you. Dre... watch Deacon. Something's wrong with him.", 4); mari->missionKeep = false;
	}; story.push_back(std::move(family));
}
} // namespace atg
