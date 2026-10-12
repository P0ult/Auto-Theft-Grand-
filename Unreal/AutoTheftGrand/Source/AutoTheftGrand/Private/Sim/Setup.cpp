#include "Setup.h"
#include "Net.h"
#include "FreeRoam.h"
#include "ModShop.h"
#include "Properties.h"
#include "Races.h"
#include "SaveSystem.h"
#include "Special.h"
#include "Army.h"
#include "Audio.h"
#include "Boats.h"
#include "Combat.h"
#include "Effects.h"
#include "Game.h"
#include "Gameplay.h"
#include "Heists.h"
#include "Military.h"
#include "Missions.h"
#include "Hud.h"
#include "NpcCrime.h"
#include "Peds.h"
#include "Pets.h"
#include "Phone.h"
#include "Pickups.h"
#include "Police.h"
#include "Rail.h"
#include "Roadblocks.h"
#include "ShipRaid.h"
#include "Shops.h"
#include "Skateparks.h"
#include "Story.h"
#include "TaxiSystem.h"
#include "Traffic.h"
#include "Vigilante.h"
#include "WeaponWheel.h"
#include "Wildlife.h"

namespace atg {

void InstallSystems(Game& g) {
	auto hud = std::make_unique<HudModel>(g);
	g.hudModel = hud.get();
	g.hud = hud.get();
	g.ownedHud = std::move(hud);
	g.effects = g.addSystem("effects", std::make_unique<Effects>(g));
	g.combat = g.addSystem("combat", std::make_unique<Combat>(g));
	g.peds = g.addSystem("peds", std::make_unique<PedManager>(g));
	g.traffic = g.addSystem("traffic", std::make_unique<Traffic>(g));
	g.policeSys = g.addSystem("police", std::make_unique<Police>(g));
	g.police = g.policeSys;
	g.npcCrime = g.addSystem("npcCrime", std::make_unique<NpcCrime>(g));
	g.pickupsSys = g.addSystem("pickups", std::make_unique<Pickups>(g));
	g.pickups = g.pickupsSys;
	g.military = g.addSystem("military", std::make_unique<Military>(g));
	g.army = g.addSystem("army", std::make_unique<Army>(g));
	g.roadblocks = g.addSystem("roadblocks", std::make_unique<Roadblocks>(g));
	g.addSystem("heists", std::make_unique<Heists>(g));
	g.wheel = g.addSystem("weaponWheel", std::make_unique<WeaponWheel>(g));
	g.special = g.addSystem("special", std::make_unique<Special>(g));
	g.phone = g.addSystem("phone", std::make_unique<Phone>(g));
	g.rail = g.addSystem("rail", std::make_unique<RailSystem>(g));
	g.shops = g.addSystem("shops", std::make_unique<ShopSystem>(g));
	g.wildlife = g.addSystem("wildlife", std::make_unique<Wildlife>(g));
	g.pets = g.addSystem("pets", std::make_unique<PetSystem>(g));
	g.addSystem("skateparks", std::make_unique<Skateparks>(g));
	g.addSystem("boats", std::make_unique<BoatSystem>(g));
	g.shipRaid = g.addSystem("shipRaid", std::make_unique<ShipRaid>(g));
	g.missions = g.addSystem("missions", std::make_unique<Missions>(g));
	g.missions->story = BuildStory();
	g.audioSys = g.addSystem("audio", std::make_unique<Audio>(g));
	g.audio = g.audioSys;
	g.gameplay = g.addSystem("gameplay", std::make_unique<Gameplay>(g));
	// main.js: freeroam, admin, taxi, modshop, races, vigilante, properties (the admin tools and taxis come later)
	g.freeroam = g.addSystem("freeroam", std::make_unique<FreeRoam>(g));
	g.taxi = g.addSystem("taxi", std::make_unique<TaxiSystem>(g));
	g.taxiSystem = g.taxi;
	g.modShop = g.addSystem("modshop", std::make_unique<ModShop>(g));
	g.races = g.addSystem("races", std::make_unique<RaceSystem>(g));
	g.vigilante = g.addSystem("vigilante", std::make_unique<Vigilante>(g));
	g.properties = g.addSystem("properties", std::make_unique<Properties>(g));
	g.saveSys = g.addSystem("save", std::make_unique<SaveSystem>(g)); // (game.save: not a system in main.js; it has no frame work)
	g.missions->autosave = [&g]() { if (g.saveSys) g.saveSys->save(true); };
	g.pickupsSys->refreshPackages();
	// multiplayer (main.js: game.net, outside the systems list: it ticks on real time after the update)
	auto net = std::make_unique<NetSystem>(g);
	g.net = net.get();
	g.ownedSystems.push_back(std::move(net));
	if (g.pets) g.pets->peerPresent = [&g](const std::string& id) { return g.net && g.net->online() && g.net->peers.count(id) > 0; };
}

void StartGame(Game& g) {
	g.player->visible = true;
	g.rig.clearCinematic();
	g.env.timeScale = 1;
	if (g.gameplay) g.gameplay->state = "playing";
	g.input.enabled = true;
}

void PopulateWorld(Game& g) {
	if (g.traffic) g.traffic->populate((int)std::floor(g.traffic->maxCars * 0.7));
	if (g.peds) g.peds->populate((int)std::floor(g.peds->maxPeds * 0.6));
}

void StartJourney(Game& g, const std::string& mode) {
	Player& p = *g.player;
	const auto it = g.map.landmarks.find("home");
	if (it == g.map.landmarks.end()) return;
	const Landmark& home = it->second;
	if (mode == "continue" && g.saveSys && g.saveSys->load()) {
		p.setPosition(home.x, home.z);
		p.setYaw(0);
		g.rig.yaw = kPi;
		if (g.missions) g.missions->refreshContacts();
		if (g.hud) g.hud->help("Welcome back to Los Soles.", 4);
		return;
	}
	if (mode == "free") {
		p.setPosition(home.x, home.z);
		for (const auto& wa : FreeRoamKit()) p.giveWeapon(wa.first, wa.second);
		p.equip("pistol");
		g.freeRoam = true;
		g.env.setTime(17.5);
		if (g.missions) { g.missions->completed.clear(); for (const MissionDef& m : g.missions->story) g.missions->completed.insert(m.id); }
		if (g.freeroam) g.freeroam->update(0);
		if (g.hud) g.hud->help("Free roam: every weapon, unlimited cash and ammo. Press <b>T</b> to teleport anywhere. Cause some chaos!", 8);
		return;
	}
	if (mode == "story" && g.missions) {
		p.money = 250; // (main.js startGame: a new game starts with $250)
		g.gangDensity = { { "kings", 0.35 }, { "vipers", 0.4 }, { "cuervos", 0.3 } };
		g.missions->start("welcome");
	}
}

} // namespace atg
