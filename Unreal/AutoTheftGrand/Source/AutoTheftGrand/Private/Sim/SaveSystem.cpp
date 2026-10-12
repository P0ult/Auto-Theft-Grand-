#include "SaveSystem.h"
#include "Game.h"
#include "Missions.h"
#include "Pets.h"
#include "Pickups.h"
#include "Properties.h"
#include "Weapons.h"
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace atg {

SaveSystem::SaveSystem(Game& g) : game(g) {}

// ------------------------------------------------------------------ a small JSON writer / reader
namespace {

struct W {
	std::string& s;
	W(std::string& s) : s(s) {}
	void str(const std::string& v) {
		s += '"';
		for (char c : v) switch (c) {
			case '"': s += "\\\""; break;
			case '\\': s += "\\\\"; break;
			case '\n': s += "\\n"; break;
			case '\r': s += "\\r"; break;
			case '\t': s += "\\t"; break;
			default: s += c;
		}
		s += '"';
	}
	void num(double v) {
		if (!std::isfinite(v)) s += "null"; // (an infinite clip is written as -1 on save instead)
		else if (v == std::floor(v) && std::fabs(v) < 1e15) s += std::to_string((long long)v);
		else { char b[32]; std::snprintf(b, sizeof b, "%.15g", v); s += b; }
	}
};

struct P {
	const char* p;
	Json value(const char* msg) {
		ws();
		char c = peek();
		if (c == '{') return obj(msg);
		if (c == '[') return arr(msg);
		if (c == '"') return str();
		if (c == 't') { expect("true"); Json v; v.type = Json::Bool; v.b = true; return v; }
		if (c == 'f') { expect("fals"); expectCh('e'); Json v; v.type = Json::Bool; return v; }
		if (c == 'n') { expect("null"); return Json(); }
		return numOrFail(msg);
	}
	char peek() const { return *p; }
	void ws() { while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++; }
	void expect(const char* word) { while (*word && *p == *word) { p++; word++; } }
	void expectCh(char c) { ws(); if (*p == c) p++; }
	Json str() {
		Json v; v.type = Json::Str;
		if (*p != '"') return v;
		p++;
		while (*p && *p != '"') {
			if (*p == '\\' && p[1]) {
				p++;
				char e = *p++;
				if (e == 'n') v.s += '\n'; else if (e == 't') v.s += '\t'; else if (e == 'r') v.s += '\r';
				else if (e == 'u') { std::string h(p, 4); p += 4; v.s += (char)std::strtol(h.c_str(), nullptr, 16); }
				else v.s += e;
			} else v.s += *p++;
		}
		if (*p == '"') p++;
		return v;
	}
	Json numOrFail(const char* msg) {
		Json v; v.type = Json::Num;
		char* end = nullptr;
		v.n = std::strtod(p, &end);
		if (end == p) throw msg;
		p = end;
		return v;
	}
	Json arr(const char* msg) {
		Json v; v.type = Json::Arr;
		p++;
		ws();
		if (peek() == ']') { p++; return v; }
		for (;;) {
			v.a.push_back(value(msg));
			ws();
			if (*p == ',') { p++; continue; }
			if (*p == ']') { p++; return v; }
			throw msg;
		}
	}
	Json obj(const char* msg) {
		Json v; v.type = Json::Obj;
		p++;
		ws();
		if (peek() == '}') { p++; return v; }
		for (;;) {
			ws();
			Json k = str();
			expectCh(':');
			v.o.push_back({ k.s, value(msg) });
			ws();
			if (*p == ',') { p++; continue; }
			if (*p == '}') { p++; return v; }
			throw msg;
		}
	}
};

} // namespace

Json Json::parse(const std::string& text) {
	P p{ text.c_str() };
	try { p.ws(); return p.value("bad json"); } catch (const char*) { return Json(); }
}

namespace {
double Num(const Json& v, double alt = 0) { return v.type == Json::Num ? v.n : alt; }
double NumAt(const Json* o, const char* key, double alt = 0) { const Json* v = o ? o->get(key) : nullptr; return v && v->type == Json::Num ? v->n : alt; }
std::string StrAt(const Json* o, const char* key) { const Json* v = o ? o->get(key) : nullptr; return v && v->type == Json::Str ? v->s : std::string(); }
std::string IsoDate() {
	std::time_t t = std::time(nullptr); std::tm tm;
	gmtime_s(&tm, &t);
	char b[40]; std::snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02d.000Z", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	return b;
}
}

bool SaveSystem::hasSave() const { return !memory.empty() || (!path.empty() && std::ifstream(path).good()); }

bool SaveSystem::save(bool autoSave) {
	if (game.freeRoam) return false; // free roam (everything unlocked, unlimited cash) never touches the story save
	memory = encode();
	if (!path.empty()) { std::ofstream f(path, std::ios::binary); if (!f) return false; f << memory; }
	if (!autoSave) game.sound("checkpoint");
	return true;
}

bool SaveSystem::load() {
	std::string text = memory;
	if (text.empty() && !path.empty()) { std::ifstream f(path, std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); text = ss.str(); }
	return !text.empty() && decode(text);
}

void SaveSystem::clear() {
	memory.clear();
	if (!path.empty()) std::remove(path.c_str());
}

std::string SaveSystem::encode() const {
	const Player& p = *game.player;
	std::string s;
	W w(s);
	s += "{\"version\":1";
	s += ",\"date\":"; w.str(IsoDate());
	s += ",\"money\":"; w.num(p.money);
	s += ",\"health\":"; w.num(p.health);
	s += ",\"armor\":"; w.num(p.armor);
	s += ",\"weapons\":{";
	bool first = true;
	for (const auto& [id, slot] : p.weapons) {
		if (!first) s += ',';
		first = false;
		w.str(id);
		s += ":{\"ammo\":"; w.num(std::isfinite(slot.ammo) ? slot.ammo : -1);
		s += ",\"clip\":"; w.num(std::isfinite(slot.clip) ? slot.clip : -1);
		s += '}';
	}
	s += "},\"weapon\":"; w.str(p.weapon);
	s += ",\"hours\":"; w.num(game.env.hours);
	// missions: { completed: [...], log: [...] }
	if (game.missions) {
		s += ",\"missions\":{\"completed\":[";
		first = true;
		for (const std::string& id : game.missions->completed) { if (!first) s += ','; first = false; w.str(id); }
		s += "],\"log\":[";
		first = true;
		for (const std::string& e : game.missions->log) { if (!first) s += ','; first = false; w.str(e); }
		s += "]}";
	}
	const auto& st = game.stats;
	s += ",\"stats\":{";
	first = true;
	auto stat = [&](const char* k, double v) { if (!first) s += ','; first = false; w.str(k); s += ':'; w.num(v); };
	stat("kills", st.kills); stat("copKills", st.copKills); stat("headshots", st.headshots); stat("carsStolen", st.carsStolen);
	stat("carsDestroyed", st.carsDestroyed); stat("runOver", st.runOver); stat("wasted", st.wasted); stat("busted", st.busted);
	stat("maxWanted", st.maxWanted); stat("bestDrift", st.bestDrift); stat("driven", st.driven); stat("walked", st.walked);
	stat("playTime", st.playTime); stat("missions", st.missions); stat("sprays", st.sprays); stat("tricks", st.tricks); stat("shipRaids", st.shipRaids);
	stat("vigilanteBest", st.vigilanteBest); stat("taxiRides", st.taxiRides); stat("racesWon", st.racesWon); stat("properties", st.properties);
	s += ",\"raceBest\":{";
	first = true;
	for (const auto& [k, v] : st.raceBest) { if (!first) s += ','; first = false; w.str(k); s += ':'; w.num(v); }
	s += "}";
	s += "}";
	// the hidden packages
	if (game.pickupsSys) {
		s += ",\"packages\":[";
		first = true;
		for (int id : game.pickupsSys->collectedPackages) { if (!first) s += ','; first = false; w.num(id); }
		s += "]";
	}
	// gang turf thinned out by the story
	s += ",\"gang\":{";
	first = true;
	for (const auto& [k, v] : game.gangDensity) { if (!first) s += ','; first = false; w.str(k); s += ':'; w.num(v); }
	s += "}";
	// the pet
	if (game.pets && game.pets->pet && !game.pets->pet->dead && !game.pets->info.breed.empty()) {
		s += ",\"pet\":{\"breed\":"; w.str(game.pets->info.breed); s += ",\"name\":"; w.str(game.pets->info.name); s += '}';
	}
	// owned properties
	if (game.properties) {
		s += ",\"props\":[";
		first = true;
		for (const auto& [id, till] : game.properties->serialize()) { if (!first) s += ','; first = false; s += "{\"id\":"; w.str(id); s += ",\"till\":"; w.num(till); s += '}'; }
		s += "]";
	}
	s += '}';
	return s;
}

bool SaveSystem::decode(const std::string& text) {
	const Json d = Json::parse(text);
	if (d.type != Json::Obj) return false;
	Player& p = *game.player;
	p.money = NumAt(&d, "money", 0);
	const double health = NumAt(&d, "health", 0);
	p.health = health > 0 ? health : p.maxHealth;
	p.armor = NumAt(&d, "armor", 0);
	p.weapons.clear();
	p.weapons["fist"] = { kInf, kInf };
	if (const Json* ws = d.get("weapons")) for (const auto& [id, w] : ws->o) {
		const double ammo = NumAt(&w, "ammo", -1), clip = NumAt(&w, "clip", -1);
		p.weapons[id] = { ammo < 0 ? kInf : ammo, clip < 0 ? kInf : clip };
	}
	const std::string wep = StrAt(&d, "weapon");
	p.equip(p.weapons.count(wep) ? wep : "fist");
	if (game.missions) {
		std::set<std::string> completed; std::vector<std::string> log;
		if (const Json* mi = d.get("missions")) {
			if (const Json* c = mi->get("completed")) for (const Json& v : c->a) if (v.type == Json::Str) completed.insert(v.s);
			if (const Json* l = mi->get("log")) for (const Json& v : l->a) if (v.type == Json::Str) log.push_back(v.s);
		}
		game.missions->load(completed, log);
	}
	if (const Json* g = d.get("gang")) for (const auto& [k, v] : g->o) if (v.type == Json::Num) game.gangDensity[k] = v.n;
	if (const Json* st = d.get("stats")) {
		auto& s = game.stats;
		auto take = [&](const char* k, double& f) { if (const Json* v = st->get(k); v && v->type == Json::Num) f = v->n; };
		take("kills", s.kills); take("copKills", s.copKills); take("headshots", s.headshots); take("carsStolen", s.carsStolen);
		take("carsDestroyed", s.carsDestroyed); take("runOver", s.runOver); take("wasted", s.wasted); take("busted", s.busted);
		take("maxWanted", s.maxWanted); take("bestDrift", s.bestDrift); take("driven", s.driven); take("walked", s.walked);
		take("playTime", s.playTime); take("missions", s.missions); take("sprays", s.sprays); take("tricks", s.tricks); take("shipRaids", s.shipRaids);
		take("vigilanteBest", s.vigilanteBest); take("taxiRides", s.taxiRides); take("racesWon", s.racesWon);
		double props = s.properties; take("properties", props); s.properties = (int)props;
		if (const Json* rb = st->get("raceBest")) for (const auto& [k, v] : rb->o) if (v.type == Json::Num) s.raceBest[k] = v.n;
	}
	if (game.pickupsSys) {
		game.pickupsSys->collectedPackages.clear();
		if (const Json* pk = d.get("packages")) for (const Json& v : pk->a) if (v.type == Json::Num) game.pickupsSys->collectedPackages.insert((int)v.n);
		game.pickupsSys->refreshPackages();
		game.pickupsSys->spawnSafehouseRewards();
	}
	game.env.setTime(NumAt(&d, "hours", 9));
	if (const Json* pet = d.get("pet")) {
		const std::string breed = StrAt(pet, "breed");
		if (!breed.empty() && game.pets) game.pets->adopt(breed, StrAt(pet, "name"));
	}
	if (game.properties) {
		std::vector<std::pair<std::string, double>> props;
		if (const Json* pr = d.get("props")) for (const Json& v : pr->a) props.push_back({ StrAt(&v, "id"), NumAt(&v, "till", 0) });
		game.properties->load(props);
	}
	return true;
}

} // namespace atg
