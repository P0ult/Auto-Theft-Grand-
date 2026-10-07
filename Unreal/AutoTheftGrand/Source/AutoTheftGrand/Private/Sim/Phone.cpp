#include "Phone.h"
#include "Collision.h"
#include "Game.h"
#include "Gameplay.h"
#include "Peds.h"
#include "Police.h"
#include "RoadNet.h"
#include "Special.h"
#include "WeaponWheel.h"
#include "Weapons.h"

namespace atg {

namespace {
const double CHEAT_TIME = 300; // seconds a timed cheat lasts outside free roam
const char* DOT = " \xc2\xb7 "; // ( · )

// '$' + Math.round(n).toLocaleString('en-US')
std::string Money(double n) {
	long long v = (long long)std::floor(n + 0.5);
	const bool neg = v < 0; if (neg) v = -v;
	std::string s = std::to_string(v), out;
	for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) out += ','; out += s[i]; }
	return std::string("$") + (neg ? "-" : "") + out;
}

const std::vector<Phone::App>& APPS() {
	static const std::vector<Phone::App> a = {
		{ "contacts", "Contacts", "\xe2\x98\x8e", 0x2fa84f },
		{ "cheats", "Cheats", "#", 0xb33a3a },
		{ "camera", "Snapmatic", "\xe2\x97\x89", 0xe08a1e },
		{ "map", "Map", "\xe2\x8c\x96", 0x2c79d6 },
		{ "weather", "Weather", "\xe2\x98\x80", 0xd9b82b },
		{ "stats", "Stats", "\xe2\x96\xa4", 0x7a4cc2 },
	};
	return a;
}

std::string VehName(const std::string& type) { const VehicleDef* d = FindVehicle(type); return d ? d->name : type; }

// the timed cheats' keys as the game's cheat switches
void ApplyCheat(Cheats& c, const std::string& key) {
	if (key == "god") c.god = true;
	else if (key == "superJump") c.superJump = true;
	else if (key == "superRun") c.superRun = true;
	else if (key == "explosive") c.explosive = true;
}
}

const std::vector<std::pair<std::string, std::string>> CHEATS = {
	{ "PAINKILLER", "Invincibility" },
	{ "TURTLE", "Max health and armour" },
	{ "TOOLUP", "Every weapon, plenty of ammo" },
	{ "LAWYERUP", "Lose your wanted level" },
	{ "FUGITIVE", "Raise your wanted level" },
	{ "SKYFALL", "Skydive from 400 m" },
	{ "BUZZOFF", "Spawn a Warhawk gunship" },
	{ "COMET", "Spawn a Zenith supercar" },
	{ "OFFROAD", "Spawn a Trailblazer dirt bike" },
	{ "HOPTOIT", "Super jump" },
	{ "CATCHME", "Run twice as fast" },
	{ "HIGHEX", "Explosive bullets" },
	{ "POWERUP", "Refill the special ability" },
	{ "SLOWMO", "Slow motion on / off" },
	{ "MAKEITRAIN", "Change the weather" },
};

