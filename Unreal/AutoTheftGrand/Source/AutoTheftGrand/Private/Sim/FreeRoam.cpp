#include "FreeRoam.h"
#include "Net.h"
#include "Aircraft.h"
#include "Combat.h"
#include "Collision.h"
#include "Game.h"
#include "Gameplay.h"
#include "Peds.h"
#include "Phone.h"
#include "Police.h"
#include "Traffic.h"
#include "Weapons.h"
#include "AnimalModels.h"
#include "NpcCrime.h"
#include "Pets.h"
#include "VehicleDefs.h"
#include "Wildlife.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace atg {

FreeRoam::FreeRoam(Game& g) : game(g) {}

bool FreeRoam::active() const { return game.freeroamActive(); }

bool FreeRoam::toggleCheat(const std::string& key) {
	if (!active()) return false;
	bool* flag = nullptr;
	if (key == "God mode") flag = &game.cheats.god;
	else if (key == "Bulletproof vehicle") flag = &game.cheats.vehGod;
	else if (key == "Never wanted") flag = &game.cheats.neverWanted;
	else if (key == "Super jump") flag = &game.cheats.superJump;
	else if (key == "Super run") flag = &game.cheats.superRun;
	else if (key == "Infinite sprint") flag = &game.cheats.infSprint;
	else if (key == "Low gravity") flag = &game.cheats.lowGravity;
	else if (key == "Explosive bullets") flag = &game.cheats.explosive;
	else if (key == "One-hit kills") flag = &game.cheats.oneHit;
	else if (key == "Freeze time") flag = &game.cheats.freezeTime;
	else if (key == "Slow motion") flag = &game.cheats.slowmo;
	else if (key == "Riot") flag = &game.cheats.riot;
	if (!flag) return false;
	*flag = !*flag;
	return true;
}

bool FreeRoam::setCheat(const std::string& key, bool enabled) {
	if (!active()) return false;
	bool* flag = nullptr;
	if (key == "God mode") flag = &game.cheats.god;
	else if (key == "Bulletproof vehicle") flag = &game.cheats.vehGod;
	else if (key == "Never wanted") flag = &game.cheats.neverWanted;
	else if (key == "Super jump") flag = &game.cheats.superJump;
	else if (key == "Super run") flag = &game.cheats.superRun;
	else if (key == "Infinite sprint") flag = &game.cheats.infSprint;
	else if (key == "Low gravity") flag = &game.cheats.lowGravity;
	else if (key == "Explosive bullets") flag = &game.cheats.explosive;
	else if (key == "One-hit kills") flag = &game.cheats.oneHit;
	else if (key == "Freeze time") flag = &game.cheats.freezeTime;
	else if (key == "Slow motion") flag = &game.cheats.slowmo;
	else if (key == "Riot") flag = &game.cheats.riot;
	if (!flag) return false;
	*flag = enabled;
	return true;
}

void FreeRoam::setWanted(int level) {
	if (!active() || !game.policeSys) return;
	level = (int)Clamp(level, 0, 5);
	if (level > 0) game.cheats.neverWanted = false;
	if (level == 0) game.policeSys->clear();
	else game.policeSys->setLevel(level);
}

bool FreeRoam::setTimeOfDay(double hours) {
	if (!active() || !Finite(hours)) return false;
	game.env.setTime(hours);
	return true;
}

bool FreeRoam::setWeather(const std::string& weather) {
	static const std::set<std::string> allowed = { "clear", "cloudy", "rain", "storm", "fog" };
	if (!active() || !allowed.count(weather)) return false;
	game.env.setWeather(weather, true);
	game.env.weatherLocked = true;
	game.env.weatherTimer = 1e9;
	return true;
}

void FreeRoam::earlyInput(double dt) {
	applyAdminPowers(dt);
}

