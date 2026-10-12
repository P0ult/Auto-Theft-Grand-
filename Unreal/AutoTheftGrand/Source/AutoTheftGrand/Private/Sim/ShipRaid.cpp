#include "ShipRaid.h"
#include "Collision.h"
#include "Game.h"
#include "Missions.h"
#include "Peds.h"
#include "Pickups.h"
#include "Player.h"
#include "Police.h"
#include "Skeleton.h"
#include "Weapons.h"

namespace atg {

namespace {
constexpr double RESTOCK = 900; // seconds before the ship has a crew and cargo again

// [local x, local z, deck (0-3 superstructure levels, -1 the forecastle), role, facing, patrol z range]
struct Post { double lx, lz; int lvl; const char* role; double face; bool patrol; double z0, z1; };
const Post POSTS[] = {
	{ -10.6, 47.2, 0, "security", -kPi / 2, false, 0, 0 }, { -10.2, 51.5, 0, "deck", kPi, false, 0, 0 },
	{ 11.4, -30, 0, "deck", 0, true, -58, 44 }, { -11.4, 10, 0, "deck", kPi, true, -58, 44 },
	{ 3, -39.5, 0, "deck", kPi / 2, false, 0, 0 }, { -5, 23.5, 0, "security", -kPi / 2, false, 0, 0 }, { 6, 2.5, 0, "deck", kPi / 2, false, 0, 0 },
	{ 0, -80, -1, "security", kPi, false, 0, 0 }, { 4, -62, 0, "deck", kPi, false, 0, 0 },
	{ -6, 57, 1, "security", kPi, false, 0, 0 }, { 5, 59, 2, "security", kPi, false, 0, 0 },
	{ -5.5, 65.5, 3, "security", kPi, false, 0, 0 }, { 1.5, 70, 3, "captain", kPi, false, 0, 0 },
};

// crewAppearance: randomAppearance(rng, opts), the options' own draws coming off the generator first
Appearance CrewAppearance(const std::string& role, RNG& rng) {
	if (role == "captain") {
		Appearance a = RandomAppearance(rng, 0);
		a.beard = true; a.hairStyle = "short"; a.hair = 0x9a9a9a; a.shirtType = "long";
		a.hasUniform = true; a.uniformShirt = 0xf4f4f0; a.uniformPants = 0x1a2440; a.uniformHat = 0xf4f4f0; a.uniformBand = 0xd4af37;
		a.noBadge = true; a.shoes = 0x0a0a0a; a.build = 1.12;
		return a;
	}
	if (role == "security") {
		const bool mask = rng.Chance(0.55);
		const bool female = rng.Chance(0.15);
		const bool glasses = !mask && rng.Chance(0.4);
		Appearance a = RandomAppearance(rng, female ? 1 : 0);
		a.shirtType = "long";
		a.hasUniform = true; a.uniformShirt = 0x26282b; a.uniformPants = 0x1f2124; a.uniformHat = -1;
		a.noBadge = true; a.vest = "tactical"; a.mask = mask ? 0x151515 : -1;
		a.hairStyle = mask ? "buzz" : "cap"; a.hat = 0x151515; a.shoes = 0x0a0a0a; a.glasses = glasses;
		return a;
	}
	const uint32_t over = rng.Pick(std::vector<uint32_t>{ 0xe06c1b, 0x2c4a7a, 0x3d3f42 });
	const bool female = rng.Chance(0.2);
	const uint32_t vest = rng.Pick(std::vector<uint32_t>{ 0xd7ff1e, 0xff8a1e });
	const uint32_t hardhat = rng.Pick(std::vector<uint32_t>{ 0xf2d21b, 0xf4f4f4, 0xff7a1a });
	const bool beard = rng.Chance(0.3);
	Appearance a = RandomAppearance(rng, female ? 1 : 0);
	a.shirtType = "long";
	a.hasUniform = true; a.uniformShirt = over; a.uniformPants = over; a.uniformHat = -1;
	a.noBadge = true; a.vest = "hiviz"; a.vestColor = vest; a.hardhat = hardhat;
	a.hairStyle = "buzz"; a.shoes = 0x3a2a1a; a.beard = beard;
	return a;
}

// Number.toLocaleString for whole dollars: 15300 -> "15,300"
std::string Thousands(long long n) {
	std::string s = std::to_string(n < 0 ? -n : n), out;
	for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) out += ','; out += s[i]; }
	return (n < 0 ? "-" : "") + out;
}
const std::string& RoleOf(const Ped* c) { static const std::string none; auto it = c->str.find("role"); return it == c->str.end() ? none : it->second; }
} // namespace

