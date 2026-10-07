// Chapter V: End of the Line (src/game/story.js, papertrail through finale).
#include "Story.h"
#include "Game.h"
#include "Hud.h"
#include "Police.h"

namespace atg {
void AddChapterFive(std::vector<MissionDef>& story) {
	MissionDef paper; paper.id = "papertrail"; paper.title = "Paper Trail"; paper.contact = "M"; paper.requiresIds = { "kingpin" }; paper.reward = 5000;
	paper.log = "Walked into LSPD Central in a stolen cruiser and walked out with Voss's dirty files.";
	paper.start = [](const CityMap& map) { const auto& h = map.landmarks.at("home"); return V3(h.x - 3, 0, h.z + 2); };
	paper.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 s = StoryLandmark(game, "home", -3, 2); Ped* mari = m.ped(s.x - 1.6, s.z - 1, StoryCast("marisol", true)); m.speakers = { {"Marisol", mari}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, mari]() -> MissionTask {
			m.face(mari, &m.player()); m.face(&m.player(), mari); m.twoShot(&m.player(), mari, 1, 3.2);
			co_await m.lines({ {"Marisol", "Voss keeps his dirty files at LSPD Central. Payoffs, Salazar's money, everything. A friend at the DA told me."}, {"Marisol", "Steal a police car so nobody looks twice, drive into the back lot and grab the file boxes from the loading dock."}, {"Dre", "Walk into a police station with a stolen cop car. What could go wrong?"} });
		});
		const V3 p = StoryLandmark(game, "police"); const auto r = StoryRoadNear(game, p.x - 120, p.z + 80); SpawnOpts parked; parked.parked = true; Vehicle* cop = m.car("police", r.x, r.z, r.yaw, parked); cop->ai.reset();
		game.policeSys->enabled = false; co_await m.getIn(cop, "Steal a <span class=\"b\">police cruiser</span>."); game.policeSys->clearWanted();
		GoToOpts go; go.vehicle = true; go.radius = 4; go.inCar = cop; go.inCarMsg = "You need a police car."; go.text = "Drive into the <span class=\"y\">LSPD back lot</span>.";
		co_await m.goTo(p.x, p.z + 42, go); game.vehicles.exit(&m.player()); co_await m.wait(1.3);
		GoToOpts foot; foot.onFoot = true; foot.radius = 1.2; foot.text = "Grab the <span class=\"g\">files</span> from the loading dock."; co_await m.goTo(p.x + 10, p.z + 32, foot);
		game.sound("pickup"); game.policeSys->enabled = true; game.policeSys->setLevel(4); game.hud->subtitle("Officer: Hey! That's Detective Voss's stuff! STOP HIM!", "Officer", 3);
		co_await m.loseWanted("Escape with the files! Lose the cops."); GoToOpts home; home.radius = 2; home.text = "Bring the files to <span class=\"b\">Marisol</span>."; co_await m.goTo(s.x, s.z, home);
		co_await m.say("Marisol", "This is it, Dre. Voss is done. Now... Deacon.", 3);
	}; story.push_back(std::move(paper));

	MissionDef tower; tower.id = "tower"; tower.title = "Deacon's Tower"; tower.contact = "L"; tower.requiresIds = { "papertrail" }; tower.reward = 8000;
	tower.log = "Fought to the roof of Deacon Tower. Deacon confessed Tino was about to go to the Feds about Voss."; tower.start = story[1].start;
	tower.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 c = StoryLandmark(game, "court", 0, 12); Ped* lou = m.ped(c.x - 2, c.z - 2, StoryCast("lou", true)); m.speakers = { {"Lou", lou}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, lou]() -> MissionTask {
			m.face(lou, &m.player()); m.face(&m.player(), lou); m.twoShot(&m.player(), lou, 1, 3.4);
			co_await m.lines({ {"Lou", "Deacon took Salazar's money and bought himself the penthouse on that glass tower downtown."}, {"Lou", "Last of his hired guns are in the plaza out front. Go get him, D. For Tino."} });
		});
		const V3 t = StoryLandmark(game, "tower"), base = t + V3(0, 0, 6); MissionEnemyOpts guard; guard.guard = true; guard.health = 120; guard.accuracy = 0.5;
		const auto outside = StoryCrew(m, "cuervos", StoryRing(base.x, base.z + 6, 8, 10), { "rifle", "smg", "shotgun", "pistol" }, guard); AggroWhenNear(m, outside, 45); m.gps(base.x, base.z);
		co_await m.killAll(outside, "Clear the <span class=\"r\">guards</span> outside Deacon Tower.", "GUARDS");
		GoToOpts elevator; elevator.onFoot = true; elevator.radius = 1.5; elevator.text = "Take the <span class=\"y\">elevator</span> to the roof."; co_await m.goTo(t.x, t.z - 1.5, elevator); game.policeSys->clearWanted();
		const auto top = StoryPoint(game, "tower", "top"); co_await FadeTeleport(m, top.x - 6, top.z - 6, kPi * 0.25, top.y);
		MissionEnemyOpts boss; boss.gang = "cuervos"; boss.weapon = "rifle"; boss.health = 350; boss.armor = 100; boss.hasAppearance = true; boss.appearance = StoryLook("deacon"); boss.blip = false; boss.guard = false;
		Ped* deacon = m.enemy(top.x + 5, top.z + 5, boss); deacon->setPosition(top.x + 5, top.y, top.z + 5); m.blipEntity(deacon, 0xff3030, "skull");
		MissionEnemyOpts attack; attack.guard = false; attack.health = 110; auto roof = StoryCrew(m, "cuervos", StoryRing(top.x, top.z, 4, 7, 0.8), { "smg", "shotgun", "rifle", "smg" }, attack);
		for (auto* g : roof) g->setPosition(g->pos.x, top.y, g->pos.z); roof.insert(roof.begin(), deacon); m.speakers = { {"Deacon", deacon}, {"Dre", &m.player()} };
		game.hud->subtitle("Dre... I knew you'd come. Let's finish this!", "Deacon", 3); co_await m.killAll(roof, "Kill <span class=\"r\">Deacon</span>.");
		co_await m.lines({ {"Deacon", "(dying) Tino... he was going to the Feds about Voss. Voss said... he'd kill us all... I had no choice..."}, {"Dre", "You always had a choice, Deacon."} });
		co_await FadeTeleport(m, t.x, t.z + 3, 0);
	}; story.push_back(std::move(tower));

	MissionDef finale; finale.id = "finale"; finale.title = "Grand Finale"; finale.contact = "M"; finale.requiresIds = { "tower" }; finale.reward = 25000; finale.chapterEnd = { "CHAPTER VI", "Out of Town" };
	finale.log = "Chased Voss to the Santa Luz Pier and ended him. Tino can rest. Los Soles has a new king."; finale.start = story[19].start;
	finale.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 s = StoryLandmark(game, "home", -3, 2); Ped* mari = m.ped(s.x - 1.6, s.z - 1, StoryCast("marisol", true)), *lou = m.ped(s.x + 1.6, s.z - 1.3, StoryCast("lou", true)); m.speakers = { {"Marisol", mari}, {"Lou", lou}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, mari, lou]() -> MissionTask {
			m.face(mari, &m.player()); m.face(lou, &m.player()); m.face(&m.player(), mari); m.twoShot(&m.player(), mari, 1, 3.6);
			co_await m.lines({ {"Marisol", "The files went out to every newsroom in the city this morning. Voss is finished."}, {"Lou", "He knows it too. His cruiser just went tearing down toward the beach. Ruiz is with him."}, {"Dre", "Then this ends at the pier. For Tino."} });
		});
		game.env.setTime(18.2); const V3 h = StoryLandmark(game, "home"); const auto r = StoryRoadNear(game, h.x + 140, h.z + 40); Vehicle* vcar = m.car("police", r.x, r.z, r.yaw); vcar->health = 1800;
		Ped* voss = m.ped(r.x, r.z, StoryCast("voss")), *ruiz = m.ped(r.x, r.z, StoryCast("ruiz")); vcar->putIn(voss); vcar->putIn(ruiz, 1); ruiz->giveWeapon("smg", 999); ruiz->equip("smg"); m.driveBy(ruiz, nullptr, 40); vcar->sirenOn = true;
		const auto& p = game.map.landmarks.at("pier"); const double xm = (p.vals.at("x0") + p.vals.at("x1")) / 2; RouteDriverOpts drive; drive.speed = 26;
		auto drv = std::make_shared<RouteDriver>(game, vcar, V3(xm, 0, CityMap::CITY_MAXZ - 8), drive); vcar->ai = drv; m.blipEntity(voss, 0xff3030, "skull"); m.player().giveWeapon("rifle", 60);
		const auto road = StoryRoadNear(game, h.x, h.z + 20); SpawnOpts paint; paint.hasColor = true; paint.color = 0x111111; m.car("brawler", road.x, road.z, road.yaw, paint); game.missionMaxWanted = 1;
		m.objective("Chase <span class=\"r\">Voss</span> to the pier!"); co_await m.until([drv, vcar, voss]() { return drv->arrived || vcar->isWrecked() || voss->dead; });
		const V3 spot(xm, p.y, p.vals.at("z0") + 120);
		if (!voss->dead && voss->vehicle) { const V3 at(spot.x, 0, spot.z); vcar->takeOut(voss, &at); voss->setPosition(spot.x, spot.y, spot.z); }
		if (!ruiz->dead && ruiz->vehicle) { const V3 at(spot.x + 3, 0, spot.z - 4); vcar->takeOut(ruiz, &at); ruiz->setPosition(at.x, spot.y, at.z); }
		for (auto* cop : { voss, ruiz }) if (!cop->dead) { cop->brain = "gang"; cop->gang = "cuervos"; cop->threat = &m.player(); cop->setState("attack"); cop->persistent = true; }
		if (!voss->dead) { voss->giveWeapon("rifle", 999); voss->equip("rifle"); voss->health = voss->maxHealth = 400; voss->armor = 100; voss->accuracy = 0.55; }
		MissionEnemyOpts attack; attack.guard = false; const auto dirty = StoryCrew(m, "cuervos", StoryRing(spot.x, spot.z + 20, 4, 5), { "shotgun", "pistol", "smg", "pistol" }, attack); for (auto* d : dirty) d->setPosition(d->pos.x, spot.y, d->pos.z);
		m.objective("End it. Kill <span class=\"r\">Voss</span>."); co_await m.until([voss]() { return voss->dead; }); game.missionMaxWanted = NaN(); game.policeSys->clearWanted(); co_await m.wait(1.5);
		const double fx = spot.x, fz = spot.z + 150; mari->setPosition(fx - 2, spot.y, fz + 1.5); lou->setPosition(fx + 2, spot.y, fz + 1.2); co_await FadeTeleport(m, fx, fz, kPi, spot.y); game.env.setTime(19);
		co_await m.cutscene([&m, mari, lou, fx, fz, spot]() -> MissionTask {
			m.face(mari, &m.player()); m.face(lou, &m.player()); m.face(&m.player(), mari); m.shot(V3(fx + 12, spot.y + 4, fz - 10), V3(fx, spot.y + 1.5, fz), 45);
			co_await m.lines({ {"Marisol", "It's over. It's really over."}, {"Lou", "Voss, Deacon, Salazar. All gone. Cedar Row's free, man."} }); m.twoShot(&m.player(), mari, -1, 3.3);
			co_await m.lines({ {"Dre", "Tino used to sit right here and watch the sun go down. Said one day he'd own this whole city."}, {"Marisol", "So what now, big brother? You going back East?"}, {"Dre", "Nah. Somebody's got to keep an eye on Los Soles."} });
			m.shot(V3(fx - 25, spot.y + 18, fz + 60), V3(fx, spot.y + 25, fz - 80), 55); co_await m.wait(4);
		}); mari->missionKeep = false; game.hud->showCredits();
	}; story.push_back(std::move(finale));
}
} // namespace atg
