// A vehicle (port of src/entities/vehicle.js): bicycle-model tyre physics with slip angles, weight transfer,
// traction circles, handbrake drifting, ground following and jumps, collisions, damage and dents, fire,
// explosions, and full 3D rigid-body tumbling after hard crashes, blasts and rollovers. Subclasses (bikes,
// boats, aircraft, the tank, trains) override the physics. What the renderer needs (door and lid angles,
// dents, lost panels, lights) is kept as plain state here.
#pragma once

#include "Collision.h"
#include "VehicleModels.h"

namespace atg {

class Game;
class Character;

struct VehicleLayout {
	std::vector<V3> seats;    // seat points (driver first)
	V3 doorPos;               // where the driver stands to get in
	double seatHip = 0.52;    // hips above the seat point
	bool stand = false; double standYaw = 0; // standing (a skateboard): feet on the seat point, side-on
	bool hasHull = false; VehicleModel::Hull hull{};
	double doorMax = -1.1;
};

struct VehInput { double throttle = 0, brake = 0, steer = 0; bool handbrake = false; };

struct SpawnOpts {
	bool hasColor = false; uint32_t color = 0;
	bool hasY = false; double y = 0;
	bool locked = false, parked = false, persistent = false;
	std::string missionTag;
};

struct VehicleAI { virtual ~VehicleAI() = default; virtual void update(double dt) = 0; };

class Vehicle : public std::enable_shared_from_this<Vehicle> {
public:
	Vehicle(Game& game, const std::string& type, double x, double z, double yaw, const SpawnOpts& opts);
	virtual ~Vehicle() = default;
	// (called once constructed: subclasses set up their own models and numbers)
	virtual void setup(const SpawnOpts&) {}

	Game& game;
	int vid;
	std::string type;
	const VehicleDef& def;
	uint32_t color;
	bool painted = false; // (resprayed: the paint is color, even on a police livery)
	const VehicleModel* model = nullptr; // cars, vans and trucks (vehiclemodels.js)
	VehicleLayout layout;

	V3 pos, vel;
	double yaw = 0, r = 0;
	double hx, hz, mass, I, a, b;
	double steerAngle = 0;
	VehInput input;
	double axLong = 0, ayLat = 0, rearGrip = 1;
	double health, maxHealth, burnTime = 0;
	bool onFire = false, exploded = false, sunk = false;
	std::shared_ptr<Character> occupants[4];
	bool sirenOn = false, lightsOn = false, horn = false;
	double bodyPitch = 0, bodyRoll = 0, bodyPitchV = 0, bodyRollV = 0, bodyY = 0, bodyYV = 0;
	bool airborne = false;
	double vy = 0, lastGroundY = 0, groundVy = 0, groundPitch = 0, groundRoll = 0;
	double wheelRot = 0, slipRear = 0, slipFront = 0, wheelspin = 0, skid = 0;
	bool locked = false, parked = false;
	std::shared_ptr<VehicleAI> ai;
	double lastHit = -10;
	double hydraulic = 0, hydraulicV = 0, sirenPhase = 0;
	std::vector<Contact> contacts;
	double stuckTime = 0;
	bool removed = false, persistent = false;
	std::string missionTag;
	Ref<Character> lastDamager;
	double wreckTime = 0, crushT = 0;
	double surface = 1;
	double rollT = 0;
	bool flipped = false;
	double crouch = 0;  // (skateboards)
	// set by the systems that own a vehicle
	bool ownedByPlayer = false, skidding = false; // (gameplay.js: stats, skid marks)
	bool proxy = false; // (a train carriage's stand-in for the contact code)
	bool traffic = false, policeUnit = false, armyUnit = false, remote = false, npcRemote = false, flat = false, stable = false, heistVan = false, roadblock = false;

	// ---- what the renderer reads
	double doorOpen = 0;
	double rearDoorOpen[2] = { 0, 0 };
	double hoodAngle = 0, trunkAngle = 0; bool hoodOpen = false, trunkOpen = false;
	std::set<std::string> detached;   // panels torn off
	bool glassBroken = false;
	struct DentRec { V3 local; double strength; };
	std::vector<DentRec> dents;       // (vehicle frame); cleared by undent()
	int dentVersion = 0;
	bool braking = false;

