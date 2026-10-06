#include "Setup.h"
#include "Combat.h"
#include "Effects.h"
#include "Game.h"
#include "Gameplay.h"
#include "Hud.h"
#include "NpcCrime.h"
#include "Peds.h"
#include "Pickups.h"
#include "Police.h"
#include "Rail.h"
#include "Roadblocks.h"
#include "Traffic.h"

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
	// (military, army: next)
	g.roadblocks = g.addSystem("roadblocks", std::make_unique<Roadblocks>(g));
	// (heists, weaponWheel, special, phone: later phases)
	g.rail = g.addSystem("rail", std::make_unique<RailSystem>(g));
	// (shops, wildlife, pets, skateparks, boats, shipRaid, missions, audio: later)
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