void FreeRoam::applyAdminPowers(double dt) {
	Player& p = *game.player;
	// (admin.js allowed: free roam, and not online)
	if (!active() || (game.net && game.net->online())) {
		if (!adminApplied) return;
		p.invincible = false;
		p.infiniteSprint = false;
		p.sprinting = false;
		game.gravity = 1;
		if (adminSlow) game.timeScale = normalTimeScale;
		if (adminFreeze) game.env.timeScale = normalEnvTimeScale;
		if (game.traffic && baseCars >= 0) game.traffic->maxCars = baseCars;
		if (game.peds && basePeds >= 0) game.peds->maxPeds = basePeds;
		// (admin.js _reset: the switches stay as they were for when free roam is back; the phone's cheats apply)
		game.cheatsOn = Cheats();
		adminApplied = adminSlow = adminFreeze = false;
		game.adminPowersActive = false;
		return;
	}
	const Cheats& c = game.cheats;
	game.cheatsOn = c;
	game.adminPowersActive = true;
	adminApplied = true;
	p.invincible = c.god;
	if (c.god && p.health < p.maxHealth && !p.dead) p.health = p.maxHealth;
	if (c.neverWanted && game.policeSys && (game.policeSys->level > 0 || game.policeSys->heat > 0)) game.policeSys->clear();
	p.infiniteSprint = c.infSprint;
	if (c.infSprint) p.stamina = 1;
	game.gravity = c.lowGravity ? 0.3 : 1;
	if (c.slowmo != adminSlow) {
		adminSlow = c.slowmo;
		if (adminSlow) { normalTimeScale = game.timeScale; game.timeScale *= 0.45; }
		else game.timeScale = normalTimeScale;
	}
	if (c.freezeTime != adminFreeze) {
		adminFreeze = c.freezeTime;
		if (adminFreeze) { normalEnvTimeScale = game.env.timeScale; game.env.timeScale = 0; }
		else game.env.timeScale = normalEnvTimeScale;
	}
	if (c.vehGod && p.vehicle && !p.vehicle->exploded) {
		p.vehicle->health = p.vehicle->maxHealth;
		p.vehicle->onFire = false;
		p.vehicle->burnTime = 0;
	}
	density();
	if (!c.riot) riotTimer = 0;
	if (c.riot && game.peds) {
		riotTimer -= dt;
		if (riotTimer <= 0) {
			riotTimer = 1.5;
			std::vector<Ped*> near;
			for (const auto& q : game.peds->list) if (!q->dead && !q->vehicle && q->brain == "civilian" && Hypot(q->pos.x - p.pos.x, q->pos.z - p.pos.z) < 70) near.push_back(q.get());
			for (Ped* q : near) {
				if (q->state == "attack" && q->threat && !q->threat->dead && !q->threat->removed) continue;
				if (Rand() < 0.5) continue;
				std::vector<Ped*> others;
				for (Ped* o : near) if (o != q) others.push_back(o);
				Character* threat = !others.empty() && Rand() >= 0.15 ? others[(size_t)(Rand() * others.size())] : p.shared_from_this().get();
				if (Rand() < 0.35 && !q->weapons.count("pistol")) { q->giveWeapon(Rand() < 0.5 ? "pistol" : "bat", 60); q->equip(q->weapons.count("pistol") ? "pistol" : "bat"); }
				q->threat = Ref<Character>(threat);
				q->threatAnimal = nullptr;
				q->setState("attack");
			}
		}
	}
}

void FreeRoam::update(double) {
	// hud.js: T opens the Teleport tab in free roam
	if (game.input.hit("teleport") && !game.menuOpen && game.gameplay && game.gameplay->state == "playing" && !game.cutscene && active()) openTeleport();
	if (!active()) return;
	Player& p = *game.player;
	p.money = FREE_CASH;
	// magazines stay full (Player.fire / throwGrenade skip the decrement too); keep a full reserve for show
	for (auto& [id, w] : p.weapons) {
		const WeaponDef* def = FindWeapon(id);
		if (!def || def->type == "melee") continue;
		if (def->type == "thrown") { w.clip = Max(w.clip, 1); w.ammo = Max(w.ammo, 9); continue; }
		w.clip = def->clip; w.ammo = Max(w.ammo, def->clip * 4.0);
	}
	if (p.reloading > 0) p.reloading = 0;
}

// ------------------------------------------------------------------ admin.js: the rest of the admin tools
const std::vector<SpawnGroup>& SpawnGroups() {
	static const std::vector<SpawnGroup> G = {
		{ "Cars", { "meridian", "pico", "buffalo", "kestrel", "brawler", "zenith", "tempest", "bouncer", "summit", "baller", "taxi" } },
		{ "Bikes & boards", { "razor", "trail", "bmx", "roadbike", "skateboard" } },
		{ "Boats", { "dinghy", "jetski", "speedboat", "cruiser", "policeboat" } },
		{ "Work & emergency", { "hauler", "parcel", "boxer", "stockade", "police", "sheriff", "enforcer" } },
		{ "Military", { "ranger", "barracks", "mammoth" } },
		{ "Aircraft", { "skipper", "skylark", "hercules", "warhawk", "raptor" } },
	};
	return G;
}

const std::vector<AdminToggle>& AdminToggles() {
	static const std::vector<AdminToggle> T = {
		{ "god", "God mode", "You take no damage", &Cheats::god },
		{ "vehGod", "Bulletproof vehicle", "Whatever you drive can't be damaged", &Cheats::vehGod },
		{ "neverWanted", "Never wanted", "The police ignore you", &Cheats::neverWanted },
		{ "superJump", "Super jump", "Jump over buses (no fall damage)", &Cheats::superJump },
		{ "superRun", "Super speed", "Run twice as fast", &Cheats::superRun },
		{ "infSprint", "Endless sprint", "Never run out of breath", &Cheats::infSprint },
		{ "lowGravity", "Moon gravity", "Everything falls slowly", &Cheats::lowGravity },
		{ "explosive", "Explosive bullets", "Every bullet you fire explodes", &Cheats::explosive },
		{ "oneHit", "One-hit kills", "Your weapons are ten times stronger", &Cheats::oneHit },
		{ "freezeTime", "Freeze time", "The clock stops", &Cheats::freezeTime },
		{ "slowmo", "Slow motion", "Half-speed world", &Cheats::slowmo },
		{ "riot", "Riot", "Everyone in the streets starts fighting", &Cheats::riot },
	};
	return T;
}

