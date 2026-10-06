#include "Camera.h"
#include "Collision.h"
#include "Game.h"
#include "Input.h"
#include "Player.h"
#include "Vehicle.h"

namespace atg {

namespace {
// three.js Euler (XYZ) from a quaternion, and back
V3 EulerXYZ(const Quat& q) {
	const M4 m = M4::Compose(V3(), q);
	const double m11 = m.m[0], m12 = m.m[4], m13 = m.m[8], m22 = m.m[5], m23 = m.m[9], m32 = m.m[6], m33 = m.m[10];
	V3 e;
	e.y = std::asin(Clamp(m13, -1, 1));
	if (std::fabs(m13) < 0.9999999) { e.x = std::atan2(-m23, m33); e.z = std::atan2(-m12, m11); }
	else { e.x = std::atan2(m32, m22); e.z = 0; }
	return e;
}
}

bool CameraRig::inView(const V3& p, double radius) const {
	const V3 l = camQuat.inverse().rotate(p - camPos); // camera space: looking down -z
	const double depth = -l.z;
	if (depth < 0.25 - radius || depth > 9000 + radius) return false;
	const double ty = std::tan(camFov * kPi / 360), tx = ty * game.viewAspect;
	// (distance from each side plane, its normal pointing in)
	const double ny = 1 / std::sqrt(1 + ty * ty), nx = 1 / std::sqrt(1 + tx * tx);
	if ((depth * ty - std::fabs(l.y)) * ny < -radius) return false;
	if ((depth * tx - std::fabs(l.x)) * nx < -radius) return false;
	return true;
}

void CameraRig::lookAt(const V3& target, const V3& up) {
	// a camera looks down its -z: z = from the target to the camera
	V3 z = camPos - target;
	if (z.lengthSq() == 0) z.z = 1;
	z.normalize();
	V3 x = up.cross(z);
	if (x.lengthSq() == 0) {
		if (std::fabs(up.z) == 1) z.x += 0.0001; else z.z += 0.0001;
		z.normalize();
		x = up.cross(z);
	}
	x.normalize();
	const V3 y = z.cross(x);
	camQuat = Quat::FromMatrix(M4::Basis(x, y, z).m);
}

void CameraRig::update(double dt, const Input* input, Player& player) {
	time += dt;
	double dx = 0, dy = 0;
	if (input && input->enabled) input->lookDelta(dx, dy);
	if (scopeBlend > 0.01) { const double k = Lerp(1, fov / 60, scopeBlend); dx *= k; dy *= k; }
	const bool moved = std::fabs(dx) + std::fabs(dy) > 0.0005;
	if (moved) lastLookInput = time;
	if (hasCine) {
		camPos = camPos.lerp(cine.pos, 1 - std::exp(-dt * 3));
		lookAt(cine.target);
		fov = Damp(fov, cine.fov, 4, dt);
		finish(dt);
		return;
	}
	Vehicle* veh = player.vehicle;
	if (veh && (veh->def.kind == "plane" || veh->def.kind == "jet") && !player.dead) { flightCam(dt, veh); return; }
	const bool cineOn = veh && cineHeld && !veh->def.tank && !player.dead;
	if (cineOn != cineBars) { cineBars = cineOn; if (!cineOn) hasCv = false; }
	if (cineOn) { vehCine(dt, veh); return; }
	hasFlightOff = false;
	if (veh && veh->def.tank) {
		yaw = WrapAngle(yaw - dx);
		pitch = Clamp(pitch - dy, -0.8, 0.28);
		const double size = veh->def.CamDist(12);
		const double ds[3] = { size, size * 1.4, size * 0.62 };
		dist = ds[vehicleCamIndex % 3];
		pivot.set(veh->pos.x, veh->pos.y + veh->def.CamHeight(3), veh->pos.z);
		fovBase = 60;
	} else if (veh) {
		const double vyaw = veh->yaw, speed = veh->speed();
		if (moved) { vehYawOffset = WrapAngle(vehYawOffset - dx); vehPitch = Clamp(vehPitch - dy, -0.9, 0.35); }
		else if (time - lastLookInput > 1.2 && std::fabs(speed) > 2) {
			vehYawOffset = DampAngle(vehYawOffset, 0, 2.5, dt);
			vehPitch = Damp(vehPitch, -0.12, 2, dt);
		}
		const double velYaw = Hypot(veh->vel.x, veh->vel.z) > 3 ? std::atan2(veh->vel.x, veh->vel.z) : vyaw;
		const double drift = WrapAngle(velYaw - vyaw);
		double baseYaw = vyaw + Clamp(drift, -0.6, 0.6) * 0.45;
		if (lookBehind) baseYaw += kPi;
		const double targetYaw = baseYaw + vehYawOffset + kPi;
		yaw = DampAngle(yaw, targetYaw, moved ? 30 : 6, dt);
		pitch = Damp(pitch, vehPitch, 8, dt);
		const double size = veh->def.CamDist(7.5);
		const double ds[3] = { size, size * 1.45, size * 0.6 };
		dist = ds[vehicleCamIndex % 3];
		pivot.set(veh->pos.x, veh->pos.y + veh->def.CamHeight(1.6), veh->pos.z);
		fovBase = 64 + Clamp(std::fabs(speed) / 45, 0, 1) * 14 + boostFov;
	} else {
		yaw = WrapAngle(yaw - dx);
		pitch = Clamp(pitch - dy, -1.35, 0.9);
		const bool aiming = player.aiming && !player.dead;
		aimBlend = Damp(aimBlend, aiming ? 1 : 0, 12, dt);
		const bool scoped = aiming && player.weaponDef().scope && !player.swimming;
		scopeBlend = Damp(scopeBlend, scoped ? 1 : 0, scoped ? 16 : 22, dt);
		const bool hide = scopeBlend > 0.55;
		if (hide != scopeHid) { scopeHid = hide; player.visible = !hide; }
		const double hy = player.swimming ? 0.6 : player.crouching ? 1.15 : 1.62;
		const V3 tp = player.ragdolling ? player.ragdoll->center() : player.pos;
		const double py = player.ragdolling ? tp.y + 0.6 : tp.y + hy;
		const V3 t(tp.x, py, tp.z);
		pivot = pivot.lerp(t, player.ragdolling ? 1 - std::exp(-dt * 5) : 1);
		if (!player.ragdolling) pivot = t;
		dist = player.chute ? 9 : Lerp(Lerp(4.3, 2.0, aimBlend), 0.02, scopeBlend);
		if (player.chute) pivot.y += 2.4;
		fovBase = Lerp(Lerp(64, 48, aimBlend) + (player.sprinting ? 4 : 0), scopeFov, scopeBlend * scopeBlend);
	}
	const double cp = std::cos(pitch), sp = std::sin(pitch);
	const V3 d(std::sin(yaw) * cp, -sp, std::cos(yaw) * cp);
	const double side = veh ? 0 : Lerp(Lerp(0.35, 0.62, aimBlend), 0, scopeBlend);
	const double rx = -std::cos(yaw), rz = std::sin(yaw);
	V3 piv = pivot;
	piv.x += rx * side * -1; piv.z += rz * side * -1;
	if (!veh) piv.y += Lerp(0.05, 0.12, aimBlend);
	double want = dist;
	RayHit hit;
	RayOpts ro; ro.ignoreProps = true; ro.ignoreSoft = true;
	if (game.collision->raycast(piv.x, piv.y, piv.z, d.x, d.y, d.z, want + 0.3, hit, ro)) want = Max(0.35, hit.t - 0.3);
	curDist = want < curDist ? want : Damp(curDist, want, 4, dt);
	pos = piv + d * curDist;
	const double gh = game.map.GroundHeight(pos.x, pos.z);
	if (pos.y < gh + 0.25) pos.y = gh + 0.25;
	camPos = pos;
	lookAt(piv - d * 10);
	fov = Damp(fov, fovBase, 6, dt);
	finish(dt);
}

void CameraRig::flightCam(double dt, Vehicle* veh) {
	const Quat q = veh->bodyQuat();
	const V3 f = q.rotate(V3(0, 0, 1)), u = q.rotate(V3(0, 1, 0));
	const V3 cg = veh->cgPoint();
	const double size = veh->def.CamDist(15);
	const double ds[3] = { size, size * 1.6, size * 0.5 };
	const double d = ds[vehicleCamIndex % 3];
	const double back = lookBehind ? -1 : 1;
	V3 o = f * (-d * back);
	o.y *= 0.55;
	o.addScaled(u, d * 0.17);
	o.y += d * 0.08;
	if (!hasFlightOff) { hasFlightOff = true; flightOff = o; }
	flightOff = flightOff.lerp(o, 1 - std::exp(-dt * 7));
	pos = cg + flightOff;
	const double gh = game.map.GroundHeight(pos.x, pos.z);
	if (pos.y < gh + 1.2) pos.y = gh + 1.2;
	camPos = pos;
	const V3 up = V3(0, 1, 0).lerp(u, 0.4).normalized();
	lookAt(cg + f * (300 * back), up);
	yaw = veh->yaw + kPi;
	pitch = -0.15;
	curDist = d;
	const double spd = Max(0, veh->forwardSpeed());
	fov = Damp(fov, 60 + Clamp(spd / 160, 0, 1) * 16, 4, dt);
	finish(dt);
}

void CameraRig::vehCine(double dt, Vehicle* veh) {
	if (!hasCv) { hasCv = true; cv = CineShot(); }
	CineShot& st = cv;
	st.t += dt;
	const double sp = Hypot(veh->vel.x, veh->vel.z);
	const double hy = sp > 2 ? std::atan2(veh->vel.x, veh->vel.z) : veh->yaw;
	const double fx = std::sin(hy), fz = std::cos(hy), rx = -fz, rz = fx;
	const double h = Max(1, veh->def.CamHeight(1.6) * 0.55);
	const double size = Max(4.5, veh->def.L ? veh->def.L : 4.5);
	const V3 tgt(veh->pos.x, veh->pos.y + h, veh->pos.z);
	CollisionWorld& col = *game.collision;
	bool cut = st.shot < 0 || st.t > st.len;
	if (st.shot == 0) {
		const double along = (veh->pos.x - st.anchor.x) * fx + (veh->pos.z - st.anchor.z) * fz;
		if (along > 22 + size || Hypot(veh->pos.x - st.anchor.x, veh->pos.z - st.anchor.z) > 90) cut = true;
	}
	if (cut) {
		st.n++;
		static const int o4[4] = { 0, 1, 2, 3 }, o3[3] = { 1, 2, 3 };
		st.shot = sp > 6 ? o4[st.n % 4] : o3[st.n % 3];
		st.t = 0; st.len = 4 + Rand() * 2.5; st.side = Rand() < 0.5 ? 1 : -1;
		if (st.shot == 0) {
			bool ok = false;
			const double tries[4][2] = { { 1, st.side }, { 1, -st.side }, { 0.6, st.side }, { 0.6, -st.side } };
			for (const auto& tr : tries) {
				const double k = tr[0], sd = tr[1];
				const double ahead = Clamp(sp * 2.4, 16, 55) * k, off = (5 + Rand() * 4) * k;
				st.anchor.set(veh->pos.x + fx * ahead + rx * off * sd, 0, veh->pos.z + fz * ahead + rz * off * sd);
				st.anchor.y = Max(game.map.GroundHeight(st.anchor.x, st.anchor.z), veh->pos.y - 2) + 1.1 + Rand() * 1.6;
				const V3 dd = (st.anchor - tgt).normalized();
				if (col.lineOfSight(st.anchor.x, st.anchor.y, st.anchor.z, tgt.x + dd.x * 3.5, tgt.y + 0.4 + dd.y * 3.5, tgt.z + dd.z * 3.5)) { ok = true; break; }
			}
			st.len = 9;
			if (!ok) { st.shot = 1; st.len = 4.5; }
		}
		st.fresh = true;
	}
	V3 want;
	V3 look = tgt;
	double f = 45, follow = 7;
	switch (st.shot) {
	case 0: want = st.anchor; f = 36; follow = 0; break;
	case 1: want.set(veh->pos.x + rx * (size * 1.1) * st.side + fx * size * 0.35, tgt.y + 0.1, veh->pos.z + rz * (size * 1.1) * st.side + fz * size * 0.35); look.addScaled(V3(fx, 0, fz), size * 0.3); f = 44; follow = 9; break;
	case 2: want.set(veh->pos.x - fx * size * 3.4 + rx * 3 * st.side, tgt.y + size * 1.9, veh->pos.z - fz * size * 3.4 + rz * 3 * st.side); look.addScaled(V3(fx, 0, fz), size); f = 40; follow = 4; break;
	default: want.set(veh->pos.x + fx * size * 2.8 + rx * 1.4 * st.side, tgt.y + 0.45, veh->pos.z + fz * size * 2.8 + rz * 1.4 * st.side); f = 46; follow = 12; break;
	}
	if (st.shot != 0) {
		V3 d = want - tgt; const double L = d.length(); d = d / (L > 0 ? L : 1);
		RayHit hit; RayOpts ro; ro.ignoreProps = true; ro.ignoreSoft = true;
		if (col.raycast(tgt.x, tgt.y, tgt.z, d.x, d.y, d.z, L + 0.3, hit, ro)) want = tgt + d * Max(1.5, hit.t - 0.4);
	}
	if (st.fresh || follow == 0) { pos = want; st.fresh = false; }
	else pos = pos.lerp(want, 1 - std::exp(-dt * follow));
	const double gh = game.map.GroundHeight(pos.x, pos.z);
	if (pos.y < gh + 0.35) pos.y = gh + 0.35;
	camPos = pos;
	lookAt(look);
	fov = f;
	yaw = veh->yaw + kPi;
	finish(dt);
}

void CameraRig::finish(double dt) {
	if (shake > 0.001) {
		const double s = shake * shake * 0.08;
		const double t = time * 40;
		V3 e = EulerXYZ(camQuat);
		e.x += (std::sin(t * 1.3) + std::sin(t * 2.7)) * s;
		e.y += (std::sin(t * 1.7 + 3) + std::sin(t * 3.1)) * s;
		e.z += std::sin(t * 2.1 + 1) * s * 0.5;
		camQuat = Quat::FromEuler(e.x, e.y, e.z);
		shake = Max(0, shake - dt * 1.8);
	}
	camFov = fov;
}

} // namespace atg
