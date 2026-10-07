#include "Military.h"
#include "Game.h"
#include "Gameplay.h"
#include "Peds.h"
#include "Police.h"
#include "Skeleton.h"
#include "Vehicles.h"
#include "WorldGen.h"

namespace atg {

namespace {
// x, z, facing yaw, and the far end of a patrol
struct Post { double x, z, face; bool patrol; double px, pz; };
const Post POSTS[] = {
	{ -3834, -4048, kPi / 2 }, { -3834, -4074, kPi / 2 },                                               // main gate
	{ -4880, -4200, kPi }, { -4700, -4195, kPi, true, -4450, -4195 }, { -4560, -4205, kPi },              // flight line
	{ -4680, -4055, kPi }, { -4600, -3995, 0 },                                                         // helipads
	{ -4095, -4205, kPi }, { -4160, -4100, -kPi / 2, true, -4160, -4200 },                                // tank yard
	{ -4290, -4196, kPi }, { -4200, -3984, kPi },                                                       // tower, HQ
	{ -5010, -3984, kPi, true, -4800, -3984 }, { -5150, -4470, kPi / 2 }, { -4005, -4470, -kPi / 2 },     // barracks, runway ends
	{ -5250, -4700, 0, true, -4400, -4700 },                                                            // south fence patrol
};
const int NPOSTS = (int)(sizeof(POSTS) / sizeof(POSTS[0]));
const double SPAWN_R = 1100, DESPAWN_R = 1500;
}

Appearance SoldierLook(RNG& rng) {
	const bool female = rng.Chance(0.15);
	Appearance a = RandomAppearance(rng, female ? 1 : 0);
	a.shirt = 0x5b6b3a; a.shirtType = "long"; a.pants = 0x4d5a33; a.jacketColor = 0x5b6b3a;
	a.hairStyle = "cap"; a.hat = 0x46542c; a.shoes = 0x2a2620;
	a.glasses = false; a.bandana = -1; a.beard = false; a.shorts = false;
	return a;
}

Military::Military(Game& g) : game(g) {
	for (const FixedVehicle& e : g.map.fixedVehicles) { Fixed f; f.type = e.type; f.x = e.x; f.z = e.z; f.yaw = e.yaw; f.y = e.y; f.respawn = e.respawn; fixed.push_back(f); }
	soldiers.resize(NPOSTS);
	// map icons for the hardware (the radar shows them only when close)
	const std::map<std::string, std::string> icon = { { "skipper", "plane" }, { "hercules", "plane" }, { "raptor", "jet" }, { "warhawk", "heli" }, { "skylark", "heli" }, { "mammoth", "tank" } };
	std::set<std::string> seen;
	for (const Fixed& e : fixed) {
		const auto it = icon.find(e.type);
		if (it == icon.end()) continue;
		char key[96]; snprintf(key, sizeof key, "%s%.0f,%.0f", it->second.c_str(), std::round(e.x / 150) + 0.0, std::round(e.z / 150) + 0.0);
		if (seen.count(key)) continue;
		seen.insert(key);
		Blip b; b.x = e.x; b.z = e.z; b.icon = it->second; b.color = 0x9fe3ff; b.small = true; b.noEdge = true;
		g.addBlip(b);
	}
	// stealing military hardware or firing inside the fence puts the base on alert at once
	g.events.enteredVehicle.on([this](Character* c, Vehicle* v) { if (c && c->isPlayer && v->def.military && inBase(v->pos)) alert("Military hardware stolen!"); });
	g.events.gunshot.on([this](Character* s, V3 pos, const std::string&) { if (s && s->isPlayer && inBase(pos)) alert(); });
	g.events.explosion.on([this](V3 pos, double, Character* src) { if (src && src->isPlayer && inBase(pos)) alert(); });
	g.events.kill.on([this](Character* killer, Character* victim, const std::string&, const std::string&) {
		const Ped* v = dynamic_cast<const Ped*>(victim);
		if (killer && killer->isPlayer && v && v->gang == "army" && (!v->response || inBase(v->pos))) alert();
	});
}

bool Military::inBase(const V3& p, double margin) const {
	return p.x > BASE.minX - margin && p.x < BASE.maxX + margin && p.z > BASE.minZ - margin && p.z < BASE.maxZ + margin;
}

void Military::alert(const std::string& msg) {
	if (!alerted) {
		alerted = true;
		if (game.hud) game.hud->bigMessage("ARMY ALERTED", "failed", 2.5, msg.empty() ? "Fort Carver is under lockdown" : msg);
	}
	game.peds->gangAggro["army"] = true;
	if (!game.player->dead && game.policeSys) game.policeSys->raise(3);
}

void Military::update(double dt) {
	Player& p = *game.player;
	if (game.gameplay && game.gameplay->state == "menu") return;
	const V3 pos = p.vehicle ? p.vehicle->pos : p.pos;
	// the restricted zone
	const bool high = pos.y - game.map.GroundHeight(pos.x, pos.z) > 220;
	const bool in = inBase(pos) && !high && !p.dead;
	if (in && !inside) {
		grace = alerted ? 0 : 8;
		if (!alerted && game.hud) {
			game.hud->bigMessage("RESTRICTED AREA", "failed", 3, "Fort Carver \xc2\xb7 military personnel only");
			game.hud->help("You are trespassing on a military base. Leave now or the army will open fire.", 6);
		}
	}
	inside = in;
	if (in) {
		grace -= dt;
		if (grace <= 0) alert();
		if (game.peds->gangAggro["army"] && !alerted) alert();
		// while inside and on alert, the heat doesn't cool off
		raiseT -= dt;
		if (alerted && raiseT <= 0) { raiseT = 2; if (game.policeSys) game.policeSys->raise(3); }
	} else if (alerted && (game.policeSys ? game.policeSys->level : 0) == 0) {
		// the lockdown is lifted once the wanted level is gone
		alerted = false;
		game.peds->gangAggro["army"] = false;
		for (const auto& r : soldiers) if (Ped* s = r.get()) if (!s->dead && s->state == "attack") { s->threat = nullptr; s->setState("guard"); }
	}
	if (p.dead) inside = false;

	t -= dt;
	if (t > 0) return;
	t = 0.5;
	streamVehicles(pos);
	garrison(pos);
}

// ------------------------------------------------------------------ parked aircraft and armour
void Military::streamVehicles(const V3& pos) {
	for (Fixed& e : fixed) {
		const double d2 = Dist2(e.x, e.z, pos.x, pos.z);
		if (e.hasVeh) {
			Vehicle* v = e.veh.get();
			const bool removed = !v || v->removed;
			const bool moved = !removed && Dist2(v->pos.x, v->pos.z, e.x, e.z) > 25 * 25;
			if (removed || v->isWrecked() || moved || v->driver()) {
				// taken or destroyed: leave it to the world and respawn a fresh one later
				e.timer = e.respawn ? e.respawn : 180;
				e.old = removed ? Ref<Vehicle>() : e.veh;
				if (!removed) v->persistent = v->driver() && v->driver()->isPlayer;
				e.veh = nullptr; e.hasVeh = false;
				continue;
			}
			if (d2 > DESPAWN_R * DESPAWN_R) { game.vehicles.remove(v); e.veh = nullptr; e.hasVeh = false; e.timer = 0; }
			continue;
		}
		e.timer -= 0.5;
		if (e.timer > 0 || d2 > SPAWN_R * SPAWN_R) continue;
		if (Vehicle* o = e.old.get()) {
			// tidy up the last one if it was abandoned somewhere far from the player
			if (!o->removed && !o->driver() && Dist2(o->pos.x, o->pos.z, pos.x, pos.z) > 400 * 400) { game.vehicles.remove(o); e.old = nullptr; }
		}
		// don't pop in right in front of the player, and don't spawn into something parked on the spot
		if (e.respawned && d2 < 160 * 160) continue;
		bool blocked = false;
		for (const auto& o : game.vehicles.list) if (!o->removed && Dist2(o->pos.x, o->pos.z, e.x, e.z) < 14 * 14 && std::fabs(o->pos.y - e.y) < 5) { blocked = true; break; }
		if (blocked) continue;
		SpawnOpts so; so.hasY = true; so.y = e.y; so.persistent = true; so.parked = true;
		Vehicle* v = game.vehicles.spawn(e.type, e.x, e.z, e.yaw, so);
		e.veh = v; e.hasVeh = v != nullptr;
		e.respawned = true;
	}
}

// ------------------------------------------------------------------ soldiers
void Military::garrison(const V3& pos) {
	const double cx = (BASE.minX + BASE.maxX) / 2, cz = (BASE.minZ + BASE.maxZ) / 2;
	const double d = Hypot(pos.x - cx, pos.z - cz);
	if (!garrisoned && d < 900) {
		garrisoned = true;
		RNG rng(4242);
		soldiers.resize(NPOSTS);
		for (int i = 0; i < NPOSTS; i++) {
			if (Ped* s = soldiers[i].get()) if (!s->removed) continue;
			soldiers[i] = Ref<Ped>(spawnSoldier(i, rng));
		}
	} else if (garrisoned && d > 1300) {
		garrisoned = false;
		for (const auto& r : soldiers) if (Ped* s = r.get()) if (!s->removed) game.peds->remove(s);
		soldiers.assign(NPOSTS, Ref<Ped>());
	}
	if (!garrisoned) return;
	// patrols walk back and forth between their two points
	for (const auto& r : soldiers) {
		Ped* s = r.get();
		if (!s || s->dead || s->removed || !s->hasPatrol) continue;
		if (s->state == "guard" && s->stateTime > s->patrolWait) {
			s->patrolLeg = 1 - s->patrolLeg;
			s->targetPos.set(s->patrol[s->patrolLeg][0], 0, s->patrol[s->patrolLeg][1]);
			s->gotoSpeed = 1.5; s->afterGoto = "guard";
			s->setState("goto");
			s->patrolWait = Rand(4, 10);
		}
	}
}

Ped* Military::spawnSoldier(int i, RNG& rng) {
	const Post& post = POSTS[i];
	PedOpts o;
	o.hasAppearance = true; o.appearance = SoldierLook(rng);
	o.brain = "gang"; o.gang = "army"; o.state = "guard"; o.weapon = "rifle"; o.health = 140; o.persistent = true;
	o.hasYaw = true; o.yaw = post.face;
	Ped* s = game.peds->spawnPed(post.x, post.z, o);
	if (!s) return nullptr;
	s->hasGuardFace = true; s->guardFace = post.face;
	s->accuracy = 0.62;
	s->damageMul = 0.6;
	s->soldier = true;
	if (post.patrol) {
		s->hasPatrol = true;
		s->patrol[0][0] = post.x; s->patrol[0][1] = post.z; s->patrol[1][0] = post.px; s->patrol[1][1] = post.pz;
		s->patrolLeg = 0; s->patrolWait = Rand(2, 8);
	}
	return s;
}

} // namespace atg
