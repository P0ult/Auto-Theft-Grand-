#include "Peds.h"
#include "Animal.h"
#include "Collision.h"
#include "Game.h"

namespace atg {

const std::map<std::string, GangDef>& Gangs() {
	static const std::map<std::string, GangDef> G = {
		{ "kings", { "kings", "Cedar Row Kings", 0xf2b705, "hood", true, { "pistol", "bat", "fist" }, false, 0 } },
		{ "vipers", { "vipers", "Vipers", 0xc1121f, "corona", false, { "pistol", "smg", "bat", "knife" }, false, 0 } },
		{ "cuervos", { "cuervos", "Los Cuervos", 0x1f7a8c, "docks", false, { "pistol", "smg", "shotgun" }, false, 0 } },
		// Fort Carver garrison: only hostile once the base is on alert (see military.js)
		{ "army", { "army", "Army", 0x556b2f, "base", false, { "rifle" }, true, 70 } },
		// San Aurelio's dock mob: Vincent Castell's Harbor Saints, out of Harborside
		{ "saints", { "saints", "Harbor Saints", 0x1d3557, "aurharbor", false, { "pistol", "smg", "shotgun", "knife" }, false, 0 } },
		// the MV Pacific Star's crew and security: only hostile once the ship is alerted (see shipraid.js)
		{ "crew", { "crew", "Ship Crew", 0xff8c1a, "ship", false, { "pistol" }, true, 55 } },
	};
	return G;
}
const std::vector<std::string>& GangOrder() { static const std::vector<std::string> O = { "kings", "vipers", "cuervos", "army", "saints", "crew" }; return O; }

Appearance SaintLook(RNG& rng) {
	const bool female = rng.Chance(0.08);
	Appearance a = RandomAppearance(rng, female ? 1 : 0);
	a.shirt = rng.Pick(std::vector<uint32_t>{ 0xf2f2f2, 0xdfe6ee, 0x9aa7b5 });
	a.shirtType = "jacket";
	a.jacketColor = rng.Pick(std::vector<uint32_t>{ 0x1d3557, 0x14213d, 0x22223b, 0x2b2d42 });
	a.pants = rng.Pick(std::vector<uint32_t>{ 0x1a1a1a, 0x2b2d42, 0x3d405b });
	a.hairStyle = rng.Pick(std::vector<std::string>{ "cap", "short", "buzz", "cap" });
	a.hat = rng.Pick(std::vector<uint32_t>{ 0x14213d, 0x3d3d3d });
	a.glasses = rng.Chance(0.35);
	a.beard = rng.Chance(0.4);
	a.shoes = 0x111111;
	return a;
}

const std::vector<std::string>& PedLines(const std::string& kind) {
	static const std::map<std::string, std::vector<std::string>> L = {
		{ "bump", { "Watch it!", "Hey!", "Excuse you!", "You blind?", "Move!" } },
		{ "flee", { "Help!", "Oh my god!", "Somebody call the cops!", "Run!", "He's crazy!", "Aaaah!" } },
		{ "fight", { "You want some?", "Big mistake!", "Come on then!", "You're dead!" } },
		{ "gang", { "Wrong hood, fool!", "Vipers run this!", "Get outta here!", "You lost?" } },
		{ "handsUp", { "Take it! Take anything!", "Don't shoot!", "Please!" } },
		{ "jacked", { "My car!", "Hey, that's my ride!", "Thief!" } },
		{ "army", { "Contact!", "Intruder on base!", "Open fire!", "Take him down!", "Weapons free!" } },
		{ "crew", { "Boarder on deck!", "Get off my ship!", "Stop him!", "Protect the cargo!", "Nobody touches the bridge!" } },
	};
	static const std::vector<std::string> none;
	auto it = L.find(kind);
	return it == L.end() ? none : it->second;
}

bool WalkAreaAt(const CityMap& map, double x, double z, WalkArea& out) {
	if (const Block* b = map.BlockAt(x, z)) { out.nodeIds = &b->nodeIds; out.district = b->district; return true; }
	for (const TownArea& t : map.townAreas) if (Hypot(x - t.x, z - t.z) < t.r && !t.nodeIds.empty()) { out.nodeIds = &t.nodeIds; out.district = t.district; return true; }
	return false;
}

// ==================================================================== Ped
Ped::Ped(Game& g, const Appearance& look, const PedOpts& o)
	: Character(g, look, o.health, o.armor, o.team), brain(o.brain), gang(o.gang), state(o.state),
	  walkSpeed(Rand(1.15, 1.55)), brave(Rand()), thinkTimer(Rand() * 0.3), persistent(o.persistent), missionTag(o.missionTag),
	  accuracy(o.accuracy), damageMul(o.damageMul) {}

void Ped::say(const std::string& text) {
	lastSay = game.time;
	if (game.hud) game.hud->speech(this, text);
}

void Ped::onDamaged(Character* src, double dmg, const DamageInfo& info) { if (game.peds) game.peds->onPedDamaged(this, src, dmg, info); }
void Ped::onCarjacked(Character* by) { say(RandPick(PedLines("jacked"))); threat = by; setState(brave > 0.75 ? "attack" : "flee"); }
void Ped::onBumped(Vehicle*) { if (state == "wander" && game.time - lastSay > 3) say(RandPick(PedLines("bump"))); }

bool Ped::goTo(double x, double z, double speed, double dt, double arriveDist) {
	const double dx = x - pos.x, dz = z - pos.z;
	const double d = Hypot(dx, dz);
	if (!(d >= arriveDist) || d < 1e-4) { stop(); return true; }
	const double sp = Min(speed, d * 3);
	moveTargetX = dx / d * sp; moveTargetZ = dz / d * sp;
	yaw = DampAngle(yaw, std::atan2(dx, dz), 8, dt);
	return false;
}

void Ped::think(double dt) {
	stateTime += dt;
	if (dead || ragdolling || vehicle) { stop(); aiming = false; return; }
	if (anim->action && !anim->action->done && anim->action->name == "getup") { stop(); return; }
	if (brain == "cop") { if (game.police) game.police->copThink(this, dt); return; }
	if (brain == "script") { if (scriptThink) scriptThink(dt); return; }
	Player& player = *game.player;
	animState.cower = false;
	animState.handsUp = false;
	animState.talking = false;
	crouching = false;
	aiming = false;
	if ((crimeTask || npcCase) && game.npcCrime && game.npcCrime->pedThink(this, dt)) return;
	if (!gang.empty() && !Gangs().at(gang).friendly && !player.dead && state != "attack" && state != "flee") {
		const GangDef& G = Gangs().at(gang);
		const V3 pp = player.vehicle ? player.vehicle->pos : player.pos;
		const double d2 = Dist2(pos.x, pos.z, pp.x, pp.z);
		const double R = G.range ? G.range : 22;
		if (d2 < R * R && ((game.peds && game.peds->gangAggro[gang]) || (d2 < 12 * 12 && !G.aggroOnly))) {
			threat = &player; setState("attack");
			if (game.time - lastSay > 5) {
				const std::vector<std::string>& own = PedLines(gang);
				say(RandPick(G.aggroOnly ? (own.empty() ? PedLines("army") : own) : PedLines("gang")));
			}
		}
	}
	if (brain == "civilian" && player.aiming && !player.vehicle && state != "flee" && state != "attack") {
		const double d = Hypot(pos.x - player.pos.x, pos.z - player.pos.z);
		if (d < 14) {
			const V3 dir = game.rig.lookDir();
			const V3 cp = game.rig.camPos;
			const double tx = pos.x - cp.x, ty = pos.y + 1.2 - cp.y, tz = pos.z - cp.z;
			const double tl = Hypot3(tx, ty, tz);
			if ((dir.x * tx + dir.y * ty + dir.z * tz) / tl > 0.97) {
				if (state != "handsup") { setState("handsup"); say(RandPick(PedLines("handsUp"))); }
			}
		}
	}
	if (state == "wander") wander(dt);
	else if (state == "idle") {
		stop();
		Ped* tp = talkPartner.get();
		animState.talking = tp != nullptr;
		if (tp) faceTowards(tp->pos.x, tp->pos.z, dt, 3);
		if (stateTime > (idleTime ? idleTime : 6)) { talkPartner = nullptr; setState("wander"); }
	} else if (state == "handsup") {
		stop();
		animState.handsUp = true;
		faceTowards(player.pos.x, player.pos.z, dt, 6);
		if (!player.aiming || stateTime > 8) { threat = &player; threatPos = player.pos; setState("flee"); }
	} else if (state == "cower") {
		stop();
		crouching = true;
		animState.cower = true;
		if (stateTime > (cowerTime ? cowerTime : 4)) setState("flee");
	} else if (state == "flee") flee(dt);
	else if (state == "attack") attack(dt);
	else if (state == "follow") followLeader(dt);
	else if (state == "guard") guard(dt);
	else if (state == "goto") {
		if (goTo(targetPos.x, targetPos.z, gotoSpeed ? gotoSpeed : walkSpeed, dt, 0.6)) { setState(afterGoto.empty() ? "guard" : afterGoto); if (onArrive) { auto f = onArrive; f(); } }
	} else stop();
}

void Ped::wander(double dt) {
	const CityMap& map = game.map;
	const auto& nodes = map.walkNodes;
	if (node < 0) {
		int best = -1; double bd = kInf;
		WalkArea wa;
		if (WalkAreaAt(map, pos.x, pos.z, wa)) {
			for (int i : *wa.nodeIds) { const double d = Dist2(nodes[i].x, nodes[i].z, pos.x, pos.z); if (d < bd) { bd = d; best = i; } }
		} else {
			for (int i = 0; i < (int)nodes.size(); i++) { const double d = Dist2(nodes[i].x, nodes[i].z, pos.x, pos.z); if (d < bd) { bd = d; best = i; } }
		}
		node = best;
		nodeOffset = Rand(-1.2, 1.2);
		if (best < 0) { stop(); return; }
	}
	const WalkNode& n = nodes[node];
	const double tx = n.x + offX, tz = n.z + offZ;
	if (jaywalker != 0 && Rand() < dt * 0.015 && game.npcCrime && game.npcCrime->jaywalk(this)) return;
	if (goTo(tx, tz, walkSpeed, dt, 0.7)) {
		std::vector<int> opts;
		for (int l : n.links) if (l != prevNode) opts.push_back(l);
		if (opts.empty()) opts = n.links;
		if (opts.empty()) { stop(); return; }
		int next = RandPick(opts);
		if (std::find(n.cross.begin(), n.cross.end(), next) != n.cross.end() && Rand() < 0.5) next = RandPick(opts);
		prevNode = node;
		node = next;
		offX = Rand(-1.1, 1.1); offZ = Rand(-1.1, 1.1);
		if (Rand() < 0.08) { setState("idle"); idleTime = Rand(3, 9); stop(); }
	}
}

void Ped::flee(double dt) {
	Character* t = threat.get();
	V3 tp = t && t != this ? (t->vehicle ? t->vehicle->pos : t->pos) : threatAnimal ? threatAnimal->pos : threatPos;
	if (!Finite(tp.x) || !Finite(tp.z)) { threat = nullptr; tp = threatPos; }
	double dx = pos.x - tp.x, dz = pos.z - tp.z;
	if (!Finite(dx) || !Finite(dz)) { dx = std::sin(yaw); dz = std::cos(yaw); }
	double d = Hypot(dx, dz); if (d == 0) d = 1;
	dx /= d; dz /= d;
	const double sp = hasFleeSpeed ? fleeSpeed : 5.4 + brave * 1.2;
	const auto probe = game.collision->resolveCircle(pos.x + dx * 1.5, pos.z + dz * 1.5, 0.4, pos.y + 0.3, 1.4);
	if (probe.hit) {
		const double px = probe.x - (pos.x + dx * 1.5), pz = probe.z - (pos.z + dz * 1.5);
		dx += px * 2; dz += pz * 2;
		double l = Hypot(dx, dz); if (l == 0) l = 1;
		dx /= l; dz /= l;
	}
	moveTargetX = dx * sp; moveTargetZ = dz * sp;
	yaw = DampAngle(yaw, std::atan2(dx, dz), 10, dt);
	if (stateTime > 9 + brave * 4 && d > 40) { node = -1; setState("wander"); threat = nullptr; }
	if (stateTime > 25) { node = -1; setState("wander"); threat = nullptr; }
}

void Ped::attack(double dt) {
	Character* t = threat.get();
	Animal* a = t ? nullptr : threatAnimal.get();
	if ((!t && !a) || (t && (t == this || t->dead || t->removed)) || (a && (a->dead || a->removed))) { threat = nullptr; threatAnimal = nullptr; node = -1; setState(!gang.empty() ? "guard" : "wander"); return; }
	Vehicle* tv = t ? t->vehicle : a->inVehicle.get();
	const V3 tp = tv ? tv->pos : t ? t->pos : a->pos;
	const double dx = tp.x - pos.x, dz = tp.z - pos.z;
	const double d = Hypot(dx, dz);
	const double grange = !gang.empty() ? Gangs().at(gang).range : 0;
	if (!(d <= Max(60, grange + 25)) || d < 1e-3) { threat = nullptr; threatAnimal = nullptr; setState(!gang.empty() ? "guard" : "wander"); return; }
	const WeaponDef& def = weaponDef();
	if (def.type == "gun") {
		aiming = true;
		faceTowards(tp.x, tp.z, dt, 12);
		const double aimY = (tv ? tp.y + 1.0 : tp.y + 1.3) - (pos.y + 1.45);
		aimPitch = std::atan2(aimY, d);
		hasAimDir = true;
		aimDir = V3(dx / d * std::cos(aimPitch), std::sin(aimPitch), dz / d * std::cos(aimPitch));
		const double want = 9 + (uid % 5);
		if (d > want + 4) goTo(tp.x, tp.z, 4.2, dt, 0);
		else if (d < want - 4) { moveTargetX = -dx / d * 2; moveTargetZ = -dz / d * 2; }
		else { const double s = std::sin(game.time * 0.8 + uid) * 1.8; moveTargetX = -dz / d * s; moveTargetZ = dx / d * s; }
		yaw = std::atan2(dx, dz);
		fireTimer -= dt;
		WeaponSlot& w = weapons[weapon];
		if (fireTimer <= 0 && stateTime > 0.6) {
			const bool see = game.collision->lineOfSight(pos.x, pos.y + 1.5, pos.z, tp.x, tp.y + 1.3, tp.z);
			if (see) {
				if (w.clip <= 0) { w.clip = def.clip; fireTimer = 1.5; anim->play("reload"); return; }
				w.clip--;
				fireTimer = def.rate * (def.automatic ? 1.2 : 2.5) + Rand(0, 0.35) + (def.automatic && Rand() < 0.15 ? 0.8 : 0);
				const V3 muzzle = muzzleWorld();
				V3 dir = (V3(tp.x, tp.y + (tv ? 0.9 : 1.2), tp.z) - muzzle).normalized();
				const V3 targetVel = t ? t->vel : a->vel;
				const double inacc = (1 - accuracy) * 0.12 + (Hypot(targetVel.x, targetVel.z) > 4 ? 0.05 : 0);
				dir.x += Rand(-inacc, inacc); dir.y += Rand(-inacc, inacc) * 0.6; dir.z += Rand(-inacc, inacc); dir.normalize();
				anim->recoil = def.recoil;
				ICombat::FireOpts fo; fo.fromMuzzle = true; fo.spreadMul = 1.5;
				if (game.combat) game.combat->fireWeapon(this, def, muzzle, dir, fo);
			} else if (d > 6) goTo(tp.x, tp.z, 4.5, dt, 0);
		}
	} else {
		faceTowards(tp.x, tp.z, dt, 10);
		if (d > 1.3) goTo(tp.x, tp.z, 5.2, dt, 1.0);
		else {
			stop();
			meleeTimer -= dt;
			if (meleeTimer <= 0 && !anim->busy() && !tv) {
				meleeTimer = Rand(0.85, 1.5);
				const std::string act = weapon == "knife" ? "stab" : weapon == "bat" ? "swing" : RandPick(std::vector<std::string>{ "jab", "cross", "jab", "kick" });
				auto action = anim->play(act);
				Game* g = &game; Ref<Character> self(this);
				if (action) action->onHit = [g, self, act]() { if (Character* c = self.get()) if (g->combat) g->combat->meleeHit(c, act); };
			}
		}
		if (tv && d < 4 && tv->speedAbs() < 2 && brave > 0.6 && !game.vehicles.isBusy(this) && t && t->isPlayer) meleeTimer -= dt;
	}
}

void Ped::followLeader(double dt) {
	Character* L = follow.get();
	if (!L || L->removed) { setState("wander"); return; }
	if (L->dead) { setState("flee"); return; }
	if (L->vehicle && !vehicle && !game.vehicles.isBusy(this)) {
		Vehicle* v = L->vehicle;
		const int nSeats = v->layout.seats.empty() ? 2 : (int)v->layout.seats.size();
		int freeSeat = 0;
		for (int s = 1; s <= 3; s++) if (s < nSeats && !v->occupants[s]) { freeSeat = s; break; }
		if (freeSeat && Dist2(pos.x, pos.z, v->pos.x, v->pos.z) < 30 * 30) { game.vehicles.enter(this, v, freeSeat, true); return; }
	}
	if (!L->vehicle && vehicle) { game.vehicles.exit(this); return; }
	Character* t = threat.get();
	if (((t && !t->dead) || (threatAnimal && !threatAnimal->dead)) && weapons.count(weapon) && weaponDef().type == "gun") { attack(dt); setState("follow"); return; }
	const double d = Hypot(L->pos.x - pos.x, L->pos.z - pos.z);
	const double side = followSlot - 1;
	const double tx = L->pos.x - std::sin(L->yaw) * 1.6 + std::cos(L->yaw) * side * 1.2, tz = L->pos.z - std::cos(L->yaw) * 1.6 - std::sin(L->yaw) * side * 1.2;
	const double speed = d > 8 ? 6.5 : d > 3 ? 4.3 : 1.5;
	goTo(tx, tz, speed, dt, 0.8);
	if (d > 150) setPosition(L->pos.x - std::sin(L->yaw) * 2, L->pos.z - std::cos(L->yaw) * 2);
}

void Ped::guard(double dt) {
	stop();
	animState.talking = std::sin(uid * 7 + game.time * 0.2) > 0.3;
	if (hasGuardFace) yaw = DampAngle(yaw, guardFace, 3, dt);
}

void Ped::update(double dt) {
	think(dt);
	Character::update(dt);
}

// ==================================================================== PedManager
PedManager::PedManager(Game& g) : game(g), maxPeds(g.quality.peds) {
	gangAggro = { { "vipers", true }, { "cuervos", false }, { "kings", false }, { "army", false }, { "crew", false }, { "saints", false } };
	game.events.gunshot.on([this](Character* shooter, V3 p, const std::string&) { onNoise(shooter, p, 45, true); });
	game.events.explosion.on([this](V3 p, double, Character*) { onNoise(nullptr, p, 60, true); });
	game.events.death.on([this](Character* c, const DamageInfo& info) { onDeath(c, info); });
	game.events.pedHitByCar.on([this](Character* c, Vehicle* v, double) { if (v->driver() && v->driver()->isPlayer) onNoise(v->driver(), c->pos, 25, false); });
	game.events.melee.on([this](Character* att, Character* vic, const std::string&) { if (att->isPlayer) onNoise(att, vic->pos, 18, false); });
	game.characterSources.push_back([this](std::vector<Character*>& out) { for (auto& p : list) out.push_back(p.get()); });
	game.characterRemover = [this](Character* c) { remove(c); };
}

std::shared_ptr<Ped> PedManager::shared(Ped* p) const { for (const auto& q : list) if (q.get() == p) return q; return nullptr; }

Ped* PedManager::spawnPed(double x, double z, const PedOpts& opts) {
	RNG rng((uint32_t)(int64_t)std::floor(Rand() * 1e9));
	Appearance app;
	if (opts.hasAppearance) app = opts.appearance;
	else if (opts.gang == "saints") app = SaintLook(rng);
	else if (!opts.gang.empty()) {
		const GangDef& g = Gangs().at(opts.gang);
		const bool female = rng.Chance(0.15);
		app = RandomAppearance(rng, female ? 1 : 0);
		app.shirt = g.color;
		app.shirtType = rng.Pick(std::vector<std::string>{ "tee", "tank", "jacket", "long" });
		app.jacketColor = 0x1a1a1a;
		app.bandana = rng.Chance(0.5) ? (int64_t)g.color : -1;
		app.hairStyle = rng.Pick(std::vector<std::string>{ "cap", "buzz", "short", "bald" });
		app.hat = g.color;
	} else app = districtLook(rng, game.map.DistrictAt(x, z));
	auto p = game.makeCharacter<Ped>(app, opts);
	if (opts.hasY) p->setPosition(x, opts.y, z); else p->setPosition(x, z);
	p->setYaw(opts.hasYaw ? opts.yaw : Rand() * 6.28);
	if (!opts.weapon.empty()) { p->giveWeapon(opts.weapon, 999); p->equip(opts.weapon); }
	list.push_back(p);
	return p.get();
}

Appearance PedManager::districtLook(RNG& rng, const std::string& district) {
	// (randomAppearance with overrides: the female choice is made first, the rest overrides afterwards)
	if (district == "docks") {
		const bool female = rng.Chance(0.1);
		// (the JS passes these as opts: shirtType, jacketColor, hairStyle and hat are drawn before randomAppearance)
		const std::string st = rng.Pick(std::vector<std::string>{ "long", "jacket" });
		const uint32_t jc = rng.Pick(std::vector<uint32_t>{ 0xff8800, 0x2a4d69 });
		const uint32_t hat = rng.Pick(std::vector<uint32_t>{ 0xffcc00, 0x333333 });
		Appearance a = RandomAppearance(rng, female ? 1 : 0);
		a.shirtType = st; a.jacketColor = jc; a.hairStyle = "cap"; a.hat = hat;
		return a;
	}
	if (district == "beach") {
		const bool shorts = rng.Chance(0.7);
		const std::string st = rng.Pick(std::vector<std::string>{ "tank", "tee", "tee" });
		const bool glasses = rng.Chance(0.4);
		Appearance a = RandomAppearance(rng);
		a.shorts = shorts; a.shirtType = st; a.glasses = glasses;
		return a;
	}
	if (district == "downtown" || district == "aurcentro") {
		if (rng.Chance(0.5)) {
			const uint32_t jc = rng.Pick(std::vector<uint32_t>{ 0x222222, 0x2b2d42, 0x3d405b, 0x555555 });
			Appearance a = RandomAppearance(rng);
			a.shirtType = "jacket"; a.jacketColor = jc; a.shirt = 0xf2f2f2; a.pants = jc; a.shoes = 0x111111;
			return a;
		}
		return RandomAppearance(rng);
	}
	if (district == "hills") {
		const std::string st = rng.Pick(std::vector<std::string>{ "long", "jacket", "tee" });
		const bool glasses = rng.Chance(0.5);
		const uint32_t shirt = rng.Pick(std::vector<uint32_t>{ 0xffffff, 0xe9d8a6, 0xa8dadc, 0xffc8dd });
		Appearance a = RandomAppearance(rng);
		a.shirtType = st; a.glasses = glasses; a.shirt = shirt;
		return a;
	}
	return RandomAppearance(rng);
}

void PedManager::populate(int n) { ignoreView = true; for (int i = 0; i < n; i++) spawnAmbient(); ignoreView = false; }

void PedManager::spawnAmbient() {
	const CityMap& map = game.map;
	Player& pl = *game.player;
	const V3 p = pl.vehicle ? pl.vehicle->pos : pl.pos;
	const auto& nodes = map.walkNodes;
	for (int tries = 0; tries < 12; tries++) {
		const double ang = Rand() * kTau;
		const double r = ignoreView ? Rand(12, 90) : Rand(55, 95);
		const double x = p.x + std::cos(ang) * r, z = p.z + std::sin(ang) * r;
		WalkArea b;
		if (!WalkAreaAt(map, x, z, b) || b.nodeIds->empty()) continue;
		const bool view = inView(x, z, 1);
		if (view && r < 80 && !ignoreView) continue;
		const WalkNode& n = nodes[RandPick(*b.nodeIds)];
		if (n.links.empty()) continue;
		const WalkNode& n2 = nodes[RandPick(n.links)];
		const double t = Rand();
		const double sx = n.x + (n2.x - n.x) * t + Rand(-1, 1), sz = n.z + (n2.z - n.z) * t + Rand(-1, 1);
		if (map.IsOnRoad(sx, sz)) continue;
		const std::string& district = b.district;
		for (const std::string& gid : GangOrder()) {
			const GangDef& g = Gangs().at(gid);
			if (g.aggroOnly) continue;
			auto dens = game.gangDensity.find(gid);
			if (g.district == district && Rand() < (dens != game.gangDensity.end() ? dens->second : 0.3)) {
				const int count = RandInt(2, 4);
				for (int k = 0; k < count; k++) {
					PedOpts o; o.brain = "gang"; o.gang = gid; o.state = "guard"; o.weapon = RandPick(g.weapons); o.health = 110;
					Ped* ped = spawnPed(sx + std::cos(k * 2.1) * 1.4, sz + std::sin(k * 2.1) * 1.4, o);
					ped->hasGuardFace = true; ped->guardFace = std::atan2(sx - ped->pos.x, sz - ped->pos.z);
					ped->accuracy = 0.45;
				}
				return;
			}
		}
		Ped* ped = spawnPed(sx, sz);
		ped->node = n2.id; ped->prevNode = n.id;
		if (Rand() < 0.07 && district != "docks" && game.wildlife) game.wildlife->addWalkedDog(ped);
		if (Rand() < 0.15 && (int)list.size() < maxPeds - 1) {
			Ped* other = spawnPed(sx + 1.2, sz + 0.3);
			ped->setState("idle"); other->setState("idle");
			ped->idleTime = other->idleTime = Rand(6, 16);
			ped->talkPartner = Ref<Ped>(shared(other)); other->talkPartner = Ref<Ped>(shared(ped));
		}
		return;
	}
}

bool PedManager::inView(double x, double z, double) const { return game.rig.inView(V3(x, 1, z)); }

double PedManager::density() const {
	const double h = game.env.hours;
	const double night = h < 6 || h > 22 ? 0.45 : h < 8 || h > 20 ? 0.75 : 1;
	const double rain = 1 - game.env.rain * 0.5;
	static const std::map<std::string, double> DM = { { "downtown", 1.2 }, { "beach", 1.15 }, { "midtown", 1.1 }, { "hood", 0.9 }, { "corona", 0.9 }, { "westside", 0.9 },
		{ "docks", 0.55 }, { "hills", 0.4 }, { "aurcentro", 1.25 }, { "aurharbor", 1.1 }, { "aurmission", 1.1 }, { "aurcathedral", 1.05 }, { "aurnorth", 1.0 }, { "aurheights", 0.85 }, { "aurbay", 0.9 } };
	auto it = DM.find(game.map.DistrictAt(game.player->pos.x, game.player->pos.z));
	return Clamp(night * rain * (it == DM.end() ? 1 : it->second), 0.2, 1.3);
}

void PedManager::onNoise(Character* src, const V3& p, double radius, bool gunfire) {
	for (const auto& sp : std::vector<std::shared_ptr<Ped>>(list)) {
		Ped* q = sp.get();
		if (q->dead || q->ragdolling || q->vehicle || q == src) continue;
		if (q->brain == "cop" || q->brain == "script" || q->state == "follow") continue;
		const double d2 = Dist2(q->pos.x, q->pos.z, p.x, p.z);
		if (d2 > radius * radius) continue;
		if (!q->gang.empty() && src && src->isPlayer) {
			const GangDef& g = Gangs().at(q->gang);
			if (!g.friendly || d2 < 15 * 15) { q->threat = src; q->setState(g.friendly && gunfire ? "flee" : "attack"); if (!g.friendly) gangAggro[q->gang] = true; continue; }
		}
		if (q->state == "attack") continue;
		q->threat = src; q->threatPos = p;
		if (gunfire && d2 < 12 * 12 && Rand() < 0.35) { q->setState("cower"); q->cowerTime = Rand(2, 5); }
		else { q->setState("flee"); if (Rand() < 0.3 && game.time - q->lastSay > 4) q->say(RandPick(PedLines("flee"))); }
	}
}

void PedManager::onPedDamaged(Ped* ped, Character* src, double, const DamageInfo& info) {
	if (info.animalSource && !ped->dead) {
		Animal* a = info.animalSource;
		ped->threat = nullptr; ped->threatAnimal = a;
		if (ped->brain == "cop" || ped->brain == "script" || ped->state == "follow") return;
		if (!ped->gang.empty()) {
			ped->setState("attack");
			for (const auto& q : list) if (q->gang == ped->gang && !q->dead && Dist2(q->pos.x, q->pos.z, ped->pos.x, ped->pos.z) < 30 * 30) { q->threat = nullptr; q->threatAnimal = a; q->setState("attack"); }
		} else if (ped->brave > 0.78 && info.type == "melee") { ped->setState("attack"); if (game.time - ped->lastSay > 3) ped->say(RandPick(PedLines("fight"))); }
		else { ped->threatPos = a->pos; ped->setState("flee"); if (Rand() < 0.5) ped->say(RandPick(PedLines("flee"))); }
		return;
	}
	if (ped->dead || !src || src == ped) return;
	ped->threatAnimal = nullptr;
	if (ped->brain == "cop" || ped->brain == "script") { ped->threat = src; return; }
	if (ped->state == "follow") { ped->threat = src; return; }
	if (!ped->gang.empty()) {
		Ped* sp = dynamic_cast<Ped*>(src);
		if (sp && sp->gang == ped->gang) return;
		ped->threat = src; ped->setState("attack");
		for (const auto& q : list) if (q.get() != src && q->gang == ped->gang && !q->dead && Dist2(q->pos.x, q->pos.z, ped->pos.x, ped->pos.z) < 30 * 30) { q->threat = src; q->setState("attack"); }
		if (src->isPlayer) gangAggro[ped->gang] = true;
		return;
	}
	if (ped->brave > 0.78 && info.type == "melee") { ped->threat = src; ped->setState("attack"); if (game.time - ped->lastSay > 3) ped->say(RandPick(PedLines("fight"))); }
	else { ped->threat = src; ped->threatPos = src->pos; ped->setState("flee"); if (Rand() < 0.5) ped->say(RandPick(PedLines("flee"))); }
}

void PedManager::onDeath(Character* c, const DamageInfo& info) {
	if (c->isPlayer) return;
	Ped* p = dynamic_cast<Ped*>(c);
	if (p) p->deathTime = game.time;
	onNoise(info.source, c->pos, 25, false);
	if (!game.pickups || !p) return;
	if (p->moneyDrop > 0) game.pickups->dropMoney(c->pos, (int)std::round(p->moneyDrop));
	else if (Rand() < (!p->gang.empty() ? 0.7 : 0.35)) game.pickups->dropMoney(c->pos, !p->gang.empty() ? RandInt(20, 120) : RandInt(5, 60));
	if (!p->gang.empty() && c->weapon != "fist" && Rand() < 0.5) game.pickups->dropWeapon(c->pos, c->weapon, c->weapon == "bat" || c->weapon == "knife" ? 1 : RandInt(8, 30));
	if (p->brain == "cop" && Rand() < 0.8) game.pickups->dropWeapon(c->pos, c->weapon == "fist" ? "pistol" : c->weapon, RandInt(10, 30));
}

void PedManager::remove(Character* c) {
	if (!c) return;
	for (size_t i = 0; i < list.size(); i++) if (list[i].get() == c) { game.graveyard(std::shared_ptr<Character>(list[i])); list.erase(list.begin() + i); break; }
	if (Vehicle* v = c->vehicle) { const int s = v->seatOf(c); if (s >= 0) { game.graveyard(v->occupants[s]); v->occupants[s].reset(); } c->vehicle = nullptr; }
	c->remove();
}

void PedManager::update(double dt) {
	Player& pl = *game.player;
	const V3 pp = pl.vehicle ? pl.vehicle->pos : pl.pos;
	spawnTimer -= dt;
	int ambientCount = 0;
	std::vector<Ped*> bodies;
	for (const auto& p : list) {
		if (p->persistent || p->brain == "cop" || p->vehicle) continue;
		if (p->dead) bodies.push_back(p.get()); else ambientCount++;
	}
	const int target = (int)std::floor(maxPeds * density());
	if (spawnTimer <= 0 && ambientCount < target && !game.disableAmbient) {
		spawnTimer = ambientCount < target * 0.6 ? 0.1 : 0.25;
		spawnAmbient();
	}
	if (bodies.size() > 10) {
		std::stable_sort(bodies.begin(), bodies.end(), [](Ped* a, Ped* b) { return a->deathTime < b->deathTime; });
		for (size_t k = 0; k < bodies.size() - 10; k++) {
			Ped* b = bodies[k];
			const double bx = b->ragdolling ? b->ragdoll->pos[0] : b->pos.x, bz = b->ragdolling ? b->ragdoll->pos[2] : b->pos.z;
			if (game.time - b->deathTime > 12 && Dist2(bx, bz, pp.x, pp.z) > 30 * 30 && !inView(bx, bz)) remove(b);
		}
	}
	for (int i = (int)list.size() - 1; i >= 0; i--) {
		if (i >= (int)list.size()) continue;
		std::shared_ptr<Ped> sp = list[i];
		Ped* p = sp.get();
		if (p->removed) { list.erase(list.begin() + i); continue; }
		if (!Finite(p->pos.x) || !Finite(p->pos.z) || (p->ragdolling && !Finite(p->ragdoll->pos[0]))) {
			if (!p->persistent || !p->hasGoodPos) { remove(p); continue; }
			p->recoverFrom();
		}
		const double cx = p->ragdolling ? p->ragdoll->pos[0] : p->pos.x, cz = p->ragdolling ? p->ragdoll->pos[2] : p->pos.z;
		const double d2 = Dist2(cx, cz, pp.x, pp.z);
		if (!p->persistent && !p->vehicle) {
			if (d2 > 150 * 150 || (p->dead && game.time - p->deathTime > 60 && d2 > 40 * 40)) { remove(p); continue; }
		}
		if (p->vehicle) { p->visible = !p->hiddenInVehicle; p->update(dt); continue; }
		const bool vis = game.rig.inView(V3(cx, (p->ragdolling ? p->ragdoll->pos[1] : p->pos.y) + 0.9, cz), 1.4);
		p->visible = vis;
		p->animLod = (p->animLod + 1) % (d2 > 70 * 70 || !vis ? 3 : 1);
		if (p->animLod == 0) p->update(dt * (d2 > 70 * 70 || !vis ? 3 : 1));
		else p->think(dt);
	}
	// separation
	for (size_t i = 0; i < list.size(); i++) {
		Ped* a = list[i].get();
		if (a->vehicle || a->ragdolling) continue;
		for (size_t j = i + 1; j < list.size(); j++) {
			Ped* b = list[j].get();
			if (b->vehicle || b->ragdolling) continue;
			const double dx = b->pos.x - a->pos.x, dz = b->pos.z - a->pos.z;
			const double d2 = dx * dx + dz * dz;
			if (d2 < 0.36 && d2 > 1e-6) { const double d = std::sqrt(d2), push = (0.6 - d) * 0.5; a->pos.x -= dx / d * push; a->pos.z -= dz / d * push; b->pos.x += dx / d * push; b->pos.z += dz / d * push; }
		}
		if (!pl.vehicle && !pl.ragdolling) {
			const double dx = pl.pos.x - a->pos.x, dz = pl.pos.z - a->pos.z;
			const double d2 = dx * dx + dz * dz;
			if (d2 < 0.4 && d2 > 1e-6) {
				const double d = std::sqrt(d2), push = 0.63 - d;
				a->pos.x -= dx / d * push * 0.7; a->pos.z -= dz / d * push * 0.7; pl.pos.x += dx / d * push * 0.3; pl.pos.z += dz / d * push * 0.3;
				if (Hypot(pl.vel.x, pl.vel.z) > 5 && a->state == "wander" && Rand() < 0.05 && !a->dead) { a->knockDown(V3(pl.vel.x * 0.4, 1, pl.vel.z * 0.4)); a->say(RandPick(PedLines("bump"))); }
			}
		}
	}
}

} // namespace atg
