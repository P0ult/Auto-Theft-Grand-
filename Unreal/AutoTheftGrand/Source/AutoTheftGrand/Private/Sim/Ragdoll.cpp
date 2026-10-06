#include "Ragdoll.h"
#include "Collision.h"

namespace atg {

namespace {
struct PDef { int bone; V3 off; };
const PDef P_DEF[Ragdoll::N] = {
	{ Bone::hips, { 0, 0, 0 } }, { Bone::chest, { 0, 0, 0 } }, { Bone::neck, { 0, 0, 0 } }, { Bone::head, { 0, 0.2, 0 } },
	{ Bone::lUpperArm, { 0, 0, 0 } }, { Bone::lForearm, { 0, 0, 0 } }, { Bone::lHand, { 0, -0.08, 0 } },
	{ Bone::rUpperArm, { 0, 0, 0 } }, { Bone::rForearm, { 0, 0, 0 } }, { Bone::rHand, { 0, -0.08, 0 } },
	{ Bone::lThigh, { 0, 0, 0 } }, { Bone::lShin, { 0, 0, 0 } }, { Bone::lFoot, { 0, 0, 0 } }, { Bone::lFoot, { 0, -0.04, 0.18 } },
	{ Bone::rThigh, { 0, 0, 0 } }, { Bone::rShin, { 0, 0, 0 } }, { Bone::rFoot, { 0, 0, 0 } }, { Bone::rFoot, { 0, -0.04, 0.18 } },
};
const double RADIUS[Ragdoll::N] = { 0.12, 0.13, 0.07, 0.1, 0.06, 0.05, 0.05, 0.06, 0.05, 0.05, 0.07, 0.06, 0.05, 0.04, 0.07, 0.06, 0.05, 0.04 };
const int LINKS[][2] = {
	{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 1, 4 }, { 1, 7 }, { 4, 7 }, { 2, 4 }, { 2, 7 }, { 0, 10 }, { 0, 14 }, { 10, 14 }, { 4, 10 }, { 7, 14 }, { 4, 14 }, { 7, 10 }, { 1, 10 }, { 1, 14 }, { 1, 3 }, { 4, 3 }, { 7, 3 },
	{ 4, 5 }, { 5, 6 }, { 7, 8 }, { 8, 9 },
	{ 10, 11 }, { 11, 12 }, { 12, 13 }, { 14, 15 }, { 15, 16 }, { 16, 17 }, { 11, 13 }, { 15, 17 },
};
constexpr int NLINKS = sizeof(LINKS) / sizeof(LINKS[0]);
const double MINS[][3] = { { 4, 6, 0.9 }, { 7, 9, 0.9 }, { 10, 12, 0.72 }, { 14, 16, 0.72 }, { 3, 0, 0.9 }, { 6, 1, 0.5 }, { 9, 1, 0.5 }, { 12, 14, 0.3 }, { 16, 10, 0.3 }, { 11, 15, 0.12 } };
const V3 DOWN(0, -1, 0);
const V3 TOE_REST = V3(0, -0.04, 0.18).normalized();
}

double Ragdoll::dist(int a, int b) const { return Hypot3(pos[a * 3] - pos[b * 3], pos[a * 3 + 1] - pos[b * 3 + 1], pos[a * 3 + 2] - pos[b * 3 + 2]); }

void Ragdoll::start(const V3& vel, const V3* impulse, int impulsePoint) {
	for (int i = 0; i < N; i++) {
		const V3 v = pose->WorldPoint(P_DEF[i].bone, P_DEF[i].off);
		pos[i * 3] = v.x; pos[i * 3 + 1] = v.y; pos[i * 3 + 2] = v.z;
	}
	const double h = 1.0 / 60;
	for (int i = 0; i < N; i++) {
		double vx = vel.x, vy = vel.y, vz = vel.z;
		if (impulse) {
			const double w = impulsePoint < 0 ? 1 : Max(0.25, 1 - dist(i, impulsePoint) * 0.9);
			vx += impulse->x * w; vy += impulse->y * w; vz += impulse->z * w;
		}
		vx += (Rand() - 0.5) * 0.4; vz += (Rand() - 0.5) * 0.4;
		prev[i * 3] = pos[i * 3] - vx * h; prev[i * 3 + 1] = pos[i * 3 + 1] - vy * h; prev[i * 3 + 2] = pos[i * 3 + 2] - vz * h;
	}
	rest.clear();
	for (const auto& l : LINKS) rest.push_back(dist(l[0], l[1]));
	mins.clear();
	for (const auto& m : MINS) mins.push_back({ m[0], m[1], dist((int)m[0], (int)m[1]) * m[2] });
	active = true; settled = false; sleepT = 0; time = 0;
}

void Ragdoll::push(int i, double vx, double vy, double vz) {
	const double h = 1.0 / 60;
	for (int k = 0; k < N; k++) {
		const double w = Max(0.15, 1 - dist(k, i) * 1.2);
		prev[k * 3] -= vx * h * w; prev[k * 3 + 1] -= vy * h * w; prev[k * 3 + 2] -= vz * h * w;
	}
	settled = false; sleepT = 0;
}

int Ragdoll::nearestParticle(double x, double y, double z) const {
	int best = 0; double bd = kInf;
	for (int i = 0; i < N; i++) { const double d = std::pow(pos[i * 3] - x, 2) + std::pow(pos[i * 3 + 1] - y, 2) + std::pow(pos[i * 3 + 2] - z, 2); if (d < bd) { bd = d; best = i; } }
	return best;
}

void Ragdoll::update(double dt) {
	if (!active) return;
	time += dt;
	if (settled) return;
	const int steps = 2;
	const double h = Min(dt, 1.0 / 30) / steps;
	double* p = pos; double* q = prev;
	double maxV = 0;
	for (int s = 0; s < steps; s++) {
		for (int i = 0; i < N; i++) {
			const int k = i * 3;
			const double x = p[k], y = p[k + 1], z = p[k + 2];
			const double vx = (x - q[k]) * 0.995, vy = (y - q[k + 1]) * 0.995, vz = (z - q[k + 2]) * 0.995;
			q[k] = x; q[k + 1] = y; q[k + 2] = z;
			p[k] = x + vx; p[k + 1] = y + vy - 9.81 * h * h; p[k + 2] = z + vz;
		}
		for (int it = 0; it < 8; it++) {
			for (int c = 0; c < NLINKS; c++) {
				const int ka = LINKS[c][0] * 3, kb = LINKS[c][1] * 3;
				const double dx = p[kb] - p[ka], dy = p[kb + 1] - p[ka + 1], dz = p[kb + 2] - p[ka + 2];
				double d = std::sqrt(dx * dx + dy * dy + dz * dz); if (d == 0) d = 1e-6;
				const double diff = (d - rest[c]) / d * 0.5;
				p[ka] += dx * diff; p[ka + 1] += dy * diff; p[ka + 2] += dz * diff;
				p[kb] -= dx * diff; p[kb + 1] -= dy * diff; p[kb + 2] -= dz * diff;
			}
			for (const auto& m : mins) {
				const int ka = (int)m[0] * 3, kb = (int)m[1] * 3;
				const double dx = p[kb] - p[ka], dy = p[kb + 1] - p[ka + 1], dz = p[kb + 2] - p[ka + 2];
				double d = std::sqrt(dx * dx + dy * dy + dz * dz); if (d == 0) d = 1e-6;
				if (d < m[2]) {
					const double diff = (d - m[2]) / d * 0.5;
					p[ka] += dx * diff; p[ka + 1] += dy * diff; p[ka + 2] += dz * diff;
					p[kb] -= dx * diff; p[kb + 1] -= dy * diff; p[kb + 2] -= dz * diff;
				}
			}
			for (int i = 0; i < N; i++) {
				const int k = i * 3;
				const double r = RADIUS[i];
				const double gh = col->floorHeight(p[k], p[k + 2], q[k + 1]) + r;
				if (p[k + 1] < gh) {
					p[k + 1] = gh;
					const double fr = 0.6;
					q[k] = p[k] - (p[k] - q[k]) * (1 - fr);
					q[k + 2] = p[k + 2] - (p[k + 2] - q[k + 2]) * (1 - fr);
					if (q[k + 1] < p[k + 1]) q[k + 1] = p[k + 1] + (p[k + 1] - q[k + 1]) * 0.1;
				}
				if (it == 7 && (i == 0 || i == 1 || i == 3 || i == 6 || i == 9 || i == 12 || i == 16)) {
					const auto res = col->resolveCircle(p[k], p[k + 2], r + 0.05, p[k + 1] - 0.1, 0.2);
					if (res.hit) { p[k] = res.x; p[k + 2] = res.z; }
				}
			}
		}
		for (int i = 0; i < N * 3; i++) maxV = Max(maxV, std::fabs(p[i] - q[i]) / h);
	}
	if (maxV < 0.35 && time > 0.6) { sleepT += dt; if (sleepT > 0.8) settled = true; }
	else sleepT = 0;
}

void Ragdoll::apply(const M4& root) {
	using namespace Bone;
	Quat W[Bone::COUNT];
	auto frame = [](V3 xAxis, V3 yAxis) {
		const V3 Y = yAxis.normalized();
		const V3 X = (xAxis - Y * xAxis.dot(Y)).normalized();
		const V3 Z = X.cross(Y).normalized();
		return Quat::FromMatrix(M4::Basis(X, Y, Z).m);
	};
	const Quat rootQ = root.rotation();
	W[hips] = frame(particle(10) - particle(14), particle(1) - particle(0));
	W[chest] = frame(particle(4) - particle(7), particle(2) - particle(1));
	W[spine] = Quat::Slerp(W[hips], W[chest], 0.5);
	W[neck] = frame(particle(4) - particle(7), particle(3) - particle(2));
	W[head] = W[neck];
	auto setLocal = [&](int bi, const Quat& parentWorld) { pose->rot[bi] = parentWorld.inverse() * W[bi]; };
	setLocal(hips, rootQ);
	setLocal(spine, W[hips]);
	setLocal(chest, W[spine]);
	setLocal(neck, W[chest]);
	pose->rot[head] = Quat();
	auto limb = [&](int bi, int parentBi, int from, int to, const V3& restDir) {
		const V3 a = W[parentBi].inverse().rotate((particle(to) - particle(from)).normalized());
		pose->rot[bi] = Quat::FromUnitVectors(restDir, a);
		W[bi] = W[parentBi] * pose->rot[bi];
	};
	limb(lUpperArm, chest, 4, 5, DOWN); limb(lForearm, lUpperArm, 5, 6, DOWN); pose->rot[lHand] = Quat();
	limb(rUpperArm, chest, 7, 8, DOWN); limb(rForearm, rUpperArm, 8, 9, DOWN); pose->rot[rHand] = Quat();
	limb(lThigh, hips, 10, 11, DOWN); limb(lShin, lThigh, 11, 12, DOWN); limb(lFoot, lShin, 12, 13, TOE_REST);
	limb(rThigh, hips, 14, 15, DOWN); limb(rShin, rThigh, 15, 16, DOWN); limb(rFoot, rShin, 16, 17, TOE_REST);
	pose->pos[hips] = root.inverse().apply(particle(0));
}

} // namespace atg
