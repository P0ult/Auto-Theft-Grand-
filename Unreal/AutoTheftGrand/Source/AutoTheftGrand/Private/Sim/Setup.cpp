#include "Setup.h"
#include "Game.h"
#include "Peds.h"
#include "Rail.h"
#include "Traffic.h"

namespace atg {

void InstallSystems(Game& g) {
	// (effects and combat come first in main.js; they arrive with phase 3)
	g.peds = g.addSystem("peds", std::make_unique<PedManager>(g));
	g.traffic = g.addSystem("traffic", std::make_unique<Traffic>(g));
	// (police, npcCrime, pickups, military, army, roadblocks, heists, weaponWheel, special, phone: later phases)
	g.rail = g.addSystem("rail", std::make_unique<RailSystem>(g));
}

void PopulateWorld(Game& g) {
	if (g.traffic) g.traffic->populate((int)std::floor(g.traffic->maxCars * 0.7));
	if (g.peds) g.peds->populate((int)std::floor(g.peds->maxPeds * 0.6));
}

} // namespace atg
