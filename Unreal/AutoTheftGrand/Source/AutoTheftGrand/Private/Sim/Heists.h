// Armoured cash vans (port of src/game/heists.js, GTA V's Gruppe Sechs random event): every few minutes a
// Stockade does its rounds somewhere near you, a green $ on the radar. It's bullet resistant and the two guards
// inside are armed. Shoot the back doors (or blow them) and the cash spills out of the back; the guards bail out
// and fight, the van may make a run for it, and the alarm brings the police.
#pragma once

#include "Skeleton.h"
#include "Systems.h"

namespace atg {

class Game;
class Ped;
class Pickup;
struct Blip;

Appearance GuardLook(RNG& rng);

class Heists : public System {
public:
	explicit Heists(Game& game);
	Game& game;
	struct Cash { std::shared_ptr<Pickup> pk; double amount; bool got = false; };
	struct Van {
		Ref<Vehicle> v;
		std::vector<Ref<Ped>> guards;
		std::string state = "rounds";   // rounds, alerted, fleeing, open
		int hits = 0;
		std::vector<Cash> cash;
		double taken = 0, t = 0, outT = 0, still = 0;
		bool told = false, done = false;
	};
	std::unique_ptr<Van> van;
	double timer;
	bool enabled = true;
	int robbed = 0;

	void blipList(std::vector<Blip>& out) const;
	void update(double dt) override;
	void reset() override;
	bool spawn();

private:
	void alert();
	void guardsOut();
	void open();
	void finish();
};

} // namespace atg
