// Procedural character animation (port of src/entities/animator.js): an IK-driven gait (walk, run, sprint,
// strafe, backpedal), crouch, jump and fall, swimming, sitting and driving, riding bikes and boards, aiming
// (pistol, rifle, heavy), melee actions (punch combo, kick, stab, bat), reload, flinch, cower, hands up,
// talking gestures and getting up. Writes Euler angles (YXZ) into the pose's bones.
#pragma once

#include "Skeleton.h"
#include <functional>

namespace atg {

struct AnimState {
	double speed = 0, moveAngle = 0, vy = 0, aimPitch = 0, steer = 0, lookYaw = 0, lookPitch = 0, turn = 0, boardCrouch = 0, aimTwist = 0;
	bool grounded = true, aim = false, crouch = false, swim = false, cower = false, handsUp = false, talking = false;
	int sit = 0;                 // 0 standing, 1 driving, 2 passenger
	std::string weapon = "none"; // hold type
	std::string bike;            // "" / moto / bicycle / board / pillion
	bool hasFeet = false; V3 feet[2];
	bool hasGrips = false; V3 grips[2];
};

struct ActionDef;
struct AnimAction {
	std::string name;
	const ActionDef* def = nullptr;
	double t = 0, dur = 1, hitTime = -1;
	bool hitDone = false, done = false;
	std::function<void()> onHit;
};

class Animator {
public:
	Animator(Pose* pose, const V3 rest[Bone::COUNT]);
	struct Weights { double idle = 1, walk = 0, run = 0, sprint = 0, aim = 0, crouch = 0, air = 0, sit = 0, swim = 0, cower = 0, hands = 0, rifle = 0, talk = 0; } w;
	std::shared_ptr<AnimAction> action;
	std::vector<std::shared_ptr<AnimAction>> additive;
	double recoil = 0, hipH = 0.98, time = 0, phase = 0;
	bool enabled = true;

	// start an action (jab, cross, kick, stab, swing, reload, throw, pull, wave, flinch, getup); null if unknown
	std::shared_ptr<AnimAction> play(const std::string& name, double speed = 1);
	bool busy() const { return action && !action->done; }
	// remember the current pose and blend from it (after a ragdoll)
	void beginBlend(double dur = 0.4);
	void update(double dt, const AnimState& s);

private:
	Pose* pose;
	V3 rest[Bone::COUNT];
	double P[Bone::COUNT * 3] = {};
	double hipsOff[3] = {}, hipsRot[3] = {};
	double L1 = 0.44, L2 = 0.42;
	V3 thighOff[2];
	double prevSpeed = 0, accel = 0, turnF = 0, dt_ = 0;
	bool blending = false; Quat blendFrom[Bone::COUNT]; V3 blendFromPos; double blendT = 0, blendDur = 0.4;
	void set(int b, double x, double y, double z) { P[b * 3] = x; P[b * 3 + 1] = y; P[b * 3 + 2] = z; }
	void add(int b, double x, double y, double z) { P[b * 3] += x; P[b * 3 + 1] += y; P[b * 3 + 2] += z; }
	void mix(int b, double x, double y, double z, double k) { P[b * 3] += (x - P[b * 3]) * k; P[b * 3 + 1] += (y - P[b * 3 + 1]) * k; P[b * 3 + 2] += (z - P[b * 3 + 2]) * k; }
	void legIK(int side, double tx, double ty, double tz);
	void setBones();
};

} // namespace atg
