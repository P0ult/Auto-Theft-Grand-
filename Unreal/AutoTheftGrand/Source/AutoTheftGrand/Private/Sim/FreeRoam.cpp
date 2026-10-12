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
		game.cheats = Cheats();
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
				if (auto* combat = dynamic_cast<Combat*>(game.combat)) {
					const WeaponDef* weapon = FindWeapon(q->weapon);
					if (weapon && weapon->type != "melee") {
						q->aiming = false;
						q->faceTowards(threat->pos.x, threat->pos.z, 0);
						combat->fireWeapon(q, *weapon, q->chestPos(), (threat->chestPos() - q->chestPos()).normalized());
					}
				}
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
