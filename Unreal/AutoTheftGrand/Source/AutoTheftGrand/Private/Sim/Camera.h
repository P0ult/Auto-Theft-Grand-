// The camera rig (port of src/game/camera.js): third-person orbit on foot, the over-the-shoulder aim camera
// and sniper scope, the vehicle chase camera, the tank's free orbit, GTA V's cinematic car camera and set
// shots for cutscenes. The result is a camera position, orientation and vertical field of view in the
// game's axes (three.js camera conventions: it looks down its own -z).
#pragma once

#include "Core.h"

namespace atg {

class Game;
class Player;
class Vehicle;
class Input;

class CameraRig {
public:
	explicit CameraRig(Game& game) : game(game) {}
	Game& game;
	// the camera
	V3 camPos;
	Quat camQuat;
	double camFov = 62;
	// rig state
	double yaw = kPi, pitch = -0.15, dist = 4.3, curDist = 4.3;
	V3 pivot, pos;
	double aimBlend = 0, shake = 0, fovBase = 62, fov = 62, scopeBlend = 0, scopeFov = 16, lastLookInput = 0;
	double vehYawOffset = 0, vehPitch = -0.12;
	bool lookBehind = false;
	double vehDist = 7.5;
	int vehicleCamIndex = 0;
	double time = 0;
	bool cineHeld = false, cineBars = false, scopeHid = false;
	double boostFov = 0;
	struct Cine { V3 pos, target; double fov; };
	bool hasCine = false; Cine cine;
	struct CineShot { int shot = -1; double t = 0, len = 0; int n = 0; V3 anchor; double side = 1; bool fresh = false; };
	bool hasCv = false; CineShot cv;
	bool hasFlightOff = false; V3 flightOff;

	void addShake(double a) { shake = Min(1.2, shake + a); }
	// is a point (or a sphere round it) inside the camera's view (near 0.25 m, far 9 km)?
	bool inView(const V3& p, double radius = 0) const;
	double forwardYaw() const { return yaw + kPi; }
	V3 lookDir() const { return camQuat.rotate(V3(0, 0, -1)); }
	void setCinematic(const V3& p, const V3& target, double f = 50) { hasCine = true; cine = { p, target, f }; }
	void clearCinematic() { hasCine = false; }
	void update(double dt, const Input* input, Player& player);

private:
	void lookAt(const V3& target, const V3& up = V3(0, 1, 0));
	void vehCine(double dt, Vehicle* veh);
	void flightCam(double dt, Vehicle* veh);
	void finish(double dt);
};

} // namespace atg
