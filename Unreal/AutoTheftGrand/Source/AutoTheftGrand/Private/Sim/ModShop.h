// Customs garages (port of src/game/modshop.js): drive in and upgrade whatever you're driving. Engine stages
// (more power and top speed), armour (more health, bullets do less), nitrous (Shift for a three-second shove,
// three bottles), a respray in the colour you pick, and the old Spray Shack service (fixed up, cops lose you).
// Upgrades live on the vehicle itself (Vehicle::mods, and its tuning multipliers).
#pragma once

#include "Systems.h"

namespace atg {

class Game;
class Marker;
class Vehicle;

struct EngineStage { double force, top; int price; };
struct ArmorLevel { double hp, bullet; int price; };
const std::vector<EngineStage>& EngineStages(); // (MOD_TABLES.ENGINE)
const std::vector<ArmorLevel>& ArmorLevels();   // (MOD_TABLES.ARMOR)

class ModShop : public System {
public:
	explicit ModShop(Game& g);
	Game& game;
	struct Garage { std::string name; double x, z; Marker* marker; };
	std::vector<Garage> garages;
	double boostT = 0, flameT = 0;

	static void Apply(Vehicle* v);
	static bool CanMod(const Vehicle* v);
	void open(const std::string& name);
	void update(double dt) override;

private:
	std::vector<StoreItem> rows(Vehicle* v);
};

} // namespace atg
