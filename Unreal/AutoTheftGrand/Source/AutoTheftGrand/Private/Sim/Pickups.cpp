#include "Pickups.h"
#include "Collision.h"
#include "Game.h"
#include "Police.h"
#include "Weapons.h"

namespace atg {

namespace {
// three.js Color.setHSL (h, s, l in sRGB) -> a hex colour
uint32_t HslHex(double h, double s, double l) {
	h = h - std::floor(h); s = Clamp(s, 0, 1); l = Clamp(l, 0, 1);
	auto hue2rgb = [](double p, double q, double t) {
		if (t < 0) t += 1;
		if (t > 1) t -= 1;
		if (t < 1.0 / 6) return p + (q - p) * 6 * t;
		if (t < 1.0 / 2) return q;
		if (t < 2.0 / 3) return p + (q - p) * 6 * (2.0 / 3 - t);
		return p;
	};
	double r = l, g = l, b = l;
	if (s != 0) {
		const double p = l <= 0.5 ? l * (1 + s) : l + s - l * s;
		const double q = 2 * l - p;
		r = hue2rgb(q, p, h + 1.0 / 3); g = hue2rgb(q, p, h); b = hue2rgb(q, p, h - 1.0 / 3);
	}
	auto c = [](double v) { return (uint32_t)std::round(Clamp(v * 255, 0, 255)); };
	return (c(r) << 16) | (c(g) << 8) | c(b);
}

uint32_t GlowColor(const std::string& kind) {
	if (kind == "money") return 0x44ff44;
	if (kind == "health") return 0xff4444;
	if (kind == "armor") return 0x4488ff;
	if (kind == "weapon") return 0xffcc44;
	return 0xffddaa; // package
}

std::string Num(double v) { char b[32]; snprintf(b, sizeof b, "%g", v); return b; }
}

// ------------------------------------------------------------------ Marker
Marker::Marker(Game& g, double x, double z, const MarkerOpts& o) : game(g) {
	pos = V3(x, o.hasY ? o.y : g.map.GroundHeight(x, z), z);
	radius = o.radius;
	color = o.color;
	height = IsSet(o.height) ? o.height : (radius > 3 ? 3 : 1.8);
	arrow = o.arrow;
	arrowY = height + 1.2;
	if (o.blip) { Blip b; b.x = x; b.z = z; b.color = color; b.icon = o.icon.empty() ? "dot" : o.icon; b.label = o.label; b.marker = true; blip = g.addBlip(b); }
	onEnter = o.onEnter;
	vehicleOnly = o.vehicleOnly;
	footOnly = o.footOnly;
}

void Marker::setPos(double x, double z) {
	pos = V3(x, game.map.GroundHeight(x, z), z);
	if (blip) { blip->x = x; blip->z = z; }
}

void Marker::update(double dt, double t) {
	time = t;
	if (arrow) { arrowY = height + 1.2 + std::sin(t * 3) * 0.2; arrowSpin += dt * 2; }
	Player& pl = *game.player;
	const V3 p = pl.vehicle ? pl.vehicle->pos : pl.pos;
	const bool ok = (!vehicleOnly || pl.vehicle) && (!footOnly || !pl.vehicle) && !pl.dead;
	const double r = radius + (pl.vehicle ? 0.8 : 0);
	const bool in = ok && Dist2(p.x, p.z, pos.x, pos.z) < r * r && std::fabs(p.y - pos.y) < 4;
	if (in && !inside) { inside = true; if (onEnter) { auto f = onEnter; f(this); } }
	else if (!in) inside = false;
	// hide when very far
	visible = Dist2(p.x, p.z, pos.x, pos.z) < 400 * 400;
}

void Marker::remove() {
	if (removed) return;
	removed = true;
	if (blip) game.removeBlip(blip);
}

// ------------------------------------------------------------------ Pickup
Pickup::Pickup(Game& g, const std::string& k, const V3& p, const PickupData& d) : game(g), kind(k), data(d), pos(p) {
	pos.y = d.hasY ? d.y : g.map.GroundHeight(p.x, p.z);
	glow = GlowColor(k);
	t = Rand() * 6;
	life = d.life;
	respawn = d.respawn;
	if (d.blip) { Blip b; b.x = p.x; b.z = p.z; b.color = glow; b.icon = k; b.small = true; blip = g.addBlip(b); }
}

void Pickup::update(double dt) {
	t += dt;
	spin += dt * 2;
	bob = 0.7 + std::sin(t * 2.5) * 0.1;
	life -= dt;
}

void Pickup::remove() { removed = true; if (blip) { game.removeBlip(blip); blip.reset(); } }

// ------------------------------------------------------------------ Pickups
Pickups::Pickups(Game& g) : game(g) { setupWorld(); }

Marker* Pickups::addMarker(double x, double z, const MarkerOpts& opts) {
	markers.push_back(std::make_shared<Marker>(game, x, z, opts));
	return markers.back().get();
}

void Pickups::removeMarker(Marker* m) {
	if (!m) return;
	m->remove();
	for (size_t i = 0; i < markers.size(); i++) if (markers[i].get() == m) { markers.erase(markers.begin() + i); break; }
}

void Pickups::dropMoney(const V3& pos, int amount) {
	if (amount <= 0) return;
	const double dx = Rand(-0.4, 0.4), dz = Rand(-0.4, 0.4);
	PickupData d; d.amount = amount; d.life = 40;
	if (game.collision) { d.hasY = true; d.y = game.collision->floorHeight(pos.x, pos.z, pos.y + 0.3); }
	list.push_back(std::make_shared<Pickup>(game, "money", pos + V3(dx, 0, dz), d));
}

void Pickups::dropWeapon(const V3& pos, const std::string& weapon, int ammo) {
	if (!FindWeapon(weapon) || weapon == "fist") return;
	const double dx = Rand(-0.6, 0.6), dz = Rand(-0.6, 0.6);
	PickupData d; d.weapon = weapon; d.ammo = ammo; d.life = 45;
	if (game.collision) { d.hasY = true; d.y = game.collision->floorHeight(pos.x, pos.z, pos.y + 0.3); }
	list.push_back(std::make_shared<Pickup>(game, "weapon", pos + V3(dx, 0, dz), d));
}

Pickup* Pickups::spawn(const std::string& kind, double x, double z, const PickupData& data) {
	list.push_back(std::make_shared<Pickup>(game, kind, V3(x, 0, z), data));
	return list.back().get();
}

void Pickups::setupWorld() {
	const auto& L = game.map.landmarks;
	auto at = [&](const char* n) -> const Landmark& { return L.at(n); };
	// world pickups (respawning)
	struct Fixed { const char* kind; double x, z; const char* weapon; double ammo; };
	const Fixed fixed[] = {
		{ "health", at("hospital").x - 6, at("hospital").z - 3, nullptr, 0 }, { "armor", at("police").x + 12, at("police").z + 1, nullptr, 0 },
		{ "health", at("court").x + 8, at("court").z + 12, nullptr, 0 }, { "weapon", at("projects").x, at("projects").z + 4, "bat", 1 },
		{ "weapon", at("plaza").x + 20, at("plaza").z, "pistol", 34 }, { "armor", at("warehouse").x, at("warehouse").z - 30, nullptr, 0 },
		{ "weapon", at("beach").x + 30, at("beach").z + 12, "knife", 1 }, { "health", at("burger").x, at("burger").z - 2, nullptr, 0 },
		{ "weapon", at("docksQuay").x - 40, at("docksQuay").z + 80, "shotgun", 14 }, { "weapon", at("mansion").x + 30, at("mansion").z - 40, "smg", 64 },
	};
	for (const Fixed& f : fixed) {
		PickupData d; d.respawn = 90;
		if (f.weapon) { d.weapon = f.weapon; d.ammo = f.ammo; }
		list.push_back(std::make_shared<Pickup>(game, f.kind, V3(f.x, 0, f.z), d));
	}
	// hidden packages
	RNG rng(2024);
	packageSpots.clear();
	const auto& blocks = game.map.blocks;
	for (int i = 0; i < 30; i++) {
		const Block& b = game.map.blockStore[blocks[rng.Int(0, (int)blocks.size() - 1)]];
		const double x = rng.Range(b.ix0 + 1, b.ix1 - 1), z = rng.Range(b.iz0 + 1, b.iz1 - 1);
		const bool blocked = game.collision->resolveCircle(x, z, 0.8, 0.2, 1).hit != nullptr;
		if (blocked) { i--; continue; }
		packageSpots.push_back({ i, x, z });
	}
	packages.clear();
	// services
	// (the Gun Barn, Big Bun Burgers and Ray's Liquor are served at their counters: see shops.js)
	for (const char* key : { "spray", "spray2" }) {
		const Landmark& s = at(key);
		MarkerOpts o; o.color = 0x6df0ff; o.radius = 3.5; o.height = 2.5; o.icon = "spray"; o.label = "Spray Shack"; o.vehicleOnly = true; o.arrow = false;
		o.onEnter = [this](Marker*) { spray(); };
		services.push_back(addMarker(s.x, s.z, o));
	}
	const P3 door = at("home").pts.at("door");
	MarkerOpts o; o.color = 0x6cff6c; o.radius = 1.0; o.icon = "house"; o.label = "Safehouse (save)"; o.footOnly = true;
	o.onEnter = [this](Marker*) { if (game.hud) game.hud->promptSave(); };
	saveMarker = addMarker(door.x, door.z + 1.2, o);
	services.push_back(saveMarker);
}

void Pickups::removeFromList(Pickup* p) {
	for (size_t i = 0; i < list.size(); i++) if (list[i].get() == p) { list.erase(list.begin() + i); return; }
}

void Pickups::refreshPackages() {
	// (the browser game leaves the old ones in its list, still collectable but unseen, after loading a save;
	// here they go)
	for (const auto& p : packages) { p->remove(); removeFromList(p.get()); }
	packages.clear();
	for (const Spot& s : packageSpots) {
		if (collectedPackages.count(s.id)) continue;
		PickupData d; d.packageId = s.id;
		spawn("package", s.x, s.z, d);
		packages.push_back(list.back());
	}
}

void Pickups::spray() {
	Vehicle* v = game.player->vehicle;
	if (!v || v->isWrecked()) return;
	if (game.player->money < 100) { if (game.hud) game.hud->help("The Spray Shack costs $100. Come back when you have the cash."); return; }
	if (game.missionNoSpray) { if (game.hud) game.hud->help("Not now \xe2\x80\x94 you're on a job."); return; }
	game.player->money -= 100;
	Ref<Vehicle> ref(v);
	auto paint = [this, ref]() {
		Vehicle* v = ref.get();
		if (!v) return;
		RandInt(0, (int)v->def.colors.size() - 1); // (the browser game picks a catalogue colour and doesn't use it)
		const double h = Rand(), s = Rand(0.4, 0.8), l = Rand(0.25, 0.55);
		v->color = HslHex(h, s, l);
		v->painted = true;
		v->health = 1000; v->onFire = false; v->burnTime = 0;
		if (game.policeSys) game.policeSys->clear();
		game.sound("cash");
		if (game.hud) game.hud->help("New paint job, fixed up and the cops won't recognise you. $100.");
		game.stats.sprays++;
	};
	if (game.hud) game.hud->fade(0.6, paint); else paint();
}

void Pickups::eat() {
	Player& p = *game.player;
	if (p.money < 10) { if (game.hud) game.hud->help("A Big Bun Combo is $10."); return; }
	if (p.health >= p.maxHealth) { if (game.hud) game.hud->help("You're not hungry right now."); return; }
	p.money -= 10;
	p.health = p.maxHealth;
	game.sound("pickup");
	if (game.hud) game.hud->help("Big Bun Double Stack. Health restored. $10.");
}

void Pickups::update(double dt) {
	t += dt;
	{ const auto ms = markers; for (const auto& m : ms) if (!m->removed) m->update(dt, t); }
	Player& pl = *game.player;
	const V3 pp = pl.vehicle ? pl.vehicle->pos : pl.pos;
	for (int i = (int)list.size() - 1; i >= 0; i--) {
		if (i >= (int)list.size()) continue; // (a collect can change the list)
		std::shared_ptr<Pickup> p = list[i];
		if (p->hiddenUntil > t) continue;
		if (!p->visible && p->hiddenUntil) p->visible = true;
		p->update(dt);
		if (p->life <= 0) { p->remove(); list.erase(list.begin() + i); continue; }
		if (pl.dead) continue;
		const double r = pl.vehicle ? 2.2 : 1.1;
		if (Dist2(p->pos.x, p->pos.z, pp.x, pp.z) < r * r && std::fabs(pp.y - p->pos.y) < 2.5) {
			if (collect(p.get())) {
				if (p->respawn) { p->hiddenUntil = t + p->respawn; p->visible = false; }
				else { p->remove(); removeFromList(p.get()); }
			}
		}
	}
}

bool Pickups::collect(Pickup* p) {
	Player& pl = *game.player;
	if (p->kind == "money") { pl.money += p->data.amount; game.sound("cash"); if (game.hud) game.hud->moneyFlash(p->data.amount); return true; }
	if (p->kind == "health") { if (pl.health >= pl.maxHealth) return false; pl.health = pl.maxHealth; game.sound("pickup"); return true; }
	if (p->kind == "armor") { if (pl.armor >= 100) return false; pl.armor = 100; game.sound("pickup"); return true; }
	if (p->kind == "weapon") {
		if (pl.vehicle) return false;
		const WeaponDef* def = FindWeapon(p->data.weapon);
		if (!def) return false;
		const bool had = pl.weapons.count(p->data.weapon) > 0;
		const bool melee = def->type == "melee";
		pl.giveWeapon(p->data.weapon, melee ? 0 : p->data.ammo);
		const WeaponDef* cur = FindWeapon(pl.weapon);
		if (!had && (pl.weapon == "fist" || def->slot > (cur ? cur->slot : 0))) pl.switchTo(p->data.weapon);
		game.sound("pickup");
		if (game.hud) game.hud->help(def->name + (melee ? std::string() : " +" + Num(p->data.ammo) + " rounds"), 2);
		return true;
	}
	if (p->kind == "package") {
		collectedPackages.insert(p->data.packageId);
		const int n = (int)collectedPackages.size();
		pl.money += 100;
		game.sound("passed", 0.6);
		if (game.hud) {
			game.hud->bigMessage("HIDDEN PACKAGE " + std::to_string(n) + " OF " + std::to_string(packageSpots.size()), "hint", 3);
			if (n == 10) game.hud->help("10 packages: a Micro SMG will now spawn at your safehouse.");
			if (n == 20) game.hud->help("20 packages: an Assault Rifle will now spawn at your safehouse.");
		}
		if (n == 30) { if (game.hud) game.hud->help("All packages found! A Rocket Launcher waits at your safehouse. You're a legend."); pl.money += 50000; }
		spawnSafehouseRewards();
		return true;
	}
	return false;
}

void Pickups::spawnSafehouseRewards() {
	const int n = (int)collectedPackages.size();
	const Landmark& h = game.map.landmarks.at("home");
	for (const auto& p : rewardPickups) { p->remove(); removeFromList(p.get()); }
	rewardPickups.clear();
	auto add = [&](const char* w, double ammo, double dx) {
		PickupData d; d.weapon = w; d.ammo = ammo; d.respawn = 60;
		spawn("weapon", h.x + dx, h.z + 2, d);
		rewardPickups.push_back(list.back());
	};
	if (n >= 10) add("smg", 96, -2);
	if (n >= 20) add("rifle", 90, 0);
	if (n >= 30) add("rpg", 5, 2);
}

} // namespace atg