ShipRaid::ShipRaid(Game& g) : game(g), ship(BuildCargoShip()) {
	// the ship's collision, in the order buildCargoShip adds it
	CollisionWorld& col = *game.collision;
	for (const ShipCollider& c : ship.colliders) {
		if (c.kind == "box") col.addBox(c.minX, c.minY, c.minZ, c.maxX, c.maxY, c.maxZ, c.type);
		else if (c.kind == "obox") col.addOBox(c.cx, c.cz, c.hx, c.hz, c.yaw, c.minY, c.maxY, c.type);
		else if (c.kind == "deck") col.addDeck(c.ax, c.az, c.ay, c.bx, c.bz, c.by, c.hl, c.hr);
		else col.addCircle(c.x, c.z, c.r, c.h, c.y0, c.type);
	}
	Blip b; b.x = SHIP.x; b.z = SHIP.z; b.icon = "ship"; b.color = 0xff9a1f; b.label = SHIP.name; b.small = true;
	blip = game.addBlip(b);
	Landmark lm; lm.x = ship.gangwayFoot.x; lm.z = ship.gangwayFoot.z; lm.name = SHIP.name;
	const_cast<CityMap&>(game.map).landmarks["cargoShip"] = lm;
	auto near = [](const V3& p) { return Dist2(p.x, p.z, SHIP.x, SHIP.z) < 110 * 110; };
	Events& ev = game.events;
	ev.gunshot.on([this, near](Character* s, V3 pos, const std::string&) {
		if (s && s->isPlayer && spawned && (near(pos) || CargoShip::Aboard(game.player->pos.x, game.player->pos.y, game.player->pos.z))) alert("Shots fired on deck!");
	});
	ev.explosion.on([this, near](V3 pos, double, Character* src) { if (src && src->isPlayer && spawned && near(pos)) alert(); });
	ev.kill.on([this](Character* killer, Character* victim, const std::string&, const std::string&) {
		const Ped* v = dynamic_cast<Ped*>(victim);
		if (killer && killer->isPlayer && v && v->gang == "crew") alert();
	});
	ev.charDamaged.on([this](Character* who, double, Character* src) {
		const Ped* p = dynamic_cast<Ped*>(who);
		if (src && src->isPlayer && p && p->gang == "crew") alert("A crewman's down!");
	});
}

bool ShipRaid::restocked() const { return game.time - lootedAt > RESTOCK; }

void ShipRaid::alert(const std::string& msg) {
	if (!spawned) return;
	if (!alerted) {
		alerted = true;
		if (game.hud) game.hud->bigMessage("SHIP ALERTED", "failed", 2.5, msg.empty() ? "The crew are armed" : msg);
		radioT = 9; // the captain calls it in
		if (game.audio) game.audio->play("wanted", 0.6);
	}
	game.peds->gangAggro["crew"] = true;
	for (auto& c : crew) if (!c->dead && c->state != "attack") { c->threat = game.player.get(); c->setState("attack"); }
}

