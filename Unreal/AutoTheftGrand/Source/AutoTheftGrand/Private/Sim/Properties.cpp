#include "Properties.h"
#include "Game.h"
#include "Phone.h"
#include "Pickups.h"
#include "SaveSystem.h"

namespace atg {

namespace {
std::string Money(double n) {
	std::string s = std::to_string((long long)JsRound(n)), out;
	for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) out += ','; out += s[i]; }
	return "$" + out;
}
std::string Upper(std::string s) { for (char& c : s) c = (char)std::toupper((unsigned char)c); return s; }
}

Properties::Properties(Game& g) : game(g) {
	const auto& L = game.map.landmarks;
	struct Where { bool ok; double x, z; };
	auto at = [&L](const char* k, double dx, double dz) -> Where { auto it = L.find(k); return it == L.end() ? Where{ false, 0, 0 } : Where{ true, it->second.x + dx, it->second.z + dz }; };
	auto town = [](const char* k, double dx, double dz) -> Where { const Town& t = TownByKey(k); return { true, t.x + dx, t.z + dz }; };
	struct Def { const char* id; const char* name; const char* kind; int price, income; Where where; const char* blurb; };
	const Def defs[] = {
		{ "beachhouse", "Santa Luz Beach House", "safehouse", 45000, 0, at("beach", 20, -40), "Two bedrooms, a deck and the sound of the waves. Save and heal here." },
		{ "carwash", "Corona Car Wash", "business", 30000, 700, at("spray2", 22, 18), "Suds, wax and no questions about where the car came from." },
		{ "hotelaurelio", "Hotel Aurelio", "business", 120000, 2600, at("aurelio", -40, 55), "Sixty rooms on the Plaza. Tourists, conventions, the odd discreet guest." },
		{ "harborloft", "Harborside Loft", "safehouse", 80000, 0, at("aurHarbor", -60, 40), "Exposed brick, a view of the bay and a lock the Saints can't pick." },
		{ "clamshack", "The Clam Shack, Gull Bay", "business", 25000, 450, town("gull", 40, 16), "Fried clams and cold beer. Surprisingly profitable." },
		{ "lodge", "Timberline Lodge", "both", 55000, 600, town("timber", 34, -22), "A log lodge up in the pines: rooms to let, and a bed of your own." },
		{ "arenabox", "Aurelio Arena Skybox", "business", 200000, 4200, at("aurArena", 45, -20), "A cut of every ticket, every beer and every bet on fight night." },
		{ "desertmotel", "Desert Rose Motel", "both", 38000, 520, town("dry", 36, 24), "Twelve rooms, a pool that sometimes has water in it." },
	};
	for (const Def& d : defs) {
		if (!d.where.ok || !Finite(d.where.x)) continue;
		RoadSpot sp;
		const bool has = FindRoadSpot(game, d.where.x, d.where.z, true, sp);
		Property p; p.id = d.id; p.name = d.name; p.kind = d.kind; p.price = d.price; p.income = d.income; p.blurb = d.blurb;
		p.x = has ? sp.x : d.where.x; p.z = has ? sp.z : d.where.z;
		list.push_back(p);
	}
	markers();
}

void Properties::markers() {
	for (int i = 0; i < (int)list.size(); i++) {
		Property& p = list[i];
		if (p.marker) game.pickupsSys->removeMarker(p.marker);
		const bool own = owned.count(p.id) > 0;
		MarkerOpts o; o.color = own ? 0x4aa3ff : 0x6cff6c; o.radius = 1.2; o.icon = "house2";
		o.label = own ? p.name : p.name + " \xE2\x80\x94 for sale"; o.footOnly = true; o.arrow = !own;
		o.onEnter = [this, i](Marker*) { open(i); };
		p.marker = game.pickupsSys->addMarker(p.x, p.z, o);
		if (p.marker->blip) { p.marker->blip->color = own ? 0x4aa3ff : 0x6cff6c; p.marker->blip->small = true; }
	}
}