// ------------------------------------------------------------------ freeroam.js roadSpot
bool FindRoadSpot(Game& g, double x, double z, bool onFoot, RoadSpot& out) {
	const RoadNet& net = g.map.roads;
	EdgeHit c = net.Closest(x, z, [onFoot](const REdge& e) { return !e.removed && e.type != ERoad::Rail && (!onFoot || e.T->cls < 2); }, 600);
	if (!c.valid()) c = net.Closest(x, z, [](const REdge& e) { return !e.removed && e.type != ERoad::Rail; }, 1500);
	if (!c.valid()) return false;
	const REdge& e = net.edges[c.e];
	const double s = Min(Max(c.s, 6), e.len - 6);
	const EdgePoint at = net.At(e, s);
	double rx = at.x, ry = at.y, rz = at.z, tx = at.tx, tz = at.tz;
	// kerbside lane, travelling with the traffic (one-way edges may only run b -> a)
	const bool back = !(e.lanesF > 0) && e.lanesB > 0;
	if (back) { tx = -tx; tz = -tz; }
	const int nl = back ? e.lanesB : e.lanesF;
	const double laneOff = nl > 0 ? net.LaneOffset(e, back ? 1 : 0, nl - 1) : 0;
	const double yaw = std::atan2(tx, tz);
	const RoadSpot lane{ rx - tz * laneOff, rz + tx * laneOff, ry, yaw };
	if (!onFoot) { out = lane; return true; }
	// pavement: just past the right-hand edge of the road
	// city streets have a pavement inside the paved width; elsewhere step onto the verge
	const double w = back ? e.wL : e.wR, laneEdge = e.T->off0 + e.T->laneW * Max(1, nl);
	const double off = w - laneEdge > 2 ? (w + laneEdge) / 2 + 0.4 : w + 1.2;
	const double px = rx - tz * off, pz = rz + tx * off;
	const double fy = g.collision->floorHeight(px, pz, ry + 1);
	const auto res = g.collision->resolveCircle(px, pz, 0.45, fy, 1.8);
	if (std::fabs(fy - ry) < 1.2 && !res.hit) { out = { px, pz, fy, yaw }; return true; }
	out = lane;
	return true;
}

// ------------------------------------------------------------------ the phone
Phone::Phone(Game& g) : game(g) {
	// wasted or busted: the contractors leave and any call in progress is dropped
	auto off = [this]() { reset(true); };
	g.events.playerDied.on(off);
	g.events.busted.on(off);
}

std::map<std::string, bool> Phone::activeCheats() {
	std::map<std::string, bool> out;
	for (auto it = timed.begin(); it != timed.end();) {
		if (it->second > game.time) { out[it->first] = true; ++it; }
		else it = timed.erase(it);
	}
	return out;
}

bool Phone::canOpen() const {
	const Player& p = *game.player;
	return game.gameplay && game.gameplay->state == "playing" && !game.cutscene && !game.menuOpen && !p.dead && game.input.enabled && !(game.wheel && game.wheel->open);
}

// before the player's controls read the keys: the phone takes the arrows, Enter and Backspace (and the D-pad, A
// and B) while it's out, so you can still walk with WASD / the left stick
void Phone::earlyInput(double) {
	Input& input = game.input;
	const Player& p = *game.player;
	auto take = [&](const char* code) { const bool hit = input.pressed.count(code) > 0; input.pressed.erase(code); input.keys.erase(code); return hit; };
	auto pad = [&](int i) { const bool hit = input.gpHit(i); input.mask.insert(i); return hit; };
	if (photo) {
		const bool out = take("KeyI") | take("Backspace") | take("Escape") | pad(GP::B) | pad(GP::UP);
		if (out || !canOpen()) setPhoto(false);
		return;
	}
	if (!open) {
		// D-pad up takes it out on foot, and in vehicles without hydraulics
		const bool padOpen = (!p.vehicle || !p.vehicle->def.hydraulics) && input.gpHit(GP::UP);
		if ((input.keyHit("KeyI") || padOpen) && canOpen()) { take("KeyI"); if (padOpen) input.mask.insert(GP::UP); show(); }
		return;
	}
	if (take("KeyI") | take("Escape")) { hide(); return; }
	if (!canOpen()) { hide(); return; }
	const bool up = take("ArrowUp") | pad(GP::UP), down = take("ArrowDown") | pad(GP::DOWN);
	const bool left = take("ArrowLeft") | pad(GP::LEFT), right = take("ArrowRight") | pad(GP::RIGHT);
	const bool ok = take("Enter") | pad(GP::A), back = take("Backspace") | pad(GP::B);
	const int n = (int)items.size();
	const int cols = screen == "home" ? 3 : 1;
	if (n) {
		if (up) sel = (sel - cols + n) % n;
		if (down) sel = (sel + cols) % n;
		if (left && cols > 1) sel = (sel - 1 + n) % n;
		if (right && cols > 1) sel = (sel + 1) % n;
		if (up || down || left || right) game.sound("ui", 0.5);
		if (ok) { game.sound("ui", 1); if (sel < (int)items.size() && items[sel].go) { auto f = items[sel].go; f(); } }
	}
	if (back) { if (screen == "home") hide(); else go("home"); }
}