namespace {
const char* const HELP[] = {
	"god \xC2\xB7 car <name> \xC2\xB7 repair \xC2\xB7 flip \xC2\xB7 heal \xC2\xB7 weapons \xC2\xB7 wanted <0-5> \xC2\xB7 never",
	"time <0-24> \xC2\xB7 weather clear|cloudy|rain|storm|fog \xC2\xB7 freeze \xC2\xB7 slowmo",
	"jump \xC2\xB7 run \xC2\xB7 sprint \xC2\xB7 gravity \xC2\xB7 boom \xC2\xB7 riot \xC2\xB7 traffic off|normal|heavy \xC2\xB7 peds off|normal|heavy",
	"clear \xC2\xB7 explode \xC2\xB7 skydive \xC2\xB7 bodyguard \xC2\xB7 enemies \xC2\xB7 tp <place> \xC2\xB7 colour <n>",
	"pet <lab|shepherd|husky|rottweiler|pug|poodle|tabby|blackcat|siamese> [name] \xC2\xB7 animals",
	"crime off|normal|high (street crime) \xC2\xB7 crime now [mug|steal|speed|jaywalk]",
};
std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string NoSpaces(std::string s) { s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return std::isspace((unsigned char)c); }), s.end()); return s; }
bool StartsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
// parseFloat: the leading number, or NaN
double ParseFloat(const std::string& s) {
	const char* b = s.c_str();
	while (*b && std::isspace((unsigned char)*b)) b++;
	char* e = nullptr;
	const double v = std::strtod(b, &e);
	return e == b ? NaN() : v;
}
std::string Fmt(double v) { char b[32]; std::snprintf(b, sizeof b, "%g", v); return b; }
}

bool FreeRoam::allowed() const { return game.freeroamActive() && !(game.net && game.net->online()); }

std::string FreeRoam::toggle(const std::string& key, int on) {
	if (!allowed()) return "Admin tools are for single-player free roam.";
	for (const AdminToggle& t : AdminToggles()) if (t.key == key) {
		bool& f = game.cheats.*(t.flag);
		f = on < 0 ? !f : on > 0;
		return t.label + ": " + (f ? "on" : "off");
	}
	return key + ": off";
}

std::string FreeRoam::heal() { Player& p = *game.player; p.health = p.maxHealth; p.armor = 100; return "Health and armour full."; }

std::string FreeRoam::weapons() {
	Player& p = *game.player;
	for (const std::string& id : WeaponOrder()) { const WeaponDef* d = FindWeapon(id); if (d) p.giveWeapon(id, (d->clip ? d->clip : 1) * 8); }
	return "All weapons.";
}

std::string FreeRoam::wanted(double n) {
	Police* pol = game.policeSys;
	if (!pol) return "No police here.";
	const int k = (int)Clamp(Finite(n) ? JsRound(n) : 0, 0, 5);
	if (k > 0) game.cheats.neverWanted = false;
	if (k == 0) pol->clear(); else pol->setLevel(k);
	return k ? "Wanted level " + std::to_string(k) + "." : "Wanted level cleared.";
}

std::string FreeRoam::setTime(double h) { game.env.setTime(std::fmod(std::fmod(h, 24) + 24, 24)); return "Time set to " + game.env.timeString() + "."; }

std::string FreeRoam::weather(const std::string& w) {
	static const std::vector<std::string> ok = { "clear", "cloudy", "rain", "storm", "fog" };
	if (std::find(ok.begin(), ok.end(), w) == ok.end()) return "Weather: clear, cloudy, rain, storm, fog.";
	game.env.setWeather(w);
	game.env.weatherLocked = true;
	game.env.weatherTimer = 1e9;
	return "Weather: " + w + ".";
}

std::string FreeRoam::repair() {
	Vehicle* v = game.player->vehicle;
	if (!v) return "Get in a vehicle first.";
	if (v->exploded) return "That one's beyond repair \xE2\x80\x94 spawn a new one.";
	v->health = v->maxHealth; v->onFire = false; v->burnTime = 0;
	v->undent();
	return v->def.name + " repaired.";
}

std::string FreeRoam::flip() {
	Vehicle* v = game.player->vehicle;
	if (!v) return "Get in a vehicle first.";
	v->bodyRoll = 0; v->bodyPitch = 0; v->groundPitch = 0; v->groundRoll = 0; v->r = 0;
	if (AirVehicle* a = dynamic_cast<AirVehicle*>(v)) { a->quat = Quat::FromAxisAngle(V3(0, 1, 0), v->yaw); a->angVel = V3(); }
	v->pos.y = game.collision->floorHeight(v->pos.x, v->pos.z, v->pos.y + 2) + 0.2;
	v->vel = V3();
	return "Back on your wheels.";
}

std::string FreeRoam::clearArea() {
	Game& g = game;
	Player& p = *g.player;
	const double R = 90;
	int n = 0;
	const std::vector<std::shared_ptr<Vehicle>> vs = g.vehicles.list;
	for (const auto& sv : vs) {
		Vehicle* v = sv.get();
		if (v == p.vehicle || v->def.train || v->remote || v->persistent) continue;
		if (Hypot(v->pos.x - p.pos.x, v->pos.z - p.pos.z) > R) continue;
		bool player = false;
		for (const auto& o : v->occupants) if (o && o->isPlayer) player = true;
		if (player) continue;
		if (g.traffic) g.traffic->despawn(v); else g.vehicles.remove(v);
		n++;
	}
	if (g.peds) {
		const std::vector<std::shared_ptr<Ped>> qs = g.peds->list;
		for (const auto& sq : qs) {
			Ped* q = sq.get();
			if (q->persistent || q->vehicle) continue;
			if (Hypot(q->pos.x - p.pos.x, q->pos.z - p.pos.z) > R) continue;
			g.peds->remove(q);
			n++;
		}
	}
	if (g.policeSys) g.policeSys->clear();
	return "Cleared " + std::to_string(n) + " people and vehicles.";
}

