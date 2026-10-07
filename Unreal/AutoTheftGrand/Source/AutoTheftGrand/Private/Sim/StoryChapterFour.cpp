// Chapter IV: Rise (src/game/story.js, heist through kingpin).
#include "Story.h"
#include "Game.h"
#include "Hud.h"
#include "Police.h"

namespace atg {
void AddChapterFour(std::vector<MissionDef>& story) {
	MissionDef heist; heist.id = "heist"; heist.title = "Harbor Heist"; heist.contact = "R"; heist.requiresIds = { "hospital" }; heist.reward = 5000;
	heist.log = "Stole a truck full of Cuervo guns from Port Morena and delivered it to Rico's lock-up."; heist.start = story[5].start;
	heist.run = [](MissionContext& m, Game& game) -> MissionTask {
		const auto e = StoryPoint(game, "garage", "entry"); Ped* rico = m.ped(e.x + 2, e.z - 3, StoryCast("rico", true)); m.speakers = { {"Rico", rico}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, rico]() -> MissionTask {
			m.face(rico, &m.player()); m.face(&m.player(), rico); m.twoShot(&m.player(), rico, 1, 3.3);
			co_await m.lines({ {"Rico", "Heard about Lou. You want to hit back? Hit their wallet."}, {"Rico", "Cuervos run guns for the Vipers through the docks. Tonight a Boxer truck full of hardware is sitting at Port Morena."}, {"Rico", "Take it. Bring it here. The Vipers go to war with empty hands."} });
		});
		const V3 w = StoryLandmark(game, "warehouse"); SpawnOpts paint; paint.hasColor = true; paint.color = 0x1d3557; Vehicle* truck = m.car("boxer", w.x - 20, w.z + 38, kPi / 2, paint); truck->ai.reset(); truck->health = 1600;
		MissionEnemyOpts guard; guard.guard = true; const auto guards = StoryCrew(m, "cuervos", StoryRing(w.x - 20, w.z + 38, 6, 7), { "smg", "shotgun", "pistol", "rifle", "smg", "pistol" }, guard); AggroWhenNear(m, guards, 30);
		m.keepAlive(truck, "The truck was destroyed."); m.gps(truck->pos.x, truck->pos.z); co_await m.getIn(truck, "Steal the <span class=\"b\">weapons truck</span> at Port Morena.");
		for (int i = 0; i < 2; i++) { const auto r = StoryRoadNear(game, w.x - 100 - i * 60, w.z + 40 - i * 80); StoryChaseCar(m, RandPick(std::vector<std::string>{ "brawler", "summit" }), r.x, r.z, r.yaw, "cuervos", 1); }
		m.tick([&game, truck](double) { const std::string label = "TRUCK"; game.hud->setBar(&label, truck->health / 1600, "#7dff8a"); });
		const V3 garage = StoryLandmark(game, "garage"); GoToOpts go; go.vehicle = true; go.radius = 5; go.inCar = truck; go.text = "Deliver the truck to Rico's <span class=\"y\">lock-up</span>. Lose the cops first."; go.condition = [&game]() { return game.policeSys->level == 0; };
		co_await m.goTo(garage.x, garage.z, go); game.hud->setBar(nullptr); game.vehicles.exit(&m.player()); truck->locked = true; m.player().giveWeapon("rifle", 90);
		co_await m.say("Rico", "Whoa. Christmas came early. Take a rifle, on the house.", 4);
	}; story.push_back(std::move(heist));

	MissionDef vista; vista.id = "vistawood"; vista.title = "Vistawood Nights"; vista.contact = "Z"; vista.requiresIds = { "hospital" }; vista.reward = 5000;
	vista.log = "Pulled off a hills-to-pier stunt run for Hollywood producer Maddox \xE2\x80\x94 the client behind the Zenith theft.";
	vista.start = [](const CityMap& map) { const auto& g = map.landmarks.at("mansion").pts.at("gate"); return V3(g.x - 180, 0, g.z + 1); };
	vista.run = [](MissionContext& m, Game& game) -> MissionTask {
		const auto gate = StoryPoint(game, "mansion", "gate"); const V3 s(gate.x - 180, 0, gate.z + 1); Ped* mad = m.ped(s.x + 1.8, s.z - 1, StoryCast("maddox", true)); m.speakers = { {"Maddox", mad}, {"Dre", &m.player()} };
		const auto r = StoryRoadNear(game, s.x, s.z - 10); SpawnOpts paint; paint.hasColor = true; paint.color = 0xffd60a; Vehicle* car = m.car("zenith", r.x, r.z, r.yaw, paint);
		co_await m.cutscene([&m, mad]() -> MissionTask {
			m.face(mad, &m.player()); m.face(&m.player(), mad); m.twoShot(&m.player(), mad, 1, 3.3);
			co_await m.lines({ {"Maddox", "So YOU'RE the guy who jacked my Zenith! Relax, relax \xE2\x80\x94 Rico's client was me. I love it."}, {"Maddox", "I'm making a movie about this city and I need a stunt driver with... authenticity."}, {"Maddox", "From here to the Santa Luz Pier in a minute forty. Cameras are rolling. Don't scratch the car. Much."} });
		});
		co_await m.getIn(car, "Get in the <span class=\"b\">Zenith</span>."); game.cutscene = true; co_await StoryCountdown(m); game.cutscene = false;
		auto stop = m.timer(100, "Too slow. Maddox wanted it under a minute forty."); m.keepAlive(car, "You wrecked the star car.");
		GoToOpts go; go.vehicle = true; go.radius = 8; go.height = 5; go.text = "Stunt run: reach the checkpoints.";
		co_await m.goTo(XS[9], ZS[5], go); go.text.clear(); co_await m.goTo(XS[9], ZS[10], go);
		const auto& p = game.map.landmarks.at("pier"); go.radius = 7; go.text = "Finish on the <span class=\"y\">Santa Luz Pier</span>!";
		co_await m.goTo((p.vals.at("x0") + p.vals.at("x1")) / 2, p.vals.at("z0") + 30, go); stop();
		co_await m.say("Maddox", "(radio) CUT! Print it! That's the money shot, baby! Come by the hills anytime.", 4);
	}; story.push_back(std::move(vista));

