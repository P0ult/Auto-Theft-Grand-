// Flyable planes, jets and helicopters, and the tank (port of src/entities/aircraft.js). They share the Vehicle
// interface (occupants, damage, getting in and out, collisions with cars and people) but run their own physics,
// controls and weapons. Planes fly an arcade model: thrust along the nose, lift up to 1 g fading below stall
// speed, air "grip" swinging the velocity onto the nose and weathervaning swinging the nose onto the velocity.
// Helicopters tilt their rotor disc to move and hold their altitude. The tank drives on tracks and turns its
// turret toward the crosshair. The models are Gen/AircraftModels.
#pragma once

#include "AircraftModels.h"
#include "Vehicle.h"

namespace atg {

// where the crosshair (screen centre) points in the world
V3 AimPoint(Game& game, double maxDist = 900, Character* exclude = nullptr);

class AirVehicle : public Vehicle {
public:
	AirVehicle(Game& game, const std::string& type, double x, double z, double yaw, const SpawnOpts& opts);
	const AircraftModel* air = nullptr;
	Quat quat;
	double cgHeight = 1;
	bool grounded = true, gearDown = true;
	double gearK = 1, power = 0, spool = 0;
	V3 angVel;
	struct Ctl { double pitch = 0, roll = 0, yaw = 0, coll = 0, brake = 0; bool reverse = false; } ctl;
	double mouseP = 0, mouseR = 0, gunT = 0, missileT = 0, propAngle = 0, aimT = 0;
	int side = 1;
	bool hasAim = false; V3 aimAt;
	double doorAngle() const { return doorOpen * air->doorMax; }
	double strobe() const; // (the strobe's emissive: on for 0.07 s every 1.3 s)

	bool armed() const override { return def.weapons; }
	bool showCrosshair() const override { return armed(); }
	double forwardSpeed() const override { return vel.dot(quat.rotate(V3(0, 0, 1))); }
	double throttle() const { return def.kind == "heli" ? spool : power; }
	double altitude() const override;
	double cgY() const override { return cgHeight; }
	bool isGrounded() const override { return grounded; }
	V3 cgPoint() const override { return V3(pos.x, pos.y + cgHeight, pos.z); }
	// model-space point (y = 0 is the ground contact) to world space, following the full attitude
	V3 localPoint(double x, double y, double z) const override;
	Quat bodyQuat() const override { return quat; }
	M4 groupMatrix() const override;
	M4 bodyMatrix() const override { return groupMatrix(); }
	void putIn(Character* c, int seat = 0) override;
	void takeOut(Character* c, const V3* at = nullptr) override;
	void dent(double, double, double, double) override {}
	void explode() override;
	void crash(double speed, const V3* where = nullptr);

protected:
	struct Floor { double y; bool water; };
	Floor floor() const;
	void wreckStep(double h);
	void statics();
	bool water(const Floor& fl);
	bool asleep(double dt);
};

class Plane : public AirVehicle {
public:
	using AirVehicle::AirVehicle;
	void playerControl(const Input& in, double dt) override;
	void fireCannon();
	void fireMissile();
	void update(double dt) override;
	double flameScale(int i) const; // (the afterburner's length; 0: off)
private:
	void fly(double h);
	void visual(double dt);
};

class Heli : public AirVehicle {
public:
	Heli(Game& game, const std::string& type, double x, double z, double yaw, const SpawnOpts& opts);
	double tiltP = 0, tiltR = 0, yawRate = 0, rotorAngle = 0, tailAngle = 0, gunYaw = 0, gunPitch = 0, dustT = 0;
	double forwardSpeed() const override { return Hypot(vel.x, vel.z); }
	void playerControl(const Input& in, double dt) override;
	void fireMinigun();
	void fireRocket();
	void update(double dt) override;
private:
	void fly(double h);
	void heliStatics();
	void visual(double dt);
};

class Tank : public Vehicle {
public:
	Tank(Game& game, const std::string& type, double x, double z, double yaw, const SpawnOpts& opts);
	const AircraftModel* air = nullptr;
	double turretYaw = 0, gunPitch = 0, reload = 0, recoil = 0, wheelL = 0, wheelR = 0, trackL = 0, trackR = 0, aimT = 0;
	bool hasAim = false; V3 aimAt;
	// the turret blown off its ring: its height, spin and tilt
	double turretY = 1.78, turretVy = NaN(), turretSpin = 0, turretTurn = 0, turretTilt = 0;
	bool armed() const override { return true; }
	bool showCrosshair() const override { return true; }
	void putIn(Character* c, int seat = 0) override;
	void takeOut(Character* c, const V3* at = nullptr) override;
	void dent(double, double, double, double) override {}
	void playerControl(const Input& in, double dt) override;
	// the gun's world matrix and its muzzle
	M4 gunMatrix() const;
	V3 muzzleWorld() const;
	void fireCannon();
	void update(double dt) override;
	void explode() override;
protected:
	void step(double h) override;
	void updateVisual(double dt) override;
};

} // namespace atg