std::string FreeRoam::explodeNearby() {
	Game& g = game;
	Player& p = *g.player;
	int n = 0;
	const std::vector<std::shared_ptr<Vehicle>> vs = g.vehicles.list;
	for (const auto& sv : vs) {
		Vehicle* v = sv.get();
		if (v == p.vehicle || v->def.train || v->remote || v->exploded) continue;
		if (Hypot(v->pos.x - p.pos.x, v->pos.z - p.pos.z) > 45) continue;
		v->lastDamager = Ref<Character>(&p); v->explode(); n++;
	}
	return n ? "Boom (" + std::to_string(n) + ")." : "Nothing close enough to blow up.";
}

std::string FreeRoam::skydive() {
	Game& g = game;
	Player& p = *g.player;
	if (p.vehicle) p.vehicle->takeOut(&p);
	p.setPosition(p.pos.x, g.map.GroundHeight(p.pos.x, p.pos.z) + 420, p.pos.z);
	p.vel = V3();
	p.bailOut(400);
	return "Geronimo! Space opens the parachute.";
}

std::string FreeRoam::bodyguard() {
	Game& g = game;
	Player& p = *g.player;
	if (!g.peds) return "";
	const double a = p.yaw + kPi;
	PedOpts o; o.brain = "civilian"; o.team = "player"; o.health = 250; o.persistent = true;
	Ped* q = g.peds->spawnPed(p.pos.x + std::sin(a) * 2, p.pos.z + std::cos(a) * 2, o);
	if (!q) return "";
	q->giveWeapon("rifle", 900); q->equip("rifle");
	q->follow = Ref<Character>(&p); q->followSlot = (++guards) % 3; q->setState("follow");
	return "A bodyguard has your back.";
}

std::string FreeRoam::enemies() {
	Game& g = game;
	Player& p = *g.player;
	if (!g.peds) return "";
	for (int k = 0; k < 5; k++) {
		const double a = Rand() * kPi * 2, d = 25 + Rand() * 10;
		const double x = p.pos.x + std::cos(a) * d, z = p.pos.z + std::sin(a) * d;
		PedOpts o; o.brain = "gang"; o.gang = "vipers"; o.health = 110;
		Ped* q = g.peds->spawnPed(x, z, o);
		if (!q) continue;
		q->giveWeapon(k % 2 ? "smg" : "pistol", 400); q->equip(k % 2 ? "smg" : "pistol");
		q->threat = Ref<Character>(&p); q->setState("attack");
	}
	return "Here they come.";
}

// too many buildings round about for a helicopter to spin up?
bool FreeRoam::cramped(double x, double z, const VehicleDef& d) {
	const double r = std::max(d.L, d.W) / 2 + 2;
	std::vector<CollObj*> list;
	game.collision->query(x - r, z - r, x + r, z + r, list);
	for (const CollObj* o : list) if ((o->kind == CollObj::Box || o->kind == CollObj::OBox) && o->maxY > 3) return true;
	return false;
}

