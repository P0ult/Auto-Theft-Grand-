#include "Wildlife.h"
#include "Game.h"
#include "Gameplay.h"
#include "Peds.h"
#include "Ragdoll.h"

namespace atg {
namespace {
struct MixRow { std::string breed; int weight, lo, hi; };
const std::map<std::string, std::vector<MixRow>> Mix = {
	{ "downtown", {{"pigeon",6,3,7}} },
	{ "midtown", {{"pigeon",5,3,6},{"tabby",1,1,1},{"blackcat",1,1,1}} },
	{ "westside", {{"pigeon",2,2,5},{"tabby",2,1,1},{"siamese",1,1,1}} },
	{ "hood", {{"pigeon",2,2,5},{"tabby",2,1,1},{"blackcat",2,1,1},{"stray",1,1,2}} },
	{ "corona", {{"pigeon",2,2,5},{"blackcat",2,1,1},{"stray",1,1,2}} },
	{ "beach", {{"seagull",6,2,6}} },
	{ "docks", {{"seagull",4,2,5},{"pigeon",2,2,4},{"stray",1,1,1}} },
	{ "hills", {{"deer",2,1,3},{"rabbit",3,1,2},{"crow",1,2,4},{"tabby",1,1,1}} },
	{ "country", {{"rabbit",3,1,2},{"deer",2,1,3},{"crow",2,3,6},{"cow",2,3,6}} },
	{ "forest", {{"deer",4,1,4},{"rabbit",2,1,2},{"crow",2,2,5}} },
	{ "desert", {{"coyote",3,1,3},{"rabbit",2,1,2},{"crow",1,1,3}} },
	{ "base", {{"crow",1,2,4}} },
};
const std::vector<std::string> Strays = { "lab", "rottweiler", "shepherd", "husky" };
const std::set<std::string> City = { "downtown", "midtown", "westside", "hood", "corona", "docks", "beach" };
}

Wildlife::Wildlife(Game& g) : game(g) {
	max = g.settings.quality == "low" ? 11 : g.settings.quality == "medium" ? 15 : g.settings.quality == "high" ? 19 : g.settings.quality == "ultra" ? 23 : 15;
	g.events.gunshot.on([this](Character*, V3 pos, const std::string&) { scare(pos, 55); });
	g.events.explosion.on([this](V3 pos, double, Character*) { scare(pos, 90); });
}

double Wildlife::count() const {
	double n = 0;
	for (const auto& a : list) if (!a->dead && !a->owner) n += a->sp.bird ? 0.6 : 1;
	return n;
}

std::vector<Animal*> Wildlife::all() const {
	std::vector<Animal*> out;
	for (const auto& a : list) out.push_back(a.get());
	if (game.animalExtras) game.animalExtras(out);
	return out;
}

void Wildlife::update(double dt) {
	Player& pl = *game.player;
	const V3 pp = pl.vehicle ? pl.vehicle->pos : pl.pos;
	spawnT -= dt;
	if (spawnT <= 0 && !game.disableAmbient && (!game.gameplay || game.gameplay->state != "menu")) {
		spawnT = 0.6;
		if (count() < max) spawnGroup(pp);
	}
	for (int i = (int)list.size() - 1; i >= 0; i--) {
		Animal& a = *list[i];
		if (a.removed) { list.erase(list.begin() + i); continue; }
		const double d2 = Dist2(a.pos.x, a.pos.z, pp.x, pp.z);
		const bool far = a.owner ? a.owner->removed || (a.owner.get() != &pl && d2 > 170 * 170) : d2 > 170 * 170 || (a.dead && a.deathT > 40 && d2 > 50 * 50);
		if (far && !a.pet) { a.remove(); list.erase(list.begin() + i); continue; }
		if (!a.pet) think(a, dt, pp);
		a.update(dt);
		a.visible = d2 < 230 * 230;
	}
	roadkill();
}

void Wildlife::spawnGroup(const V3& pp) {
	const CityMap& map = game.map;
	for (int tries = 0; tries < 6; tries++) {
		const double ang = Rand() * kPi * 2, r = Rand(45, 115);
		const double x = pp.x + std::cos(ang) * r, z = pp.z + std::sin(ang) * r;
		const std::string district = map.DistrictAt(x, z);
		auto mix = Mix.find(district);
		if (mix == Mix.end()) continue;
		int total = 0; for (const auto& row : mix->second) total += row.weight;
		double pickR = Rand() * total;
		const MixRow* row = &mix->second[0];
		for (const auto& m : mix->second) { pickR -= m.weight; if (pickR <= 0) { row = &m; break; } }
		const int n = RandInt(row->lo, row->hi);
		V3 at;
		if (!spot(x, z, district, row->breed, at)) continue;
		if (game.peds && game.peds->inView(at.x, at.z) && r < 80) continue;
		for (int k = 0; k < n; k++) {
			const std::string breed = row->breed == "stray" ? RandPick(Strays) : row->breed == "cow" && Rand() < 0.35 ? "brown_cow" : row->breed;
			const double sx = at.x + Rand(-2.5, 2.5) * (k ? 1 : 0), sz = at.z + Rand(-2.5, 2.5) * (k ? 1 : 0);
			const double y = game.collision->floorHeight(sx, sz, at.y + 1.5);
			if (!Finite(y) || WATER_Y - map.GroundHeight(sx, sz) > 0.2) continue;
			auto a = std::make_shared<Animal>(game, breed, sx, sz, true, y);
			a->hasHome = true; a->homeX = at.x; a->homeZ = at.z;
			a->state = a->sp.bird ? "peck" : Rand() < 0.5 ? "graze" : "wander";
			if (row->breed == "stray") a->stray = true;
			list.push_back(a);
		}
		return;
	}
}

bool Wildlife::spot(double x, double z, const std::string& district, const std::string& breed, V3& out) {
	const CityMap& map = game.map;
	if (City.count(district) && !(breed == "seagull" && district == "beach")) {
		WalkArea b;
		if (!WalkAreaAt(map, x, z, b) || !b.nodeIds || b.nodeIds->empty()) return false;
		const WalkNode& n = map.walkNodes[RandPick(*b.nodeIds)];
		const double px = n.x + Rand(-1.5, 1.5), pz = n.z + Rand(-1.5, 1.5);
		if (map.IsOnRoad(px, pz)) return false;
		out = V3(px, game.collision->floorHeight(px, pz, 50), pz); return true;
	}
	if (map.IsOnRoad(x, z) || WATER_Y - map.GroundHeight(x, z) > 0) return false;
	const double y = game.collision->floorHeight(x, z, 400);
	if (!Finite(y) || game.collision->resolveCircle(x, z, 1.5, y + 0.2, 1.5).hit) return false;
	const double s = std::fabs(map.GroundHeight(x + 2, z) - map.GroundHeight(x - 2, z)) + std::fabs(map.GroundHeight(x, z + 2) - map.GroundHeight(x, z - 2));
	if (s > 2.4) return false;
	out = V3(x, y, z); return true;
}

void Wildlife::addWalkedDog(Ped* ped) {
	int n = 0; for (const auto& a : list) if (a->owner && !a->pet) n++;
	if (n > 5) return;
	static const std::vector<std::string> breeds = { "lab", "lab", "poodle", "pug", "husky", "shepherd" };
	auto a = std::make_shared<Animal>(game, RandPick(breeds), ped->pos.x + 1, ped->pos.z - 1, true, ped->pos.y);
	a->owner = ped; a->state = "follow";
	ped->walkedDog = a;
	list.push_back(a);
}

void Wildlife::think(Animal& a, double dt, const V3& pp) {
	if (a.dead) return;
	if (a.owner) {
		if (a.owner->dead) { a.owner = nullptr; a.stray = true; a.state = "flee"; a.threatX = a.pos.x + Rand(-1, 1); a.threatZ = a.pos.z + Rand(-1, 1); a.stateT = 0; a.fleeT = 6; bark(a); return; }
		FollowOwner(a, *a.owner.get(), dt); return;
	}
	const AnimalSpecies& sp = a.sp;
	Player& pl = *game.player;
	const double threatD = Hypot(a.pos.x - pp.x, a.pos.z - pp.z);
	Vehicle* pv = pl.vehicle;
	const double fear = sp.fear * (pv ? (pv->speedAbs() > 5 ? 1.7 : 1.1) : pl.aiming ? 1.6 : pl.sprinting ? 1.3 : 1);
	if (a.state != "flee" && a.state != "fly" && threatD < fear && !(a.stray && threatD > 4)) startFlee(a, pp.x, pp.z);
	if (a.state == "flee") {
		double dx = a.pos.x - a.threatX, dz = a.pos.z - a.threatZ;
		const double dist = Hypot(dx, dz), d = dist ? dist : 1;
		dx /= d; dz /= d;
		if (a.bumpT > 0.4) { const double s = a.id % 2 ? 1 : -1, nx = -dz * s, nz = dx * s; dx = nx; dz = nz; }
		a.goTo(a.pos.x + dx * 10, a.pos.z + dz * 10, sp.run);
		if (a.stateT > (a.fleeT ? a.fleeT : 5) && d > (sp.fear ? sp.fear : 8) * 1.5) { a.state = "wander"; a.stateT = 0; a.stop(); }
	} else if (a.state == "fly") {
		if (!a.hasFlyTo) {
			const double ang = std::atan2(a.pos.x - a.threatX, a.pos.z - a.threatZ) + Rand(-0.6, 0.6);
			a.hasFlyTo = true; a.flyToX = a.pos.x + std::sin(ang) * Rand(35, 70); a.flyToZ = a.pos.z + std::cos(ang) * Rand(35, 70); a.alt = a.pos.y + Rand(7, 14);
		}
		a.goTo(a.flyToX, a.flyToZ, sp.fly);
		if (Hypot(a.pos.x - a.flyToX, a.pos.z - a.flyToZ) < 2) {
			const double y = game.collision->floorHeight(a.pos.x, a.pos.z, a.pos.y + 1);
			a.alt = y;
			if (a.pos.y - y < 0.4) { a.flying = false; a.hasFlyTo = false; a.state = "peck"; a.stateT = 0; a.stop(); }
		}
	} else if (a.state == "peck") {
		if (!a.hasWant || a.stateT > 3) { a.stateT = 0; const double x = a.pos.x + Rand(-2, 2), z = a.pos.z + Rand(-2, 2); a.goTo(x, z, sp.walk * (Rand() < 0.5 ? 1 : 0)); }
	} else if (a.state == "graze") {
		a.stop();
		if (a.stateT > Rand(4, 9)) { a.state = "wander"; a.stateT = 0; }
		if (a.kind == "cow" && Rand() < dt * 0.02) game.soundAt("moo", a.pos, 0.7);
	} else {
		if (!a.hasWant || a.stateT > 7 || a.bumpT > 1) {
			a.stateT = 0;
			const double R = a.kind == "cow" ? 10 : a.kind == "cat" || a.stray ? 14 : 22;
			const double x = (a.hasHome ? a.homeX : a.pos.x) + Rand(-R, R), z = (a.hasHome ? a.homeZ : a.pos.z) + Rand(-R, R);
			a.goTo(x, z, sp.walk);
			if (!sp.bird && Rand() < 0.4) { a.state = "graze"; a.stop(); }
		}
		if (a.stray && threatD < 6 && Rand() < dt * 0.4) bark(a);
	}
}

void Wildlife::startFlee(Animal& a, double x, double z) {
	a.threatX = x; a.threatZ = z; a.stateT = 0; a.fleeT = Rand(4, 8);
	if (a.sp.bird) { a.state = "fly"; a.flying = true; a.hasFlyTo = false; if (Rand() < 0.5) game.soundAt("flap", a.pos, 0.6); }
	else { a.state = "flee"; if (a.kind == "cat" && Rand() < 0.5) game.soundAt("meow", a.pos, 0.8); }
}

void Wildlife::bark(Animal& a) {
	if (game.time - (a.barkT ? a.barkT : -9) < 1.5) return;
	a.barkT = game.time; game.soundAt("bark", a.pos, a.scale > 0.9 ? 1 : 0.7);
}

void Wildlife::scare(const V3& pos, double radius) {
	for (const auto& a : list) {
		if (a->dead || a->pet || a->owner || a->inVehicle) continue;
		if (Dist2(a->pos.x, a->pos.z, pos.x, pos.z) < radius * radius) startFlee(*a, pos.x, pos.z);
	}
}

void Wildlife::roadkill() {
	for (const auto& v : game.vehicles.list) {
		if (v->removed || v->speedAbs() < 3.5 || v->def.kind == "train" || v->def.aircraft) continue;
		for (Animal* a : all()) {
			if (a->dead || a->removed || a->inVehicle || a->flying) continue;
			const double dx = a->pos.x - v->pos.x, dz = a->pos.z - v->pos.z;
			if (dx * dx + dz * dz > 49 || std::fabs(a->pos.y - v->pos.y) > 1.8) continue;
			double lx, lz; v->worldToLocal(a->pos.x, a->pos.z, lx, lz);
			if (std::fabs(lx) < v->hx + a->radius && std::fabs(lz) < v->hz + a->radius) {
				a->takeDamage(v->speedAbs() * 12, v->driver(), "vehicle");
				if (!a->dead) { const double d = Hypot(dx, dz); a->pos.x += dx / (d ? d : 1) * 1.2; a->pos.z += dz / (d ? d : 1) * 1.2; }
				game.soundAt("bodyhit", a->pos, 0.5);
			}
		}
	}
}

void Wildlife::clear() {
	for (int i = (int)list.size() - 1; i >= 0; i--) if (!list[i]->pet) { list[i]->remove(); list.erase(list.begin() + i); }
}

void FollowOwner(Animal& a, Character& owner, double dt, bool sit, double teleport) {
	const V3 op = owner.vehicle ? owner.vehicle->pos : owner.ragdolling && owner.ragdoll ? owner.ragdoll->center() : owner.pos;
	const double oy = owner.yaw;
	if (!a.followSide) a.followSide = a.id % 2 ? 1 : -1;
	const double tx = op.x - std::sin(oy) * 1.5 + std::cos(oy) * 0.9 * a.followSide, tz = op.z - std::cos(oy) * 1.5 - std::sin(oy) * 0.9 * a.followSide;
	const double d = Hypot(tx - a.pos.x, tz - a.pos.z);
	const double ownerSpeed = owner.vehicle ? owner.vehicle->speedAbs() : Hypot(owner.vel.x, owner.vel.z);
	if (d > teleport) {
		const double y = a.game.collision->floorHeight(tx, tz, op.y + 1.5);
		if (Finite(y)) { a.pos.set(tx, y, tz); a.speed = 0; } return;
	}
	if (d > 1.2) {
		const double spd = d > 6 || ownerSpeed > 3.5 ? Min(a.sp.run, Max(ownerSpeed * 1.15, a.sp.walk * 2.2)) : Max(a.sp.walk, ownerSpeed);
		a.goTo(tx, tz, spd); a.state = "follow"; a.idleT = 0;
	} else {
		a.stop(); a.idleT += dt;
		if (a.idleT > 2.5 && sit) a.state = "sit"; else if (a.state != "sit") a.state = "follow";
	}
	a.happy = true;
}

} // namespace atg