void ShipRaid::update(double dt) {
	Player& p = *game.player;
	const V3 pp = p.vehicle ? p.vehicle->pos : p.pos;
	const double d = Hypot(pp.x - SHIP.x, pp.z - SHIP.z);
	// crew aboard while you're around (and the ship's been restocked)
	if (d < 300 && !spawned && restocked()) spawn();
	if (spawned && d > 420) { farT += dt; if (farT > 20) despawn(); } else farT = 0;
	if (!spawned) return;
	crew.erase(std::remove_if(crew.begin(), crew.end(), [](const std::shared_ptr<Ped>& c) { return c->removed; }), crew.end());
	const bool aboard = CargoShip::Aboard(p.pos.x, p.pos.y, p.pos.z) && !p.dead;
	// trespass: a warning, then they come for you (at once if you've a gun out)
	if (aboard && !alerted) {
		if (!warned) {
			warned = true; aboardT = 0;
			if (game.hud) {
				std::string upper = SHIP.name; for (char& ch : upper) ch = (char)std::toupper((unsigned char)ch);
				game.hud->bigMessage(upper, "failed", 3, "Crew only");
				game.hud->help(std::string("You're aboard the <b>") + SHIP.name + "</b>. Security wants you off the ship. Draw a gun or go near the bridge and they'll open fire.", 7);
			}
			if (Ped* sec = nearestCrew(p.pos, "security")) { sec->say("Hey! Off the ship. Now!"); sec->threat = nullptr; }
		}
		aboardT += dt;
		const bool armed = p.aiming && p.weaponDef().type != "melee";
		const bool bridge = p.pos.y > SHIP.levels[1] - 0.5 && p.pos.z - SHIP.z > 50;
		if (armed || bridge || aboardT > 25) alert(armed ? "He's armed!" : bridge ? "Intruder at the bridge!" : "");
		// security turns to watch you
		for (auto& c : crew) if (!c->dead && RoleOf(c.get()) == "security" && c->state == "guard" && Dist2(c->pos.x, c->pos.z, p.pos.x, p.pos.z) < 30 * 30) { c->hasGuardFace = true; c->guardFace = std::atan2(p.pos.x - c->pos.x, p.pos.z - c->pos.z); }
	}
	if (!aboard && warned && !alerted && d > 60) warned = false;
	// the coast guard
	if (radioT > 0) {
		radioT -= dt;
		if (radioT <= 0) {
			Ped* cap = nullptr;
			for (auto& c : crew) if (RoleOf(c.get()) == "captain") { cap = c.get(); break; }
			if (cap && !cap->dead) { if (game.policeSys) game.policeSys->raise(2); if (game.hud) game.hud->dispatch(std::string(SHIP.name) + ": armed boarders, Port Morena", "Coast guard"); }
		}
	}
	// patrols along the side decks
	for (auto& c : crew) {
		if (c->dead || !c->num.count("patrolZ0") || c->state == "attack" || c->state == "flee") continue;
		if (c->state == "guard") {
			if (!c->num.count("patrolT")) c->num["patrolT"] = Rand(2, 6);
			c->num["patrolT"] -= dt;
			if (c->num["patrolT"] <= 0) {
				c->num["patrolT"] = Rand(6, 14);
				double x, z; CargoShip::ToWorld(c->num["postX"], Rand(c->num["patrolZ0"], c->num["patrolZ1"]), x, z);
				c->targetPos = V3(x, 0, z); c->gotoSpeed = 1.3; c->afterGoto = "guard"; c->setState("goto");
			}
		}
	}
	// cracking the safe (stand at it)
	if (safeMarker && !safeDone) {
		const ShipPoint& sp = ship.safe;
		const bool inside = Dist2(p.pos.x, p.pos.z, sp.x, sp.z) < 1.6 * 1.6 && std::fabs(p.pos.y - sp.y) < 1.2 && !p.vehicle && !p.dead;
		if (inside) {
			if (safeK == 0) { if (game.hud) game.hud->help("Cracking the safe... stay put.", 3); alert("The safe!"); }
			safeK = Min(1.0, safeK + dt / 6);
			const std::string label = "CRACKING SAFE";
			if (game.hud) game.hud->setBar(&label, safeK, "#6cff6c");
			if (Rand() < dt * 4) game.soundAt("clink", V3(sp.x, sp.y, sp.z), 0.3);
			if (safeK >= 1) openSafe();
		} else if (safeK > 0 && !(game.missions && game.missions->active)) { if (game.hud) game.hud->setBar(nullptr); }
	}
}