	MissionDef streets; streets.id = "streets"; streets.title = "Taking Back the Streets"; streets.contact = "L"; streets.requiresIds = { "heist", "vistawood" }; streets.reward = 4000;
	streets.log = "Lou recovered. Together they cleared three Viper crews out of Cedar Row and Rosewood."; streets.start = story[1].start;
	streets.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 c = StoryLandmark(game, "court", 0, 12); Ped* lou = m.ped(c.x - 2, c.z - 2, StoryCast("lou", true)), *k1 = m.ped(c.x + 2, c.z - 2, StoryCast("king"));
		for (auto* k : { lou, k1 }) { k->giveWeapon("rifle", 999); k->equip("rifle"); } m.speakers = { {"Lou", lou}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, lou]() -> MissionTask {
			m.face(lou, &m.player()); m.face(&m.player(), lou); m.twoShot(&m.player(), lou, 1, 3.4);
			co_await m.lines({ {"Lou", "Doc says I'm too pretty to die. Now let's get our streets back."}, {"Lou", "Three Viper crews are holding corners in Cedar Row and Rosewood. We clear 'em, the neighborhood breathes again."} });
		}); lou->invincible = false; m.follower(lou, 0); m.follower(k1, 1); m.keepAlive(lou, "Lou died.");
		std::vector<const Block*> blocks; for (int i : game.map.blocks) { const auto& b = game.map.blockStore[i]; if ((b.district == "hood" || b.district == "westside") && b.special.empty()) blocks.push_back(&b); }
		RNG rng(99); const std::vector<const Block*> picks = { rng.Pick(blocks), rng.Pick(blocks), rng.Pick(blocks) }; std::vector<Ped*> all; MissionEnemyOpts guard; guard.guard = true; guard.health = 120;
		for (const auto* b : picks) { const auto crew = StoryCrew(m, "vipers", StoryRing(b->x0 + 2, b->z0 + 14, 4, 3), { "smg", "pistol", "shotgun", "pistol" }, guard); AggroWhenNear(m, crew, 30); all.insert(all.end(), crew.begin(), crew.end()); }
		CrewSupport(m, { lou, k1 }, [all]() { return all; }); co_await m.killAll(all, "Clear out the <span class=\"r\">Viper</span> crews.", "VIPERS");
		game.missions->gangDensity["vipers"] = 0.15; game.missions->gangDensity["kings"] = 0.5; co_await m.loseWanted();
		co_await m.say("Lou", "Cedar Row belongs to the Kings again. Now there's only one Viper left who matters.", 4);
	}; story.push_back(std::move(streets));

	MissionDef king; king.id = "kingpin"; king.title = "Kingpin"; king.contact = "L"; king.requiresIds = { "streets" }; king.reward = 10000;
	king.log = "Stormed the Salazar Estate in Vistawood Hills. The Vipers' boss is dead."; king.chapterEnd = { "CHAPTER V", "End of the Line" }; king.start = story[1].start;
	king.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 c = StoryLandmark(game, "court", 0, 12); Ped* lou = m.ped(c.x - 2, c.z - 2, StoryCast("lou", true)); m.speakers = { {"Lou", lou}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, lou]() -> MissionTask {
			m.face(lou, &m.player()); m.face(&m.player(), lou); m.twoShot(&m.player(), lou, -1, 3.4);
			co_await m.lines({ {"Lou", "Salazar's holed up in his mansion in Vistawood Hills. Guards, walls, the works."}, {"Lou", "Every Viper in this city answers to him. We cut the head off the snake, the body dies."}, {"Dre", "Then I'm going snake hunting."} });
		});
		const V3 mansion = StoryLandmark(game, "mansion"); auto points = StoryRing(mansion.x - 30, mansion.z - 20, 6, 6); const auto more = StoryRing(mansion.x, mansion.z + 9, 6, 8, 0.3); points.insert(points.end(), more.begin(), more.end());
		MissionEnemyOpts guard; guard.guard = true; guard.health = 130; guard.accuracy = 0.5; const auto guards = StoryCrew(m, "vipers", points, { "rifle", "smg", "shotgun", "pistol", "smg", "rifle" }, guard); AggroWhenNear(m, guards, 45);
		MissionEnemyOpts boss; boss.gang = "vipers"; boss.weapon = "shotgun"; boss.guard = true; boss.health = 450; boss.armor = 100; boss.hasAppearance = true; boss.appearance = StoryLook("salazar"); boss.blip = false;
		Ped* sal = m.enemy(mansion.x, mansion.z - 1, boss); m.blipEntity(sal, 0xff3030, "skull"); AggroWhenNear(m, { sal }, 25); m.wanted(1);
		const auto gate = StoryPoint(game, "mansion", "gate"); m.gps(gate.x, gate.z); m.objective("Kill <span class=\"r\">Salazar</span> at his Vistawood estate."); co_await m.until([sal]() { return sal->dead; });
		game.hud->subtitle("Salazar: Voss... will bury you... Castillo...", "Salazar", 3); co_await m.loseWanted();
		co_await m.say("Lou", "(phone) It's done? The Vipers are finished. Now it's just Deacon... and Voss.", 4);
	}; story.push_back(std::move(king));
}
} // namespace atg