void Phone::show() {
	open = true;
	game.phoneOpen = true;
	game.sound("ui", 1);
	go("home");
}

void Phone::hide() {
	open = false;
	game.phoneOpen = false;
}

void Phone::go(const std::string& s) {
	screen = s;
	sel = 0;
	build();
}

// ------------------------------------------------------------------ screens
void Phone::build() {
	const bool free = game.freeroamActive();
	items.clear();
	if (screen == "home") {
		for (const App& a : APPS()) { Item it; it.name = a.name; it.icon = a.icon; it.color = a.color; const std::string id = a.id; it.go = [this, id]() { app(id); }; items.push_back(it); }
	} else if (screen == "contacts") {
		const std::string pay = free ? "" : std::string(DOT) + "$200";
		const int lvl = game.police ? game.police->wantedLevel() : 0;
		items.push_back({ "Downtown Cab Co.", "A cab comes to you", "", 0, [this]() { call("Downtown Cab Co.", [this]() { cab(); }); } });
		items.push_back({ "Benny's Motorworks", hasLastCar ? "Deliver your " + VehName(lastCarType) + pay : "Deliver a car" + pay, "", 0, [this]() { call("Benny", [this]() { mechanic(); }); } });
		items.push_back({ "Lester", lvl ? "Lose your " + std::to_string(lvl) + "-star wanted level" + (free ? std::string() : std::string(DOT) + Money(lesterPrice())) : "Makes the cops lose your file", "", 0, [this]() { call("Lester", [this]() { lester(); }); } });
		items.push_back({ "Merryweather Security", std::string("Three armed contractors") + (free ? "" : std::string(DOT) + "$1,000"), "", 0, [this]() { call("Merryweather", [this]() { merryweather(); }); } });
		if (free) items.push_back({ "Pegasus Concierge", "A Skylark helicopter dropped off nearby", "", 0, [this]() { call("Pegasus", [this]() { pegasus(); }); } });
	} else if (screen == "cheats") {
		for (const auto& c : CHEATS) { const std::string code = c.first; items.push_back({ c.first, c.second, "", 0, [this, code]() { cheat(code); } }); }
	} else if (screen == "weather") {
		Environment& env = game.env;
		if (free) for (const char* w : { "clear", "cloudy", "rain", "storm", "fog" }) {
			std::string name = w; name[0] = (char)std::toupper(name[0]);
			const std::string ws = w;
			// (admin.js setWeather: with the admin tools; until then the clock's own)
			items.push_back({ name, env.weather == w ? "now" : "", "", 0, [this, ws]() { game.env.setWeather(ws, true); build(); } });
		}
		else items.push_back({ "Forecast: " + env.weather, "Change the weather in free roam, or with the MAKEITRAIN cheat", "", 0, []() {} });
	} else if (screen == "stats") {
		const auto& st = game.stats;
		auto num = [](double v) { char b[32]; snprintf(b, sizeof b, "%g", v); return std::string(b); };
		const std::vector<std::pair<std::string, std::string>> rows = {
			{ "Kills", num(st.kills) }, { "Headshots", num(st.headshots) }, { "Cars stolen", num(st.carsStolen) }, { "Highest wanted level", num(st.maxWanted) + " \xe2\x98\x85" },
			{ "Missions passed", num(st.missions) }, { "Best drift", num(std::floor(st.bestDrift)) + " m" }, { "Times wasted", num(st.wasted) }, { "Times busted", num(st.busted) },
		};
		for (const auto& r : rows) items.push_back({ r.first, r.second, "", 0, []() {} });
	}
}

