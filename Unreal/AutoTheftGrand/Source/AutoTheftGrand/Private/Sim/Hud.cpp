#include "Hud.h"
#include "Game.h"
#include "Player.h"
#include "Weapons.h"

#include <cstdio>

namespace atg {

namespace {
// CSS's default transition timing ("ease" = cubic-bezier(0.25, 0.1, 0.25, 1))
double CssEase(double x) {
	if (x <= 0) return 0;
	if (x >= 1) return 1;
	const double x1 = 0.25, y1 = 0.1, x2 = 0.25, y2 = 1;
	auto bx = [&](double t) { const double u = 1 - t; return 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t; };
	auto by = [&](double t) { const double u = 1 - t; return 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t; };
	double lo = 0, hi = 1, t = x;
	for (int i = 0; i < 30; i++) { t = (lo + hi) / 2; if (bx(t) < x) lo = t; else hi = t; }
	return by(t);
}
// utils.js formatMoney: eight digits, zero padded
std::string FormatMoney(double n) {
	char buf[32];
	std::snprintf(buf, sizeof buf, "%08lld", (long long)Max(0, std::floor(std::fabs(n))));
	return std::string(n < 0 ? "-$" : "$") + buf;
}
}

double HudModel::Fade::value() const {
	if (dur <= 0) return to;
	return from + (to - from) * CssEase(t / dur);
}

HudModel::HudModel(Game& g) : game(g) {}

void HudModel::help(const std::string& text, double seconds) { helpLine.text = text; helpLine.t = seconds; }

void HudModel::speech(Character* who, const std::string& text) {
	if (speeches.size() > 5) speeches.erase(speeches.begin());
	speeches.push_back({ Ref<Character>(who), text, 3 });
}

void HudModel::bigMessage(const std::string& text, const std::string& style, double dur, const std::string& sub) { big = { text, style, sub, dur }; }

void HudModel::subtitle(const std::string& text, const std::string& speaker, double dur) { subs.text = text; subsSpeaker = speaker; subs.t = dur; }

void HudModel::objective(const std::string& text, double dur) { subs.text = text; subsSpeaker.clear(); subs.t = dur; objectiveText = text; }

void HudModel::setBar(const std::string* label, double v, const std::string& color) {
	if (!label) { bar.on = false; return; }
	bar = { true, *label, Clamp(v, 0, 1), color };
}

// the safehouse: no saving on a job or in free roam (the save menu itself comes with save and load)
void HudModel::promptSave() {
	if (game.missionActive) { help("You can't save during a mission."); return; }
	if (game.freeRoam) { help("Free roam isn't saved \xe2\x80\x94 your story save is left untouched."); return; }
}

// ------------------------------------------------------------------ the shop menus
namespace {
const std::vector<std::string>& GunShopItems() {
	static const std::vector<std::string> v = { "bat", "knife", "pistol", "smg", "shotgun", "rifle", "sniper", "minigun", "rpg", "grenade", "molotov" };
	return v;
}
// a number as JavaScript prints it in a template string (whole numbers without a decimal point)
std::string HudNum(double v) {
	if (v == std::floor(v) && std::fabs(v) < 1e15) return std::to_string((long long)v);
	char b[32]; std::snprintf(b, sizeof b, "%.15g", v); return b;
}
}

void HudModel::openShop() {
	if (game.player->vehicle) return;
	game.paused = true;
	game.menuOpen = true;
	menu = Menu();
	menu.kind = "gunshop";
	menu.title = "GUN BARN";
	menu.sub = "Est. 1979 \xe2\x80\x94 No questions asked";
	menuRender();
}

void HudModel::openStore(const std::string& title, const std::string& sub, const std::vector<StoreItem>& items) {
	if (game.player->vehicle) return;
	game.paused = true;
	game.menuOpen = true;
	menu = Menu();
	menu.kind = "store";
	menu.title = title; menu.sub = sub;
	storeItems = items;
	menuRender();
}

// render(): the rows from the player's cash, weapons and armour
void HudModel::menuRender() {
	Player& p = *game.player;
	menu.rows.clear();
	if (menu.kind == "gunshop") {
		for (const std::string& id : GunShopItems()) {
			const WeaponDef* d = FindWeapon(id);
			if (!d) continue;
			const bool owned = p.weapons.count(id) > 0;
			MenuRow r;
			r.icon = id;
			r.name = d->name;
			r.desc = d->type == "melee" ? "Melee" : d->type == "thrown" ? "Explosive" :
				"Dmg " + HudNum(d->damage) + (d->pellets > 1 ? "\xc3\x97" + std::to_string(d->pellets) : "") + " \xc2\xb7 " + (d->automatic ? "Automatic" : "Semi") + " \xc2\xb7 Clip " + std::to_string(d->clip);
			r.button = owned && d->type != "melee" ? "Ammo $" + HudNum(d->ammoPrice) : owned ? "Owned" : "Buy $" + HudNum(d->price);
			r.enabled = !(owned && d->type == "melee");
			menu.rows.push_back(r);
		}
		MenuRow a;
		a.name = "Body Armor"; a.desc = "Absorbs 80% of incoming damage";
		a.button = p.armor >= 100 ? "Full" : "Buy $200";
		menu.rows.push_back(a);
		MenuRow l; l.button = "Leave shop (Esc)"; l.primary = true;
		menu.rows.push_back(l);
	} else {
		for (const StoreItem& it : storeItems) {
			MenuRow r;
			r.name = it.name; r.desc = it.desc;
			r.button = "Buy $" + std::to_string(it.price);
			r.enabled = it.available;
			menu.rows.push_back(r);
		}
		MenuRow l; l.button = "Leave (Esc)"; l.primary = true;
		menu.rows.push_back(l);
	}
	menu.focus = Clamp(menu.focus, 0, (int)menu.rows.size() - 1);
	if (!menu.rows[menu.focus].enabled) menuMove(1);
}

void HudModel::menuMove(int dir) {
	const int n = (int)menu.rows.size();
	for (int k = 1; k <= n; k++) {
		const int i = menu.focus + dir * k;
		if (i < 0 || i >= n) return;
		if (menu.rows[i].enabled) { menu.focus = i; return; }
	}
}

void HudModel::menuPress(int row) {
	if (menu.kind.empty() || row < 0 || row >= (int)menu.rows.size() || !menu.rows[row].enabled) return;
	menu.focus = row;
	if (row == (int)menu.rows.size() - 1) { closeOverlay(); return; }
	Player& p = *game.player;
	if (menu.kind == "gunshop") {
		if (row < (int)GunShopItems().size()) {
			const std::string& id = GunShopItems()[row];
			const WeaponDef* d = FindWeapon(id);
			if (!d) return;
			const bool owned = p.weapons.count(id) > 0;
			const double cost = owned ? d->ammoPrice : d->price;
			if (p.money < cost) { if (game.audio) game.audio->play("locked"); menu.rows[row].button = "Not enough cash"; return; }
			p.money -= cost;
			if (owned) p.giveWeapon(id, d->ammoPack); else { p.giveWeapon(id, d->type == "melee" ? 0 : d->ammoPack); p.switchTo(id); }
			if (game.audio) game.audio->play("cash");
			game.events.purchase.emit(id);
		} else {
			// body armour
			if (p.armor >= 100) return;
			if (p.money < 200) { menu.rows[row].button = "Not enough cash"; return; }
			p.money -= 200; p.armor = 100;
			if (game.audio) game.audio->play("cash");
		}
	} else {
		const StoreItem& it = storeItems[row];
		if (p.money < it.price) { if (game.audio) game.audio->play("locked"); menu.rows[row].button = "Not enough cash"; return; }
		p.money -= it.price;
		if (game.audio) game.audio->play("cash");
		const std::string msg = it.use ? it.use(p, game) : std::string();
		menu.note = msg.empty() ? it.name + " bought." : msg;
	}
	menuRender();
}

void HudModel::closeOverlay() {
	menu = Menu();
	storeItems.clear();
	game.menuOpen = false;
	game.paused = false;
}

void HudModel::moneyFlash(double amount) {
	std::string s = FormatMoney(std::fabs(amount));
	// (.replace('$0000', '$').replace(/^\$0+/, '$'))
	const size_t p = s.find("$0000");
	if (p != std::string::npos) s.replace(p, 5, "$");
	if (!s.empty() && s[0] == '$') { size_t k = 1; while (k < s.size() && s[k] == '0') k++; if (k > 1) s = "$" + s.substr(k); }
	money.text = std::string(amount >= 0 ? "+" : "-") + s;
	money.t = 2;
}

void HudModel::dispatch(const std::string& text, const std::string& where) {
	if (text.empty()) return;
	dispatchLine.text = text; dispatchWhere = where; dispatchLine.t = 5;
}

void HudModel::fade(double dur, std::function<void()> mid) {
	fadeState = { fadeState.value(), 1, dur, 0 };
	game.setTimeout(dur + 0.15, [this, dur, mid]() {
		if (mid) mid();
		fadeState = { fadeState.value(), 0, dur, 0 };
	});
}

void HudModel::fadeTo(double v, double dur) { fadeState = { fadeState.value(), v, dur, 0 }; }

void HudModel::update(double dt) {
	Player& p = *game.player;
	if (Finite(creditsT)) {
		const bool firstFrame = creditsT == 0;
		creditsT += dt;
		if (creditsT >= 42 || (!firstFrame && (game.input.keyHit("MouseLeft") || game.input.hit("skip") || game.input.gpHit(GP::B)))) dismissCredits();
	}
	routeTimer -= dt;
	if (gpsTarget && routeTimer <= 0) {
		routeTimer = 1;
		const V3 pp = p.vehicle ? p.vehicle->pos : p.pos;
		if (Hypot(gpsTarget->x - pp.x, gpsTarget->z - pp.z) < 15) route.clear();
		else { const auto r = game.map.roads.FindRoute(pp.x, pp.z, gpsTarget->x, gpsTarget->z); route = game.map.roads.RoutePolyline(r ? &*r : nullptr, pp.x, pp.z, gpsTarget->x, gpsTarget->z); }
	}
	// zone and vehicle names
	const V3 zp = p.vehicle ? p.vehicle->pos : p.pos;
	const std::string z = game.map.ZoneName(zp.x, zp.z);
	if (z != lastZone) { lastZone = z; zone.text = z; zone.t = 3.5; }
	Vehicle* pv = p.vehicle;
	if (pv != lastVeh) {
		lastVeh = pv;
		if (pv) {
			veh.text = pv->def.name; veh.t = 3;
			if (game.audio && pv->def.bike.empty()) { const std::string l = game.audio->radioLabel(); const size_t nl = l.find('\n'); if (!l.empty()) showRadio(l.substr(0, nl), nl == std::string::npos ? "" : l.substr(nl + 1)); }
		}
	}
	// speech bubbles
	for (int i = (int)speeches.size() - 1; i >= 0; i--) {
		Speech& s = speeches[i];
		s.t -= dt;
		Character* c = s.who.get();
		if (s.t <= 0 || !c || c->removed) speeches.erase(speeches.begin() + i);
	}
	damageFlash = Max(0, damageFlash - dt * 1.5);
	const double hp = p.health / (p.maxHealth ? p.maxHealth : 100);
	vignette = Max(damageFlash, hp < 0.2 && !p.dead ? 0.35 + std::sin(game.time * 4) * 0.1 : 0);
	fadeState.t = Min(fadeState.dur, fadeState.t + dt);
	if (!shard.empty()) shardT += dt;
	for (Line* l : { &helpLine, &subs, &zone, &veh, &radio, &money, &dispatchLine }) if (l->t > 0) l->t -= dt;
	if (big.t > 0) big.t -= dt;
}

void HudModel::dismissCredits() { creditsT = NaN(); game.paused = false; }

} // namespace atg
