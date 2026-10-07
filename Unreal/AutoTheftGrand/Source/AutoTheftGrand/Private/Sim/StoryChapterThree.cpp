// Chapter III: Betrayal (src/game/story.js, evidence through hospital).
#include "Story.h"
#include "Game.h"
#include "Hud.h"
#include "Police.h"

namespace atg {
void AddChapterThree(std::vector<MissionDef>& story) {
	MissionDef evidence; evidence.id = "evidence"; evidence.title = "Evidence"; evidence.contact = "V"; evidence.requiresIds = { "familyties" }; evidence.reward = 3000;
	evidence.log = "Destroyed an LSPD evidence van for Voss. The evidence tied Voss to Tino's murder."; evidence.start = story[8].start;
	evidence.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 s = StoryLandmark(game, "hospital", -30, -4); Ped* voss = m.ped(s.x + 1.8, s.z - 1.2, StoryCast("voss", true)); m.speakers = { {"Voss", voss}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, voss]() -> MissionTask {
			m.face(voss, &m.player()); m.face(&m.player(), voss); m.overShoulder(&m.player(), voss);
			co_await m.lines({ {"Voss", "The DA's moving an evidence van from Central to the courthouse. In it: a gun with your brother's blood on it."}, {"Voss", "And, hypothetically, my fingerprints."}, {"Dre", "You killed Tino."}, {"Voss", "I said hypothetically. Blow the van. Or the next body they pull out of the ocean is your sister's."} });
			co_await m.say("Voss", "Here. A little something from the evidence locker.", 3);
		});
		m.player().giveWeapon("rpg", 4); m.player().switchTo("rpg"); const V3 p = StoryLandmark(game, "police", 0, -20); const auto r = StoryRoadNear(game, p.x, p.z);
		SpawnOpts paint; paint.hasColor = true; paint.color = 0xf2f2f2; Vehicle* van = m.car("parcel", r.x, r.z, r.yaw, paint); van->health = 2600;
		MissionPedOpts cop; cop.hasAppearance = true; cop.appearance = CopAppearance(); m.driver(van, cop);
		RouteDriverOpts drive; drive.speed = 15; drive.ignoreLights = false; auto drv = std::make_shared<RouteDriver>(game, van, StoryLandmark(game, "tower", 0, 10), drive); van->ai = drv;
		Vehicle* escort = m.car("police", r.x - 12 * std::sin(r.yaw), r.z - 12 * std::cos(r.yaw), r.yaw); cop.appearance = CopAppearance(); m.driver(escort, cop);
		escort->ai = std::make_shared<RouteDriver>(game, escort, StoryLandmark(game, "tower", 0, 20), drive); escort->sirenOn = true; m.blipEntity(van, 0xff3030, "target");
		m.failIf([drv, van]() { return drv->arrived && !van->isWrecked(); }, "The evidence reached the courthouse.");
		m.objective("Destroy the <span class=\"r\">evidence van</span> before it reaches the courthouse."); m.help("The <b>Rocket Launcher</b> is selected. Aim with right mouse, fire with left. Watch your distance!", 7);
		co_await m.until([van]() { return van->isWrecked(); }); game.policeSys->setLevel(3); co_await m.loseWanted("Lose the cops.");
		co_await m.say("Dre", "(to himself) Voss killed Tino. And I just burned the proof. Not for long, Voss.", 4);
	}; story.push_back(std::move(evidence));

	MissionDef beach; beach.id = "beachparty"; beach.title = "Beach Party"; beach.contact = "L"; beach.requiresIds = { "evidence" }; beach.reward = 3000;
	beach.log = "Crashed Chino's party at the Santa Luz Pier. Chino, the triggerman in Tino's shooting, is dead."; beach.start = story[9].start;
	beach.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 s = StoryLandmark(game, "home", 4, 2); Ped* lou = m.ped(s.x + 1.6, s.z - 1, StoryCast("lou", true)); m.speakers = { {"Lou", lou}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, lou]() -> MissionTask {
			m.face(lou, &m.player()); m.face(&m.player(), lou); m.twoShot(&m.player(), lou, 1, 3.4);
			co_await m.lines({ {"Lou", "Chino. Salazar's right hand. He was the shooter in Tino's drive-by \xE2\x80\x94 three different people told me."}, {"Lou", "He throws a party on the Santa Luz Pier every Friday. Guess what day it is."} });
		});
		game.env.setTime(Max(18.4, game.env.hours)); const auto& p = game.map.landmarks.at("pier"); const V3 party((p.vals.at("x0") + p.vals.at("x1")) / 2, p.y, p.vals.at("z0") + 180);
		MissionEnemyOpts boss; boss.gang = "vipers"; boss.weapon = "smg"; boss.guard = true; boss.health = 180; boss.hasAppearance = true; boss.appearance = StoryLook("chino"); boss.blip = false;
		Ped* chino = m.enemy(party.x, party.z, boss); m.blipEntity(chino, 0xff3030, "target");
		MissionEnemyOpts guard; guard.guard = true; const auto guests = StoryCrew(m, "vipers", StoryRing(party.x, party.z, 5, 5), { "pistol", "bat", "pistol", "smg", "fist" }, guard);
		chino->setPosition(chino->pos.x, party.y, chino->pos.z); for (auto* g : guests) g->setPosition(g->pos.x, party.y, g->pos.z);
		SpawnOpts paint; paint.hasColor = true; paint.color = 0xc1121f; Vehicle* getaway = m.car("zenith", p.vals.at("x0") + 6, p.vals.at("z0") + 44, 0, paint);
		bool fleeing = false;
		m.tick([&m, &game, chino, guests, getaway, &fleeing](double) {
			const V3 pp = m.player().vehicle ? m.player().vehicle->pos : m.player().pos;
			if (!fleeing && !chino->dead && (Hypot(chino->pos.x - pp.x, chino->pos.z - pp.z) < 35 || chino->health < chino->maxHealth)) {
				fleeing = true; for (auto* g : guests) { g->threat = &m.player(); g->setState("attack"); }
				chino->brain = "script"; chino->scriptThink = [&game, chino, getaway](double dt) {
					if (!chino->vehicle && !game.vehicles.isBusy(chino)) { const V3 dp = getaway->doorWorld(); if (chino->goTo(dp.x, dp.z, 6, dt, 1.2)) game.vehicles.enter(chino, getaway, 0, true); }
				}; game.hud->subtitle("Chino: It's Castillo! Get him! I'm out of here!", "Chino", 3);
			}
			if (chino->vehicle == getaway && !getaway->ai) { RouteDriverOpts o; o.speed = 30; o.flee = true; getaway->ai = std::make_shared<RouteDriver>(game, getaway, V3(), o); }
		});
		m.gps(party.x, party.z); m.objective("Kill <span class=\"r\">Chino</span> at the pier party."); co_await m.until([chino]() { return chino->dead; }); co_await m.loseWanted();
		co_await m.say("Lou", "(phone) Chino's done? ...Good. That's one. Tino can rest a little easier tonight.", 4);
	}; story.push_back(std::move(beach));

	MissionDef tail; tail.id = "tail"; tail.title = "Snake in the Grass"; tail.contact = "L"; tail.requiresIds = { "beachparty" }; tail.reward = 1000;
	tail.log = "Tailed Deacon to Pershing Plaza: he was working with Voss and Salazar \xE2\x80\x94 and set Tino up."; tail.start = story[9].start;
	tail.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 s = StoryLandmark(game, "home", 4, 2); Ped* lou = m.ped(s.x + 1.6, s.z - 1, StoryCast("lou", true)); m.speakers = { {"Lou", lou}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, lou]() -> MissionTask {
			m.face(lou, &m.player()); m.face(&m.player(), lou); m.twoShot(&m.player(), lou, -1, 3.2);
			co_await m.lines({ {"Lou", "Mari says the kidnappers talked about Deacon paying for her. Deacon's been driving a brand new Summit, too."}, {"Lou", "He's leaving the projects right now. Follow him. Don't let him see you."} });
		});
		const V3 h = StoryLandmark(game, "home"), proj = StoryLandmark(game, "projects", 40, -50), plaza = StoryLandmark(game, "plaza"); const auto r = StoryRoadNear(game, proj.x, proj.z);
		m.car("meridian", h.x + 6, h.z + 22, kPi / 2); SpawnOpts paint; paint.hasColor = true; paint.color = 0x111111;
		Vehicle* dcar = m.car("summit", r.x, r.z, r.yaw, paint); Ped* deacon = m.ped(r.x, r.z, StoryCast("deacon", true)); dcar->putIn(deacon);
		RouteDriverOpts o; o.speed = 14; o.ignoreLights = true; auto drv = std::make_shared<RouteDriver>(game, dcar, plaza + V3(0, 0, 40), o); dcar->ai.reset();
		m.blipEntity(dcar, 0x4aa3ff, "car"); m.objective("Follow <span class=\"b\">Deacon</span>. Keep your distance.");
		UntilOpts start; start.timeout = 60; start.resolveTimeout = true; co_await m.until([&m, deacon]() { return m.distTo(deacon) < 90 || m.player().vehicle; }, start); dcar->ai = drv;
		double close = 0;
		m.tick([&m, &game, deacon, &close](double dt) {
			const double d = m.distTo(deacon); close = d < 14 ? close + dt : Max(0, close - dt);
			const std::string label = d < 25 ? "TOO CLOSE!" : d > 110 ? "LOSING HIM" : "DISTANCE";
			game.hud->setBar(&label, 1 - Clamp((d - 10) / 140, 0, 1), d < 25 ? "#ff5252" : d > 110 ? "#ffd23f" : "#7fb6ff");
		});
		m.failIf([&m, deacon]() { return m.distTo(deacon) > 150; }, "You lost Deacon."); m.failIf([&close]() { return close > 2.5; }, "Deacon spotted you.");
		bool spotted = false; std::weak_ptr<MissionContext> weak = m.shared_from_this();
		const int off1 = game.events.carCrash.on([weak, dcar, &spotted](Vehicle* a, Vehicle* b, double) { if (auto ctx = weak.lock()) if (auto* pv = ctx->player().vehicle) if ((a == dcar && b == pv) || (b == dcar && a == pv)) spotted = true; });
		const int off2 = game.events.vehicleShot.on([weak, dcar, &spotted](Vehicle* v, Character* sh, V3) { if (auto ctx = weak.lock()) if (v == dcar && sh == &ctx->player()) spotted = true; });
		const int off3 = game.events.gunshot.on([weak, deacon, &spotted](Character* sh, V3, const std::string&) { if (auto ctx = weak.lock()) if (sh == &ctx->player() && ctx->distTo(deacon) < 60) spotted = true; });
		const auto unsubscribe = [&game, off1, off2, off3]() { game.events.carCrash.off(off1); game.events.vehicleShot.off(off2); game.events.gunshot.off(off3); };
		m.onCleanup(unsubscribe); m.failIf([&spotted]() { return spotted; }, "Deacon noticed you."); co_await m.until([drv]() { return drv->arrived; }); unsubscribe(); game.hud->setBar(nullptr);
		Ped* voss = m.ped(plaza.x + 2, plaza.z + 3, StoryCast("voss", true)), *sal = m.ped(plaza.x - 1.5, plaza.z + 3.5, StoryCast("salazar", true));
		game.vehicles.exit(deacon); co_await m.wait(1.5); deacon->brain = "script"; deacon->setPosition(plaza.x, plaza.z + 1);
		m.speakers = { {"Voss", voss}, {"Salazar", sal}, {"Deacon", deacon}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, plaza, voss, sal, deacon]() -> MissionTask {
			m.face(deacon, voss); m.face(voss, deacon); m.face(sal, deacon); m.shot(plaza + V3(14, 6, 16), plaza + V3(0, 1.5, 2), 40);
			co_await m.say("Salazar", "Castillo is becoming a problem, Deacon. Chino is dead. My stash house, my van..."); m.twoShot(deacon, sal, 1, 3.5);
			co_await m.say("Deacon", "Dre's not like Tino. Give me time, he'll fall in line."); m.overShoulder(deacon, voss);
			co_await m.say("Voss", "He'd better. Our little arrangement needs Cedar Row quiet. Tino wouldn't stay quiet either. Remember how that ended?"); m.twoShot(deacon, voss, -1, 3.2);
			co_await m.lines({ {"Deacon", "...I set Tino up for you. I'm not doing that to Dre."}, {"Salazar", "You'll do what you're told. Friday night we hit Cedar Row and end the Kings for good."} });
			const V3 pp = m.player().pos; m.shot(pp + V3(3, 2.2, 3), pp + V3(0, 1.5, 0), 45); co_await m.say("Dre", "Deacon... you snake. You sold out my brother.", 4);
		}); voss->remove(); sal->remove(); deacon->remove(); dcar->remove();
	}; story.push_back(std::move(tail));

	MissionDef ambush; ambush.id = "ambush"; ambush.title = "Ambush"; ambush.contact = "L"; ambush.autoStart = true; ambush.requiresIds = { "tail" }; ambush.reward = 2000;
	ambush.log = "Survived the Vipers' assault on Cedar Row. Lou was shot defending the block."; ambush.start = story[9].start;
	ambush.run = [](MissionContext& m, Game& game) -> MissionTask {
		game.env.setTime(21.5); const V3 h = StoryLandmark(game, "home"); co_await FadeTeleport(m, h.x + 2, h.z + 4, kPi);
		Ped* lou = m.ped(h.x + 4, h.z + 5, StoryCast("lou", true)); std::vector<Ped*> kings;
		for (int i = 0; i < 3; i++) kings.push_back(m.ped(h.x - 4 + i * 4, h.z + 8, StoryCast("king")));
		std::vector<Ped*> crew = kings; crew.insert(crew.begin(), lou); for (auto* k : crew) { k->giveWeapon("smg", 999); k->equip("smg"); }
		m.player().giveWeapon("smg", 96); m.speakers = { {"Lou", lou}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, lou]() -> MissionTask {
			m.face(lou, &m.player()); m.face(&m.player(), lou); m.twoShot(&m.player(), lou, 1, 3.6);
			co_await m.lines({ {"Lou", "Deacon sold out TINO?! To Voss and the Vipers?!"}, {"Dre", "And they're coming tonight. For all of us."}, {"Lou", "Then let 'em come. KINGS! Get strapped! Nobody takes the Row!"} });
		}); lou->invincible = false;
		for (auto* k : crew) { k->brain = "gang"; k->gang = "kings"; k->state = "guard"; k->hasGuardFace = true; k->guardFace = kPi / 2; }
		std::vector<Ped*> enemies; CrewSupport(m, crew, [&enemies]() { return enemies; });
		m.tick([crew](double) { for (auto* k : crew) if (!k->dead && k->threat && !k->threat->dead) k->setState("attack"); else if (!k->dead && k->state == "attack") k->setState("guard"); }); game.missionMaxWanted = 0;
		struct Wave { int n; double x, z; std::vector<std::string> weapons; bool car; };
		const std::vector<Wave> waves = { {5,-60,25,{"pistol","bat","pistol","smg","knife"},false}, {6,65,22,{"smg","pistol","shotgun","pistol","smg","pistol"},false}, {7,0,-70,{"smg","shotgun","rifle","pistol","smg","pistol","shotgun"},true} };
		for (size_t i = 0; i < waves.size(); i++) {
			const auto& w = waves[i]; game.hud->bigMessage("WAVE " + std::to_string(i + 1), "hint", 2);
			MissionEnemyOpts attack; attack.guard = false; const auto wave = StoryCrew(m, "vipers", StoryRing(h.x + w.x, h.z + w.z, w.n, 4), w.weapons, attack);
			for (auto* e : wave) { e->threat = &m.player(); e->setState("attack"); }
			if (w.car) { const auto r = StoryRoadNear(game, h.x + 80, h.z + 20); StoryChaseCar(m, "brawler", r.x, r.z, r.yaw, "vipers", 1); }
			enemies.insert(enemies.end(), wave.begin(), wave.end()); co_await m.killAll(wave, "Defend <span class=\"y\">Cedar Row</span>! Kill the <span class=\"r\">Vipers</span>.", "ATTACKERS"); co_await m.wait(2);
		}
		game.missionMaxWanted = NaN(); lou->health = 5; lou->knockDown(V3(0, 1, -2)); co_await m.wait(1.2); co_await m.say("Lou", "Argh! I'm hit... Dre... I'm hit bad...", 3);
	}; story.push_back(std::move(ambush));

	MissionDef hospital; hospital.id = "hospital"; hospital.title = "Rush to All Saints"; hospital.contact = "L"; hospital.autoStart = true; hospital.requiresIds = { "ambush" }; hospital.reward = 2000;
	hospital.log = "Raced a bleeding Lou to All Saints General with Vipers on the bumper. He pulled through."; hospital.chapterEnd = { "CHAPTER IV", "Rise" }; hospital.start = story[9].start;
	hospital.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 h = StoryLandmark(game, "home"); const auto r = StoryRoadNear(game, h.x, h.z + 20); SpawnOpts paint; paint.hasColor = true; paint.color = 0xf2b705;
		Vehicle* car = m.car("summit", r.x, r.z, r.yaw, paint); Ped* lou = m.ped(h.x + 3, h.z + 6, StoryCast("lou")); lou->health = 40; lou->invincible = true;
		m.speakers = { {"Lou", lou}, {"Dre", &m.player()} }; m.objective("Get <span class=\"b\">Lou</span> into the car!"); m.follower(lou, 0); co_await m.getIn(car);
		UntilOpts board; board.timeout = 12; board.resolveTimeout = true; co_await m.until([lou, car]() { return lou->vehicle == car; }, board); if (lou->vehicle != car) car->putIn(lou, 1);
		auto stop = m.timer(150, "Lou didn't make it..."); m.keepAlive(car, "The car was destroyed. Lou didn't make it.");
		m.failIf([&m, &game, car]() { return m.player().vehicle != car && !game.vehicles.isBusy(&m.player()); }, "You abandoned Lou.");
		const auto r2 = StoryRoadNear(game, h.x - 80, h.z); StoryChaseCar(m, "brawler", r2.x, r2.z, r2.yaw, "vipers", 1);
		std::weak_ptr<MissionContext> weak = m.shared_from_this();
		game.setTimeout(25, [weak, &game]() { if (auto ctx = weak.lock()) if (game.missions->active == ctx) { const V3 p = StoryLandmark(game, "hospital", -120, 200); const auto at = StoryRoadNear(game, p.x, p.z); StoryChaseCar(*ctx, "kestrel", at.x, at.z, at.yaw, "vipers", 1); } });
		game.missionMaxWanted = 0;
		game.setTimeout(2, [&game]() { game.hud->subtitle("Stay with me, big man. Stay with me!", "Dre", 3); });
		game.setTimeout(16, [&game]() { game.hud->subtitle("Tell Mari... tell her the lasagna recipe is in the... blue book...", "Lou", 4); });
		const V3 dest = StoryLandmark(game, "hospital"); GoToOpts go; go.vehicle = true; go.radius = 5; go.text = "Get Lou to <span class=\"y\">All Saints General</span>!"; go.inCar = car;
		co_await m.goTo(dest.x, dest.z + 4, go); stop(); game.missionMaxWanted = NaN(); car->input.brake = 1;
		co_await m.say("Lou", "Man... you drive like a maniac. Thanks, D.", 3); game.vehicles.exit(lou);
	}; story.push_back(std::move(hospital));
}
} // namespace atg