Ped* ShipRaid::nearestCrew(const V3& pos, const std::string& role) {
	Ped* best = nullptr; double bd = kInf;
	for (auto& c : crew) { if (c->dead || (!role.empty() && RoleOf(c.get()) != role)) continue; const double d = Dist2(c->pos.x, c->pos.z, pos.x, pos.z); if (d < bd) { bd = d; best = c.get(); } }
	return best;
}

void ShipRaid::spawn() {
	if (!game.peds) return;
	spawned = true; alerted = false; warned = false; safeK = 0; safeDone = false; lootLeft = 0;
	game.peds->gangAggro["crew"] = false;
	RNG rng((uint32_t)(int64_t)std::floor(Rand() * 1e9));
	for (const Post& post : POSTS) {
		double x, z; CargoShip::ToWorld(post.lx, post.lz, x, z);
		const double y = post.lvl < 0 ? SHIP.deckY + 2.4 : SHIP.levels[post.lvl];
		const std::string role = post.role;
		const std::string weapon = role == "captain" ? "pistol" : role == "security" ? rng.Pick(std::vector<std::string>{ "smg", "shotgun", "rifle", "smg" }) : rng.Pick(std::vector<std::string>{ "pistol", "bat", "knife", "pistol" });
		PedOpts o;
		o.hasAppearance = true; o.appearance = CrewAppearance(role, rng);
		o.brain = "gang"; o.gang = "crew"; o.state = "guard"; o.weapon = weapon;
		o.health = role == "captain" ? 160 : role == "security" ? 130 : 100; o.armor = role == "security" ? 40 : 0; o.persistent = true;
		Ped* ped = game.peds->spawnPed(x, z, o);
		if (!ped) continue;
		ped->setPosition(x, y, z);
		ped->hasGuardFace = true; ped->guardFace = post.face; ped->yaw = post.face;
		ped->str["role"] = role;
		ped->accuracy = role == "security" ? 0.5 : 0.35;
		ped->damageMul = role == "security" ? 0.6 : 0.45;
		ped->moneyDrop = role == "captain" ? 600 : Rand(40, 180);
		if (post.patrol) { ped->num["patrolZ0"] = post.z0; ped->num["patrolZ1"] = post.z1; ped->num["postX"] = post.lx; }
		crew.push_back(std::static_pointer_cast<Ped>(ped->shared_from_this()));
	}
	// the safe on the bridge, and three containers of contraband off the cross walkways
	const ShipPoint& sf = ship.safe;
	MarkerOpts so; so.hasY = true; so.y = sf.y; so.color = 0x6cff6c; so.radius = 0.9; so.height = 1.2; so.icon = "money"; so.label = "Captain's safe"; so.footOnly = true; so.blip = false;
	safeMarker = game.pickupsSys->addMarker(sf.x, sf.z - 0.6, so);
	markers.push_back(safeMarker);
	std::vector<ShipStack> pool;
	for (const ShipStack& s : ship.stacks) if (std::abs(s.row) == 1 && std::fabs(s.lx) < 9) pool.push_back(s);
	std::vector<ShipStack> picks;
	for (int n = 0; n < 3 && !pool.empty(); n++) { const int i = rng.Int(0, (int)pool.size() - 1); picks.push_back(pool[i]); pool.erase(pool.begin() + i); }
	for (const ShipStack& st : picks) {
		const double face = st.row > 0 ? 1 : -1;
		const double gz = st.lz + face * 3.6; // the door end, facing a cross walkway
		double x, z; CargoShip::ToWorld(st.lx, gz, x, z);
		MarkerOpts mo; mo.hasY = true; mo.y = SHIP.deckY; mo.color = 0xffb000; mo.radius = 1.0; mo.height = 1.4; mo.icon = "weapon"; mo.label = "Contraband"; mo.footOnly = true; mo.blip = false;
		mo.onEnter = [this, x, z](Marker* m) { openContainer(m, x, z); };
		markers.push_back(game.pickupsSys->addMarker(x, z, mo));
		lootLeft++;
	}
}