std::string Phone::statusTime() const { return game.env.timeString(); }

void Phone::app(const std::string& id) {
	if (id == "camera") { hide(); setPhoto(true); return; }
	if (id == "map") { hide(); if (game.hud) game.hud->openPause(game.freeroamActive() ? "teleport" : "map"); return; }
	go(id);
}

void Phone::setPhoto(bool on) {
	photo = on;
	game.photoMode = on;
	if (on) game.sound("ui", 1);
}

// ------------------------------------------------------------------ calls
void Phone::call(const std::string& who, std::function<void()> fn) {
	hide();
	if (game.hud) game.hud->help("Calling <b>" + who + "</b>\xe2\x80\xa6", 2);
	pending.push_back({ game.time + 1.6, std::move(fn) });
}

bool Phone::pay(double amount) {
	Player& p = *game.player;
	if (game.freeroamActive() || amount <= 0) return true;
	if (p.money < amount) return false;
	p.money -= amount;
	game.sound("cash");
	return true;
}

void Phone::cab() {
	if (game.player->vehicle) { if (game.hud) game.hud->subtitle("You're already driving, pal. Call us when you're on foot.", "Downtown Cab Co.", 4); return; }
	// (the taxi system comes with the taxis; until then the dispatcher doesn't pick up)
	if (!game.taxiSystem) return;
	if (game.hud) game.hud->subtitle("Downtown Cab Co., where to? We'll send someone right over.", "Dispatcher", 3.5);
}

double Phone::lesterPrice() const { const int l = game.police ? game.police->wantedLevel() : 0; return 500 * (l ? l : 1); }

void Phone::lester() {
	Police* pol = game.policeSys;
	if (!pol || pol->level == 0) { if (game.hud) game.hud->subtitle("You're not wanted. Why are you calling me? Don't answer that.", "Lester", 4); return; }
	if (game.missionActive && IsSet(game.missionMaxWanted)) { if (game.hud) game.hud->subtitle("Not while you're on a job. They'll trace it straight back to me.", "Lester", 4); return; }
	if (!pay(lesterPrice())) { if (game.hud) game.hud->subtitle("Clean records cost money. " + Money(lesterPrice()) + ". Call me back.", "Lester", 4); return; }
	if (game.hud) game.hud->subtitle("Give me a minute. I'm in the dispatch system now\xe2\x80\xa6", "Lester", 3.5);
	pending.push_back({ game.time + 5, [this]() {
		Police* pl = game.policeSys;
		if (pl && pl->level > 0) { pl->clear(); if (game.hud) game.hud->subtitle("Done. As far as the LSPD knows, you were never there.", "Lester", 4); }
	} });
}

void Phone::mechanic() {
	if (game.missionActive) { if (game.hud) game.hud->subtitle("I don't deliver into the middle of a job, homie.", "Benny", 4); return; }
	const std::string type = hasLastCar ? lastCarType : "kestrel";
	if (!pay(200)) { if (game.hud) game.hud->subtitle("Delivery's two hundred, homie. Come back with the cash.", "Benny", 4); return; }
	if (game.hud) game.hud->subtitle("One " + VehName(type) + ", coming right up. Give me a sec.", "Benny", 3.5);
	const bool hasColor = hasLastCar; const uint32_t color = lastCarColor;
	pending.push_back({ game.time + 6, [this, type, hasColor, color]() {
		Vehicle* v = deliver(type, hasColor, color);
		if (!v) {
			if (game.hud) game.hud->subtitle("Can't find a street out there to leave it on. Here's your money back.", "Benny", 4);
			if (!game.freeroamActive()) game.player->money += 200;
			return;
		}
		if (game.hud) game.hud->subtitle("Your ride's out front. The blue blip. Try to bring it back in one piece.", "Benny", 4.5);
	} });
}

