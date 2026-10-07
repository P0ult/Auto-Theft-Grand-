// An animal (port of the Animal class in src/entities/animals.js): dogs, cats, deer, rabbits, coyotes, cows and
// birds with a light procedural rig (four swinging legs that walk, trot, gallop or hop, a bobbing body, a head
// that looks, grazes and pants, a wagging tail, flapping wings) and the movement shared by the wildlife and the
// pets: steering round walls, keeping to the ground and out of the sea, riding in a car, dying. The rig is
// plain numbers here (the three.js groups' transforms); ATGAnimals draws it with the breed's parts
// (Gen/AnimalModels). The wildlife's and the pets' own state (home, threat, owner, ...) lives here too, as it
// does on the JavaScript object.
#pragma once

#include "Core.h"
#include "AnimalModels.h"

#include <functional>

namespace atg {

class Game;
class Character;
class Vehicle;

class Animal : public std::enable_shared_from_this<Animal> {
public:
	Animal(Game& game, const std::string& breed, double x, double z, bool hasY = false, double y = 0, bool hasYaw = false, double yaw = 0);
	Game& game;
	int id;
	std::string breed, kind;              // kind: the species (dog, cat, deer, ...)
	const AnimalParts& P;
	const AnimalSpecies& sp;
	double scale;
	V3 pos, vel;
	double yaw;
	double speed = 0;                     // current ground speed
	struct Want { double x, z, speed; };
	bool hasWant = false; Want want{};    // steering target
	double phase, t;
	std::string state = "idle";
	double stateT = 0;
	double health, maxHealth;
	bool dead = false, removed = false;
	bool flying = false; double alt = 0;
	double sitK = 0, grazeK = 0, wag = 0, lookYaw = 0, flapT = 0;
	double radius;
	bool grounded = true;
	bool visible = true;
	Ref<Vehicle> inVehicle; int seat = 0;
	double deathT = 0, bumpT = 0;
	bool happy = false;
	Ref<Character> lastDamager;
	std::function<void(Character* /*source*/)> onHurt;
	std::function<void()> onBlocked;

	// the wildlife's and the pets' state
	bool hasHome = false; double homeX = 0, homeZ = 0;
	bool stray = false, pet = false, display = false;
	Ref<Character> owner;
	std::string petName;
	double threatX = 0, threatZ = 0, fleeT = 0;
	bool hasFlyTo = false; double flyToX = 0, flyToZ = 0;
	double barkT = -9;
	int followSide = 0;
	double idleT = 0, biteT = 0;

	// the rig: the body group's height and pitch, each leg's swing and height (front right, front left, hind
	// right, hind left), the head's nod and turn, the tail's wag, the wings' fold (rotation z, y); the roll of a
	// dead animal lying on its side
	double bodyY = 0, bodyRotX = 0;
	double legRotX[4] = {}, legY[4] = {};
	double headRotX = 0, headRotY = 0, tailRotY = 0;
	double wingRotZ[2] = {}, wingRotY[2] = {};
	double rootRoll = 0;

	void goTo(double x, double z, double targetSpeed) { hasWant = true; want = { x, z, targetSpeed }; }
	void stop() { hasWant = false; }
	void update(double dt);
	void animate(double dt);
	bool takeDamage(double amount, Character* source = nullptr, const std::string& type = "");
	void die(Character* source = nullptr);
	void sitIn(Vehicle* v, int seat);
	void getOut(double x, double z);
	// the distance along a ray to this animal's body (a sphere), or -1
	double rayHit(double ox, double oy, double oz, double dx, double dy, double dz, double maxT) const;
	void remove() { removed = true; }
	// the root group in the world: position and heading (or the seat in a car), the roll when dead
	M4 rootMatrix() const;

private:
	void deadPose(double dt);
	void ride(double dt);
};

} // namespace atg
