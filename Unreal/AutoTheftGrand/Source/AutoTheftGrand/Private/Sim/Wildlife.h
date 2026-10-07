// District-specific wildlife and walked dogs (src/game/wildlife.js). Animals keep the browser's
// population weights, spawning ranges, fear distances, flight, grazing, roadkill and cleanup rules.
#pragma once

#include "Animal.h"
#include "Systems.h"

namespace atg {

class Wildlife : public System, public IWildlife {
public:
	explicit Wildlife(Game& g);
	Game& game;
	std::vector<std::shared_ptr<Animal>> list;
	double spawnT = 0, max = 15;
	double count() const;
	void update(double dt) override;
	void addWalkedDog(Ped* owner) override;
	std::vector<Animal*> all() const override;
	void clear();
	void startFlee(Animal& a, double x, double z);
	void scare(const V3& pos, double radius);
	void roadkill();
	void spawnGroup(const V3& pp);
private:
	bool spot(double x, double z, const std::string& district, const std::string& breed, V3& out);
	void think(Animal& a, double dt, const V3& pp);
	void bark(Animal& a);
};

void FollowOwner(Animal& a, Character& owner, double dt, bool sit = false, double teleport = 70);

} // namespace atg