void ShipRaid::openContainer(Marker* m, double x, double z) {
	const double y = SHIP.deckY;
	const V3 at = m->pos;
	game.pickupsSys->removeMarker(m);
	markers.erase(std::remove(markers.begin(), markers.end(), m), markers.end());
	game.soundAt("metalhit", at, 0.9);
	const int cash = (int)std::floor(Rand(1500, 3200) + 0.5);
	PickupData money; money.amount = cash; money.hasY = true; money.y = y; money.life = 120;
	game.pickupsSys->spawn("money", x + Rand(-0.6, 0.6), z + Rand(-0.6, 0.6), money);
	const std::string w = RandPick(std::vector<std::string>{ "rifle", "shotgun", "smg", "grenade", "rpg" });
	PickupData gun; gun.weapon = w; gun.ammo = w == "rpg" ? 4 : w == "grenade" ? 6 : 90; gun.hasY = true; gun.y = y; gun.life = 120;
	game.pickupsSys->spawn("weapon", x + Rand(-0.8, 0.8), z + Rand(-0.8, 0.8), gun);
	if (Rand() < 0.5) { PickupData armor; armor.hasY = true; armor.y = y; armor.life = 120; game.pickupsSys->spawn("armor", x + Rand(-0.8, 0.8), z + Rand(-0.8, 0.8), armor); }
	if (game.hud) game.hud->help("Contraband: <b>$" + Thousands(cash) + "</b> and a " + w + ".", 3);
	alert();
	lootLeft--;
	checkDone();
}

void ShipRaid::openSafe() {
	const ShipPoint sp = ship.safe;
	safeDone = true;
	game.pickupsSys->removeMarker(safeMarker);
	markers.erase(std::remove(markers.begin(), markers.end(), safeMarker), markers.end());
	safeMarker = nullptr;
	if (game.hud) game.hud->setBar(nullptr);
	const double total = std::floor(Rand(12000, 18000) / 100 + 0.5) * 100;
	lastSafe = total;
	for (int i = 0; i < 4; i++) { PickupData d; d.amount = total / 4; d.hasY = true; d.y = sp.y; d.life = 180; game.pickupsSys->spawn("money", sp.x + Rand(-1, 1), sp.z - Rand(0.8, 1.8), d); }
	if (game.hud) game.hud->bigMessage("SAFE CRACKED", "passed", 3, "The payroll: $" + Thousands((long long)total));
	if (game.audio) game.audio->play("cash", 0.6);
	game.events.shipSafe.emit(total);
	checkDone();
}

void ShipRaid::checkDone() {
	if (!safeDone || lootLeft > 0) return;
	lootedAt = game.time;
	game.stats.shipRaids++;
	if (game.hud) game.hud->bigMessage("SHIP RAIDED", "passed", 3.5, "Now get off the ship");
	game.events.shipRaided.emit();
}

void ShipRaid::despawn() {
	for (auto& c : crew) if (!c->removed) game.peds->remove(c.get());
	crew.clear();
	for (Marker* m : markers) game.pickupsSys->removeMarker(m);
	markers.clear(); safeMarker = nullptr;
	spawned = false; alerted = false; warned = false;
	game.peds->gangAggro["crew"] = false;
}

} // namespace atg
