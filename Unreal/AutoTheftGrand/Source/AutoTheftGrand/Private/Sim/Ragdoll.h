// Verlet particle ragdoll mapped back onto the humanoid's bones (port of src/entities/ragdoll.js).
#pragma once

#include "Skeleton.h"

namespace atg {

class CollisionWorld;

class Ragdoll {
public:
	static constexpr int N = 18;
	Ragdoll(Pose* pose, CollisionWorld* col) : pose(pose), col(col) {}
	double pos[N * 3] = {}, prev[N * 3] = {};
	bool active = false, settled = false;
	double sleepT = 0, time = 0;

	// from the pose's current world matrices (Update them first); vel: the body's velocity
	void start(const V3& vel, const V3* impulse = nullptr, int impulsePoint = -1);
	void push(int i, double vx, double vy, double vz);
	int nearestParticle(double x, double y, double z) const;
	V3 center() const { return { pos[0], pos[1], pos[2] }; }
	V3 particle(int i) const { return { pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2] }; }
	void update(double dt);
	// pose the bones from the particles (root: the character's root matrix, mesh-space = root space)
	void apply(const M4& root);

private:
	Pose* pose;
	CollisionWorld* col;
	std::vector<double> rest;
	std::vector<std::array<double, 3>> mins;
	double dist(int a, int b) const;
};

} // namespace atg
