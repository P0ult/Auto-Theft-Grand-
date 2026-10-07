// Particles, decals, skid marks, tracers, flash lights, explosions and flying debris (the simulation half of
// src/game/effects.js). Three particle pools as in the browser game: smoke and dust (alpha blended, lit),
// fire and sparks (additive), and droplets (alpha blended dots). The renderer (ATGEffects) draws whatever is
// alive in them each frame; nothing here knows about Unreal.
#pragma once

#include "Systems.h"
#include <deque>

namespace atg {

class Game;

struct Particle {
	double x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, age = 0, life = 1, size0 = 0.5, size1 = 1, rot = 0, spin = 0, grav = 0, drag = 0.5;
	double alpha = 1, fadeIn = 0.05, fadePow = 1.5;
	float color[3] = { 1, 1, 1 }, color1[3] = { 1, 1, 1 };
	bool hasColor1 = false;
	double floor = NaN();
	std::function<void(Particle&)> onFloor;
	// what the renderer reads (set by update)
	float size = 0, a = 0, col[3] = { 1, 1, 1 };
};

class ParticlePool {
public:
	explicit ParticlePool(int max) : max(max) {}
	int max;
	std::deque<Particle> parts;
	void spawn(Particle p) { if ((int)parts.size() >= max) parts.pop_front(); parts.push_back(std::move(p)); }
	void update(double dt);
};

class Effects : public System, public IEffects {
public:
	explicit Effects(Game& game);
	Game& game;
	ParticlePool alphaPool{ 3500 }, addPool{ 2000 }, dotAlpha{ 1200 };
	// decals: 0 bullet hole, 1 blood pool, 2 scorch, 3 blood splat (an atlas of four tiles)
	struct Decal { bool live = false; V3 pos; Quat q; double size = 0, target = 0, grow = 0, t = 0, life = 120, alpha = 1; int tile = 0; };
	std::vector<Decal> decals; int nextDecal = 0; int decalVersion = 0;
	// skid marks: quads laid behind the tyres (a ring of 1500)
	struct Skid { V3 a0, a1, b1, b0; double alpha = 0; };
	std::vector<Skid> skids; int nextSkid = 0; int skidVersion = 0;
	struct SkidLast { double x, y, z, t; };
	std::map<std::string, SkidLast> skidLast;
	// boat wakes (WakeTrails): quads on the water behind boats, each spreading and fading over 9 s (a ring of 900)
	struct Wake { V3 a0, a1, b1, b0; double born = -1e4, strength = 0; };
	std::vector<Wake> wakes; int nextWake = 0;
	struct WakeLast { double x, y, z, w, t; };
	std::map<int, WakeLast> wakeLast;
	double wakeTime = 0;
	// tracers: a short streak from the muzzle (a ring of 64)
	struct Tracer { bool live = false; V3 a, b; double t = 0; };
	std::vector<Tracer> tracers; int nextTracer = 0;
	// flash lights (three, reused): colour as 0xRRGGBB, intensity in three.js units
	struct Flash { V3 pos; uint32_t color = 0xffaa55; double intensity = 0, peak = 0, t = 0, life = 0, range = 30; };
	Flash lights[3];
	struct Emitter { std::string type; V3 pos, vel; double t = 0, life = 0, size = 1; bool landed = false; };
	std::vector<Emitter> emitters;
	struct Pending { double t; V3 pos; double r; };
	std::vector<Pending> pending;
	// flying debris: a prop knocked over, or a panel torn off a vehicle (drawn with that vehicle's mesh)
	struct Debris {
		int prop = -1; std::string propType; double scale = 1;
		std::string vehicleType, part; uint32_t color = 0;
		V3 pos; double rx = 0, ry = 0, rz = 0;
		double vx = 0, vy = 0, vz = 0, ax = 0, az = 0, t = 0;
		bool tall = false, isPart = false;
		M4 matrix() const;
	};
	std::vector<Debris> debris;
	double realNow = 0, restoreTimer = 0;

	// ---- IEffects
	void sparks(const V3& pos, double strength) override;
	void dust(const V3& pos, double amount) override;
	void splash(const V3& pos, double size) override;
	void blood(const V3& pos, const V3& dir, double amount) override;
	void glassBurst(const V3& pos, double w) override;
	void propDebris(CollObj* prop, const V3& vel) override;
	void hydrantSpray(double x, double y, double z) override;
	void panelDebris(Vehicle* v, const std::string& part, const V3& vel) override;
	void engineSmoke(const V3& pos, double dark) override;
	void fire(const V3& pos, double size) override;
	void tireSmoke(const V3& pos, double amount) override;
	void skidAdd(const std::string& key, double x, double y, double z, double w, double strength) override;
	void wakeAdd(int key, double x, double y, double z, double w, double strength) override;
	void wakeBreak(int key) override { wakeLast.erase(key); }
	void skidBreak(const std::string& key) override { skidLast.erase(key); }
	void muzzleFlash(const V3& pos, const V3& dir, bool big) override;
	void impact(const V3& pos, const V3& normal, const std::string& kind) override;
	void bloodPool(const V3& pos) override;
	void tracer(const V3& a, const V3& b) override;
	void explosion(const V3& pos, double radius = 6, const double* foot = nullptr, bool secondary = false) override;
	void flash(const V3& pos, uint32_t color, double intensity, double life, double range) override;
	void foam(const V3& pos, double s, double c, double spd, double w) override;
	void bowSpray(const V3& pos, double s, double c, double spd, double w) override;

	void tireSmokeColor(const V3& pos, double amount, double color);
	void addDecal(const V3& pos, const V3& normal, double size, int tile, double life = 120, double grow = 0, double targetSize = -1, double alpha = 1);
	void update(double dt) override;
};

} // namespace atg
