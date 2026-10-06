// Combat (port of src/game/combat.js): hitscan firing with spread and pellets, melee hits, explosions,
// rockets, homing missiles, tank shells, grenades and molotovs with their pools of burning fuel. The
// projectiles are plain state (position, velocity, kind) for the renderer to draw.
#pragma once

#include "Systems.h"
#include "VehicleDefs.h"
#include "Weapons.h"

namespace atg {

class Game;
struct CollObj;

// how big a vehicle goes up, relative to a family car (by mass; aircraft carry far more fuel)
double BlastScale(const VehicleDef& def);

struct CombatHit {
	double t = 0;
	enum Kind { Static, Char, Vehicle_ } kind = Static;
	CollObj* obj = nullptr;
	Character* ch = nullptr;
	Vehicle* veh = nullptr;
	std::string part;
	int particle = -1;
	V3 normal, point;
};

class Combat : public System, public ICombat {
public:
	explicit Combat(Game& game);
	Game& game;
	bool visualOnly = false; // (another player's vehicle blowing up online: their client deals the damage)

	struct Projectile {
		std::string type;   // rocket, grenade, molotov
		std::string kind;   // (rockets) rocket, missile, shell
		V3 pos, vel;
		double t = 0;
		Ref<Character> owner;
		double radius = 8, damage = 220, gravity = 0, life = 5, turn = 0, accel = 0, maxSpeed = 0, spin = 0;
		double scale = 1; // (the mesh: 1.8 for rockets from vehicles)
		Ref<Vehicle> target;
	};
	std::vector<Projectile> projectiles;
	struct Fire { double x, y, z, r, t, life; Ref<Character> owner; double tick; };
	std::vector<Fire> fires;

	bool raycast(double ox, double oy, double oz, double dx, double dy, double dz, double maxT, Character* exclude, CombatHit& out);
	void fireWeapon(Character* shooter, const WeaponDef& def, const V3& origin, const V3& aimDir, const FireOpts& opts = FireOpts()) override;
	bool fireWeaponHit(Character* shooter, const WeaponDef& def, const V3& origin, const V3& aimDir, const FireOpts& opts = FireOpts());
	void applyHit(const CombatHit& hit, const WeaponDef& def, Character* shooter, const V3& dir, double shooterDamageMul = NaN());
	void meleeHit(Character* attacker, const std::string& act) override;
	bool melee(Character* attacker, const std::string& act);
	void vehicleExplosion(Vehicle* v, const V3& pos) override;
	struct ExplosionOpts { double size = NaN(); bool hasFoot = false; double foot[4] = { 0, 0, 0, 1 }; };
	void explosion(const V3& pos, double radius, double damage, Character* source = nullptr, Vehicle* excludeVehicle = nullptr, const ExplosionOpts& opts = ExplosionOpts());
	void fireRocket(Character* shooter, const V3& origin, const V3& aimDir);
	void vehicleGun(Character* shooter, const V3& muzzle, const V3& dir, const WeaponDef& def);
	struct ProjectileOpts { double speed = 80; bool hasInherit = false; V3 inherit; double radius = 8, damage = 220, gravity = 0, life = 5, turn = 0, accel = 0, maxSpeed = 0; Vehicle* target = nullptr; };
	void fireProjectile(Character* shooter, const std::string& kind, const V3& pos, const V3& dir, const ProjectileOpts& opts = ProjectileOpts());
	Vehicle* lockTarget(const V3& from, const V3& dir, Vehicle* exclude = nullptr, double maxDist = 1000);
	void throwGrenade(Character* thrower, const V3& dir, const std::string& kind) override;
	void ignite(const V3& pos, Character* owner, double r = 4.2, double life = 9);
	void update(double dt) override;

private:
	void updateFires(double dt);
};

} // namespace atg