// a car left at the kerb near the player, clear of traffic
Vehicle* Phone::deliver(const std::string& type, bool hasColor, uint32_t color) {
	Player& p = *game.player;
	const VehicleDef* d = FindVehicle(type);
	if (!d) return nullptr;
	const V3 pp = p.vehicle ? p.vehicle->pos : p.pos;
	const double offs[7][2] = { { 16, 0 }, { 26, 0 }, { -16, 0 }, { 0, 16 }, { 0, -16 }, { 36, 10 }, { -30, -10 } };
	for (const auto& o : offs) {
		const double ahead = o[0], side = o[1];
		const double fx = std::sin(p.yaw), fz = std::cos(p.yaw);
		RoadSpot sp;
		if (!FindRoadSpot(game, pp.x + fx * ahead - fz * side, pp.z + fz * ahead + fx * side, false, sp)) continue;
		const double dd = Hypot(sp.x - pp.x, sp.z - pp.z);
		if (dd > 70 || dd < 5) continue;
		// nudge towards the kerb, and make sure nothing's parked there already
		const double x = sp.x - std::cos(sp.yaw) * 0.8, z = sp.z + std::sin(sp.yaw) * 0.8;
		bool taken = false;
		for (const auto& ov : game.vehicles.list) if (!ov->removed && Hypot(ov->pos.x - x, ov->pos.z - z) < (ov->def.L + d->L) / 2 + 1) { taken = true; break; }
		if (taken) continue;
		if (Vehicle* old = delivered.get()) if (!old->removed && !old->driver()) game.vehicles.remove(old);
		SpawnOpts so; so.persistent = true; so.parked = true; so.hasColor = hasColor; so.color = color; so.hasY = true; so.y = sp.y;
		Vehicle* v = game.vehicles.spawn(type, x, z, sp.yaw, so);
		if (!v) return nullptr;
		v->ownedByPlayer = true;
		delivered = Ref<Vehicle>(v);
		if (deliveredBlip) game.removeBlip(deliveredBlip);
		Blip b; b.x = x; b.z = z; b.icon = "car"; b.color = 0x5fb2ff;
		deliveredBlip = game.addBlip(b);
		return v;
	}
	return nullptr;
}

void Phone::merryweather() {
	Player& p = *game.player;
	{ std::vector<Ref<Ped>> keep; for (auto& m : mercs) if (Ped* q = m.get()) if (!q->removed && !q->dead) keep.push_back(m); mercs = keep; }
	if (mercs.size() >= 3) { if (game.hud) game.hud->subtitle("Your detail is already on site, sir.", "Merryweather", 3.5); return; }
	if (!pay(1000)) { if (game.hud) game.hud->subtitle("Merryweather requires payment up front. One thousand dollars.", "Merryweather", 4); return; }
	if (game.hud) game.hud->subtitle("Contractors dispatched. Three operators, inbound to your position.", "Merryweather", 4);
	pending.push_back({ game.time + 4, [this]() {
		Player& pl = *game.player;
		RNG rng((uint32_t)(int64_t)std::floor(Rand() * 1e9));
		const double a0 = pl.yaw + kPi;
		for (int i = (int)mercs.size(); i < 3; i++) {
			const double a = a0 + (i - 1) * 0.5, d = 22 + i * 3;
			double x = pl.pos.x + std::sin(a) * d, z = pl.pos.z + std::cos(a) * d;
			// (walk in off the pavement rather than out of a wall)
			RoadSpot sp;
			if (FindRoadSpot(game, x, z, true, sp) && Hypot(sp.x - pl.pos.x, sp.z - pl.pos.z) < 45) { x = sp.x + (i - 1) * 1.2; z = sp.z; }
			// (the options object is evaluated first: its chances come off the generator before the look)
			const bool female = rng.Chance(0.15);
			const bool beard = rng.Chance(0.4);
			Appearance look = RandomAppearance(rng, female ? 1 : 0);
			look.shirt = 0x24272c; look.shirtType = "jacket"; look.jacketColor = 0x1b1d21; look.pants = 0x2a2d33; look.hairStyle = "cap"; look.hat = 0x111214;
			look.glasses = true; look.shoes = 0x0d0d0d; look.beard = beard; look.bandana = -1; look.shorts = false;
			PedOpts po; po.hasAppearance = true; po.appearance = look; po.brain = "civilian"; po.team = "player"; po.health = 200; po.armor = 60; po.persistent = true;
			po.hasY = true; po.y = game.collision->floorHeight(x, z, pl.pos.y + 1.5);
			Ped* m = game.peds->spawnPed(x, z, po);
			m->giveWeapon("rifle", 900); m->equip("rifle");
			m->accuracy = 0.7; m->damageMul = 1;
			m->follow = Ref<Character>(&pl); m->followSlot = i % 3; m->setState("follow");
			m->num["hireUntil"] = game.time + 600; // (merc)
			mercs.push_back(Ref<Ped>(m));
		}
	} });
}

