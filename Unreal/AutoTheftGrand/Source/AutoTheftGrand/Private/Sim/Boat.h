// Boats (port of the Boat class in src/entities/boat.js): they float on the sea (and Lake Mirador), ride the
// swell, lift onto the plane at speed, heel into turns, throw spray and leave a wake. Run them aground and they
// stop; hole them and they burn, blow up and go down. The police boat has a bow gun. The models are
// Gen/BoatModels.
#pragma once

#include "BoatModels.h"
#include "Vehicle.h"

namespace atg {

// the swell (metres) at a point: bigger at sea than on the lake, and in a storm
double WaveHeight(const Game& game, double x, double z, double t, bool lake = false);

class Boat : public Vehicle {
public:
	Boat(Game& game, const std::string& type, double x, double z, double yaw, const SpawnOpts& opts);
	const BoatModel* boat = nullptr;
	bool floating = true, sinking = false;
	double pitch = 0, roll = 0, heave = 0, heaveV = 0, propSpin = 0, gunT = 0, wakeT = 0, sinkT = 0;
	double gunYaw = 0, gunPitch = 0; // (the bow gun, turned toward the camera while the player drives)
	bool moored = false, policeBoat = false;
	double gunT2 = 1; int burst = 0;  // (boats.js: the police gunner's bursts)

	double waterLevel() const;
	bool lake() const;
	bool armed() const override { return def.weapons; }
	bool showCrosshair() const override { return armed(); }
	bool doorFor(int seat, Character* c, V3& out) const override;
	V3 nearestDoor(const V3& p, int& seat) const override;
	bool feetFor(int seat, V3 out[2]) const override;
	bool gripsFor(V3 out[2]) const override;
	void playerControl(const Input& in, double dt) override;
	void takeOut(Character* c, const V3* at = nullptr) override;
	// the bow gun: at the crosshair, or at a target (the police gunner)
	void fireGun(const V3* target = nullptr);
	M4 gunMatrix() const;
	void update(double dt) override;
	void explode() override;

protected:
	void boatStep(double h);
	void boatAfter(double dt);
	void updateVisual(double dt) override;
};

} // namespace atg
