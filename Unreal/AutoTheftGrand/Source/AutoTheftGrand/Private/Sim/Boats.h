// Boats in the world (port of src/game/boats.js): marinas with boats tied up (the Santa Luz pontoon, Port Morena,
// Port Hale, Lake Mirador and San Aurelio), pleasure boats and jet skis cruising the coast, and police boats that
// come after you when you're wanted out on the water (they ram, and the bow gunner opens up). The pontoons'
// meshes are built here and drawn by the Unreal side.
#pragma once

#include "Loft.h"
#include "Systems.h"
#include "Vehicle.h"

namespace atg {

class Boat;
class Game;

// drives a boat round a loop of points, or straight at a target (the police: ramming speed), feeling ahead for
// the shallows and for piers
class BoatDriver : public VehicleAI {
public:
	BoatDriver(Game& game, Vehicle* v, const std::vector<std::array<double, 2>>* pts, int idx, double pace = 0.6);
	Game& game;
	Vehicle* v;
	std::vector<std::array<double, 2>> pts;
	int idx;
	double pace;
	bool hasTarget = false; V3 target;
	bool ram = false;
	double stuckT = 0;
	void update(double dt) override;
};

class BoatSystem : public System {
public:
	explicit BoatSystem(Game& game);
	Game& game;
	struct Spot { double x, z, yaw; std::vector<std::string> types; bool police = false; };
	struct Marina { std::string name; double x, z; std::vector<Spot> spots; std::vector<Ref<Vehicle>> boats; bool active = false; };
	struct Route { std::string name; bool lake; std::vector<std::string> types; int n; std::vector<std::array<double, 2>> pts; std::vector<Ref<Vehicle>> boats; };
	std::vector<Marina> marinas;
	std::vector<Route> routes;
	std::vector<Ref<Vehicle>> police;
	std::vector<MeshBuf> pontoons; // (planks, floats and cleats, in world coordinates)
	double t = 0, policeT = 0;

	bool playerAtSea() const;
	void update(double dt) override;

private:
	void plan();
	void pontoon(double x0, double z0, double x1, double z1, double y);
	bool deep(double x, double z, double d = 1.5) const;
	V3 playerPos() const;
	void stream();
	void spawnCruiser(Route& r);
	void despawn(Vehicle* b);
	void policeUpdate(double dt);
	Vehicle* spawnPolice();
};

} // namespace atg
