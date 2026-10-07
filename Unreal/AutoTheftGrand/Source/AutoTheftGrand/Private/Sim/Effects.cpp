#include "Effects.h"
#include "Collision.h"
#include "Game.h"

namespace atg {

namespace {
const V3 UPV3(0, 1, 0);

// the particle defaults (effects.js P())
Particle Pt() {
	Particle p;
	p.rot = Rand() * 6.28;
	p.spin = Rand(-1, 1);
	return p;
}
void Col(Particle& p, float r, float g, float b) { p.color[0] = r; p.color[1] = g; p.color[2] = b; }
void Col1(Particle& p, float r, float g, float b) { p.color1[0] = r; p.color1[1] = g; p.color1[2] = b; p.hasColor1 = true; }

// three.js Euler (XYZ) from a quaternion
void EulerXYZ(const Quat& q, double& x, double& y, double& z) {
	const M4 m = M4::Compose(V3(), q);
	const double* te = m.m;
	const double m11 = te[0], m12 = te[4], m13 = te[8], m22 = te[5], m23 = te[9], m32 = te[6], m33 = te[10];
	y = std::asin(Clamp(m13, -1, 1));
	if (std::fabs(m13) < 0.9999999) { x = std::atan2(-m23, m33); z = std::atan2(-m12, m11); }
	else { x = std::atan2(m32, m22); z = 0; }
}
}

void ParticlePool::update(double dt) {
	for (auto it = parts.begin(); it != parts.end();) {
		Particle& p = *it;
		p.age += dt;
		if (p.age >= p.life) { it = parts.erase(it); continue; }
		const double t = p.age / p.life;
		p.vx *= 1 - p.drag * dt; p.vy *= 1 - p.drag * dt; p.vz *= 1 - p.drag * dt;
		p.vy += p.grav * dt;
		p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
		if (IsSet(p.floor) && p.y < p.floor) {
			p.y = p.floor; p.vy *= -0.2; p.vx *= 0.6; p.vz *= 0.6;
			if (p.onFloor) { auto f = std::move(p.onFloor); p.onFloor = nullptr; f(p); }
		}
		p.rot += p.spin * dt;
		p.size = (float)(p.size0 + (p.size1 - p.size0) * t);
		p.a = (float)(p.alpha * (t < p.fadeIn ? t / p.fadeIn : 1) * (1 - std::pow(t, p.fadePow)));
		const float* c = p.color;
		const float* c1 = p.hasColor1 ? p.color1 : p.color;
		for (int k = 0; k < 3; k++) p.col[k] = (float)(c[k] + (c1[k] - c[k]) * t);
		++it;
	}
}

M4 Effects::Debris::matrix() const { return M4::Compose(pos, Quat::FromEuler(rx, ry, rz), V3(scale, scale, scale)); }

Effects::Effects(Game& g) : game(g) {
	decals.resize(400);
	skids.resize(1500);
	wakes.resize(900);
	tracers.resize(64);
}

void Effects::flash(const V3& pos, uint32_t color, double intensity, double life, double range) {
	Flash* best = &lights[0];
	for (Flash& l : lights) if (l.intensity < best->intensity) best = &l;
	best->pos = pos; best->color = color; best->range = range;
	best->peak = intensity; best->t = 0; best->life = life;
	best->intensity = intensity;
}

void Effects::addDecal(const V3& pos, const V3& normal, double size, int tile, double life, double grow, double targetSize, double alpha) {
	Decal& d = decals[nextDecal];
	nextDecal = (nextDecal + 1) % (int)decals.size();
	const Quat spin = Quat::FromAxisAngle(V3(0, 0, 1), Rand() * 6.28);
	d.q = Quat::FromUnitVectors(V3(0, 0, 1), normal) * spin;
	d.pos = pos + normal * 0.02;
	d.size = size; d.target = targetSize > 0 ? targetSize : size; d.grow = grow; d.t = 0; d.life = life; d.alpha = alpha; d.tile = tile; d.live = true;
	decalVersion++;
}

// ----------------------------------------------------------- emitters
void Effects::muzzleFlash(const V3& pos, const V3& dir, bool big) {
	const double s = big ? 1.4 : 1;
	for (int i = 0; i < 3; i++) {
		Particle p = Pt();
		p.x = pos.x + dir.x * i * 0.08; p.y = pos.y + dir.y * i * 0.08; p.z = pos.z + dir.z * i * 0.08;
		p.life = 0.05; p.size0 = (0.35 - i * 0.08) * s; p.size1 = (0.5 - i * 0.1) * s; Col(p, 6, 3.6f, 1.2f); p.alpha = 1; p.fadePow = 1; p.spin = 0;
		addPool.spawn(std::move(p));
	}
	Particle p = Pt();
	p.x = pos.x; p.y = pos.y; p.z = pos.z; p.vx = dir.x * 1.5; p.vy = 0.4; p.vz = dir.z * 1.5; p.life = 0.6; p.size0 = 0.15; p.size1 = 0.6; Col(p, 0.6f, 0.6f, 0.6f); p.alpha = 0.25;
	alphaPool.spawn(std::move(p));
	flash(pos, 0xffb060, big ? 18 : 10, 0.06, 14);
}

void Effects::impact(const V3& pos, const V3& n, const std::string& kind) {
	if (kind == "metal") {
		for (int i = 0; i < 6; i++) {
			Particle p = Pt();
			p.x = pos.x; p.y = pos.y; p.z = pos.z; p.vx = n.x * 3 + Rand(-3, 3); p.vy = n.y * 3 + Rand(0, 4); p.vz = n.z * 3 + Rand(-3, 3);
			p.life = Rand(0.15, 0.4); p.size0 = 0.06; p.size1 = 0.02; Col(p, 5, 3, 1); p.grav = -12; p.drag = 1;
			addPool.spawn(std::move(p));
		}
	} else {
		for (int i = 0; i < 4; i++) {
			Particle p = Pt();
			p.x = pos.x; p.y = pos.y; p.z = pos.z; p.vx = n.x * 2 + Rand(-1, 1); p.vy = n.y * 2 + Rand(0, 1.5); p.vz = n.z * 2 + Rand(-1, 1);
			p.life = Rand(0.5, 0.9); p.size0 = 0.1; p.size1 = 0.55; Col(p, 0.55f, 0.52f, 0.48f); p.alpha = 0.5; p.grav = -1.5; p.drag = 2.5;
			alphaPool.spawn(std::move(p));
		}
		for (int i = 0; i < 3; i++) {
			Particle p = Pt();
			p.x = pos.x; p.y = pos.y; p.z = pos.z; p.vx = n.x * 3 + Rand(-2, 2); p.vy = n.y * 3 + Rand(1, 3); p.vz = n.z * 3 + Rand(-2, 2);
			p.life = 0.5; p.size0 = 0.05; p.size1 = 0.03; Col(p, 0.3f, 0.28f, 0.25f); p.grav = -12; p.drag = 0.5;
			dotAlpha.spawn(std::move(p));
		}
	}
	addDecal(pos, n, 0.14 + Rand() * 0.05, 0, 60);
}

void Effects::blood(const V3& pos, const V3& dir, double amount) {
	const double floor = game.map.GroundHeight(pos.x, pos.z) + 0.07;
	for (int i = 0; i < (int)amount; i++) {
		const double sp = Rand(1, 4);
		Particle p = Pt();
		p.x = pos.x; p.y = pos.y; p.z = pos.z;
		p.vx = dir.x * sp + Rand(-1.2, 1.2); p.vy = dir.y * sp + Rand(0, 2); p.vz = dir.z * sp + Rand(-1.2, 1.2);
		p.life = Rand(0.5, 1.0); p.size0 = Rand(0.05, 0.1); p.size1 = 0.04; Col(p, 0.45f, 0, 0); p.alpha = 0.95; p.grav = -12; p.drag = 0.4; p.floor = floor;
		p.onFloor = [this, floor](Particle& q) { if (Rand() < 0.3) addDecal(V3(q.x, floor, q.z), UPV3, Rand(0.15, 0.35), 3, 90); };
		dotAlpha.spawn(std::move(p));
	}
	Particle p = Pt();
	p.x = pos.x; p.y = pos.y; p.z = pos.z; p.vx = dir.x; p.vy = 0.2; p.vz = dir.z; p.life = 0.35; p.size0 = 0.15; p.size1 = 0.6; Col(p, 0.5f, 0, 0); p.alpha = 0.6;
	alphaPool.spawn(std::move(p));
}

void Effects::bloodPool(const V3& pos) {
	const double y = game.map.GroundHeight(pos.x, pos.z) + 0.07;
	addDecal(V3(pos.x, y, pos.z), UPV3, 0.3, 1, 150, 0.18, Rand(1.4, 2.2));
}

void Effects::sparks(const V3& pos, double strength) {
	const int n = (int)Min(24, 4 + strength);
	const double floor = game.map.GroundHeight(pos.x, pos.z);
	for (int i = 0; i < n; i++) {
		Particle p = Pt();
		p.x = pos.x; p.y = pos.y; p.z = pos.z; p.vx = Rand(-5, 5); p.vy = Rand(0, 5); p.vz = Rand(-5, 5);
		p.life = Rand(0.2, 0.6); p.size0 = 0.08; p.size1 = 0.02; Col(p, 6, 3.5f, 1); p.grav = -14; p.drag = 0.8; p.floor = floor;
		addPool.spawn(std::move(p));
	}
	flash(pos, 0xffc070, 4, 0.08, 8);
}

void Effects::tireSmokeColor(const V3& pos, double amount, double color) {
	Particle p = Pt();
	p.x = pos.x + Rand(-0.2, 0.2); p.y = pos.y + 0.2; p.z = pos.z + Rand(-0.2, 0.2); p.vx = Rand(-0.5, 0.5); p.vy = Rand(0.3, 1.0); p.vz = Rand(-0.5, 0.5);
	p.life = Rand(1.2, 2.2); p.size0 = 0.5; p.size1 = 3.2 * amount; Col(p, (float)color, (float)color, (float)color); p.alpha = 0.35 * amount; p.drag = 1.2; p.fadeIn = 0.1;
	alphaPool.spawn(std::move(p));
}
void Effects::tireSmoke(const V3& pos, double amount) { tireSmokeColor(pos, amount, 0.85); }

void Effects::dust(const V3& pos, double amount) {
	Particle p = Pt();
	p.x = pos.x; p.y = pos.y + 0.1; p.z = pos.z; p.vx = Rand(-1, 1); p.vy = Rand(0.2, 0.8); p.vz = Rand(-1, 1);
	p.life = Rand(0.8, 1.5); p.size0 = 0.4; p.size1 = 2.2 * amount; Col(p, 0.55f, 0.48f, 0.38f); p.alpha = 0.4; p.drag = 1.5;
	alphaPool.spawn(std::move(p));
}

void Effects::engineSmoke(const V3& pos, double dark) {
	const float c = (float)(0.7 - dark * 0.6);
	Particle p = Pt();
	p.x = pos.x + Rand(-0.3, 0.3); p.y = pos.y; p.z = pos.z + Rand(-0.3, 0.3); p.vx = Rand(-0.3, 0.3); p.vy = Rand(1.2, 2.2); p.vz = Rand(-0.3, 0.3);
	p.life = Rand(1.5, 2.5); p.size0 = 0.4; p.size1 = 2.5; Col(p, c, c, c); p.alpha = 0.4 + dark * 0.3; p.drag = 0.6;
	alphaPool.spawn(std::move(p));
}

void Effects::fire(const V3& pos, double size) {
	Particle p = Pt();
	p.x = pos.x + Rand(-0.3, 0.3) * size; p.y = pos.y; p.z = pos.z + Rand(-0.3, 0.3) * size; p.vx = Rand(-0.3, 0.3); p.vy = Rand(1.5, 3); p.vz = Rand(-0.3, 0.3);
	p.life = Rand(0.35, 0.7); p.size0 = 0.9 * size; p.size1 = 0.2 * size; Col(p, 4, 1.6f, 0.35f); Col1(p, 2, 0.4f, 0.1f); p.alpha = 0.9; p.drag = 0.5; p.fadeIn = 0.1;
	addPool.spawn(std::move(p));
	if (Rand() < 0.5) {
		Particle s = Pt();
		s.x = pos.x; s.y = pos.y + 0.8 * size; s.z = pos.z; s.vx = Rand(-0.3, 0.3); s.vy = Rand(1.5, 2.5); s.vz = Rand(-0.3, 0.3);
		s.life = Rand(1.5, 2.5); s.size0 = 0.6 * size; s.size1 = 3 * size; Col(s, 0.12f, 0.11f, 0.1f); s.alpha = 0.55; s.drag = 0.4;
		alphaPool.spawn(std::move(s));
	}
}

// radius: visual size (6.75 = a car). foot: [halfWidth, halfLength, sin(yaw), cos(yaw)] of the vehicle that
// went up, so a bus or a plane burns along its whole length rather than from one point.
void Effects::explosion(const V3& pos, double radius, const double* foot, bool secondary) {
	const double s = radius / 6;
	const int n = (int)std::round(Clamp(std::pow(s, 0.8), 0.5, 3.4) * 40);
	const double gy = game.map.GroundHeight(pos.x, pos.z);
	double F[4] = { 0, 0, 0, 1 };
	if (foot) for (int i = 0; i < 4; i++) F[i] = foot[i];
	auto at = [&](double spread, double& x, double& z) {
		if (!foot) { x = pos.x; z = pos.z; return; }
		const double lx = Rand(-1, 1) * F[0] * spread, lz = Rand(-1, 1) * F[1] * spread;
		x = pos.x + lx * F[3] + lz * F[2]; z = pos.z - lx * F[2] + lz * F[3];
	};
	const double ps = Min(s, 2.2) / s; // big blasts get more particles rather than ever-bigger ones
	for (int i = 0; i < n; i++) {
		const double a = Rand() * 6.28, e = Rand(-0.2, 1.2), sp = Rand(4, 14) * s;
		double x, z; at(0.8, x, z);
		Particle p = Pt();
		p.x = x; p.y = pos.y; p.z = z; p.vx = std::cos(a) * std::cos(e) * sp; p.vy = std::sin(e) * sp + 2; p.vz = std::sin(a) * std::cos(e) * sp;
		p.life = Rand(0.4, 0.9) * std::sqrt(Max(1, s)); p.size0 = Rand(1.5, 3) * s * std::sqrt(ps); p.size1 = Rand(3, 5) * s * std::sqrt(ps);
		Col(p, 6, 2.6f, 0.6f); Col1(p, 2.5f, 0.5f, 0.1f); p.alpha = 1; p.drag = 3.5; p.fadeIn = 0.02;
		addPool.spawn(std::move(p));
	}
	for (int i = 0; i < (int)std::round(n * 0.75); i++) {
		const double a = Rand() * 6.28, sp = Rand(1, 6) * s;
		double x, z; at(0.9, x, z);
		Particle p = Pt();
		p.x = x; p.y = pos.y + Rand(0, 2) * s; p.z = z; p.vx = std::cos(a) * sp; p.vy = Rand(2, 7) * s; p.vz = std::sin(a) * sp;
		p.life = Rand(2.5, 5) * std::sqrt(Max(1, s)); p.size0 = Rand(2, 3) * s; p.size1 = Rand(7, 11) * s;
		Col(p, 0.1f, 0.09f, 0.08f); Col1(p, 0.25f, 0.24f, 0.23f); p.alpha = 0.75; p.drag = 1.2; p.fadeIn = 0.08;
		alphaPool.spawn(std::move(p));
	}
	const int ns = (int)std::round(30 * Clamp(s, 0.5, 2.5));
	for (int i = 0; i < ns; i++) {
		Particle p = Pt();
		p.x = pos.x; p.y = pos.y; p.z = pos.z; p.vx = Rand(-15, 15) * std::sqrt(s); p.vy = Rand(4, 18) * std::sqrt(s); p.vz = Rand(-15, 15) * std::sqrt(s);
		p.life = Rand(0.6, 1.4); p.size0 = 0.15; p.size1 = 0.05; Col(p, 6, 3, 1); p.grav = -16; p.drag = 0.3;
		addPool.spawn(std::move(p));
	}
	for (int i = 0; i < (int)std::round(16 * Clamp(s, 0.5, 2.5)); i++) {
		Particle p = Pt();
		p.x = pos.x; p.y = pos.y; p.z = pos.z; p.vx = Rand(-9, 9) * std::sqrt(s); p.vy = Rand(4, 14) * std::sqrt(s); p.vz = Rand(-9, 9) * std::sqrt(s);
		p.life = Rand(1, 2); p.size0 = Rand(0.1, 0.25); p.size1 = 0.1; Col(p, 0.05f, 0.05f, 0.05f); p.grav = -16; p.drag = 0.2; p.floor = gy;
		dotAlpha.spawn(std::move(p));
	}
	flash(pos, 0xff8a3a, 250 * Min(3, s), 0.9 * Min(2, Max(1, std::sqrt(s))), 60 * Min(3, Max(0.5, s)));
	if (pos.y - gy < 3 * Max(1, s)) addDecal(V3(pos.x, gy + 0.07, pos.z), UPV3, radius * 0.9, 2, 200);
	// lingering fire (the length of the wreck for a long vehicle)
	const int fires = foot ? (int)Clamp(std::round(F[1] / 3), 1, 4) : 1;
	for (int i = 0; i < fires; i++) {
		double x = pos.x, z = pos.z;
		if (fires > 1) at(0.7, x, z);
		Emitter e; e.type = "fire"; e.pos = V3(x, game.map.GroundHeight(x, z) + 0.2, z); e.life = 6 + 3 * Min(3, s); e.size = Min(s, 3) * 1.2 / std::sqrt((double)fires);
		emitters.push_back(e);
	}
	if (secondary) return;
	if (s > 0.9) {
		// flaming wreckage thrown out on smoky arcs
		const int nc = (int)std::round(Clamp(2 + s * 5, 0, 26));
		for (int i = 0; i < nc; i++) {
			const double a = Rand() * 6.28, sp = Rand(5, 13) * std::sqrt(s);
			double x, z; at(0.6, x, z);
			Emitter e; e.type = "chunk"; e.pos = V3(x, pos.y + 0.5, z); e.vel = V3(std::cos(a) * sp, Rand(6, 15) * std::sqrt(s), std::sin(a) * sp);
			e.life = Rand(2.5, 4.5); e.size = Rand(0.35, 0.7) * Min(1.6, std::sqrt(s));
			emitters.push_back(e);
		}
	}
	if (s > 1.6) {
		// a shock ring of dust along the ground and a rolling column of smoke above the fireball
		for (int i = 0; i < 36; i++) {
			const double a = i / 36.0 * 6.28, sp = Rand(14, 22) * std::sqrt(s);
			Particle p = Pt();
			p.x = pos.x; p.y = gy + 0.6; p.z = pos.z; p.vx = std::cos(a) * sp; p.vy = Rand(0.2, 1); p.vz = std::sin(a) * sp;
			p.life = Rand(1.6, 2.6); p.size0 = 1.5 * s; p.size1 = 5 * s; Col(p, 0.5f, 0.45f, 0.38f); p.alpha = 0.45; p.drag = 2.2; p.fadeIn = 0.05;
			alphaPool.spawn(std::move(p));
		}
		Emitter c; c.type = "column"; c.pos = pos; c.life = 3 + s; c.size = s;
		emitters.push_back(c);
		// secondary blasts rippling along the fuel tanks
		const int n2 = (int)Min(6, std::floor(s * 1.3));
		for (int i = 0; i < n2; i++) {
			double x, z; at(0.9, x, z);
			const double t = Rand(0.15, 0.4) + i * Rand(0.15, 0.3);
			pending.push_back({ t, V3(x, pos.y + Rand(0, 1.5), z), radius * Rand(0.3, 0.45) });
		}
	}
}

// churned white water behind a boat's stern, and spray thrown off the bow at speed (s, c: heading)
void Effects::foam(const V3& pos, double s, double c, double spd, double w) {
	for (int i = 0; i < 2; i++) {
		Particle p = Pt();
		p.x = pos.x + Rand(-w, w) * 0.35; p.y = pos.y + 0.05; p.z = pos.z + Rand(-w, w) * 0.35; p.vx = -s * spd * 0.25 + Rand(-0.5, 0.5); p.vy = Rand(0.2, 0.8); p.vz = -c * spd * 0.25 + Rand(-0.5, 0.5);
		p.life = Rand(0.8, 1.6); p.size0 = 0.4; p.size1 = 1.8 + spd * 0.05; Col(p, 0.95f, 0.97f, 1); p.alpha = 0.45; p.drag = 1.5; p.fadeIn = 0.05;
		alphaPool.spawn(std::move(p));
	}
}
void Effects::bowSpray(const V3& pos, double s, double c, double spd, double w) {
	for (int sd : { -1, 1 }) for (int i = 0; i < 3; i++) {
		const double rx = -c * sd, rz = s * sd;
		Particle p = Pt();
		p.x = pos.x + rx * w * 0.45; p.y = pos.y; p.z = pos.z + rz * w * 0.45; p.vx = rx * Rand(2, 4) + s * spd * 0.3; p.vy = Rand(1.5, 3.5); p.vz = rz * Rand(2, 4) + c * spd * 0.3;
		p.life = Rand(0.5, 0.9); p.size0 = Rand(0.08, 0.16); p.size1 = 0.12; Col(p, 0.85f, 0.92f, 1); p.alpha = 0.7; p.grav = -10; p.drag = 0.6;
		dotAlpha.spawn(std::move(p));
	}
	if (Rand() < 0.5) {
		Particle p = Pt();
		p.x = pos.x; p.y = pos.y + 0.3; p.z = pos.z; p.vx = s * spd * 0.2; p.vy = 0.8; p.vz = c * spd * 0.2; p.life = 0.8; p.size0 = 0.6; p.size1 = 2.2; Col(p, 0.95f, 0.97f, 1); p.alpha = 0.3; p.drag = 2;
		alphaPool.spawn(std::move(p));
	}
}

// a window giving way: a spray of glittering cubes
void Effects::glassBurst(const V3& pos, double w) {
	const double floor = game.map.GroundHeight(pos.x, pos.z) + 0.02;
	for (int i = 0; i < 40; i++) {
		Particle p = Pt();
		p.x = pos.x + Rand(-w, w) * 0.4; p.y = pos.y + Rand(-0.2, 0.3); p.z = pos.z + Rand(-w, w) * 0.4; p.vx = Rand(-3, 3); p.vy = Rand(0.5, 3.5); p.vz = Rand(-3, 3);
		p.life = Rand(0.6, 1.3); p.size0 = Rand(0.03, 0.07); p.size1 = 0.03; Col(p, 0.85f, 0.95f, 1); p.alpha = 0.9; p.grav = -14; p.drag = 0.4; p.floor = floor;
		dotAlpha.spawn(std::move(p));
	}
	for (int i = 0; i < 12; i++) {
		Particle p = Pt();
		p.x = pos.x; p.y = pos.y; p.z = pos.z; p.vx = Rand(-2.5, 2.5); p.vy = Rand(0.5, 3); p.vz = Rand(-2.5, 2.5);
		p.life = Rand(0.2, 0.5); p.size0 = 0.05; p.size1 = 0.02; Col(p, 2.5f, 2.8f, 3); p.grav = -12; p.drag = 0.5;
		addPool.spawn(std::move(p));
	}
}

void Effects::splash(const V3& pos, double size) {
	for (int i = 0; i < 25 * size; i++) {
		Particle p = Pt();
		p.x = pos.x; p.y = pos.y; p.z = pos.z; p.vx = Rand(-3, 3) * size; p.vy = Rand(3, 7); p.vz = Rand(-3, 3) * size;
		p.life = Rand(0.6, 1.2); p.size0 = 0.2; p.size1 = 0.1; Col(p, 0.8f, 0.9f, 1); p.alpha = 0.7; p.grav = -12; p.drag = 0.3;
		dotAlpha.spawn(std::move(p));
	}
	for (int i = 0; i < 6 * size; i++) {
		Particle p = Pt();
		p.x = pos.x; p.y = pos.y; p.z = pos.z; p.vx = Rand(-1, 1); p.vy = Rand(0.5, 1.5); p.vz = Rand(-1, 1);
		p.life = 1.2; p.size0 = 0.8; p.size1 = 3 * size; Col(p, 0.9f, 0.95f, 1); p.alpha = 0.4;
		alphaPool.spawn(std::move(p));
	}
}

void Effects::hydrantSpray(double x, double y, double z) {
	Emitter e; e.type = "hydrant"; e.pos = V3(x, y + 0.6, z); e.life = 25;
	emitters.push_back(e);
}

void Effects::propDebris(CollObj* col, const V3& vel) {
	if (!col || col->prop < 0 || col->prop >= (int)game.props.size()) return;
	const PropInstance& inst = game.props[col->prop];
	Debris d;
	d.prop = col->prop; d.propType = inst.type; d.scale = inst.scale;
	d.pos = V3(inst.x, inst.y, inst.z); d.ry = inst.rot;
	const double sp = Hypot(vel.x, vel.z);
	d.vx = vel.x * 0.8 + Rand(-1, 1); d.vy = 2 + sp * 0.2; d.vz = vel.z * 0.8 + Rand(-1, 1);
	d.ax = Rand(-3, 3); d.az = Rand(-3, 3);
	d.tall = inst.type == "streetlight" || inst.type == "trafficlight";
	debris.push_back(d);
	dust(V3(inst.x, inst.y, inst.z), 1.5);
}

// a panel torn off a vehicle flies off from where it was (vehicle.js detachPart)
void Effects::panelDebris(Vehicle* v, const std::string& part, const V3& vel) {
	if (!v || !v->model) return;
	const VehicleModel& m = *v->model;
	std::string pivot;
	for (const VPart& p : m.parts) if (p.name == part) { pivot = p.pivot; break; }
	M4 local = M4::Identity();
	auto at = [](const Pt3& p) { return V3(p[0], p[1], p[2]); };
	if (pivot == "door") local = M4::Compose(at(m.doorHinge), Quat::FromEuler(0, v->doorOpen * v->layout.doorMax, 0));
	else if (pivot == "hood") local = M4::Compose(at(m.hoodHinge), Quat::FromEuler(v->hoodAngle, 0, 0));
	else if (pivot == "trunk") local = M4::Compose(at(m.trunkHinge), Quat::FromEuler(v->trunkAngle, 0, 0));
	const M4 world = v->bodyMatrix() * local;
	Debris d;
	d.vehicleType = v->type; d.part = part; d.color = v->color; d.isPart = true;
	d.pos = world.position();
	EulerXYZ(world.rotation(), d.rx, d.ry, d.rz);
	d.vx = vel.x; d.vy = vel.y; d.vz = vel.z;
	d.ax = (Rand() - 0.5) * 8; d.az = (Rand() - 0.5) * 8;
	debris.push_back(d);
}

void Effects::tracer(const V3& a, const V3& b) {
	Tracer& t = tracers[nextTracer];
	nextTracer = (nextTracer + 1) % (int)tracers.size();
	t = { true, a, b, 0 };
}

void Effects::skidAdd(const std::string& key, double x, double y, double z, double w, double strength) {
	auto it = skidLast.find(key);
	const bool had = it != skidLast.end();
	const SkidLast prev = had ? it->second : SkidLast{};
	skidLast[key] = { x, y, z, realNow };
	if (!had || realNow - prev.t > 0.12) return;
	const double dx = x - prev.x, dz = z - prev.z;
	const double len = Hypot(dx, dz);
	if (len < 0.05 || len > 4) return;
	const double nx = -dz / len * w / 2, nz = dx / len * w / 2;
	Skid& s = skids[nextSkid];
	nextSkid = (nextSkid + 1) % (int)skids.size();
	const double y0 = prev.y + 0.02, y1 = y + 0.02;
	s.a0 = V3(prev.x - nx, y0, prev.z - nz); s.a1 = V3(prev.x + nx, y0, prev.z + nz);
	s.b1 = V3(x + nx, y1, z + nz); s.b0 = V3(x - nx, y1, z - nz);
	s.alpha = Clamp(strength, 0, 1);
	skidVersion++;
}

void Effects::wakeAdd(int key, double x, double y, double z, double w, double strength) {
	auto it = wakeLast.find(key);
	const bool had = it != wakeLast.end();
	const WakeLast prev = had ? it->second : WakeLast{};
	if (had && Hypot(x - prev.x, z - prev.z) < 0.9) return;
	wakeLast[key] = { x, y, z, w, wakeTime };
	if (!had || wakeTime - prev.t > 1) return;
	const double dx = x - prev.x, dz = z - prev.z, len = Hypot(dx, dz) > 0 ? Hypot(dx, dz) : 1;
	const double nx = -dz / len, nz = dx / len;
	Wake& k = wakes[nextWake];
	nextWake = (nextWake + 1) % (int)wakes.size();
	// the older end spreads wider than the new end
	const double w0 = prev.w * 0.5 + 0.6, w1 = w * 0.5;
	k.a0 = V3(prev.x - nx * w0, y, prev.z - nz * w0); k.a1 = V3(prev.x + nx * w0, y, prev.z + nz * w0);
	k.b1 = V3(x + nx * w1, y, z + nz * w1); k.b0 = V3(x - nx * w1, y, z - nz * w1);
	k.born = wakeTime;
	k.strength = Clamp(strength, 0, 1);
}

void Effects::update(double dt) {
	realNow += game.input.frameDt;
	wakeTime += dt;
	alphaPool.update(dt);
	addPool.update(dt);
	dotAlpha.update(dt);
	// decals: grow, then fade over 5 s after their life
	for (Decal& d : decals) {
		if (!d.live) continue;
		d.t += dt;
		if (d.grow > 0 && d.size < d.target) { d.size = Min(d.target, d.size + d.grow * dt); decalVersion++; }
		if (d.t > d.life) {
			d.alpha = Max(0, 1 - (d.t - d.life) / 5);
			decalVersion++;
			if (d.alpha <= 0) d.live = false;
		}
	}
	for (Tracer& t : tracers) if (t.live) { t.t += dt; if (t.t >= 0.07) t.live = false; }
	for (Flash& l : lights) {
		if (l.intensity <= 0) continue;
		l.t += dt;
		l.intensity = l.t >= l.life ? 0 : l.peak * (1 - l.t / l.life);
	}
	for (int i = (int)emitters.size() - 1; i >= 0; i--) {
		Emitter& e = emitters[i];
		e.t += dt;
		if (e.t > e.life) { emitters.erase(emitters.begin() + i); continue; }
		if (e.type == "fire" && Rand() < dt * 30) fire(e.pos, e.size * (1 - e.t / e.life));
		else if (e.type == "chunk") {
			if (!e.landed) {
				e.vel.y -= 16 * dt;
				const double ox = e.pos.x, oy = e.pos.y, oz = e.pos.z;
				e.pos = e.pos + e.vel * dt;
				const double fy = game.map.GroundHeight(e.pos.x, e.pos.z);
				if (e.pos.y < fy + 0.1) { e.pos.y = fy + 0.1; e.landed = true; }
				// a continuous trail: fill in the path covered this frame
				const double seg = Hypot3(e.pos.x - ox, e.pos.y - oy, e.pos.z - oz);
				const int nk = (int)Min(6, 1 + std::floor(seg / (e.size * 0.6)));
				for (int k = 1; k <= nk; k++) {
					const double f = (double)k / nk, x = ox + (e.pos.x - ox) * f, y = oy + (e.pos.y - oy) * f, z = oz + (e.pos.z - oz) * f;
					Particle p = Pt();
					p.x = x; p.y = y; p.z = z; p.vx = Rand(-0.3, 0.3); p.vy = Rand(0.2, 1); p.vz = Rand(-0.3, 0.3);
					p.life = Rand(0.15, 0.3); p.size0 = e.size * 1.2; p.size1 = e.size * 0.3; Col(p, 3.5f, 1.4f, 0.3f); Col1(p, 1.5f, 0.3f, 0.08f); p.alpha = 0.8; p.drag = 1;
					addPool.spawn(std::move(p));
					if (k == nk || k % 2 == 0) {
						Particle q = Pt();
						q.x = x; q.y = y; q.z = z; q.vx = Rand(-0.2, 0.2); q.vy = Rand(0.3, 1); q.vz = Rand(-0.2, 0.2);
						q.life = Rand(1.2, 2.4); q.size0 = e.size * 0.8; q.size1 = e.size * 4; Col(q, 0.12f, 0.11f, 0.1f); q.alpha = 0.4; q.drag = 0.8;
						alphaPool.spawn(std::move(q));
					}
				}
			} else if (Rand() < dt * 12) fire(e.pos, e.size * 0.8 * (1 - e.t / e.life));
		} else if (e.type == "column" && Rand() < dt * 24) {
			// a fireball that rolls upward on a dark stalk of smoke
			const double k = e.t / e.life, s = e.size;
			const double x = e.pos.x + Rand(-1, 1) * s, y = e.pos.y + Rand(0, 2) * s, z = e.pos.z + Rand(-1, 1) * s, vy = Rand(6, 10) * std::sqrt(s) * (1 - k * 0.5);
			Particle p = Pt();
			p.x = x; p.y = y; p.z = z; p.vx = Rand(-1, 1); p.vy = vy; p.vz = Rand(-1, 1);
			p.life = Rand(4, 7); p.size0 = 2.5 * s; p.size1 = 9 * s; Col(p, 0.08f, 0.075f, 0.07f); Col1(p, 0.22f, 0.21f, 0.2f); p.alpha = 0.7; p.drag = 0.35; p.fadeIn = 0.1;
			alphaPool.spawn(std::move(p));
			if (k < 0.35) {
				Particle q = Pt();
				q.x = x; q.y = y; q.z = z; q.vx = Rand(-1, 1); q.vy = vy * 1.1; q.vz = Rand(-1, 1);
				q.life = Rand(0.7, 1.2); q.size0 = 2.8 * s; q.size1 = 1.2 * s; Col(q, 4, 1.5f, 0.3f); Col1(q, 1.2f, 0.25f, 0.05f); q.alpha = 0.8 * (1 - k / 0.35); q.drag = 0.4; q.fadeIn = 0.05;
				addPool.spawn(std::move(q));
			}
		}
		if (e.type == "hydrant") for (int k = 0; k < 3; k++) {
			Particle p = Pt();
			p.x = e.pos.x; p.y = e.pos.y; p.z = e.pos.z; p.vx = Rand(-0.6, 0.6); p.vy = Rand(7, 10); p.vz = Rand(-0.6, 0.6);
			p.life = 1.6; p.size0 = 0.25; p.size1 = 0.5; Col(p, 0.75f, 0.85f, 1); p.alpha = 0.6; p.grav = -9.8; p.drag = 0.2; p.floor = e.pos.y - 0.6;
			dotAlpha.spawn(std::move(p));
		}
	}
	for (int i = (int)pending.size() - 1; i >= 0; i--) {
		Pending& q = pending[i];
		q.t -= dt;
		if (q.t > 0) continue;
		const Pending blast = q;
		pending.erase(pending.begin() + i);
		explosion(blast.pos, blast.r, nullptr, true);
		{ SoundOpts so; so.size = blast.r / 6.75; game.soundAt("explosion", blast.pos, 0.6, so); }
	}
	for (int i = (int)debris.size() - 1; i >= 0; i--) {
		Debris& d = debris[i];
		d.t += dt;
		const double gy = game.map.GroundHeight(d.pos.x, d.pos.z);
		if (d.pos.y > gy + 0.05 || d.vy > 0) {
			d.vy -= 18 * dt;
			d.pos.x += d.vx * dt; d.pos.y += d.vy * dt; d.pos.z += d.vz * dt;
			d.rx += d.ax * dt; d.rz += d.az * dt;
			if (d.tall) d.rx = Min(kPi / 2, std::fabs(d.rx) + dt * 2.5) * Sgn(d.ax != 0 ? d.ax : 1);
			if (d.pos.y < gy) {
				d.pos.y = gy; d.vy = std::fabs(d.vy) * 0.25; d.vx *= 0.5; d.vz *= 0.5; d.ax *= 0.5; d.az *= 0.5;
				if (d.isPart && d.vy < 1) { d.vy = 0; d.ax = d.az = 0; }
			}
		}
		if (d.t > 30) debris.erase(debris.begin() + i);
	}
	// restore broken props far from the player after a while
	restoreTimer += dt;
	if (restoreTimer > 5) {
		restoreTimer = 0;
		const V3 p = game.player->pos;
		for (CollObj* col : game.propColliders) if (col && col->broken && !col->gone && Hypot(col->x - p.x, col->z - p.z) > 250) game.restoreProp(col->prop);
	}
}

} // namespace atg
