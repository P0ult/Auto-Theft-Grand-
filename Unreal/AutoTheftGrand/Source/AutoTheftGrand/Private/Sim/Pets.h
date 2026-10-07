// Pet adoption, following, commands, car rides and defensive bites (src/game/pets.js).
#pragma once
#include "Animal.h"
#include "Systems.h"

namespace atg {
class PetSystem : public System {
public:
	explicit PetSystem(Game& g);
	Game& game;
	std::shared_ptr<Animal> pet;
	struct Info { std::string breed, name; } info;
	bool stay = false;
	Ref<Character> target;
	std::map<std::string, std::shared_ptr<Animal>> remote;
	std::function<bool(const std::string&)> peerPresent; // multiplayer supplies this when it starts
	Animal* adopt(const std::string& breed, const std::string& name = "");
	void release();
	void command();
	void update(double dt) override;
	void animals(std::vector<Animal*>& out) const;
	void board(Vehicle* v);
	void alight(Vehicle* v);
	std::vector<double> netState() const;
	void poseRemote(const std::string& id, const std::vector<double>& st, double dt);
	void dropRemote(const std::string& id);
private:
	bool mourned = false;
	double deadT = 0;
	void think(Animal& a, double dt);
};
} // namespace atg