void Phone::pegasus() {
	// (admin.js summon: with the admin tools, in free roam)
}

// ------------------------------------------------------------------ cheats
void Phone::cheat(const std::string& code) {
	Player& p = *game.player;
	const bool free = game.freeroamActive(); // (admin.js allowed: free roam, not online)
	auto say = [&](const std::string& t) { if (game.hud) game.hud->help("Cheat activated: <b>" + code + "</b>" + DOT + t, 3.5); game.sound("pickup"); };
	auto timedCheat = [&](const std::string& key, const std::string& label) {
		if (free) {
			bool& sw = key == "god" ? game.cheats.god : key == "superJump" ? game.cheats.superJump : key == "superRun" ? game.cheats.superRun : game.cheats.explosive;
			sw = !sw; say(label + (sw ? " on" : " off")); return;
		}
		auto it = timed.find(key);
		if (it != timed.end() && it->second > game.time) { timed.erase(it); say(label + " off"); return; }
		timed[key] = game.time + CHEAT_TIME; say(label + " for five minutes");
	};
	auto spawnNear = [&](const std::string& type) {
		if (game.missionActive) { if (game.hud) game.hud->help("Not during a mission.", 3); return false; }
		const VehicleDef* d = FindVehicle(type);
		if (!d) return false;
		const double a = p.yaw;
		const double x = p.pos.x + std::sin(a) * (d->L / 2 + 4), z = p.pos.z + std::cos(a) * (d->L / 2 + 4);
		SpawnOpts so; so.persistent = true;
		if (d->kind == "heli") { so.hasY = true; so.y = Max(game.map.GroundHeight(x, z), 0) + 60; }
		Vehicle* v = game.vehicles.spawn(type, x, z, p.yaw, so);
		if (v && d->kind == "heli") v->airborne = true; // (spool = 1, grounded = false: with the aircraft)
		return true;
	};
	hide();
	if (code == "PAINKILLER") timedCheat("god", "Invincibility");
	else if (code == "TURTLE") { p.health = p.maxHealth; p.armor = 100; say("Max health and armour"); }
	else if (code == "TOOLUP") { for (const auto& w : Weapons()) p.giveWeapon(w.first, (w.second.clip ? w.second.clip : 1) * 8); say("Weapons"); }
	else if (code == "LAWYERUP") { if (game.policeSys) game.policeSys->clear(); say("Wanted level cleared"); }
	else if (code == "FUGITIVE") { if (Police* pol = game.policeSys) { pol->setLevel((int)Min(5, pol->level + 1)); say("Wanted level " + std::to_string(pol->level)); } }
	else if (code == "SKYFALL") { say("Skydive"); } // (admin.js skydive: with the admin tools)
	else if (code == "BUZZOFF") { if (spawnNear("warhawk")) say("Warhawk gunship"); }
	else if (code == "COMET") { if (spawnNear("zenith")) say("Zenith"); }
	else if (code == "OFFROAD") { if (spawnNear("trail")) say("Trailblazer"); }
	else if (code == "HOPTOIT") timedCheat("superJump", "Super jump");
	else if (code == "CATCHME") timedCheat("superRun", "Fast run");
	else if (code == "HIGHEX") timedCheat("explosive", "Explosive bullets");
	else if (code == "POWERUP") { if (game.special) game.special->meter = 1; say("Special ability recharged"); }
	else if (code == "SLOWMO") {
		const bool on = !slow; slow = on;
		if (on) game.slowmo["cheat"] = 0.5; else game.slowmo.erase("cheat");
		say(std::string("Slow motion ") + (on ? "on" : "off"));
	} else if (code == "MAKEITRAIN") {
		const std::vector<std::string> order = { "clear", "cloudy", "rain", "storm", "fog" };
		int i = 0; for (int k = 0; k < (int)order.size(); k++) if (order[k] == game.env.weather) i = k;
		const std::string next = order[(i + 1) % order.size()];
		game.env.setWeather(next, true);
		say("Weather: " + next);
	} else if (game.hud) game.hud->help("Unknown number.", 2);
}

