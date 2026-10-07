// A person (port of src/entities/character.js): the procedural animator, a ragdoll, simple kinematic
// physics against the collision world, weapons, damage and death. The player and the pedestrians build
// on it. Bones, weapon and seat transforms are kept here so the renderer only has to copy them.
#pragma once

#include "Animator.h"
#include "Ragdoll.h"
#include "Weapons.h"

namespace atg {

class Game;
class Character;
class Vehicle;
class Animal;
struct CollObj;

struct DamageInfo {
	std::string part;      // head, torso, limb (bullets), or empty
	double headMul = NaN();
	std::string type;      // bullet, melee, explosion, fire, fall, drown, vehicle, ...
	Character* source = nullptr;
	Animal* animalSource = nullptr;
	bool knockdown = false;
	bool hasImpulse = false; V3 impulse;
	bool hasHitPoint = false; V3 hitPoint;
	std::string weapon;
};

struct WeaponSlot { double ammo = 0, clip = 0; };   // (melee weapons: infinite)

class Character : public std::enable_shared_from_this<Character> {
public:
	Character(Game& game, const Appearance& look, double health = 100, double armor = 0, const std::string& team = "civilian");
	virtual ~Character() = default;

	Game& game;
	int uid; // (character.js id)
	Appearance appearance;
	V3 rest[Bone::COUNT];
	Pose pose;
	std::unique_ptr<Animator> anim;
	std::unique_ptr<Ragdoll> ragdoll;

	V3 pos, vel;
	double yaw = 0;
	double moveTargetX = 0, moveTargetZ = 0;
	bool grounded = true, swimming = false;
	double health, maxHealth, armor;
	bool dead = false, ragdolling = false;
	double downTime = 0;
	double radius = 0.32, height;
	std::string team;
	std::map<std::string, WeaponSlot> weapons;
	std::string weapon = "fist";
	bool weaponVisible = true;      // (hidden while swimming, in seats with melee weapons, ...)
	Vehicle* vehicle = nullptr;     // (the vehicle keeps a strong reference to its occupants)
	int seat = -1;
	bool isPlayer = false;
	bool aiming = false;
	double aimPitch = 0;
	bool hasAimDir = false; V3 aimDir;
	bool hasAimYaw = false; double aimYaw = 0;
	bool crouching = false;
	AnimState animState;
	Ref<Character> lastDamager;
	Ref<Animal> lastAnimalDamager;
	double lastHitTime = -10;
	bool removed = false, visible = true, invincible = false;
	bool hiddenInVehicle = false;
	double onFire = 0;             // (seconds still burning: combat.js fires) // (inside a train or a plane's cabin: not drawn)
	double protectUntil = 0;
	bool remote = false, npcProxy = false;   // (multiplayer avatars)
	// in a vehicle: the body's root relative to the vehicle's body (position, Euler XYZ)
	V3 rootLocalPos, rootLocalRot;
	bool hasSeatPos = false; V3 seatPos;
	double leanK = 0; bool leaning = false;
	double lastYaw = NaN();
	bool hasGoodPos = false; V3 goodPos;
	double airAccel = 0;
	double runOverT = -1;
	Vehicle* bailFlee = nullptr; // (crawled out of a crashed car: get clear of it)
	// the weapon's orientation in the right hand (for the renderer and the muzzle)
	Quat weaponLocalRot;

	void setPosition(double x, double y, double z);
	void setPosition(double x, double z);
	void setYaw(double y) { yaw = y; }
	V3 forward() const { return { std::sin(yaw), 0, std::cos(yaw) }; }
	V3 headPos() const { return { pos.x, pos.y + height * 0.93, pos.z }; }
	V3 chestPos() const { return { pos.x, pos.y + height * 0.72, pos.z }; }

	void giveWeapon(const std::string& id, double ammo = 0);
	virtual void equip(const std::string& id);
	bool hasWeaponModel() const;
	V3 muzzleWorld();
	const WeaponDef& weaponDef() const;
	std::string holdType() const;

	// the body's root (the renderer draws the mesh with this, scaled by the height) and bone world matrices
	M4 rootMatrix() const;
	void updatePose() { pose.Update(rootMatrix()); }
	M4 weaponMatrix() const; // (after updatePose)

	virtual void update(double dt);
	void physics(double dt);
	bool jump(double v = 6.2);
	void faceTowards(double x, double z, double dt, double rate = 10);

	virtual bool takeDamage(double amount, const DamageInfo& info);
	virtual void die(const DamageInfo& info);
	void startRagdoll(const V3& impulse, const V3* hitPoint = nullptr);
	void knockDown(const V3& impulse);
	void getUp();
	void recoverFrom();
	bool isDown() const { return ragdolling || dead; }

	struct BodyHit { double t; std::string part; int particle = -1; };
	bool rayHit(double ox, double oy, double oz, double dx, double dy, double dz, double maxT, BodyHit& out);
	virtual void remove();

	// hooks (the player and the pedestrians react)
	virtual void onWallHit(CollObj*) {}
	virtual void onEnterWater() {}
	virtual void onHardLanding(double) {}
	virtual void onDamaged(Character*, double, const DamageInfo&) {}
	virtual void onDeath(const DamageInfo&) {}
	virtual void onKnockedDown() {}
	virtual void onGotUp() {}
	virtual void onEnteredVehicle(Vehicle*) {}
	virtual void onCarjacked(Character*) {}
	virtual void onBumped(Vehicle*) {}
	virtual void fleeFrom(const V3&) {}

protected:
	void seatState(AnimState& st);
	void leanOut(double dt);
	void orientWeapon();
};

} // namespace atg