std::string FreeRoam::summon(const std::string& id, bool into, int air) {
	Game& g = game;
	Player& p = *g.player;
	const VehicleDef* d = FindVehicle(id);
	if (!d || d->train) return "No such vehicle.";
	if (!canSpawn()) return "Vehicle spawning is a free roam feature.";
	if (g.missionActive) return "Not during a mission.";
	Vehicle* old = p.vehicle;
	if (old && into && !old->def.train) {
		old->takeOut(&p);
		for (auto it = summoned.begin(); it != summoned.end(); ++it) if (it->get() == old) { summoned.erase(it); g.vehicles.remove(old); break; }
	} else if (old) old->takeOut(&p);
	const bool airK = d->aircraft;
	const double fx = std::sin(p.yaw), fz = std::cos(p.yaw);
	const double ahead = into ? 0 : std::max(d->L, 5.0) / 2 + 3;
	double x = p.pos.x + fx * ahead, z = p.pos.z + fz * ahead;
	if (d->kind == "boat") {
		// boats go in the nearest open water
		auto deep = [&](double px, double pz) { return g.map.WaterLevel(px, pz) - g.map.TerrainHeight(px, pz) > d->draft + 1.2; };
		bool found = deep(x, z);
		for (double r = 6; !found && r <= 400; r += 6) for (int k = 0; k < 24 && !found; k++) {
			const double a = k / 24.0 * kPi * 2, px = p.pos.x + std::cos(a) * r, pz = p.pos.z + std::sin(a) * r;
			if (deep(px, pz)) { found = true; x = px; z = pz; }
		}
		if (!found) {
			if (old && !old->removed && !p.vehicle) g.vehicles.seatNow(&p, old, 0);
			return "No open water nearby: try the beach, a harbour or the lake.";
		}
	}
	const double gy = g.map.GroundHeight(x, z);
	SpawnOpts opt; opt.persistent = true;
	if (hasColor) { opt.hasColor = true; opt.color = color; }
	const bool inAir = airK && (air >= 0 ? air > 0 : (d->kind != "heli" || cramped(x, z, *d)));
	double y = d->kind == "boat" ? g.map.WaterLevel(x, z) : g.collision->floorHeight(x, z, p.pos.y + 1.5);
	if (inAir) y = std::max(gy, 0.0) + (d->kind == "heli" ? 70 : 260);
	Vehicle* v = g.vehicles.spawn(id, x, z, p.yaw, opt);
	if (!v) return "No such vehicle.";
	v->persistent = true;
	v->pos.y = y;
	if (AirVehicle* a = dynamic_cast<AirVehicle*>(v); inAir && a) {
		a->grounded = false; a->gearDown = false; a->gearK = 0;
		v->airborne = true;
		if (d->kind == "heli") { a->spool = 1; v->vel = V3(); }
		else { a->power = 0.85; a->spool = 0.85; const double sp = std::max(IsSet(d->vRotate) ? d->vRotate : 40.0, 45.0) * 1.35; v->vel = V3(fx * sp, 0, fz * sp); }
	}
	if (into) g.vehicles.seatNow(&p, v, 0);
	summoned.push_back(Ref<Vehicle>(v));
	// keep a handful: the oldest empty one goes
	while (summoned.size() > 5) {
		size_t i = summoned.size();
		for (size_t k = 0; k < summoned.size(); k++) {
			Vehicle* o = summoned[k].get();
			bool player = false;
			if (o) for (const auto& c : o->occupants) if (c && c->isPlayer) player = true;
			if (o != p.vehicle && !player) { i = k; break; }
		}
		if (i == summoned.size()) break;
		Vehicle* o = summoned[i].get();
		summoned.erase(summoned.begin() + (long)i);
		if (o && !o->removed) { o->persistent = false; if (!o->driver()) g.vehicles.remove(o); }
	}
	return d->name + (inAir ? " (airborne)" : "") + ".";
}

// traffic and pedestrians off / normal / heavy (admin.js _density)
void FreeRoam::density() {
	Game& g = game;
	if (g.traffic) { if (baseCars < 0) baseCars = g.traffic->maxCars; g.traffic->maxCars = (int)JsRound(baseCars * (traffic == "off" ? 0 : traffic == "heavy" ? 2 : 1)); }
	if (g.peds) { if (basePeds < 0) basePeds = g.peds->maxPeds; g.peds->maxPeds = (int)JsRound(basePeds * (peds == "off" ? 0 : peds == "heavy" ? 1.8 : 1)); }
	if (traffic == "off" && g.traffic) {
		const std::vector<Ref<Vehicle>> cars = g.traffic->cars;
		for (const auto& c : cars) if (Vehicle* v = c.get()) if (!v->removed && g.player->vehicle != v && !v->persistent) g.traffic->despawn(v);
	}
	if (peds == "off" && g.peds) {
		const std::vector<std::shared_ptr<Ped>> qs = g.peds->list;
		for (const auto& q : qs) if (!q->persistent && !q->vehicle && q->brain == "civilian") g.peds->remove(q.get());
	}
}

