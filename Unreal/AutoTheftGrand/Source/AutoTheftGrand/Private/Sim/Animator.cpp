#include "Animator.h"

namespace atg {

using KeyPose = std::vector<std::pair<std::string, V3>>;
struct ActionDef { double dur; double hit; bool additive, full, legs; std::vector<std::pair<double, KeyPose>> keys; };

namespace {
struct Gait { double duty, lift, drop, bob, armA, elbow, elbowSw, lean, chestYaw, hipYaw, width; };
const Gait GAITS[4] = {
	{ 1, 0, 0.015, 0, 0, 0.12, 0, 0, 0, 0, 0.11 },                     // idle
	{ 0.62, 0.1, 0.035, 0.028, 0.32, 0.25, 0.25, 0.05, 0.12, 0.1, 0.1 }, // walk
	{ 0.38, 0.28, 0.08, 0.045, 0.62, 1.3, 0.25, 0.14, 0.2, 0.12, 0.075 }, // run
	{ 0.33, 0.36, 0.1, 0.05, 0.9, 1.35, 0.3, 0.24, 0.24, 0.14, 0.07 },   // sprint
};

KeyPose Merge(const KeyPose& a, const KeyPose& b) {
	KeyPose r = a;
	for (const auto& kv : b) {
		bool found = false;
		for (auto& x : r) if (x.first == kv.first) { x.second = kv.second; found = true; }
		if (!found) r.push_back(kv);
	}
	return r;
}

const std::map<std::string, ActionDef>& ACTIONS() {
	static const std::map<std::string, ActionDef> A = [] {
		std::map<std::string, ActionDef> a;
		const KeyPose G = { { "rUpperArm", { -0.85, 0.2, -0.35 } }, { "rForearm", { -2.1, 0, 0 } }, { "lUpperArm", { -0.85, -0.2, 0.35 } }, { "lForearm", { -2.1, 0, 0 } } };
		auto gv = [&](const char* n) { for (const auto& kv : G) if (kv.first == n) return kv.second; return V3(); };
		a["jab"] = { 0.3, 0.11, false, false, false, { { 0, G }, { 0.35, { { "lUpperArm", { -1.55, -0.3, 0.05 } }, { "lForearm", { -0.12, 0, 0 } }, { "chest", { 0.05, -0.35, 0 } }, { "rUpperArm", gv("rUpperArm") }, { "rForearm", gv("rForearm") } } }, { 1, G } } };
		a["cross"] = { 0.38, 0.15, false, false, false, { { 0, G }, { 0.4, { { "rUpperArm", { -1.55, 0.35, -0.05 } }, { "rForearm", { -0.1, 0, 0 } }, { "chest", { 0.06, 0.55, 0 } }, { "spine", { 0, 0.2, 0 } }, { "lUpperArm", gv("lUpperArm") }, { "lForearm", gv("lForearm") } } }, { 1, G } } };
		a["kick"] = { 0.55, 0.27, false, false, true, { { 0, G }, { 0.45, Merge(G, { { "rThigh", { -1.45, 0, -0.05 } }, { "rShin", { 0.15, 0, 0 } }, { "rFoot", { 0.3, 0, 0 } }, { "chest", { -0.25, 0.2, 0 } } }) }, { 1, G } } };
		a["stab"] = { 0.45, 0.22, false, false, false, {
			{ 0, { { "rUpperArm", { -1.0, 0.2, -0.2 } }, { "rForearm", { -1.6, 0, 0 } } } },
			{ 0.3, { { "rUpperArm", { -2.7, 0.25, -0.25 } }, { "rForearm", { -0.9, 0, 0 } }, { "chest", { -0.1, -0.2, 0 } } } },
			{ 0.55, { { "rUpperArm", { -1.0, 0.45, -0.05 } }, { "rForearm", { -0.25, 0, 0 } }, { "chest", { 0.3, 0.35, 0 } }, { "spine", { 0.1, 0.1, 0 } } } },
			{ 1, { { "rUpperArm", { -1.0, 0.2, -0.2 } }, { "rForearm", { -1.6, 0, 0 } } } } } };
		const KeyPose sw0 = { { "rUpperArm", { -0.9, -0.4, -0.3 } }, { "rForearm", { -1.9, 0, 0 } }, { "lUpperArm", { -1.1, -0.9, 0.2 } }, { "lForearm", { -1.7, 0, 0 } }, { "chest", { 0, -0.4, 0 } } };
		a["swing"] = { 0.62, 0.34, false, false, false, {
			{ 0, sw0 },
			{ 0.35, { { "rUpperArm", { -1.5, -0.9, -0.6 } }, { "rForearm", { -1.9, 0, 0 } }, { "lUpperArm", { -1.4, -1.3, 0.1 } }, { "lForearm", { -1.8, 0, 0 } }, { "chest", { 0, -0.95, 0 } }, { "spine", { 0, -0.3, 0 } } } },
			{ 0.6, { { "rUpperArm", { -1.45, 0.5, -0.1 } }, { "rForearm", { -0.2, 0, 0 } }, { "lUpperArm", { -1.45, -0.1, 0.1 } }, { "lForearm", { -0.35, 0, 0 } }, { "chest", { 0.1, 0.95, 0 } }, { "spine", { 0.05, 0.35, 0 } } } },
			{ 1, sw0 } } };
		const KeyPose rl = { { "lUpperArm", { -0.9, -0.6, 0.1 } }, { "lForearm", { -1.9, 0, 0 } }, { "rUpperArm", { -0.8, 0.2, -0.2 } }, { "rForearm", { -1.2, 0, 0 } } };
		a["reload"] = { 1.1, -1, false, false, false, { { 0, {} }, { 0.2, rl },
			{ 0.5, { { "lUpperArm", { -0.3, -0.2, 0.2 } }, { "lForearm", { -1.2, 0, 0 } }, { "rUpperArm", { -0.8, 0.2, -0.2 } }, { "rForearm", { -1.2, 0, 0 } } } },
			{ 0.8, rl }, { 1, {} } } };
		a["throw"] = { 0.75, 0.45, false, false, false, { { 0, {} },
			{ 0.4, { { "rUpperArm", { -2.6, 0.1, -0.5 } }, { "rForearm", { -1.2, 0, 0 } }, { "chest", { -0.15, -0.5, 0 } } } },
			{ 0.6, { { "rUpperArm", { -1.3, 0.3, -0.1 } }, { "rForearm", { -0.2, 0, 0 } }, { "chest", { 0.2, 0.5, 0 } } } }, { 1, {} } } };
		a["pull"] = { 0.8, -1, false, false, false, { { 0, {} },
			{ 0.3, { { "lUpperArm", { -1.4, -0.3, 0.1 } }, { "lForearm", { -0.3, 0, 0 } }, { "rUpperArm", { -1.4, 0.3, -0.1 } }, { "rForearm", { -0.3, 0, 0 } } } },
			{ 0.7, { { "lUpperArm", { -0.6, -0.1, 0.2 } }, { "lForearm", { -1.8, 0, 0 } }, { "rUpperArm", { -0.6, 0.1, -0.2 } }, { "rForearm", { -1.8, 0, 0 } }, { "chest", { -0.2, 0.3, 0 } } } }, { 1, {} } } };
		const KeyPose wv1 = { { "rUpperArm", { -0.4, 0, -2.4 } }, { "rForearm", { -0.9, 0, 0 } } }, wv2 = { { "rUpperArm", { -0.4, 0, -2.6 } }, { "rForearm", { -0.4, 0, 0 } } };
		a["wave"] = { 1.4, -1, false, false, false, { { 0, {} }, { 0.2, wv1 }, { 0.4, wv2 }, { 0.6, wv1 }, { 0.8, wv2 }, { 1, {} } } };
		a["flinch"] = { 0.35, -1, true, false, false, { { 0, {} }, { 0.25, { { "chest", { -0.35, 0.3, 0.1 } }, { "head", { -0.3, -0.2, 0 } }, { "spine", { -0.1, 0, 0 } } } }, { 1, {} } } };
		a["getup"] = { 1.3, -1, false, true, false, {
			{ 0, { { "hipsOff", { 0, -0.82, 0 } }, { "hipsRot", { -1.45, 0, 0 } }, { "lThigh", { -0.2, 0, 0.1 } }, { "rThigh", { -0.2, 0, -0.1 } }, { "lShin", { 0.1, 0, 0 } }, { "rShin", { 0.1, 0, 0 } }, { "lUpperArm", { -0.2, 0, 0.5 } }, { "rUpperArm", { -0.2, 0, -0.5 } } } },
			{ 0.35, { { "hipsOff", { 0, -0.7, -0.1 } }, { "hipsRot", { -0.4, 0, 0 } }, { "lThigh", { -1.9, 0, 0.15 } }, { "rThigh", { -1.9, 0, -0.15 } }, { "lShin", { 2.2, 0, 0 } }, { "rShin", { 2.2, 0, 0 } }, { "lUpperArm", { 0.4, 0, 0.3 } }, { "rUpperArm", { 0.4, 0, -0.3 } }, { "lForearm", { -0.3, 0, 0 } }, { "rForearm", { -0.3, 0, 0 } }, { "spine", { 0.5, 0, 0 } } } },
			{ 0.7, { { "hipsOff", { 0, -0.45, 0 } }, { "hipsRot", { 0.3, 0, 0 } }, { "lThigh", { -1.6, 0, 0.1 } }, { "rThigh", { -1.4, 0, -0.1 } }, { "lShin", { 1.9, 0, 0 } }, { "rShin", { 1.7, 0, 0 } }, { "spine", { 0.5, 0, 0 } }, { "lUpperArm", { -0.3, 0, 0.3 } }, { "rUpperArm", { -0.3, 0, -0.3 } } } },
			{ 1, { { "hipsOff", { 0, 0, 0 } } } } } };
		return a;
	}();
	return A;
}

struct PoseVal { std::string name; V3 v; double w; };
// the pose at t between the surrounding keys (smoothstep easing); bones only in one key fade to "no override"
void KeyInterp(const std::vector<std::pair<double, KeyPose>>& keys, double t, std::vector<PoseVal>& out) {
	const std::pair<double, KeyPose>* a = &keys.front();
	const std::pair<double, KeyPose>* b = &keys.back();
	for (size_t i = 0; i + 1 < keys.size(); i++) if (t >= keys[i].first && t <= keys[i + 1].first) { a = &keys[i]; b = &keys[i + 1]; break; }
	const double span = b->first - a->first;
	double k = span > 0 ? (t - a->first) / span : 0;
	k = k * k * (3 - 2 * k);
	out.clear();
	auto find = [](const KeyPose& p, const std::string& n) -> const V3* { for (const auto& kv : p) if (kv.first == n) return &kv.second; return nullptr; };
	std::vector<std::string> names;
	for (const auto& kv : a->second) names.push_back(kv.first);
	for (const auto& kv : b->second) if (std::find(names.begin(), names.end(), kv.first) == names.end()) names.push_back(kv.first);
	for (const std::string& n : names) {
		const V3* va = find(a->second, n); const V3* vb = find(b->second, n);
		PoseVal p; p.name = n;
		if (va && vb) { p.v = V3(Lerp(va->x, vb->x, k), Lerp(va->y, vb->y, k), Lerp(va->z, vb->z, k)); p.w = 1; }
		else if (va) { p.v = *va * (1 - k); p.w = 1 - k; }
		else { p.v = *vb * k; p.w = k; }
		out.push_back(p);
	}
}
} // namespace

Animator::Animator(Pose* p, const V3 r[Bone::COUNT]) : pose(p) {
	for (int i = 0; i < Bone::COUNT; i++) rest[i] = r[i];
	time = Rand() * 10;
	thighOff[0] = rest[Bone::lThigh]; thighOff[1] = rest[Bone::rThigh];
	hipH = rest[Bone::hips].y;
}

std::shared_ptr<AnimAction> Animator::play(const std::string& name, double speed) {
	auto it = ACTIONS().find(name);
	if (it == ACTIONS().end()) return nullptr;
	const ActionDef& def = it->second;
	auto act = std::make_shared<AnimAction>();
	act->name = name; act->def = &def; act->dur = def.dur / speed; act->hitTime = def.hit >= 0 ? def.hit / speed : -1;
	if (def.additive) { additive.push_back(act); return act; }
	action = act;
	return act;
}

void Animator::beginBlend(double dur) {
	for (int i = 0; i < Bone::COUNT; i++) blendFrom[i] = pose->rot[i];
	blendFromPos = pose->pos[0];
	blendT = 0; blendDur = dur; blending = true;
}

void Animator::setBones() {
	for (int i = 0; i < Bone::COUNT; i++) pose->rot[i] = Quat::FromEuler(P[i * 3], P[i * 3 + 1], P[i * 3 + 2], "YXZ");
	pose->pos[0] = V3(hipsOff[0], hipH + hipsOff[1], hipsOff[2]);
	pose->rot[0] = Quat::FromEuler(P[0] + hipsRot[0], P[1] + hipsRot[1], P[2] + hipsRot[2], "YXZ");
	if (blending) {
		blendT += dt_;
		const double k = Min(1, blendT / blendDur);
		const double e = k * k * (3 - 2 * k);
		for (int i = 0; i < Bone::COUNT; i++) pose->rot[i] = Quat::Slerp(blendFrom[i], pose->rot[i], e);
		pose->pos[0] = blendFromPos.lerp(pose->pos[0], e);
		if (k >= 1) blending = false;
	}
}

void Animator::legIK(int side, double tx, double ty, double tz) {
	const int th = side == 0 ? Bone::lThigh : Bone::rThigh, sh = side == 0 ? Bone::lShin : Bone::rShin, ft = side == 0 ? Bone::lFoot : Bone::rFoot;
	const double lat = std::atan2(tx, -ty);
	const double vy = Hypot(tx, ty);
	double d = Hypot(vy, tz);
	d = Clamp(d, 0.1, L1 + L2 - 0.0005);
	const double cosK = Clamp((L1 * L1 + L2 * L2 - d * d) / (2 * L1 * L2), -1, 1);
	const double knee = kPi - std::acos(cosK);
	const double cosA = Clamp((L1 * L1 + d * d - L2 * L2) / (2 * L1 * d), -1, 1);
	const double alpha = std::acos(cosA);
	const double theta = std::atan2(tz, vy);
	const double thighX = -(theta + alpha);
	set(th, thighX, 0, lat);
	set(sh, knee, 0, 0);
	set(ft, -(thighX + knee), 0, -lat * 0.5);
}

void Animator::update(double dt, const AnimState& s) {
	using namespace Bone;
	dt_ = dt;
	time += dt;
	for (double& v : P) v = 0;
	hipsOff[0] = hipsOff[1] = hipsOff[2] = 0;
	hipsRot[0] = hipsRot[1] = hipsRot[2] = 0;
	const double speed = s.speed;
	accel = Damp(accel, Clamp((speed - prevSpeed) / Max(dt, 1e-3), -12, 12), 6, dt);
	prevSpeed = speed;
	turnF = Damp(turnF, Clamp(s.turn, -6, 6), 8, dt);
	double tw[4] = { 0, 0, 0, 0 };
	if (speed < 0.15) tw[0] = 1;
	else if (speed < 2.2) { const double k = SmoothStep(0.15, 1.2, speed); tw[0] = 1 - k; tw[1] = k; }
	else if (speed < 5.2) { const double k = SmoothStep(2.2, 4.2, speed); tw[1] = 1 - k; tw[2] = k; }
	else { const double k = SmoothStep(5.2, 7.0, speed); tw[2] = 1 - k; tw[3] = k; }
	w.idle = Damp(w.idle, tw[0], 10, dt); w.walk = Damp(w.walk, tw[1], 10, dt); w.run = Damp(w.run, tw[2], 10, dt); w.sprint = Damp(w.sprint, tw[3], 10, dt);
	w.aim = Damp(w.aim, s.aim ? 1 : 0, 14, dt);
	w.crouch = Damp(w.crouch, s.crouch ? 1 : 0, 8, dt);
	w.air = Damp(w.air, !s.grounded && !s.swim && !s.sit ? 1 : 0, 10, dt);
	w.sit = Damp(w.sit, s.sit ? 1 : 0, 12, dt);
	w.swim = Damp(w.swim, s.swim ? 1 : 0, 5, dt);
	w.cower = Damp(w.cower, s.cower ? 1 : 0, 6, dt);
	w.hands = Damp(w.hands, s.handsUp ? 1 : 0, 6, dt);
	w.talk = Damp(w.talk, s.talking ? 1 : 0, 3, dt);
	const bool heavy = s.weapon == "rifle" || s.weapon == "smg" || s.weapon == "shotgun" || s.weapon == "rpg";
	w.rifle = Damp(w.rifle, heavy ? 1 : 0, 10, dt);

	Gait gp{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	const double gw[4] = { w.idle, w.walk, w.run, w.sprint };
	for (int g = 0; g < 4; g++) {
		const Gait& G2 = GAITS[g]; const double k = gw[g];
		gp.duty += G2.duty * k; gp.lift += G2.lift * k; gp.drop += G2.drop * k; gp.bob += G2.bob * k; gp.armA += G2.armA * k; gp.elbow += G2.elbow * k;
		gp.elbowSw += G2.elbowSw * k; gp.lean += G2.lean * k; gp.chestYaw += G2.chestYaw * k; gp.hipYaw += G2.hipYaw * k; gp.width += G2.width * k;
	}
	const double moving = 1 - w.idle;
	const double strideLen = Lerp(1.25, 3.9, Clamp((speed - 1.2) / 6.3, 0, 1)) * (1 - w.crouch * 0.35);
	const double freq = speed > 0.05 ? speed / strideLen : 0;
	phase = std::fmod(phase + freq * dt, 1.0);
	const double ph = phase;
	const double mA = s.moveAngle;
	const double mx = std::sin(mA), mz = std::cos(mA);

	const double bob = gp.bob * std::pow(std::sin(kTau * ph * 2 - kPi / 2) * 0.5 + 0.5, 1.0) * moving;
	hipsOff[1] = -gp.drop - bob - w.crouch * 0.42;
	const double shift = std::sin(time * 0.37) * std::sin(time * 0.13 + 1.3);
	hipsOff[0] = -std::cos(kTau * ph) * 0.018 * w.walk + (std::sin(time * 0.7) * 0.006 + shift * 0.012) * w.idle;
	const double hipYaw = -gp.hipYaw * std::sin(kTau * ph) * moving;
	const double hipRoll = std::cos(kTau * ph) * (0.05 * w.walk + 0.035 * w.run + 0.02 * w.sprint) + shift * 0.035 * w.idle;
	const double bank = Clamp(-turnF * speed * 0.018, -0.22, 0.22) * moving;
	const double accLean = Clamp(accel * 0.018, -0.12, 0.16) * moving;
	set(hips, accLean * 0.4, hipYaw * mz, hipRoll + bank);

	for (int side = 0; side < 2; side++) {
		const double p = std::fmod(ph + side * 0.5, 1.0);
		const double D = gp.duty * moving + (1 - moving);
		const double half = strideLen * D * 0.5 * moving;
		double fz, fy;
		if (p < D || moving < 0.01) { const double q = D > 0 ? p / D : 0; fz = Lerp(half, -half, q); fy = 0; }
		else {
			const double q = (p - D) / (1 - D);
			const double e = q * q * (3 - 2 * q);
			fz = Lerp(-half, half, e);
			fy = gp.lift * std::sin(kPi * std::pow(q, 0.75)) * moving;
		}
		const double lateral = (side == 0 ? 1 : -1) * gp.width;
		const double tx = lateral + fz * mx, tz = fz * mz;
		const V3& off = thighOff[side];
		const double hipY = hipH + hipsOff[1] + off.y;
		double roll = 0, heel = 0;
		if (moving > 0.05 && D < 0.999) {
			if (p < D) {
				const double q = p / D;
				roll = -0.22 * (1 - SmoothStep(0, 0.18, q)) + 0.55 * SmoothStep(0.62, 1, q);
				heel = 0.05 * SmoothStep(0.62, 1, q);
			} else {
				const double q = (p - D) / (1 - D);
				roll = Lerp(0.55, -0.22, SmoothStep(0, 0.9, q));
			}
			roll *= moving * (mz >= 0 ? 1 : 0.4) * (1 - w.crouch * 0.5);
			heel *= moving * (mz >= 0 ? 1 : 0.3);
		}
		const double ankleH = 0.06 + fy + heel;
		legIK(side, tx - off.x - hipsOff[0], ankleH - hipY, tz + (w.crouch * 0.12));
		add(side == 0 ? lFoot : rFoot, roll, 0, 0);
		add(side == 0 ? lThigh : rThigh, 0, -hipYaw * mz, -hipRoll * 0.8);
	}

	const double s1 = std::sin(kTau * ph);
	const double lean = gp.lean * (mz >= 0 ? 1 : -0.4) + w.crouch * 0.3 + accLean;
	const double breathe = std::sin(time * 1.9) * (0.018 * w.idle + 0.01 * moving);
	const double glance = w.idle * (std::sin(time * 0.23) * std::sin(time * 0.61 + 2.0)) * 0.45;
	set(spine, lean * 0.5 + breathe * 0.6, gp.chestYaw * s1 * 0.4 * moving * mz, -hipRoll * 0.5 - bank * 0.3);
	set(chest, lean * 0.5 + breathe, gp.chestYaw * s1 * 0.6 * moving * mz, -hipRoll * 0.4 - bank * 0.2);
	set(neck, -lean * 0.3, glance * 0.35, hipRoll * 0.3);
	set(head, -lean * 0.4 + 0.02 * std::sin(time * 0.9) * w.idle, -hipYaw * 0.5 + glance * 0.65, bank * 0.4 + hipRoll * 0.3);
	const double aA = gp.armA * moving;
	const double cross = (0.12 * w.run + 0.18 * w.sprint);
	const double s2 = std::sin(kTau * ph - 0.35);
	set(lUpperArm, aA * s1 + 0.04 - breathe * 0.3, -cross * Max(0, -s1), 0.09 + w.run * 0.05 + breathe * 0.4);
	set(rUpperArm, -aA * s1 + 0.04 - breathe * 0.3, cross * Max(0, s1), -0.09 - w.run * 0.05 - breathe * 0.4);
	set(lForearm, -(gp.elbow + gp.elbowSw * Max(0, -s2) * moving), 0.08 * moving, 0);
	set(rForearm, -(gp.elbow + gp.elbowSw * Max(0, s2) * moving), -0.08 * moving, 0);
	set(lHand, 0.08 * std::sin(kTau * ph - 0.8) * moving, 0, 0.1);
	set(rHand, -0.08 * std::sin(kTau * ph - 0.8) * moving, 0, -0.1);

	if (w.rifle > 0.01 && !s.sit && !s.swim) {
		const double k = w.rifle * (1 - w.aim);
		mix(rUpperArm, -0.35, 0.1, -0.25, k);
		mix(rForearm, -1.35, 0, 0, k);
		mix(lUpperArm, -0.75, -0.55, 0.1, k);
		mix(lForearm, -1.45, 0, 0, k);
	}

	if (w.air > 0.01) {
		const double k = w.air;
		const double fall = Clamp(-s.vy / 12, 0, 1);
		const double fl = std::sin(time * 12) * fall;
		mix(lThigh, -0.7, 0, 0.1, k); mix(rThigh, -0.25, 0, -0.1, k);
		mix(lShin, 1.1, 0, 0, k); mix(rShin, 0.6, 0, 0, k);
		mix(lFoot, -0.2, 0, 0, k); mix(rFoot, -0.2, 0, 0, k);
		if (w.rifle < 0.5 && w.aim < 0.5) {
			mix(lUpperArm, -0.6 + fl * 0.8, 0, 0.7 + fl * 0.3, k); mix(rUpperArm, -0.6 - fl * 0.8, 0, -0.7 - fl * 0.3, k);
			mix(lForearm, -0.5, 0, 0, k); mix(rForearm, -0.5, 0, 0, k);
		}
	}

	if (w.swim > 0.01) {
		const double k = w.swim;
		const double t = time * (speed > 0.3 ? 2.2 : 1.0);
		const double st = std::sin(t * kTau * 0.5);
		hipsRot[0] += 1.25 * k * (speed > 0.3 ? 1 : 0.45);
		hipsOff[1] = Lerp(hipsOff[1], -0.55, k);
		mix(head, -1.0 * (speed > 0.3 ? 1 : 0.4), 0, 0, k);
		mix(neck, -0.3, 0, 0, k);
		const double flutter = std::sin(time * 9) * 0.35;
		mix(lThigh, flutter, 0, 0.12, k); mix(rThigh, -flutter, 0, -0.12, k);
		mix(lShin, 0.35 + flutter * 0.4, 0, 0, k); mix(rShin, 0.35 - flutter * 0.4, 0, 0, k);
		mix(lFoot, 0.6, 0, 0, k); mix(rFoot, 0.6, 0, 0, k);
		const double arm = st * 0.5 + 0.5;
		mix(lUpperArm, Lerp(-2.9, -1.2, arm), 0, Lerp(0.2, 1.3, arm), k);
		mix(rUpperArm, Lerp(-2.9, -1.2, arm), 0, -Lerp(0.2, 1.3, arm), k);
		mix(lForearm, Lerp(-0.2, -1.4, arm), 0, 0, k); mix(rForearm, Lerp(-0.2, -1.4, arm), 0, 0, k);
	}

	const int legBones[6] = { lThigh, lShin, lFoot, rThigh, rShin, rFoot };
	if (w.sit > 0.01 && s.bike == "board" && s.hasFeet) {
		const double k = w.sit;
		const double crouch = 0.06 + s.boardCrouch;
		hipsOff[1] = Lerp(hipsOff[1], -crouch, k);
		hipsOff[0] = Lerp(hipsOff[0], 0, k); hipsOff[2] = Lerp(hipsOff[2], 0, k);
		set(hips, P[hips * 3] * (1 - k), 0.25 * k, 0);
		double save[Bone::COUNT * 3]; std::copy(P, P + Bone::COUNT * 3, save);
		const double hipY = hipH - crouch;
		for (int side = 0; side < 2; side++) {
			const V3& off = thighOff[side]; const V3& f = s.feet[side];
			legIK(side, f.x - off.x, f.y - hipY - off.y, f.z - off.z);
		}
		for (int b : legBones) for (int c = 0; c < 3; c++) { const int i = b * 3 + c; P[i] = Lerp(save[i], P[i], k); }
		const double air = Clamp(s.boardCrouch / 0.3, 0, 1);
		const double sway = std::sin(time * 1.7) * 0.06;
		mix(spine, 0.14 + air * 0.25, 0.1, 0, k); mix(chest, 0.06, 0.25, 0, k);
		mix(head, -0.1, 0.95 + s.lookYaw * 0.3, 0, k);
		mix(lUpperArm, -0.25 - air * 0.5, 0.1, 0.95 + sway + air * 0.3, k); mix(lForearm, -0.35, 0, 0, k);
		mix(rUpperArm, -0.2 - air * 0.5, -0.1, -0.85 + sway - air * 0.3, k); mix(rForearm, -0.45, 0, 0, k);
		mix(lHand, 0, 0, 0.2, k); mix(rHand, 0, 0, -0.2, k);
	} else if (w.sit > 0.01) {
		const double k = w.sit;
		hipsOff[1] = Lerp(hipsOff[1], -0.46, k);
		hipsOff[2] = Lerp(hipsOff[2], 0, k);
		hipsOff[0] = Lerp(hipsOff[0], 0, k);
		set(hips, P[hips * 3] * (1 - k), P[hips * 3 + 1] * (1 - k), 0);
		double save[Bone::COUNT * 3]; std::copy(P, P + Bone::COUNT * 3, save);
		for (int side = 0; side < 2; side++) {
			const V3& off = thighOff[side];
			if (!s.bike.empty() && s.hasFeet) { const V3& f = s.feet[side]; legIK(side, f.x - off.x, f.y - off.y, f.z - off.z); }
			else {
				const double hipY = hipH - 0.46 + off.y;
				legIK(side, (side == 0 ? 1 : -1) * 0.13 - off.x, 0.12 - hipY, 0.5);
			}
		}
		for (int b : legBones) for (int c = 0; c < 3; c++) { const int i = b * 3 + c; P[i] = Lerp(save[i], P[i], k); }
		const double steer = s.steer;
		if (!s.bike.empty()) {
			const bool pill = s.bike == "pillion", moto = s.bike == "moto";
			mix(spine, pill ? 0.18 : moto ? 0.42 : 0.3, 0, 0, k);
			mix(chest, pill ? 0.08 : moto ? 0.24 : 0.16, 0, 0, k);
			mix(head, pill ? -0.15 : -0.55, s.lookYaw * 0.5, 0, k);
			if (pill) {
				mix(lUpperArm, -0.75, -0.2, 0.35, k); mix(lForearm, -1.1, 0, 0, k);
				mix(rUpperArm, -0.75, 0.2, -0.35, k); mix(rForearm, -1.1, 0, 0, k);
			} else {
				const double st = Clamp(steer, -0.6, 0.6);
				mix(lUpperArm, -0.95 + st * 0.3, -0.15, 0.32, k); mix(lForearm, -0.45 - st * 0.25, 0, 0, k);
				mix(rUpperArm, -0.95 - st * 0.3, 0.15, -0.32, k); mix(rForearm, -0.45 + st * 0.25, 0, 0, k);
				mix(lHand, -0.2, 0, 0.3, k); mix(rHand, -0.2, 0, -0.3, k);
			}
		} else {
			mix(spine, -0.12, 0, 0, k); mix(chest, -0.06, 0, 0, k); mix(head, 0.1, s.lookYaw * 0.6, 0, k);
			if (s.sit == 1) {
				mix(lUpperArm, -0.95 + steer * 0.35, -0.25, 0.25, k); mix(lForearm, -0.75 - steer * 0.2, 0, 0, k);
				mix(rUpperArm, -0.95 - steer * 0.35, 0.25, -0.25, k); mix(rForearm, -0.75 + steer * 0.2, 0, 0, k);
				mix(lHand, -0.3, 0, 0.6, k); mix(rHand, -0.3, 0, -0.6, k);
			} else {
				mix(lUpperArm, -0.35, 0, 0.1, k); mix(lForearm, -0.9, 0, 0, k);
				mix(rUpperArm, -0.35, 0, -0.1, k); mix(rForearm, -0.9, 0, 0, k);
			}
		}
	}

	if (w.aim > 0.01) {
		const double k = w.aim;
		const double pitch = s.aimPitch;
		if (s.sit && s.aimTwist != 0) { add(spine, 0, s.aimTwist * 0.45 * k, 0); add(chest, 0, s.aimTwist * 0.4 * k, 0); }
		const double rec = recoil;
		if (s.weapon == "pistol") {
			mix(rUpperArm, -kPi / 2 - pitch - rec * 0.35, 0.3, 0, k);
			mix(rForearm, -0.05, 0, 0, k);
			mix(lUpperArm, -kPi / 2 - pitch + 0.12 - rec * 0.3, -0.62, 0, k);
			mix(lForearm, -0.55, 0, 0, k);
			mix(rHand, 0, 0, 0, k); mix(lHand, 0, 0, 0, k);
			mix(chest, -pitch * 0.3 + P[chest * 3] * 0.3, 0.12, 0, k);
			mix(head, -pitch * 0.35, -0.12, 0, k);
		} else if (s.weapon == "rpg") {
			mix(rUpperArm, -1.3 - pitch, 0.15, -0.45, k);
			mix(rForearm, -1.1, 0, 0, k);
			mix(lUpperArm, -1.5 - pitch, -0.45, 0.1, k);
			mix(lForearm, -0.5, 0, 0, k);
			mix(chest, -pitch * 0.3, 0.15, 0, k);
			mix(head, -pitch * 0.4, -0.15, 0.15, k);
		} else if (heavy) {
			mix(rUpperArm, -0.75 - pitch * 0.9 - rec * 0.2, 0.25, -0.55, k);
			mix(rForearm, -1.45, 0, 0, k);
			mix(lUpperArm, -1.35 - pitch - rec * 0.2, -0.75, 0.15, k);
			mix(lForearm, -0.45, 0, 0, k);
			mix(rHand, 0, 0, 0.2, k);
			mix(chest, -pitch * 0.35, 0.22, 0, k);
			mix(head, -pitch * 0.35 + 0.05, -0.2, 0.1, k);
		}
		recoil = Max(0, recoil - dt * 8);
	}

	std::vector<PoseVal> pv;
	if (action && !action->done) {
		AnimAction& act = *action;
		act.t += dt;
		const double nt = Min(1, act.t / act.dur);
		KeyInterp(act.def->keys, nt, pv);
		const bool full = act.def->full;
		for (const PoseVal& v : pv) {
			if (v.name == "hipsOff") { hipsOff[0] += v.v.x; hipsOff[1] += v.v.y; hipsOff[2] += v.v.z; continue; }
			if (v.name == "hipsRot") { hipsRot[0] += v.v.x; hipsRot[1] += v.v.y; hipsRot[2] += v.v.z; continue; }
			const int bi = BoneIndex(v.name);
			if (bi < 0) continue;
			const double wgt = full ? 1 : v.w;
			if (v.name == "chest" || v.name == "spine" || v.name == "head") add(bi, v.v.x, v.v.y, v.v.z);
			else mix(bi, v.v.x, v.v.y, v.v.z, Clamp(wgt, 0, 1));
		}
		if (act.t >= act.dur) act.done = true;
	}
	for (int i = (int)additive.size() - 1; i >= 0; i--) {
		AnimAction& a = *additive[i];
		a.t += dt;
		KeyInterp(a.def->keys, Min(1, a.t / a.dur), pv);
		for (const PoseVal& v : pv) { const int bi = BoneIndex(v.name); if (bi >= 0) add(bi, v.v.x, v.v.y, v.v.z); }
		if (a.t >= a.dur) additive.erase(additive.begin() + i);
	}

	if (w.cower > 0.01) {
		const double k = w.cower;
		if (s.sit) {
			mix(spine, 0.35, 0, 0, k); mix(chest, 0.3, 0, 0, k); mix(head, 0.45, 0, 0, k);
			mix(lUpperArm, -1.45, -0.2, 0.35, k); mix(rUpperArm, -1.45, 0.2, -0.35, k);
			mix(lForearm, -2.1, 0, 0, k); mix(rForearm, -2.1, 0, 0, k);
		} else {
			mix(lUpperArm, -2.3, -0.3, 0.7, k); mix(rUpperArm, -2.3, 0.3, -0.7, k);
			mix(lForearm, -2.0, 0, 0, k); mix(rForearm, -2.0, 0, 0, k);
			mix(head, 0.5, 0, 0, k);
		}
	}
	if (w.hands > 0.01) {
		const double k = w.hands;
		if (s.sit) {
			mix(lUpperArm, -0.9, 0, 0.45, k); mix(rUpperArm, -0.9, 0, -0.45, k);
			mix(lForearm, -1.5, 0, 0, k); mix(rForearm, -1.5, 0, 0, k);
		} else {
			mix(lUpperArm, -0.2, 0, 2.6, k); mix(rUpperArm, -0.2, 0, -2.6, k);
			mix(lForearm, -0.3, 0, 0, k); mix(rForearm, -0.3, 0, 0, k);
		}
	}
	if (w.talk > 0.01 && !busy()) {
		const double k = w.talk, t = time;
		add(rUpperArm, (-0.5 + std::sin(t * 2.3) * 0.25) * k, 0.2 * k, 0);
		add(rForearm, (-0.9 + std::sin(t * 3.1) * 0.3) * k, 0, 0);
		add(lUpperArm, (-0.2 + std::sin(t * 1.7 + 1) * 0.15) * k, 0, 0);
		add(lForearm, (-0.5 + std::sin(t * 2.7 + 2) * 0.2) * k, 0, 0);
		add(head, std::sin(t * 1.3) * 0.08 * k, std::sin(t * 0.8) * 0.15 * k, 0);
	}
	if (s.lookYaw != 0) add(head, s.lookPitch, Clamp(s.lookYaw, -1.1, 1.1) * 0.7, 0);
	setBones();
}

} // namespace atg