// ------------------------------------------------------------------ per frame
void Phone::update(double) {
	Player& p = *game.player;
	// the last road vehicle you drove (for Benny)
	if (Vehicle* v = p.vehicle) if (p.seat == 0 && v->def.kind.empty() && !v->def.train && !v->def.police && !v->def.military && v->missionTag.empty() && v->type != "taxi") { hasLastCar = true; lastCarType = v->type; lastCarColor = v->color; }
	// calls being acted on
	for (int i = (int)pending.size() - 1; i >= 0; i--) if (i < (int)pending.size() && game.time >= pending[i].first) { auto fn = pending[i].second; pending.erase(pending.begin() + i); fn(); }
	// Benny's delivery: the blip follows the car until you get in
	if (deliveredBlip) {
		Vehicle* dv = delivered.get();
		if (!dv || dv->removed || dv->isWrecked() || (dv->driver() && dv->driver()->isPlayer)) { game.removeBlip(deliveredBlip); deliveredBlip.reset(); }
		else { deliveredBlip->x = dv->pos.x; deliveredBlip->z = dv->pos.z; }
	}
	// Merryweather contractors: ten minutes on the clock, or until they're left behind
	for (auto& r : mercs) if (Ped* m = r.get()) {
		if (m->removed) continue;
		const bool far = Hypot(m->pos.x - p.pos.x, m->pos.z - p.pos.z) > 300;
		if (game.time > m->num["hireUntil"] || far || (m->dead && far)) game.peds->remove(m);
	}
	{ std::vector<Ref<Ped>> keep; for (auto& r : mercs) if (Ped* m = r.get()) if (!m->removed) keep.push_back(r); mercs = keep; }
	// (admin.js: what the rest of the game reads as the cheats: free roam's switches, or the phone's timed ones)
	if (game.freeroamActive()) game.cheatsOn = game.cheats;
	else {
		const auto ph = activeCheats();
		Cheats c;
		for (const auto& k : ph) ApplyCheat(c, k.first);
		game.cheatsOn = c;
		if (!ph.empty()) { phoneCheats = true; p.invincible = c.god; if (c.god && p.health < p.maxHealth && !p.dead) p.health = p.maxHealth; }
		else if (phoneCheats) { phoneCheats = false; p.invincible = false; }
	}
}

void Phone::reset(bool keepCheats) {
	for (auto& r : mercs) if (Ped* m = r.get()) if (!m->removed) { m->persistent = false; m->follow = nullptr; m->setState("wander"); }
	mercs.clear();
	if (!keepCheats) timed.clear();
	pending.clear();
	if (slow) { slow = false; game.slowmo.erase("cheat"); }
	hide();
	setPhoto(false);
}

} // namespace atg