// ------------------------------------------------------------------ the console (` key): one line in, one line out
std::string FreeRoam::run(const std::string& line) {
	Game& g = game;
	std::vector<std::string> words;
	{ std::string w; for (char c : line) { if (std::isspace((unsigned char)c)) { if (!w.empty()) words.push_back(w); w.clear(); } else w += c; } if (!w.empty()) words.push_back(w); }
	if (words.empty()) return "";
	const std::string cmd = Lower(words[0]);
	std::string a;
	for (size_t i = 1; i < words.size(); i++) a += (i > 1 ? " " : "") + words[i];
	a = Lower(a);
	if (cmd == "help" || cmd == "?") { std::string s; for (const char* h : HELP) s += (s.empty() ? "" : "<br>") + std::string(h); return s; }
	if (cmd == "car" || cmd == "spawn" || cmd == "veh") {
		const std::string want = NoSpaces(a);
		std::string id;
		for (const VehicleDef& d : VehicleDefs()) if (d.id == want || NoSpaces(Lower(d.name)) == want) { id = d.id; break; }
		if (id.empty()) for (const VehicleDef& d : VehicleDefs()) if (StartsWith(d.id, want) || StartsWith(Lower(d.name), a)) { id = d.id; break; }
		if (id.empty()) {
			std::string s = "Vehicles: ";
			bool first = true;
			for (const SpawnGroup& sg : SpawnGroups()) for (const std::string& v : sg.ids) { s += (first ? "" : ", ") + v; first = false; }
			return s;
		}
		return summon(id);
	}
	if (cmd == "tp" || cmd == "goto") {
		const std::vector<FreeDestination> list = destinations();
		const FreeDestination* d = nullptr;
		for (const FreeDestination& x : list) {
			if (a.empty() || a == "waypoint") { if (x.name == "Map waypoint") { d = &x; break; } }
			else if (Lower(x.name).find(a) != std::string::npos) { d = &x; break; }
		}
		if (!d) return !a.empty() ? "No place called \"" + a + "\"." : "Set a waypoint on the map first.";
		const std::string name = d->name;
		teleport(*d);
		return "Teleporting to " + name + ".";
	}
	if (cmd == "colour" || cmd == "color" || cmd == "paint") {
		if (a.empty() || a == "default") { hasColor = false; return "Spawned vehicles use their own colours."; }
		std::string hx = a; if (!hx.empty() && hx[0] == '#') hx.erase(0, 1);
		char* e = nullptr;
		const long n = std::strtol(hx.c_str(), &e, 16);
		if (e == hx.c_str()) return "colour <hex>, e.g. colour ff0000";
		hasColor = true; color = (uint32_t)n;
		if (Vehicle* v = g.player->vehicle) { v->color = color; v->painted = true; }
		char b[16]; std::snprintf(b, sizeof b, "%06x", (unsigned)n);
		return std::string("Paint: #") + b + ".";
	}
	if (!allowed()) return "Admin commands work in single-player free roam (the vehicle spawner works online too).";
	static const std::vector<std::pair<std::string, std::string>> T = {
		{ "god", "god" }, { "jump", "superJump" }, { "run", "superRun" }, { "speed", "superRun" }, { "sprint", "infSprint" }, { "gravity", "lowGravity" },
		{ "moon", "lowGravity" }, { "boom", "explosive" }, { "explosive", "explosive" }, { "onehit", "oneHit" }, { "freeze", "freezeTime" }, { "slowmo", "slowmo" },
		{ "never", "neverWanted" }, { "riot", "riot" }, { "bulletproof", "vehGod" } };
	for (const auto& [k, key] : T) if (k == cmd) return toggle(key, a == "on" ? 1 : a == "off" ? 0 : -1);
	if (cmd == "heal" || cmd == "health" || cmd == "armour" || cmd == "armor") return heal();
	if (cmd == "weapons" || cmd == "guns") return weapons();
	if (cmd == "wanted") return wanted(ParseFloat(a));
	if (cmd == "time") { const double h = ParseFloat(a); return setTime(Finite(h) && h != 0 ? h : 12); }
	if (cmd == "weather") return weather(a);
	if (cmd == "repair" || cmd == "fix") return repair();
	if (cmd == "flip") return flip();
	if (cmd == "clear") return clearArea();
	if (cmd == "explode" || cmd == "nuke") return explodeNearby();
	if (cmd == "skydive") return skydive();
	if (cmd == "bodyguard") return bodyguard();
	if (cmd == "enemies" || cmd == "attack") return enemies();
	if (cmd == "traffic") { if (a != "off" && a != "normal" && a != "heavy") return "traffic off | normal | heavy"; traffic = a; return "Traffic: " + a + "."; }
	if (cmd == "peds" || cmd == "people") { if (a != "off" && a != "normal" && a != "heavy") return "peds off | normal | heavy"; peds = a; return "Pedestrians: " + a + "."; }
	if (cmd == "crime") {
		if (a == "off" || a == "normal" || a == "high") { g.settings.npcCrime = a; return "Street crime: " + a + "."; }
		const size_t sp = a.find(' ');
		const std::string w = a.substr(0, sp), kind = sp == std::string::npos ? "" : a.substr(sp + 1);
		static const std::vector<std::string> kinds = { "mug", "steal", "speed", "jaywalk" };
		if (w != "now" || (!kind.empty() && std::find(kinds.begin(), kinds.end(), kind) == kinds.end())) return "crime off | normal | high \xC2\xB7 crime now [mug|steal|speed|jaywalk]";
		if (g.settings.npcCrime == "off") return "Street crime is off (crime normal to turn it on).";
		const std::string k = !kind.empty() ? kind : kinds[(size_t)(Rand() * kinds.size())];
		NpcCrime* nc = dynamic_cast<NpcCrime*>(g.npcCrime);
		const char* what = k == "mug" ? "a mugging" : k == "steal" ? "a car thief" : k == "speed" ? "a speeding driver" : "a jaywalker";
		return nc && nc->stage(k) ? std::string("Trouble nearby: ") + what + "." : "Nobody around to do it right now.";
	}
	if (cmd == "pet") {
		const size_t sp = a.find(' ');
		const std::string breed = a.substr(0, sp);
		const auto& B = PetBreeds();
		if (std::find(B.begin(), B.end(), breed) == B.end()) { std::string s = "pet "; for (size_t i = 0; i < B.size(); i++) s += (i ? " | " : "") + B[i]; return s + " [name]"; }
		std::string nm = sp == std::string::npos ? "" : a.substr(sp + 1);
		nm = nm.substr(0, 16);
		if (!nm.empty()) nm[0] = (char)std::toupper((unsigned char)nm[0]);
		Animal* pet = g.pets ? g.pets->adopt(breed, nm) : nullptr;
		return pet ? "Meet <b>" + pet->petName + "</b>." : "No pet.";
	}
	if (cmd == "animals" || cmd == "wildlife") {
		int n = 0;
		if (Wildlife* wl = dynamic_cast<Wildlife*>(g.wildlife)) for (const auto& x : wl->list) if (!x->dead) n++;
		return std::to_string(n) + " animals nearby.";
	}
	return "Unknown command \"" + cmd + "\". Type <b>help</b>.";
}

