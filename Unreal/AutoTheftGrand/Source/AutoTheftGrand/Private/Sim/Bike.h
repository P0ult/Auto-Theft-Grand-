// Two-wheelers (port of the Bike class in src/entities/bikes.js): motorbikes and bicycles run on the car tyre
// model (a narrow, light car), so traffic, collisions and carjacking work unchanged; on top of that a bike leans
// into corners (and onto its stand when parked), the rider sits astride with hands on the grips (pedalling on a
// bicycle), and a hard hit throws everyone off. The model is Gen/BikeModels.
#pragma once

#include "BikeModels.h"
#include "Vehicle.h"

namespace atg {

class Bike : public Vehicle {
public:
	Bike(Game& game, const std::string& type, double x, double z, double yaw, const SpawnOpts& opts);
	const BikeModel* bike = nullptr;
	double lean = 0, yawPrev = 0, pedalPhase = 0, throwT = 0;

	void setup(const SpawnOpts& opts) override;
	bool doorFor(int seat, Character* c, V3& out) const override;
	V3 nearestDoor(const V3& p, int& seat) const override;
	bool feetFor(int seat, V3 out[2]) const override;
	bool gripsFor(V3 out[2]) const override;
	void update(double dt) override;
	M4 groupMatrix() const override;
	void onCrash(double impact, CollObj*) override { if (impact > 6.5) throwRiders(impact); }
	void onCrashVehicle(double impact, Vehicle*) override { if (impact > 6.5) throwRiders(impact); }
	void throwRiders(double impact) override;

protected:
	void updateVisual(double dt) override;
};

} // namespace atg
