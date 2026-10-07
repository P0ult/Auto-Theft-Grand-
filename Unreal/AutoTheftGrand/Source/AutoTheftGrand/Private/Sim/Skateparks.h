// Skateparks (port of the Skateparks system in src/world/skatepark.js): concrete pads with a funbox, kickers and
// landings, a pyramid, banks, ledges and a rail. Boards lie about for anyone to pick up, and a few locals skate
// laps and throw tricks while you're near. The ramps are collision decks; their meshes (concrete and steel) are
// built here and drawn by the Unreal side.
#pragma once

#include "Loft.h"
#include "Systems.h"

namespace atg {

class Game;
class Ped;
class Vehicle;

class Skateparks : public System {
public:
	explicit Skateparks(Game& game);
	Game& game;
	struct Skater { Ref<Vehicle> v; Ref<Ped> ped; };
	struct Spot { double x, z, yaw; };
	struct Park {
		std::string key, name;
		double x, z, yaw, hx, hz, y;
		std::vector<Ref<Vehicle>> boards;
		std::vector<Skater> skaters;
		bool active = false;
		MeshBuf concrete, metal; // (the park's meshes in world coordinates: concrete, and the steel coping, rails and lights)
		std::vector<Spot> boardSpots;
		std::vector<std::array<double, 2>> loop; // the lap the locals skate
	};
	std::vector<Park> parks;
	double t = 0;
	void update(double dt) override;

private:
	void toWorld(const Park& p, double lx, double lz, double& x, double& z) const;
	void build(Park& p);
	void activate(Park& p);
	void spawnSkater(Park& p);
	void deactivate(Park& p);
};

} // namespace atg