// ------------------------------------------------------------------ destinations
std::vector<FreeDestination> FreeRoam::destinations() const {
	const auto& L = game.map.landmarks;
	std::vector<FreeDestination> out;
	auto lm = [&L](const char* k) -> const Landmark* { auto it = L.find(k); return it == L.end() ? nullptr : &it->second; };
	auto add = [&out](const std::string& group, const std::string& name, double x, double z, FreeDestination o = {}) {
		if (!Finite(x) || !Finite(z)) return;
		o.group = group; o.name = name; o.x = x; o.z = z;
		out.push_back(o);
	};
	auto foot = [](double yaw = NaN()) { FreeDestination o; o.foot = true; o.yaw = yaw; return o; };
	auto footExact = [](double yaw = NaN()) { FreeDestination o; o.foot = true; o.exactVeh = true; o.yaw = yaw; return o; };
	if (game.hud) if (const auto w = game.hud->waypoint()) add("Quick", "Map waypoint", w->x, w->z, foot());
	if (auto* h = lm("home")) add("Quick", "Safehouse", h->x, h->z, foot(kPi));

	const std::string city = "Los Soles";
	auto respawn = [&](const char* k, const char* name) {
		const Landmark* l = lm(k);
		if (!l) return;
		auto r = l->pts.find("respawn"); auto rot = l->vals.find("respawnRot");
		if (r != l->pts.end()) add(city, name, r->second.x, r->second.z, foot(rot != l->vals.end() ? rot->second : NaN()));
	};
	respawn("hospital", "Hospital");
	respawn("police", "Police HQ");
	auto at = [&](const char* k, const std::string& name, FreeDestination o = {}, double dx = 0, double dz = 0) { if (const Landmark* l = lm(k)) add(city, name, l->x + dx, l->z + dz, o); };
	auto named = [&](const char* k, const char* fallback) { const Landmark* l = lm(k); return l && !l->name.empty() ? l->name : std::string(fallback); };
	at("plaza", "City Plaza");
	at("gunshop", "Gun Barn");
	if (const Landmark* s = lm("spray")) { auto e = s->pts.find("entry"); if (e != s->pts.end()) add(city, "Spray Shack", e->second.x, e->second.z); }
	at("mall", named("mall", "Mall"));
	at("park", named("park", "Park"), foot());
	at("stadium", named("stadium", "Stadium"));
	at("burger", "Big Bun Burgers");
	at("projects", "The Projects");
	at("vipers", "Vipers Turf");
	at("mansion", "Hillside Mansion");
	at("golf", named("golf", "Country Club"));
	at("sign", "Los Soles Sign", foot());
	at("beach", "Beach", footExact());
	at("pierfront", "Pier & Ferris Wheel");
	at("skate_santaluz", "Santa Luz Skatepark", footExact(), 0, -15);
	at("marina", "Santa Luz Marina", foot(0));
	at("docksQuay", "Docks");
	at("cargoShip", named("cargoShip", "Cargo ship"), footExact(0), -3, -4);
	at("warehouse", "Warehouses");

	const std::string nc = "San Aurelio";
	auto atN = [&](const char* k, const std::string& name, FreeDestination o = {}) { if (const Landmark* l = lm(k)) add(nc, name, l->x, l->z, o); };
	atN("aurelio", "Plaza de Aurelio", foot());
	atN("aurCathedral", "Cathedral of San Aurelio");
	atN("aurPark", "Bayview Park", foot());
	atN("aurArena", "Aurelio Arena");
	atN("aurHarbor", "Harbor Drive");
	atN("aurMarina", "Aurelio Marina", footExact(kPi / 2));

	for (const Town& t : TOWNS) add("Towns", t.name, t.x, t.z);
	if (const Landmark* hp = lm("halePier")) { FreeDestination o = foot(); o.y = hp->y; add("Towns", "Port Hale Pier", hp->x - 6, hp->z, o); }

	if (game.map.roadInfo.hasRail) for (const RailStation& st : game.map.roadInfo.rail.stations) {
		// on foot: the platform (right of increasing s), facing along it; by car: the forecourt behind the building
		const double hx = st.key == "union" ? 9 : 6, lat = 6.75 + 3 + 2 * hx + 6;
		std::string lower = st.name; for (char& ch : lower) ch = (char)std::tolower((unsigned char)ch);
		FreeDestination o = foot(st.yaw); o.y = st.y + 1.05;
		o.hasVeh = true; o.vehX = st.x - st.tz * lat + st.tx * 16; o.vehZ = st.z + st.tx * lat + st.tz * 16; o.vehYaw = st.yaw;
		add("Sol Line", lower.find("station") != std::string::npos ? st.name : st.name + " station", st.x - st.tz * 4.6, st.z + st.tx * 4.6, o);
	}

	const std::string hw = "Airfields & military";
	add(hw, AIRFIELD.name, AIRFIELD.x - 20, AIRFIELD.z + 40, footExact(AIRFIELD.yaw));
	add(hw, std::string(BASE.name) + " main gate", BASE.maxX + 60, BASE.gateZ);
	for (const FixedVehicle& e : game.map.fixedVehicles) if (e.type == "raptor") { add(hw, std::string(BASE.name) + " flight line (restricted!)", e.x + 14, e.z + 4, footExact()); break; }
	for (const FixedVehicle& e : game.map.fixedVehicles) if (e.type == "skylark") { add(hw, "Skylark helipad", e.x + 9, e.z + 3, footExact()); break; }
	return out;
}

