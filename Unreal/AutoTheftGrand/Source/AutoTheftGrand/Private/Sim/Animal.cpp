#include "Animal.h"
#include "Collision.h"
#include "Game.h"
#include "Vehicle.h"

namespace atg {

namespace {
int gNextAnimalId = 1;
const AnimalSpecies& SpeciesOf(const std::string& breed) { return AnimalSpeciesTable().at(AnimalBreeds().at(breed).species); }
}

Animal::Animal(Game& g, const std::string& b, double x, double z, bool hasY, double y, bool hasYaw, double yw)
	: game(g), id(gNextAnimalId++), breed(b), kind(AnimalBreeds().at(b).species), P(BuildAnimalParts(b)), sp(SpeciesOf(b)), scale(P.k) {
	pos = V3(x, hasY ? y : g.map.GroundHeight(x, z), z);
	yaw = hasYaw ? yw : Rand() * kPi * 2;
	phase = Rand() * 6;
	t = Rand() * 10;
	health = maxHealth = sp.hp * (scale > 0.8 ? 1 : 0.7);
	radius = Max(0.15, sp.len * scale * (sp.bird ? 0.3 : 0.32));
	for (int i = 0; i < 4; i++) legY[i] = P.hipY;
}

void Animal::update(double dt) {
	if (removed) return;
	t += dt; stateT += dt;
	if (inVehicle) { ride(dt); return; }
	if (dead) { deadPose(dt); return; }
	double tgtSpeed = 0, tgtYaw = yaw;
	if (hasWant) {
		const double dx = want.x - pos.x, dz = want.z - pos.z, d = Hypot(dx, dz);
		if (d > 0.25) { tgtSpeed = Min(want.speed, d * 2.2); tgtYaw = std::atan2(dx, dz); }
	}
	const double turnRate = sp.bird ? 6 : speed > 4 ? 5 : 8;
	yaw = DampAngle(yaw, tgtYaw, turnRate, dt);
	const double align = Max(0.0, std::cos(WrapAngle(tgtYaw - yaw)));
	speed = Damp(speed, tgtSpeed * (0.35 + 0.65 * align), flying ? 2 : 5, dt);
	const double s = std::sin(yaw), c = std::cos(yaw);
	double nx = pos.x + s * speed * dt, nz = pos.z + c * speed * dt;
	CollisionWorld& col = *game.collision;
	if (!flying) {
		const auto r = col.resolveCircle(nx, nz, radius, pos.y + 0.1, Max(0.3, sp.h * scale));
		if (r.hit) { nx = r.x; nz = r.z; bumpT += dt; } else bumpT = 0;
		const double gh = col.floorHeight(nx, nz, pos.y + 0.5);
		const bool wet = (WATER_Y - game.map.GroundHeight(nx, nz)) > 0.35 && gh < WATER_Y + 0.1;
		if (wet || gh - pos.y > 0.7 || !std::isfinite(gh)) { speed *= 0.3; bumpT += dt; if (onBlocked) onBlocked(); }
		else { pos.x = nx; pos.z = nz; pos.y = Damp(pos.y, gh, 18, dt); }
	} else {
		pos.x = nx; pos.z = nz;
		const double floor = col.floorHeight(nx, nz, pos.y + 2);
		pos.y = Damp(pos.y, Max(floor + 0.1, alt), 2.5, dt);
	}
	if (!std::isfinite(pos.x) || !std::isfinite(pos.z)) { remove(); return; }
	rootRoll = 0;
	animate(dt);
}

void Animal::animate(double dt) {
	const double spd = speed;
	if (sp.bird) {
		const bool air = flying;
		flapT += dt * (air ? 14 : 0);
		const double flap = air ? std::sin(flapT) * 0.9 : 0;
		wingRotZ[0] = air ? -flap : 1.35; wingRotZ[1] = air ? flap : -1.35;
		wingRotY[0] = air ? 0 : -0.25; wingRotY[1] = air ? 0 : 0.25;
		bodyRotX = air ? -0.1 : (spd < 0.1 && std::sin(t * 3 + id) > 0.6 ? 0.55 : 0);
		bodyY = air ? 0 : std::fabs(std::sin(phase)) * 0.02;
		phase += spd * dt * 14;
		return;
	}
	const double k = scale;
	const double stride = sp.len * k * 1.1;
	phase += spd * dt / Max(0.1, stride) * kPi * 2 * 0.5;
	const double run = Clamp((spd - sp.walk) / Max(0.1, sp.run - sp.walk), 0, 1);
	const double moving = Clamp(spd / Max(0.2, sp.walk), 0, 1);
	const double amp = (0.38 + run * 0.45) * moving;
	const double ph = phase;
	sitK = Damp(sitK, state == "sit" && spd < 0.2 ? 1 : 0, 6, dt);
	const double sit = sitK;
	if (sp.hop) {
		const double hop = moving > 0.05 ? Max(0.0, std::sin(ph)) : 0;
		legRotX[0] = legRotX[1] = -std::sin(ph) * 0.6 * moving;
		legRotX[2] = legRotX[3] = std::sin(ph) * 0.8 * moving;
		bodyY = hop * 0.12 * k;
		bodyRotX = -std::cos(ph) * 0.25 * moving;
		for (int i = 0; i < 4; i++) legY[i] = P.hipY + hop * 0.12 * k;
	} else {
		static const double walkOff[4] = { 0, kPi, kPi, 0 }, runOff[4] = { 0, 0.35, kPi, kPi + 0.35 };
		const double* off = run > 0.5 ? runOff : walkOff;
		for (int i = 0; i < 4; i++) {
			double a = std::sin(ph + off[i]) * amp;
			if (i >= 2) a = a * (1 - sit) + sit * -1.35;
			else a *= 1 - sit;
			legRotX[i] = a;
		}
		const double bob = std::fabs(std::sin(ph)) * (0.012 + run * 0.035) * k * moving;
		bodyY = bob - sit * P.hipY * 0.35;
		bodyRotX = -sit * 0.45 + (run > 0.5 ? std::sin(ph) * 0.06 : 0);
		for (int i = 2; i < 4; i++) legY[i] = P.hipY - sit * P.hipY * 0.45;
	}
	const double graze = (state == "graze" && spd < 0.2) ? 1 : 0;
	grazeK = Damp(grazeK, graze, 3, dt);
	headRotX = grazeK * (kind == "deer" || kind == "cow" ? 1.3 : 0.8) - sit * 0.2 + std::sin(t * 1.7 + id) * 0.05;
	lookYaw = Damp(lookYaw, spd < 0.3 ? std::sin(t * 0.4 + id) * 0.5 : 0, 2, dt);
	headRotY = lookYaw;
	const bool wagging = kind == "dog" && (happy || spd > 0.5);
	wag = Damp(wag, wagging ? 1 : 0.15, 4, dt);
	tailRotY = std::sin(t * (wagging ? 13 : 3)) * 0.55 * wag;
}

bool Animal::takeDamage(double amount, Character* source, const std::string& type) {
	(void)type;
	if (dead || removed) return false;
	health -= amount;
	lastDamager = source;
	if (game.effects) game.effects->blood(V3(pos.x, pos.y + sp.h * scale * 0.6, pos.z), V3(0, 0.4, 0), Min(12.0, amount * 0.3));
	if (health <= 0) { die(source); return true; }
	if (onHurt) onHurt(source);
	return false;
}

void Animal::die(Character* source) {
	if (dead) return;
	dead = true; deathT = 0;
	flying = false;
	speed = 0; hasWant = false;
	game.soundAt(kind == "dog" || kind == "coyote" ? "yelp" : "bodyhit", pos, 0.6);
	game.events.animalKilled.emit(this, source);
}

void Animal::deadPose(double dt) {
	deathT += dt;
	const double k = Clamp(deathT / 0.35, 0, 1);
	rootRoll = k * kPi / 2 * (id % 2 ? 1 : -1);
	if (sp.bird) { pos.y = Damp(pos.y, game.collision->floorHeight(pos.x, pos.z, pos.y + 1), 8, dt); return; }
	for (int i = 0; i < 4; i++) legRotX[i] = Damp(legRotX[i], 0.2, 6, dt);
	pos.y = Damp(pos.y, game.collision->floorHeight(pos.x, pos.z, pos.y + 0.5) + sp.w * scale * 0.3, 8, dt);
}

void Animal::sitIn(Vehicle* v, int s) {
	inVehicle = v; seat = s;
	rootRoll = 0;
	state = "sit"; speed = 0; hasWant = false;
}

void Animal::getOut(double x, double z) {
	Vehicle* v = inVehicle.get();
	inVehicle = nullptr;
	pos = V3(x, game.collision->floorHeight(x, z, (v ? v->pos.y : pos.y) + 1), z);
	state = "follow";
}

void Animal::ride(double dt) {
	Vehicle* v = inVehicle.get();
	if (v->removed) { getOut(v->pos.x + 2, v->pos.z); return; }
	state = "sit";
	animate(dt);
	headRotY = std::sin(t * 0.8) * 0.4;
}

double Animal::rayHit(double ox, double oy, double oz, double dx, double dy, double dz, double maxT) const {
	if (removed || inVehicle) return -1;
	const double r = radius * 1.1, cy = pos.y + (sp.bird ? 0.12 : P.hipY + sp.bh * scale * 0.3);
	const double lx = pos.x - ox, ly = cy - oy, lz = pos.z - oz;
	const double tt = lx * dx + ly * dy + lz * dz;
	if (tt < 0 || tt > maxT) return -1;
	const double px = lx - dx * tt, py = ly - dy * tt, pz = lz - dz * tt;
	return px * px + py * py + pz * pz < r * r ? tt : -1;
}

M4 Animal::rootMatrix() const {
	if (inVehicle) {
		const std::vector<V3>& S = inVehicle->layout.seats;
		const V3 s = seat < (int)S.size() ? S[seat] : S.empty() ? V3() : S[0];
		return inVehicle->bodyMatrix() * M4::Compose(V3(s.x, s.y + 0.05, s.z - 0.05), Quat());
	}
	return M4::Compose(pos, Quat::FromEuler(0, yaw, rootRoll));
}

} // namespace atg
