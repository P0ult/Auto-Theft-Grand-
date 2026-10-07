#include "Setup.h"
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
#include "Hud.h"
#include "NpcCrime.h"
#include "Peds.h"
#include "Pets.h"
#include "Phone.h"
#include "Pickups.h"
#include "Police.h"
#include "Rail.h"
#include "Roadblocks.h"
#include "Shops.h"
#include "Skateparks.h"
#include "Traffic.h"
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
	// (shipRaid, missions: later)
	g.audioSys = g.addSystem("audio", std::make_unique<Audio>(g));
	g.audio = g.audioSys;
	g.gameplay = g.addSystem("gameplay", std::make_unique<Gameplay>(g));
	g.pickupsSys->refreshPackages();
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

} // namespace atg