// ------------------------------------------------------------------ travel
void FreeRoam::teleport(const FreeDestination& d, bool instant) {
	auto go = [this, d]() {
		place(d);
		if (game.traffic) game.traffic->populate(12);
		if (game.peds) game.peds->populate(12);
		if (game.hud) game.hud->help("Teleported to <b>" + d.name + "</b>.", 3);
	};
	if (instant || !game.hud) { go(); return; }
	game.hud->fadeTo(1, 0.3);
	game.setTimeout(0.38, [this, go]() { go(); game.setTimeout(0.25, [this]() { if (game.hud) game.hud->fadeTo(0, 0.6); }); });
}

// Move the player (and whatever they're driving) to a destination.
void FreeRoam::place(const FreeDestination& d) {
	Player& p = *game.player;
	Vehicle* v = p.vehicle;
	// the train can't leave its rails: step off it first
	if (v && v->def.train) { v->takeOut(&p); v = nullptr; }
	if (p.chute) p.closeChute();

	// an aircraft in flight arrives in the air over the destination
	AirVehicle* air = dynamic_cast<AirVehicle*>(v);
	if (air && !air->grounded && air->altitude() > 8) {
		const double alt = Max(air->altitude(), v->def.kind == "heli" ? 60 : 140);
		const double gy = Max(game.map.GroundHeight(d.x, d.z), 0);
		v->pos.set(d.x, gy + alt, d.z);
		game.rig.hasFlightOff = false;
		return;
	}

	double x = d.x, z = d.z, y = d.y, yaw = IsSet(d.yaw) ? d.yaw : game.rig.yaw - kPi;
	if (v && d.hasVeh) { x = d.vehX; z = d.vehZ; yaw = d.vehYaw; y = NaN(); }
	else if (v ? !d.exactVeh : (!d.foot || (!IsSet(d.y) && game.map.IsWater(d.x, d.z)))) {
		// (a destination in the sea sends you to the nearest street instead)
		RoadSpot s;
		if (FindRoadSpot(game, d.x, d.z, !v, s)) { x = s.x; z = s.z; y = s.y; yaw = s.yaw; }
	}
	if (!IsSet(y)) y = game.collision->floorHeight(x, z, game.map.GroundHeight(x, z) + 1.5);

	if (v) {
		v->pos.set(x, y, z);
		v->yaw = yaw;
		v->vel = V3();
		v->r = 0;
		v->vy = 0;
		v->airborne = false;
		v->lastGroundY = y;
		if (air) { air->quat = Quat::FromAxisAngle(V3(0, 1, 0), yaw); air->angVel = V3(); air->grounded = true; air->gearDown = true; air->gearK = 1; }
		game.rig.hasFlightOff = false;
	} else {
		p.setPosition(x, y, z);
		p.setYaw(yaw);
		p.vel = V3();
		p.swimming = false;
	}
	game.rig.yaw = yaw + kPi;
	game.rig.vehYawOffset = 0;
}

// ------------------------------------------------------------------ the Teleport tab
std::vector<StoreItem> FreeRoam::teleportRows() {
	std::vector<StoreItem> rows;
	const auto all = destinations();
	if (tpGroup.empty()) {
		std::vector<std::string> groups;
		for (const auto& d : all) if (std::find(groups.begin(), groups.end(), d.group) == groups.end()) groups.push_back(d.group);
		for (const std::string& gname : groups) {
			int n = 0; for (const auto& d : all) if (d.group == gname) n++;
			StoreItem it; it.name = gname; it.desc = std::to_string(n) + (n == 1 ? " place" : " places"); it.button = "Open";
			it.use = [this, gname](Player&, Game&) { tpGroup = gname; return std::string(); };
			rows.push_back(it);
		}
		return rows;
	}
	StoreItem back; back.name = "\xE2\x86\x90 Back"; back.desc = "All groups"; back.button = "Back";
	back.use = [this](Player&, Game&) { tpGroup.clear(); return std::string(); };
	rows.push_back(back);
	for (const auto& d : all) {
		if (d.group != tpGroup) continue;
		StoreItem it; it.name = d.name; it.desc = game.map.ZoneName(d.x, d.z); it.button = "Go"; it.closes = true;
		it.use = [this, d](Player&, Game&) { teleport(d); return std::string(); };
		rows.push_back(it);
	}
	return rows;
}

void FreeRoam::openTeleport() {
	if (!game.hud || !active()) return;
	tpGroup.clear();
	PanelOpts o; o.inVehicle = true; o.leave = "Close (Esc)";
	o.refresh = [this]() { return teleportRows(); };
	game.hud->openPanel("TELEPORT", "Free roam: go anywhere, and take your vehicle with you.", teleportRows(), o);
}

} // namespace atg