	// ---- tumbling (a full rigid body until it settles)
	struct Tumble { Quat q; V3 w; double rest = 0, flipT = 0, rockT = 0, bailT = 0, scrapeT = 0, rPrev = 0; int landed = 0; V3 cg; bool settled = false, hinted = false; };
	std::unique_ptr<Tumble> tb;
	Quat netQ; bool hasNetQ = false;

	Character* driver() const { return occupants[0].get(); }
	double speed() const { return vel.x * std::sin(yaw) + vel.z * std::cos(yaw); }
	double speedAbs() const { return Hypot(vel.x, vel.z); }
	V3 fwd() const { return { std::sin(yaw), 0, std::cos(yaw) }; }
	bool isWrecked() const { return exploded || sunk; }
	bool empty() const { for (const auto& o : occupants) if (o) return false; return true; }

	V3 localToWorld(double x, double y, double z) const;
	void worldToLocal(double x, double z, double& lx, double& lz) const;
	V3 doorWorld() const;
	V3 seatWorld(int i) const;
	// the vehicle's group and the sprung body on it, as world matrices (characters sit in the body)
	virtual M4 groupMatrix() const;
	virtual M4 bodyMatrix() const;
	// aircraft: their attitude, centre of gravity and airspeed (the flight camera)
	virtual Quat bodyQuat() const { return groupMatrix().rotation(); }
	virtual V3 cgPoint() const { return pos + V3(0, layout.hasHull ? layout.hull.cgH : 0.6, 0); }
	virtual double forwardSpeed() const { return speed(); }

	// ---- occupants
	virtual void putIn(Character* c, int seat = 0);
	virtual void takeOut(Character* c, const V3* at = nullptr);
	virtual void ejectOccupant(Character* c, bool dead = false);
	int seatOf(const Character* c) const { for (int i = 0; i < 4; i++) if (occupants[i].get() == c) return i; return -1; }
	virtual bool feetFor(int, V3[2]) const { return false; }
	virtual bool gripsFor(V3[2]) const { return false; }
	// (bikes and aircraft have several doors)
	virtual bool hasDoors() const { return false; }
	virtual V3 nearestDoor(const V3& p, int& seat) const { seat = 0; (void)p; return doorWorld(); }
	virtual bool doorFor(int, Character*, V3&) const { return false; }
	// mounted guns (aircraft, the tank, the police boat)
	virtual bool armed() const { return false; }
	virtual bool showCrosshair() const { return false; }
	// aircraft
	virtual bool isGrounded() const { return !airborne; }
	virtual double altitude() const { return 0; }
	virtual double cgY() const { return 0; }
	virtual V3 localPoint(double x, double y, double z) const { return localToWorld(x, y, z); }

	// ---- control and simulation
	virtual void playerControl(const class Input& in, double dt);
	virtual void update(double dt);
	bool canTumble() const { return def.kind.empty() && !remote && !removed; }
	void startTumble(const V3* w = nullptr, double vy = 0);
	void blast(const V3& p, double k, const V3& dir);
	void crashTumble(const V3& n, double impact, double k, bool wall = false);
	void crashParts(const V3& at, double impact);
	void detachPart(const std::string& name, const V3& at);
	void shatterGlass();
	virtual void damage(double amount, Character* source = nullptr);
	virtual void dent(double wx, double wy, double wz, double strength);
	void undent() { dents.clear(); dentVersion++; }
	virtual void explode();
	virtual void remove();
	virtual void onCrash(double, CollObj*) {}
	virtual void onCrashVehicle(double, Vehicle*) {}
	virtual void onSunk() {}
	virtual void throwRiders(double) {}

protected:
	struct HullPt { V3 p; bool wheel; };
	struct Hull { std::vector<HullPt> pts; double cgH = 0.6; V3 inv; };
	std::unique_ptr<Hull> hull;
	const Hull& hullPoints();
	V3 iinv(V3 v) const;
	void impulse(const V3& J, const V3& r);
	void endTumble();
	void tumble(double dt);
	void common(double dt);
	void rolloverCheck(double dt);
	void step(double h);
	void afterPhysics(double dt);
	void resolveStatic(const Contact& ct);
	void updateVisual(double dt);
};

} // namespace atg