std::vector<StoreItem> Properties::rows(int index) {
	std::vector<StoreItem> out;
	const Property& p = list[index];
	auto own = owned.find(p.id);
	if (own == owned.end()) {
		StoreItem it; it.name = "Buy the deeds";
		it.desc = std::string(p.kind != "business" ? "Save, heal and restock here. " : "") + (p.income ? "Income piles up here \xE2\x80\x94 up to five days' worth." : "");
		it.button = "Buy " + Money(p.price); it.price = p.price;
		it.use = [this, index](Player&, Game& g) {
			const Property& q = list[index];
			owned[q.id] = 0;
			if (g.hud) g.hud->bigMessage("PROPERTY BOUGHT", "passed", 3, q.name);
			g.stats.properties = (int)owned.size();
			g.events.propertyBought.emit(q.id);
			markers();
			if (g.saveSys) g.saveSys->save(true);
			return std::string();
		};
		out.push_back(it);
		return out;
	}
	if (p.income) {
		const double till = own->second;
		StoreItem it; it.name = "The till"; it.desc = Money(till) + " waiting \xC2\xB7 " + Money(p.income) + " a day";
		it.button = till >= 1 ? "Collect " + Money(till) : "Empty"; it.available = till >= 1;
		it.use = [this, index](Player& pl, Game& g) {
			double& t = owned[list[index].id];
			const double cash = std::floor(t);
			pl.money += cash; if (g.hud) g.hud->moneyFlash(cash);
			t = 0;
			g.sound("cash");
			return std::string();
		};
		out.push_back(it);
	}
	if (p.kind != "business") {
		StoreItem it; it.name = "Rest here"; it.desc = "Save your game, patch yourself up and sleep six hours."; it.button = "Save & rest"; it.closes = true;
		it.use = [](Player& pl, Game& g) {
			pl.health = pl.maxHealth; pl.armor = Max(pl.armor, 50);
			g.env.setTime(g.env.hours + 6);
			const bool ok = g.saveSys && g.saveSys->save();
			if (g.hud) g.hud->help(!ok ? "Rested. (Free roam doesn't save.)" : "Game saved. Health restored.", 3);
			return std::string();
		};
		out.push_back(it);
	}
	return out;
}

void Properties::open(int index) {
	if (index < 0 || index >= (int)list.size()) return;
	const Property& p = list[index];
	if (game.player->vehicle || game.missionActive || !game.hud) return;
	const std::string kind = p.kind == "safehouse" ? "Safehouse" : p.kind == "business" ? "Business" : "Safehouse & business";
	PanelOpts o; o.refresh = [this, index]() { return rows(index); };
	game.hud->openPanel(Upper(p.name), kind + (p.income ? " \xC2\xB7 earns " + Money(p.income) + " a day" : "") + " \xE2\x80\x94 " + p.blurb, rows(index), o);
}

void Properties::update(double) {
	const double h = game.env.hours;
	if (!IsSet(lastHours)) { lastHours = h; return; }
	double dh = h - lastHours;
	if (dh < 0) dh += 24;
	lastHours = h;
	if (dh <= 0 || dh > 12 || owned.empty()) return;
	for (const Property& p : list) {
		auto it = owned.find(p.id);
		if (it == owned.end() || !p.income) continue;
		it->second = Min(p.income * 5.0, it->second + p.income * dh / 24);
	}
}

std::vector<std::pair<std::string, double>> Properties::serialize() const {
	std::vector<std::pair<std::string, double>> out;
	for (const auto& [id, till] : owned) out.push_back({ id, std::floor(till) });
	return out;
}

void Properties::load(const std::vector<std::pair<std::string, double>>& data) {
	owned.clear();
	for (const auto& [id, till] : data) for (const Property& p : list) if (p.id == id) { owned[id] = till; break; }
	markers();
}

} // namespace atg
