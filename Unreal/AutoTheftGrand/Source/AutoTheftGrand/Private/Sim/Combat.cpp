#include "Combat.h"
#include "Collision.h"
#include "Effects.h"
#include "Game.h"
#include "Peds.h"
#include "Police.h"
#include "Ragdoll.h"

namespace atg {

namespace {
// utils.js rayAABB / rayOBBYaw: entry t or -1, the world normal of the face hit
double RayAABB(double ox, double oy, double oz, double dx, double dy, double dz, double minX, double minY, double minZ, double maxX, double maxY, double maxZ, V3* outN) {
	double tmin = -kInf, tmax = kInf; int axis = -1;
	const double o[3] = { ox, oy, oz }, d[3] = { dx, dy, dz }, mn[3] = { minX, minY, minZ }, mx[3] = { maxX, maxY, maxZ };
	for (int a = 0; a < 3; a++) {
		if (std::fabs(d[a]) < 1e-9) { if (o[a] < mn[a] || o[a] > mx[a]) return -1; continue; }
		double t1 = (mn[a] - o[a]) / d[a], t2 = (mx[a] - o[a]) / d[a];
		if (t1 > t2) std::swap(t1, t2);
		if (t1 > tmin) { tmin = t1; axis = a; }
		if (t2 < tmax) tmax = t2;
		if (tmin > tmax) return -1;
	}
	if (tmax < 0) return -1;
	if (outN) { double n[3] = { 0, 0, 0 }; if (axis >= 0) n[axis] = d[axis] > 0 ? -1 : 1; *outN = V3(n[0], n[1], n[2]); }
	return tmin >= 0 ? tmin : 0;
}
double RayOBBYaw(double ox, double oy, double oz, double dx, double dy, double dz, double cx, double cy, double cz, double yaw, double hx, double hy, double hz, V3* outN) {
	const double cs = std::cos(-yaw), sn = std::sin(-yaw);
	const double lx = ox - cx, lz = oz - cz;
	const double rox = lx * cs + lz * sn, roz = -lx * sn + lz * cs;
	const double rdx = dx * cs + dz * sn, rdz = -dx * sn + dz * cs;
	V3 n;
	const double t = RayAABB(rox, oy - cy, roz, rdx, dy, rdz, -hx, -hy, -hz, hx, hy, hz, &n);
	if (t >= 0 && outN) { const double c2 = std::cos(yaw), s2 = std::sin(yaw); *outN = V3(n.x * c2 + n.z * s2, n.y, -n.x * s2 + n.z * c2); }
	return t;
}
}

double BlastScale(const VehicleDef& def) {
	double k = std::cbrt((def.mass ? def.mass : 1450) / 1450);
	if (def.aircraft) k *= def.kind == "heli" ? 1.25 : 1.5;
	if (def.pedal || def.board) k *= 0.55;
	return Clamp(k, 0.35, 4.6);
}

Combat::Combat(Game& g) : game(g) {}

// Cast a ray against characters, vehicles and the static world: the nearest hit
bool Combat::raycast(double ox, double oy, double oz, double dx, double dy, double dz, double maxT, Character* exclude, CombatHit& best) {
	bool has = false;
	RayHit st;
	if (game.collision->raycast(ox, oy, oz, dx, dy, dz, maxT, st)) { has = true; best = CombatHit(); best.t = st.t; best.kind = CombatHit::Static; best.obj = st.obj; best.normal = V3(st.nx, st.ny, st.nz); }
	const double lim = has ? best.t : maxT;
	for (Character* c : game.allCharacters()) {
		if (c == exclude || c->removed) continue;
		if (exclude && exclude->vehicle && c->vehicle == exclude->vehicle) continue;
		const double cx = c->ragdolling && c->ragdoll ? c->ragdoll->pos[0] : c->pos.x, cz = c->ragdolling && c->ragdoll ? c->ragdoll->pos[2] : c->pos.z;
		// quick reject by distance from the ray
		const double lx = cx - ox, lz = cz - oz;
		const double along = lx * dx + lz * dz;
		if (along < -1 || along > lim + 1) continue;
		const double px = ox + dx * along - cx, pz = oz + dz * along - cz;
		if (px * px + pz * pz > 4) continue;
		Character::BodyHit h;
		if (c->rayHit(ox, oy, oz, dx, dy, dz, has ? best.t : maxT, h) && (!has || h.t < best.t)) {
			has = true; best = CombatHit(); best.t = h.t; best.kind = CombatHit::Char; best.ch = c; best.part = h.part; best.particle = h.particle;
		}
	}
	for (const auto& vp : game.vehicles.list) {
		Vehicle* v = vp.get();
		if (v->removed) continue;
		if (exclude && exclude->vehicle == v) continue;
		const double hy = v->def.H / 2;
		V3 n;
		const double t = RayOBBYaw(ox, oy, oz, dx, dy, dz, v->pos.x, v->pos.y + hy + 0.05, v->pos.z, v->yaw, v->hx, hy, v->hz, &n);
		if (t >= 0 && t < (has ? best.t : maxT)) {
			// occupants visible through the windows: check them first
			bool occ = false; CombatHit oh;
			for (const auto& o : v->occupants) {
				if (!o || o.get() == exclude) continue;
				Character::BodyHit h;
				if (o->rayHit(ox, oy, oz, dx, dy, dz, maxT, h) && h.t < t + 1.5) { occ = true; oh.t = h.t; oh.kind = CombatHit::Char; oh.ch = o.get(); oh.part = h.part; break; }
			}
			if (occ && Rand() < (!v->def.bike.empty() ? 0.9 : 0.55)) best = oh; // (nothing between you and a rider)
			else { best = CombatHit(); best.t = t; best.kind = CombatHit::Vehicle_; best.veh = v; best.normal = n; }
			has = true;
		}
	}
	double ht;
	if (game.police && game.police->heliRay(ox, oy, oz, dx, dy, dz, has ? best.t : maxT, ht)) {
		best = CombatHit(); best.t = ht; best.kind = CombatHit::Heli; best.normal = V3(-dx, -dy, -dz); has = true;
	}
	// (the animals join the ray with the wildlife)
	if (has) best.point = V3(ox + dx * best.t, oy + dy * best.t, oz + dz * best.t);
	return has;
}

void Combat::fireWeapon(Character* shooter, const WeaponDef& def, const V3& origin, const V3& aimDir, const FireOpts& opts) { fireWeaponHit(shooter, def, origin, aimDir, opts); }

bool Combat::fireWeaponHit(Character* shooter, const WeaponDef& def, const V3& origin, const V3& aimDir, const FireOpts& opts) {
	if (def.type == "launcher") { fireRocket(shooter, origin, aimDir); return false; }
	const V3 muzzle = shooter->muzzleWorld();
	// what the crosshair points at (the camera ray), then shoot from the muzzle toward it
	CombatHit aim;
	const bool hasAim = !opts.fromMuzzle && raycast(origin.x, origin.y, origin.z, aimDir.x, aimDir.y, aimDir.z, def.range, shooter, aim);
	const V3 target = hasAim ? aim.point : origin + aimDir * def.range;
	V3 base = target - muzzle;
	const double dist = base.length();
	base = base.normalized();
	const double spreadMul = opts.spreadMul * (shooter->isPlayer && shooter->crouching ? 0.6 : 1) * (shooter->isPlayer && Hypot(shooter->vel.x, shooter->vel.z) > 3 ? 1.8 : 1);
	const int pellets = def.pellets ? def.pellets : 1;
	bool anyHit = false;
	for (int i = 0; i < pellets; i++) {
		V3 d = base;
		const double sp = def.spread * spreadMul;
		d.x += Rand(-sp, sp); d.y += Rand(-sp, sp) * 0.8; d.z += Rand(-sp, sp);
		d = d.normalized();
		CombatHit hit;
		const bool h = raycast(muzzle.x, muzzle.y, muzzle.z, d.x, d.y, d.z, def.range, shooter, hit);
		const V3 end = h ? hit.point : muzzle + d * Min(def.range, dist + 30);
		if (i < 3 && game.effects) game.effects->tracer(muzzle, end);
		// (online: net.onShot)
		if (h) { anyHit = true; applyHit(hit, def, shooter, d); }
	}
	if (game.effects) game.effects->muzzleFlash(muzzle, base, def.id == "shotgun");
	game.soundAt(def.sound, muzzle, shooter->isPlayer ? 1 : 0.8);
	game.events.gunshot.emit(shooter, muzzle, def.id);
	return anyHit;
}

void Combat::applyHit(const CombatHit& hit, const WeaponDef& defIn, Character* shooter, const V3& dir, double shooterDamageMul) {
	WeaponDef def = defIn;
	const Cheats& ch = game.cheatsOn;
	const bool isPlayer = shooter && shooter->isPlayer;
	if (isPlayer && (ch.explosive || ch.oneHit)) {
		if (ch.explosive) explosion(hit.point, 3.2, 80, shooter);
		if (ch.oneHit) def.damage *= 10;
	}
	IEffects* fx = game.effects;
	if (hit.kind == CombatHit::Char) {
		Character* c = hit.ch;
		const bool wasDead = c->dead;
		double mul = 1;
		if (!isPlayer) {
			if (IsSet(shooterDamageMul)) mul = shooterDamageMul;
			else { const Ped* sp = dynamic_cast<const Ped*>(shooter); mul = sp && sp->damageMul ? sp->damageMul : 0.55; }
		}
		const double dmg = def.damage * mul;
		const V3 imp = dir * (def.id == "shotgun" ? 3 : 2.2) + V3(0, 0.6, 0);
		DamageInfo di; di.part = hit.part; di.source = shooter; di.type = "bullet"; di.hasImpulse = true; di.impulse = imp; di.hasHitPoint = true; di.hitPoint = hit.point; di.weapon = def.id; di.headMul = def.headMul;
		auto keep = c->shared_from_this();
		c->takeDamage(dmg, di);
		if (wasDead && c->ragdolling && c->ragdoll) c->ragdoll->push(c->ragdoll->nearestParticle(hit.point.x, hit.point.y, hit.point.z), dir.x * 3, 0.5, dir.z * 3);
		if (fx) fx->blood(hit.point, dir, hit.part == "head" ? 14 : 7);
		game.soundAt("bulletflesh", hit.point, 0.6);
		if (c->dead && !wasDead) {
			if (fx) fx->bloodPool(c->ragdolling && c->ragdoll ? V3(c->ragdoll->pos[0], 0, c->ragdoll->pos[2]) : c->pos);
			game.events.kill.emit(shooter, c, def.id, hit.part);
		}
	} else if (hit.kind == CombatHit::Vehicle_) {
		Vehicle* v = hit.veh;
		v->damage(def.damage * 0.9 * (IsSet(v->def.bulletMul) ? v->def.bulletMul : 1), shooter);
		if (fx) fx->impact(hit.point, hit.normal, "metal");
		game.soundAt("bulletmetal", hit.point, 0.5);
		// tyres and fuel: a small chance to ignite when already damaged
		if (v->health < 250 && Rand() < 0.05 && !v->def.tank) v->health = 0;
		game.events.vehicleShot.emit(v, shooter, hit.point);
	} else if (hit.kind == CombatHit::Heli) {
		game.police->heliHit(def.damage * (isPlayer ? 1 : 0.3));
		if (fx) fx->impact(hit.point, hit.normal, "metal");
		game.soundAt("bulletmetal", hit.point, 0.5);
		if (isPlayer) if (Police* pol = game.policeSys) pol->crime(0.3, hit.point, true);
	} else {
		const std::string kind = hit.obj && hit.obj->kind == CollObj::Circle ? "metal" : "concrete";
		if (fx) fx->impact(hit.point, hit.normal, kind);
		if (Rand() < 0.3) game.soundAt("ricochet", hit.point, 0.4);
	}
}

// ------------------------------------------------------------------ melee
void Combat::meleeHit(Character* attacker, const std::string& act) { melee(attacker, act); }

bool Combat::melee(Character* attacker, const std::string& act) {
	const WeaponDef* wd = FindWeapon(attacker->weapon);
	const WeaponDef& def = wd ? *wd : *FindWeapon("fist");
	const double range = def.range + (act == "kick" ? 0.3 : 0);
	const double fx = std::sin(attacker->yaw), fz = std::cos(attacker->yaw);
	bool hitAny = false;
	IEffects* fxs = game.effects;
	for (Character* c : game.allCharacters()) {
		if (c == attacker || c->dead || c->vehicle) continue;
		const double dx = c->pos.x - attacker->pos.x, dz = c->pos.z - attacker->pos.z;
		const double d = Hypot(dx, dz);
		if (!(d <= range + 0.35) || !(std::fabs(c->pos.y - attacker->pos.y) <= 1.2)) continue; // (NaN-safe)
		const double dot = (dx * fx + dz * fz) / (d ? d : 1);
		if (dot < 0.45 && d > 0.6) continue;
		double dmg = def.damage;
		if (act == "kick") dmg *= 1.6;
		if (act == "cross") dmg *= 1.2;
		// NPC melee is toned down so a group can't flatten the player in a second or two
		if (!attacker->isPlayer) dmg *= c->isPlayer ? 0.42 : 0.8;
		if (c->ragdolling) dmg *= 1.3;
		const bool strong = act == "kick" || def.id == "bat" || (act == "cross" && Rand() < 0.25);
		const V3 imp(fx * (strong ? 3.5 : 1.5), strong ? 1.5 : 0.5, fz * (strong ? 3.5 : 1.5));
		const V3 hp = c->chestPos();
		DamageInfo di;
		di.part = act == "cross" && Rand() < 0.3 && attacker->isPlayer ? "head" : "torso";
		di.headMul = 1.5; di.source = attacker; di.type = "melee"; di.hasImpulse = true; di.impulse = imp; di.knockdown = strong && !c->isPlayer; di.hasHitPoint = true; di.hitPoint = hp;
		auto keep = c->shared_from_this();
		c->takeDamage(dmg, di);
		if (def.id == "knife") { if (fxs) fxs->blood(hp, V3(fx, 0.2, fz), 10); }
		else if (def.id == "bat" && Rand() < 0.5 && fxs) fxs->blood(hp, V3(fx, 0.3, fz), 4);
		game.soundAt(def.id == "knife" ? "stab" : def.id == "bat" ? "bat" : "punch", hp, 0.9);
		game.events.melee.emit(attacker, c, def.id);
		if (c->dead) game.events.kill.emit(attacker, c, def.id, "torso");
		hitAny = true;
		if (!strong) break;
	}
	// (animals within reach: with the wildlife)
	// hitting a car with a bat dents it
	if (!hitAny && def.id == "bat") {
		for (const auto& vp : game.vehicles.list) {
			Vehicle* v = vp.get();
			double lx, lz; v->worldToLocal(attacker->pos.x + fx * 1.2, attacker->pos.z + fz * 1.2, lx, lz);
			if (std::fabs(lx) < v->hx + 0.2 && std::fabs(lz) < v->hz + 0.2) { v->damage(15, attacker); v->dent(attacker->pos.x + fx * 1.2, v->pos.y + 0.8, attacker->pos.z + fz * 1.2, 12); game.soundAt("metalhit", v->pos, 0.8); break; }
		}
	}
	return hitAny;
}

// ------------------------------------------------------------------ explosions & projectiles
// a vehicle blowing up, sized to the vehicle (see BlastScale)
void Combat::vehicleExplosion(Vehicle* v, const V3& pos) {
	const double k = BlastScale(v->def);
	double yaw = v->yaw;
	if (v->def.aircraft) { const V3 f = v->bodyQuat().rotate(V3(0, 0, 1)); yaw = std::atan2(f.x, f.z); }
	const double hx = v->def.aircraft && v->def.kind != "heli" ? v->def.W * 0.38 : v->hx; // (fuel is in the wings)
	ExplosionOpts o; o.size = k; o.hasFoot = true; o.foot[0] = hx; o.foot[1] = v->hz; o.foot[2] = std::sin(yaw); o.foot[3] = std::cos(yaw);
	explosion(pos, 9 * std::pow(k, 0.85), 180 * std::sqrt(k), v->lastDamager.get(), v, o);
}

void Combat::explosion(const V3& pos, double radius, double damage, Character* source, Vehicle* excludeVehicle, const ExplosionOpts& opts) {
	const double size = IsSet(opts.size) ? opts.size : radius / 9; // 1 = a car
	const double vis = IsSet(opts.size) ? 6.75 * size : radius * 0.75;
	if (game.effects) game.effects->explosion(pos, vis, opts.hasFoot ? opts.foot : nullptr);
	game.soundAt("explosion", pos, Min(1.6, 0.7 + size * 0.3));
	if (visualOnly) return;
	const V3 pd = game.player->vehicle ? game.player->vehicle->pos : game.player->pos;
	const double dp = (pos - pd).length();
	const double reach = 60 * Max(1, std::sqrt(size));
	game.rig.addShake(Clamp((1.2 - dp / reach) * Min(1.4, Max(0.6, std::sqrt(size))), 0, 1.5));
	for (Character* c : game.allCharacters()) {
		if (c->removed) continue;
		const V3 cp = c->ragdolling && c->ragdoll ? c->ragdoll->center() : c->pos;
		const double d = (cp - pos).length();
		if (!(d <= radius)) continue;
		const double k = 1 - d / radius;
		const V3 dir = V3(cp.x - pos.x, 0, cp.z - pos.z).normalized();
		const V3 imp = dir * (6 + k * 10) + V3(0, 4 + k * 7, 0);
		auto keep = c->shared_from_this();
		if (c->vehicle) { DamageInfo di; di.type = "explosion"; di.source = source; c->takeDamage(damage * k * 0.4, di); continue; }
		const bool wasDead = c->dead;
		DamageInfo di; di.type = "explosion"; di.source = source; di.hasImpulse = true; di.impulse = imp; di.knockdown = true;
		c->takeDamage(damage * k, di);
		if (c->ragdolling && c->ragdoll) c->ragdoll->push(0, imp.x, imp.y, imp.z);
		if (c->dead && !wasDead) game.events.kill.emit(source, c, "explosion", "torso");
	}
	// (animals: with the wildlife)
	const std::vector<std::shared_ptr<Vehicle>> list = game.vehicles.list;
	for (const auto& vp : list) {
		Vehicle* v = vp.get();
		if (v == excludeVehicle || v->exploded) continue;
		const double d = (v->pos - pos).length();
		if (!(d <= radius * 1.3)) continue;
		const double k = 1 - d / (radius * 1.3);
		v->damage(damage * k * 4.5 * (IsSet(v->def.blastMul) ? v->def.blastMul : 1), source);
		if (!v->def.kind.empty()) continue; // aircraft and tanks don't get tossed around
		const V3 dir = V3(v->pos.x - pos.x, 0, v->pos.z - pos.z).normalized();
		const double push = Min(2.2, std::sqrt(damage / 180));
		v->blast(pos, k * push, dir); // (tumbles: flips and rolls)
		v->dent(pos.x, pos.y, pos.z, 30 * k);
	}
	// props
	for (CollObj* col : game.propColliders) {
		if (!col || col->broken || !col->breakable) continue;
		const double d = Hypot(col->x - pos.x, col->z - pos.z);
		if (d < radius * 0.8) {
			game.breakProp(col);
			const V3 dir = V3(col->x - pos.x, 0, col->z - pos.z).normalized() * 12;
			if (game.effects) game.effects->propDebris(col, dir);
		}
	}
	game.events.explosion.emit(pos, radius, source);
}

void Combat::fireRocket(Character* shooter, const V3& origin, const V3& aimDir) {
	const V3 muzzle = shooter->muzzleWorld();
	CombatHit aim;
	const bool has = raycast(origin.x, origin.y, origin.z, aimDir.x, aimDir.y, aimDir.z, 300, shooter, aim);
	const V3 target = has ? aim.point : origin + aimDir * 300;
	const V3 dir = (target - muzzle).normalized();
	Projectile p; p.type = "rocket"; p.kind = "rocket"; p.pos = muzzle; p.vel = dir * 55; p.owner = Ref<Character>(shooter);
	projectiles.push_back(p);
	if (game.effects) game.effects->muzzleFlash(muzzle, dir, true);
	game.soundAt("rpg", muzzle, 1);
	game.events.gunshot.emit(shooter, muzzle, "rpg");
}

// vehicle-mounted guns (the jet's cannon, the helicopter's minigun): hitscan from a muzzle with tracers
void Combat::vehicleGun(Character* shooter, const V3& muzzle, const V3& dir, const WeaponDef& def) {
	V3 d = dir;
	const double sp = def.spread;
	d.x += Rand(-sp, sp); d.y += Rand(-sp, sp); d.z += Rand(-sp, sp);
	d = d.normalized();
	CombatHit hit;
	const bool h = raycast(muzzle.x, muzzle.y, muzzle.z, d.x, d.y, d.z, def.range, shooter, hit);
	const V3 end = h ? hit.point : muzzle + d * def.range;
	if (Rand() < 0.7 && game.effects) game.effects->tracer(muzzle, end);
	if (h) applyHit(hit, def, shooter, d, shooter ? NaN() : 1.0);
	if (game.effects) game.effects->muzzleFlash(muzzle, d, false);
	game.soundAt(def.sound.empty() ? "smg" : def.sound, muzzle, shooter && shooter->isPlayer ? 0.9 : 0.7);
	game.events.gunshot.emit(shooter, muzzle, def.id);
}

// rockets (straight), missiles (homing, accelerating) and tank shells (fast, slight drop)
void Combat::fireProjectile(Character* shooter, const std::string& kind, const V3& pos, const V3& dir, const ProjectileOpts& o) {
	Projectile p;
	p.type = "rocket"; p.kind = kind; p.pos = pos; p.owner = Ref<Character>(shooter);
	if (kind == "rocket") p.scale = 1.8;
	p.vel = dir * o.speed;
	if (o.hasInherit) p.vel = p.vel + o.inherit * 0.9;
	p.radius = o.radius; p.damage = o.damage; p.gravity = o.gravity; p.life = o.life;
	p.target = Ref<Vehicle>(o.target); p.targetHeli = o.targetHeli; p.turn = o.turn; p.accel = o.accel; p.maxSpeed = o.maxSpeed;
	projectiles.push_back(p);
	if (game.effects) game.effects->muzzleFlash(pos, dir, true);
	if (kind != "shell") game.soundAt("rpg", pos, 0.9);
	game.events.gunshot.emit(shooter, pos, "rpg");
}

// the best homing target in a narrow cone ahead: occupied vehicles and aircraft (and the police helicopter)
Vehicle* Combat::lockTarget(const V3& from, const V3& dir, Vehicle* exclude, double maxDist, bool* heli) {
	Vehicle* best = nullptr;
	double bs = -kInf;
	bool bestHeli = false;
	V3 hp;
	if (game.police && game.police->heliAlive(hp)) {
		const double dx = hp.x - from.x, dy = hp.y - from.y, dz = hp.z - from.z;
		const double d = Hypot3(dx, dy, dz);
		const double c = (dx * dir.x + dy * dir.y + dz * dir.z) / d;
		if (d >= 15 && d <= maxDist && c >= 0.97) { bs = c * 2 - d / maxDist; bestHeli = true; }
	}
	for (const auto& vp : game.vehicles.list) {
		Vehicle* v = vp.get();
		if (v == exclude || v->removed || v->isWrecked()) continue;
		if (!v->driver() && !v->def.aircraft) continue;
		const V3 p = v->def.aircraft ? v->cgPoint() : v->pos;
		const double dx = p.x - from.x, dy = p.y - from.y, dz = p.z - from.z;
		const double d = Hypot3(dx, dy, dz);
		if (d < 15 || d > maxDist) continue;
		const double c = (dx * dir.x + dy * dir.y + dz * dir.z) / d;
		if (c < 0.97) continue;
		const double score = c * 2 - d / maxDist;
		if (score > bs) { bs = score; best = v; bestHeli = false; }
	}
	if (heli) *heli = bestHeli;
	return best;
}

void Combat::throwGrenade(Character* thrower, const V3& dir, const std::string& kind) {
	const V3 p = thrower->pos + V3(0, 1.7, 0) + thrower->forward() * 0.4;
	const V3 v = V3(dir.x, Max(dir.y, 0) + 0.35, dir.z).normalized() * 17;
	Projectile pr; pr.type = kind == "molotov" ? "molotov" : "grenade"; pr.pos = p; pr.vel = v; pr.owner = Ref<Character>(thrower);
	projectiles.push_back(pr);
	game.soundAt("swoosh", p, 0.6);
}

// a pool of burning fuel: sets people alight and cooks cars that sit in it
void Combat::ignite(const V3& pos, Character* owner, double r, double life) {
	const double gy = game.collision->floorHeight(pos.x, pos.z, pos.y + 1);
	fires.push_back({ pos.x, gy, pos.z, r, 0, life, Ref<Character>(owner), 0 });
	game.soundAt("glass", pos, 0.9);
	game.soundAt("explosion", pos, 0.25);
	for (int k = 0; k < 18; k++) if (game.effects) game.effects->fire(V3(pos.x + Rand(-r, r) * 0.6, gy + 0.1, pos.z + Rand(-r, r) * 0.6), 1.4);
	game.events.explosion.emit(pos, r, owner);
}

void Combat::updateFires(double dt) {
	IEffects* fx = game.effects;
	for (int i = (int)fires.size() - 1; i >= 0; i--) {
		Fire& f = fires[i];
		f.t += dt;
		if (f.t > f.life) { fires.erase(fires.begin() + i); continue; }
		const double k = f.t < f.life - 2 ? 1 : (f.life - f.t) / 2; // dies down at the end
		const int n = Rand() < dt * 30 * k ? 2 : 0;
		for (int q = 0; q < n; q++) { const double a = Rand() * 6.283, rr = std::sqrt(Rand()) * f.r; if (fx) fx->fire(V3(f.x + std::cos(a) * rr, f.y + 0.05, f.z + std::sin(a) * rr), 1.1 + Rand() * 0.8); }
		f.tick -= dt;
		if (f.tick > 0) continue;
		f.tick = 0.25;
		Character* owner = f.owner.get();
		auto burn = [&](Character* c) {
			if (c->dead || c->vehicle || std::fabs(c->pos.y - f.y) > 2) return;
			if (Hypot(c->pos.x - f.x, c->pos.z - f.z) > f.r * (0.6 + 0.4 * k)) return;
			DamageInfo di; di.type = "fire"; di.source = owner; di.part = "torso";
			c->takeDamage(7, di);
			c->onFire = Max(c->onFire, 2.5);
			if (Ped* p = dynamic_cast<Ped*>(c)) if (p->state != "flee" && p->brain != "script") { p->threatPos = V3(f.x, f.y, f.z); p->setState("flee"); }
		};
		burn(game.player.get());
		if (game.peds) { const auto list = game.peds->list; for (const auto& p : list) burn(p.get()); }
		for (const auto& vp : game.vehicles.list) {
			Vehicle* v = vp.get();
			if (v->removed || v->isWrecked() || v->def.kind == "boat") continue;
			if (Hypot(v->pos.x - f.x, v->pos.z - f.z) < f.r + 1.5) v->damage(22, owner);
		}
	}
	// people who ran out of the fire keep burning a moment
	auto tick = [&](Character* c) {
		if (c->onFire <= 0 || c->dead) { c->onFire = 0; return; }
		c->onFire -= dt;
		if (Rand() < dt * 20 && fx) fx->fire(V3(c->pos.x, c->pos.y + 1, c->pos.z), 0.6);
		if (Rand() < dt * 3) { DamageInfo di; di.type = "fire"; di.part = "torso"; c->takeDamage(3, di); }
	};
	tick(game.player.get());
	if (game.peds) { const auto list = game.peds->list; for (const auto& p : list) if (p->onFire > 0) tick(p.get()); }
}

void Combat::update(double dt) {
	if (!fires.empty() || game.player->onFire > 0) updateFires(dt);
	Effects* fx = dynamic_cast<Effects*>(game.effects);
	for (int i = (int)projectiles.size() - 1; i >= 0; i--) {
		Projectile& pr = projectiles[i];
		pr.t += dt;
		if (pr.type == "rocket") {
			// homing: steer toward the locked target, speed up to the motor's max
			Vehicle* tg = pr.target.get();
			bool near = false;
			V3 heliPos;
			const bool heliTg = pr.targetHeli && game.police && game.police->heliAlive(heliPos);
			if (heliTg || (tg && !tg->removed && !tg->exploded)) {
				V3 tp = heliTg ? heliPos : tg->def.aircraft ? tg->cgPoint() : V3(tg->pos.x, tg->pos.y + tg->def.H / 2, tg->pos.z);
				V3 want = tp - pr.pos;
				near = want.length() < 3.5;
				want = want.normalized();
				const double sp = pr.vel.length();
				V3 cur = pr.vel * (1 / sp);
				const double ang = std::acos(Clamp(cur.dot(want), -1, 1));
				if (ang > 1e-4) cur = cur.lerp(want, Min(1, (pr.turn * dt) / ang)).normalized();
				pr.vel = cur * sp;
			}
			if (pr.accel) { const double sp = pr.vel.length(); pr.vel = pr.vel * (Min(pr.maxSpeed ? pr.maxSpeed : sp, sp + pr.accel * dt) / sp); }
			if (pr.gravity) pr.vel.y -= pr.gravity * dt;
			const double step = pr.vel.length() * dt;
			const V3 d = pr.vel.normalized();
			CombatHit hit;
			const bool h = raycast(pr.pos.x, pr.pos.y, pr.pos.z, d.x, d.y, d.z, step + 0.3, pr.owner.get(), hit);
			if (h || near || pr.t > pr.life) {
				const Projectile done = pr;
				projectiles.erase(projectiles.begin() + i);
				explosion(h ? hit.point : done.pos, done.radius, done.damage, done.owner.get());
				Vehicle* hv = h && hit.kind == CombatHit::Vehicle_ ? hit.veh : near && tg && !heliTg ? tg : nullptr;
				if (hv) { if (hv->def.tank) hv->damage(900, done.owner.get()); else hv->health = Min(hv->health, -1); if (hv->def.aircraft) hv->explode(); }
				if ((h && hit.kind == CombatHit::Heli) || (near && heliTg)) game.police->heliHit(9999);
				continue;
			}
			pr.pos = pr.pos + pr.vel * dt;
			if (pr.kind == "shell" || !fx) continue;
			Particle s; s.x = pr.pos.x; s.y = pr.pos.y; s.z = pr.pos.z; s.vx = Rand(-0.3, 0.3); s.vy = Rand(0, 0.5); s.vz = Rand(-0.3, 0.3);
			s.life = 1.6; s.size0 = 0.3; s.size1 = 1.8; s.rot = Rand() * 6; s.spin = 0.5; s.drag = 1; s.alpha = 0.5; s.fadeIn = 0.05; s.fadePow = 1.5;
			s.color[0] = s.color[1] = s.color[2] = 0.8f;
			fx->alphaPool.spawn(std::move(s));
			Particle f; f.x = pr.pos.x; f.y = pr.pos.y; f.z = pr.pos.z; f.life = 0.08; f.size0 = 0.5; f.size1 = 0.3; f.rot = 0; f.spin = 0; f.drag = 0; f.alpha = 1; f.fadeIn = 0.01; f.fadePow = 1;
			f.color[0] = 6; f.color[1] = 3; f.color[2] = 1;
			fx->addPool.spawn(std::move(f));
		} else if (pr.type == "molotov") {
			pr.vel.y -= 16 * dt;
			const double step = pr.vel.length() * dt;
			const V3 d = pr.vel.normalized();
			CombatHit hit;
			const bool h = raycast(pr.pos.x, pr.pos.y, pr.pos.z, d.x, d.y, d.z, step + 0.1, pr.owner.get(), hit);
			const double gh = game.map.GroundHeight(pr.pos.x, pr.pos.z) + 0.05;
			if (h || pr.pos.y < gh || pr.t > 4) {
				const Projectile done = pr;
				projectiles.erase(projectiles.begin() + i);
				ignite(h ? hit.point : V3(done.pos.x, Max(done.pos.y, gh), done.pos.z), done.owner.get());
				continue;
			}
			pr.pos = pr.pos + pr.vel * dt;
			pr.spin += dt * 9;
		} else if (pr.type == "grenade") {
			pr.vel.y -= 16 * dt;
			pr.pos = pr.pos + pr.vel * dt;
			const double gh = game.map.GroundHeight(pr.pos.x, pr.pos.z) + 0.06;
			if (pr.pos.y < gh) { pr.pos.y = gh; pr.vel.y = std::fabs(pr.vel.y) * 0.35; pr.vel.x *= 0.6; pr.vel.z *= 0.6; if (std::fabs(pr.vel.y) > 1.5) game.soundAt("clink", pr.pos, 0.5); }
			const CollisionWorld::CircleRes res = game.collision->resolveCircle(pr.pos.x, pr.pos.z, 0.08, pr.pos.y, 0.1);
			if (res.hit) { pr.vel.x *= -0.4; pr.vel.z *= -0.4; pr.pos.x = res.x; pr.pos.z = res.z; }
			pr.spin += dt * 10;
			if (pr.t > 3) {
				const Projectile done = pr;
				projectiles.erase(projectiles.begin() + i);
				explosion(V3(done.pos.x, done.pos.y + 0.3, done.pos.z), 7, 170, done.owner.get());
			}
		}
	}
}

} // namespace atg
