#include "Net.h"
#include "Aircraft.h"
#include "Animator.h"
#include "Combat.h"
#include "Game.h"
#include "Hud.h"
#include "Missions.h"
#include "Pets.h"
#include "Player.h"
#include "Ragdoll.h"
#include "Vehicle.h"
#include "Weapons.h"
#include "VehicleDefs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>

namespace atg {

const uint32_t PLAYER_COLORS[8] = { 0x4cc9f0, 0xf72585, 0x80ed99, 0xffd166, 0xff7b00, 0xb388ff, 0xff4d6d, 0x2ec4b6 };

namespace {
constexpr double SEND_HZ = 12;
constexpr double DELAY = 150;      // ms of interpolation delay
constexpr double EVENT_KEEP = 1600; // ms a moment stays in the log (spans several sends)
constexpr double PROTO = 2;         // (bump when the vehicle catalogue changes)

double R1(double x) { return std::floor(x * 10 + 0.5) / 10; }
double R2(double x) { return std::floor(x * 100 + 0.5) / 100; }
double R3(double x) { return std::floor(x * 1000 + 0.5) / 1000; }
double NumOr(const Json* v, double d = 0) { return JNum(v, d); }
Json JN(double v) { return Json::Number(v); }
Json JS(const std::string& v) { return Json::String(v); }
Json JArr(std::initializer_list<double> v) { Json a = Json::Array(); for (double x : v) a.push(JN(x)); return a; }
// an array of at least three finite numbers
bool Vec3(const Json* a, V3& out) {
	if (!a || !a->isArr() || a->a.size() < 3) return false;
	for (int i = 0; i < 3; i++) if (!a->a[i].finite()) return false;
	out = V3(a->a[0].n, a->a[1].n, a->a[2].n);
	return true;
}
bool Vec3At(const Json* arr, size_t from, V3& out) {
	if (!arr || !arr->isArr() || arr->a.size() < from + 3) return false;
	for (size_t i = from; i < from + 3; i++) if (!arr->a[i].finite()) return false;
	out = V3(arr->a[from].n, arr->a[from + 1].n, arr->a[from + 2].n);
	return true;
}
double WallMs() { return (double)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::string Hex(uint32_t c) { char b[8]; std::snprintf(b, sizeof b, "#%06x", c & 0xffffff); return b; }

// UTF-8 to code points and back
std::vector<uint32_t> Decode(const std::string& s) {
	std::vector<uint32_t> out;
	for (size_t i = 0; i < s.size();) {
		const unsigned char c = (unsigned char)s[i];
		uint32_t cp; int n;
		if (c < 0x80) { cp = c; n = 1; } else if ((c >> 5) == 6) { cp = c & 0x1f; n = 2; } else if ((c >> 4) == 14) { cp = c & 0x0f; n = 3; } else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; } else { i++; continue; }
		if (i + n > s.size()) break;
		for (int k = 1; k < n; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3f);
		out.push_back(cp);
		i += n;
	}
	return out;
}
std::string Encode(const std::vector<uint32_t>& cps) {
	std::string out;
	for (uint32_t cp : cps) {
		if (cp < 0x80) out += (char)cp;
		else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
		else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
		else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
	}
	return out;
}
// net.js cleanText: no control or invisible formatting characters, trimmed, at most n characters
std::string CleanText(const std::string& s, size_t n) {
	std::vector<uint32_t> cps;
	for (uint32_t c : Decode(s)) {
		if (c < 0x20 || (c >= 0x7f && c <= 0x9f) || (c >= 0x200b && c <= 0x200f) || (c >= 0x202a && c <= 0x202e) || (c >= 0x2060 && c <= 0x206f) || c == 0xfeff) continue;
		cps.push_back(c);
	}
	auto space = [](uint32_t c) { return c == ' ' || c == 0xa0 || c == 0x3000; };
	size_t b = 0, e = cps.size();
	while (b < e && space(cps[b])) b++;
	while (e > b && space(cps[e - 1])) e--;
	std::vector<uint32_t> t(cps.begin() + b, cps.begin() + e);
	if (t.size() > n) t.resize(n);
	return Encode(t);
}
std::string Esc(const std::string& s) {
	std::string o;
	for (char c : s) switch (c) { case '&': o += "&amp;"; break; case '<': o += "&lt;"; break; case '>': o += "&gt;"; break; case '"': o += "&quot;"; break; case '\'': o += "&#39;"; break; default: o += c; }
	return o;
}
std::string Upper(std::string s) { for (char& c : s) c = (char)std::toupper((unsigned char)c); return s; }
// cleanCode: lower-case letters and digits, at most 12
std::string CleanCode(const std::string& s) { std::string o; for (char c : s) { const char l = (char)std::tolower((unsigned char)c); if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9')) o += l; } return o.substr(0, 12); }
} // namespace

// ------------------------------------------------------------------ appearance (avatar.js sanitizeAppearance)
Json AppearanceJson(const Appearance& a) {
	Json o = Json::Object();
	o.set("female", Json::Boolean(a.female));
	o.set("skin", JN(a.skin)); o.set("hair", JN(a.hair)); o.set("hairStyle", JS(a.hairStyle));
	o.set("shirt", JN(a.shirt)); o.set("shirtType", JS(a.shirtType)); o.set("pants", JN(a.pants));
	o.set("shorts", Json::Boolean(a.shorts)); o.set("shoes", JN(a.shoes));
	o.set("hat", a.hat >= 0 ? JN((double)a.hat) : Json());
	o.set("build", JN(a.build)); o.set("height", JN(a.height));
	o.set("glasses", Json::Boolean(a.glasses)); o.set("beard", Json::Boolean(a.beard));
	o.set("jacketColor", JN(a.jacketColor));
	o.set("bandana", a.bandana >= 0 ? JN((double)a.bandana) : Json());
	return o;
}

Appearance SanitizeAppearance(const Json* ap) {
	Appearance out = RandomAppearance();
	if (!ap || !ap->isObj()) return out;
	auto color = [&](const char* k, auto& field, bool nullable) {
		const Json* v = ap->get(k);
		if (!v) return;
		if (v->type == Json::Null && nullable) { field = (std::remove_reference_t<decltype(field)>)-1; return; }
		if (v->integer() && v->n >= 0 && v->n <= 0xffffff) field = (std::remove_reference_t<decltype(field)>)v->n;
	};
	color("skin", out.skin, false); color("hair", out.hair, false); color("shirt", out.shirt, false); color("pants", out.pants, false);
	color("shoes", out.shoes, false); color("hat", out.hat, true); color("jacketColor", out.jacketColor, false); color("bandana", out.bandana, true);
	auto pick = [&](const char* k, std::string& field, std::initializer_list<const char*> list) {
		const Json* v = ap->get(k);
		if (v && v->isStr()) for (const char* l : list) if (v->s == l) field = v->s;
	};
	pick("hairStyle", out.hairStyle, { "short", "bald", "afro", "cap", "buzz", "long", "ponytail", "bun" });
	pick("shirtType", out.shirtType, { "tee", "long", "tank", "jacket" });
	auto flag = [&](const char* k, bool& field) { const Json* v = ap->get(k); if (v && v->type == Json::Bool) field = v->b; };
	flag("female", out.female); flag("shorts", out.shorts); flag("glasses", out.glasses); flag("beard", out.beard);
	if (const Json* v = ap->get("build"); v && v->finite()) out.build = Clamp(v->n, 0.85, 1.25);
	if (const Json* v = ap->get("height"); v && v->finite()) out.height = Clamp(v->n, 0.9, 1.1);
	out.hasUniform = false;
	return out;
}

// ------------------------------------------------------------------ RemoteAvatar (avatar.js)
RemoteAvatar::RemoteAvatar(Game& g, NetSystem& n, const std::string& id, const Appearance& look)
	: Character(g, look, 100, 0, "remote"), net(n), peerId(id) {
	remote = true;
	invincible = false;
}

void RemoteAvatar::setWeapon(const std::string& id) {
	if (!FindWeapon(id) || id == weapon) return;
	if (!weapons.count(id)) giveWeapon(id, 1);
	equip(id);
}

// posed by NetSystem each frame; just animate here
void RemoteAvatar::update(double dt) {
	if (removed) return;
	if (vehicle) {
		AnimState& st = animState;
		seatState(st); st.speed = 0; st.grounded = true; st.swim = false;
		st.weapon = holdType();
		st.aim = aiming && weaponDef().gun(); st.aimPitch = aimPitch;
		if (hasAimYaw) { hasAimDir = true; aimDir = V3(std::sin(aimYaw) * std::cos(st.aimPitch), std::sin(st.aimPitch), std::cos(aimYaw) * std::cos(st.aimPitch)); }
		leanOut(dt);
		anim->update(dt, st);
		updatePose();
		orientWeapon();
		return;
	}
	if (ragdolling) { ragdoll->update(dt); ragdoll->apply(rootMatrix()); updatePose(); return; }
	animState.sit = 0;
	weaponVisible = !swimming;
	anim->update(dt, animState);
	updatePose();
	orientWeapon();
}

// hits: only what the local player does to them counts, and it's their client that applies it
bool RemoteAvatar::takeDamage(double amount, const DamageInfo& info) {
	if (dead) return false;
	Player& p = *game.player;
	// (the player, on foot or at the wheel: a car's damage is put down to its driver)
	if (info.source != &p) return false;
	double dmg = amount;
	if (info.part == "head") dmg *= IsSet(info.headMul) ? info.headMul : 4;
	if (info.part == "limb") dmg *= 0.7;
	net.sendHit(peerId, dmg, info);
	lastHitTime = game.time;
	if (!ragdolling && !vehicle && info.type != "fire") anim->play("flinch");
	return false;
}

// their client says they're down / dead / back up
void RemoteAvatar::setDown(bool down, bool isDead) {
	if (down && !ragdolling) {
		if (vehicle) return;
		startRagdoll(V3());
	} else if (!down && ragdolling) {
		ragdolling = false;
		anim->beginBlend(0.25);
	}
	dead = isDead;
}

// ------------------------------------------------------------------ NetSystem
NetSystem::NetSystem(Game& g) : game(g) {
	name = "Player" + std::to_string(100 + (int)(Rand() * 900));
	color = (int)(Rand() * 8) % 8;
	game.characterSources.push_back([this](std::vector<Character*>& out) { for (Character* a : avatars()) out.push_back(a); });
}

NetSystem::~NetSystem() { if (transport) transport->leave(); }

std::vector<Character*> NetSystem::avatars() const {
	std::vector<Character*> list;
	for (const auto& kv : peers) if (kv.second->avatar && !kv.second->avatar->removed) list.push_back(kv.second->avatar.get());
	return list;
}

void NetSystem::reset() { disconnect(true); }

// ---- connection
void NetSystem::connect(std::unique_ptr<NetTransport> t, const std::string& c) {
	disconnect(true);
	if (!t) {
		status = "error";
		error = "Multiplayer needs the relay server: run `node server.mjs` and connect to it.";
		return;
	}
	status = "connecting"; error.clear();
	NetTransport* raw = t.get();
	raw->onPeer = [this](const std::string& id, const Json& st, bool guest) { onPeer(id, st, guest); };
	raw->onWorld = [](const std::string&, const Json&) { /* (shared pedestrians and traffic: npcsync.js, not ported yet) */ };
	raw->onLeft = [this](const std::string& id) { removePeer(id, true); };
	raw->onStatus = [this](bool ok) { linkUp = ok; };
	transport = std::move(t);
	const std::string clean = CleanCode(c);
	raw->join(clean, [this, raw, clean](bool ok, const std::string& err) {
		if (transport.get() != raw) return;
		if (!ok) { status = "error"; error = err.empty() ? "Could not join that room." : err; transport.reset(); return; }
		code = clean;
		status = "online";
		linkUp = true;
		joinedAt = WallMs();
		sendT = 0;
		sys(std::string("You joined ") + (code.empty() ? "the <b>public world</b>" : "room <b>" + Esc(Upper(code)) + "</b>") + ".");
		send();
	});
}

void NetSystem::disconnect(bool quiet) {
	std::vector<std::string> ids;
	for (const auto& kv : peers) ids.push_back(kv.first);
	for (const auto& id : ids) removePeer(id, false);
	if (transport) { transport->leave(); }
	const bool was = status == "online";
	transport.reset();
	if (was && !quiet) sys("You left multiplayer.");
	status = "offline";
	game.env.weatherLocked = false;
}

// ---- outgoing
void NetSystem::emit(Json e) {
	Json full = Json::Array();
	full.push(JN(++seq));
	for (auto& x : e.a) full.push(std::move(x));
	out.push_back({ nowMs, std::move(full) });
	if (out.size() > 24) out.pop_front();
}

void NetSystem::onShot(Character* shooter, const std::string& weaponId, const V3& from, const V3& to) {
	if (!online() || shooter != game.player.get()) return;
	if (nowMs - shotT < 70) return;
	shotT = nowMs;
	Json e = Json::Array();
	e.push(JS("sh")); e.push(JS(weaponId));
	for (double x : { R2(from.x), R2(from.y), R2(from.z), R1(to.x), R1(to.y), R1(to.z) }) e.push(JN(x));
	emit(std::move(e));
}

void NetSystem::onExplosion(const V3& pos, double radius, const void* source, double vis) {
	if (!online()) return;
	const Player& p = *game.player;
	if (source != (const void*)&p && source != (const void*)p.vehicle) return;
	Json e = Json::Array();
	e.push(JS("ex"));
	for (double x : { R1(pos.x), R1(pos.y), R1(pos.z), R1(radius), R1(vis) }) e.push(JN(x));
	emit(std::move(e));
}

void NetSystem::sendHit(const std::string& peerId, double dmg, const DamageInfo& info) {
	if (!online()) return;
	auto it = peers.find(peerId);
	if (it == peers.end() || !JTruthy(it->second->st.get("pvp"))) return;
	PendingHit& h = pendingHits[peerId];
	h.dmg += dmg;
	if (info.part == "head") h.head = true;
	if (info.knockdown) { h.kd = true; if (info.hasImpulse) h.imp = JArr({ R1(info.impulse.x), R1(info.impulse.y), R1(info.impulse.z) }); }
	h.w = !info.weapon.empty() ? info.weapon : info.type;
}

void NetSystem::sendVehicleHit(Vehicle* v, double dmg, Character* source) {
	if (!online() || !v || v->remoteOwner.empty()) return;
	Player& p = *game.player;
	if (source != &p && !(source && source->vehicle && source->vehicle == p.vehicle) && !(source && p.vehicle && p.vehicle->driver() == source)) return;
	if (v->npcRemote) return; // (shared traffic: npcsync.js, not ported yet)
	auto it = peers.find(v->remoteOwner);
	if (it == peers.end() || !JTruthy(it->second->st.get("pvp"))) return;
	Json e = Json::Array();
	e.push(JS("vh")); e.push(JS(v->remoteOwner)); e.push(JN(R1(dmg)));
	emit(std::move(e));
}

void NetSystem::say(const std::string& text) {
	const std::string t = CleanText(text, 140);
	if (t.empty()) return;
	if (online()) { Json e = Json::Array(); e.push(JS("ch")); e.push(JS(t)); emit(std::move(e)); }
	ChatLine m; m.name = name; m.col = PLAYER_COLORS[color % 8]; m.text = Esc(t); m.me = true;
	pushChat(std::move(m));
}

Json NetSystem::buildState() {
	Game& g = game;
	Player& p = *g.player;
	Vehicle* v = p.vehicle;
	while (!out.empty() && nowMs - out.front().t >= EVENT_KEEP) out.pop_front();
	for (auto& [id, h] : pendingHits) {
		Json e = Json::Array();
		e.push(JS("hit")); e.push(JS(id)); e.push(JN(R1(h.dmg))); e.push(JN(h.head ? 1 : 0)); e.push(JN(h.kd ? 1 : 0)); e.push(h.imp); e.push(JS(h.w));
		emit(std::move(e));
	}
	pendingHits.clear();
	const AnimAction* act = p.anim->action && !p.anim->action->done ? p.anim->action.get() : nullptr;
	if ((const void*)act != lastAct) { lastAct = act; if (act) actN++; }
	const V3 pos = p.ragdolling && p.ragdoll ? p.ragdoll->center() : p.pos;
	const AnimState& a = p.animState;
	Json st = Json::Object();
	st.set("v", JN(PROTO)); st.set("n", JS(name)); st.set("c", JN(color)); st.set("j", JN(joinedAt)); st.set("pvp", JN(pvp ? 1 : 0));
	st.set("ap", AppearanceJson(p.appearance));
	st.set("m", JS(g.freeRoam ? "f" : "s")); st.set("pz", JN(g.paused ? 1 : 0));
	st.set("p", JArr({ R2(pos.x), R2(pos.y), R2(pos.z) })); st.set("y", JN(R3(p.yaw))); st.set("vl", JArr({ R1(p.vel.x), R1(p.vel.y), R1(p.vel.z) }));
	st.set("a", JArr({ R2(a.speed), R2(a.moveAngle), a.grounded ? 1.0 : 0.0, p.aiming ? 1.0 : 0.0, R2(p.aimPitch), a.crouch ? 1.0 : 0.0, a.swim ? 1.0 : 0.0, a.handsUp ? 1.0 : 0.0, p.chute ? 1.0 : 0.0, R1(a.vy), p.vehicle && p.aiming ? R3(p.hasAimYaw ? p.aimYaw : 0) : 0.0 }));
	st.set("w", JS(p.weapon));
	if (act) { Json ac = Json::Array(); ac.push(JS(act->name)); ac.push(JN(actN)); st.set("ac", ac); } else st.set("ac", Json());
	st.set("hp", JN(std::floor(p.health + 0.5))); st.set("ar", JN(std::floor(p.armor + 0.5))); st.set("d", JN(p.dead ? 1 : 0)); st.set("rg", JN(p.ragdolling ? 1 : 0));
	st.set("wl", JN(g.police ? g.police->wantedLevel() : 0));
	st.set("vh", v ? vehState(v, p.seat) : Json());
	if (g.pets) { const std::vector<double> pt = g.pets->netState(); if (pt.empty()) st.set("pt", Json()); else { Json a2 = Json::Array(); for (double x : pt) a2.push(JN(x)); st.set("pt", a2); } }
	else st.set("pt", Json());
	st.set("tod", JN(R2(g.env.hours))); st.set("wx", JS(g.env.weather));
	Json ev = Json::Array();
	for (const Out& o : out) ev.push(o.e);
	st.set("ev", ev);
	return st;
}

Json NetSystem::vehState(Vehicle* v, int seat) {
	Json o = Json::Object();
	if (!v->remoteOwner.empty()) { o.set("ref", JS(v->remoteOwner)); o.set("s", JN(seat)); return o; }
	o.set("t", JS(v->type)); o.set("c", JN(v->color)); o.set("s", JN(seat));
	o.set("p", JArr({ R2(v->pos.x), R2(v->pos.y), R2(v->pos.z) })); o.set("y", JN(R3(v->yaw))); o.set("vl", JArr({ R1(v->vel.x), R1(v->vel.y), R1(v->vel.z) }));
	o.set("hp", JN(std::floor(v->health + 0.5))); o.set("x", JN(v->exploded ? 1 : 0));
	if (v->def.train) o.set("tr", JN(1));
	if (const AirVehicle* air = dynamic_cast<const AirVehicle*>(v)) {
		o.set("q", JArr({ R3(air->quat.x), R3(air->quat.y), R3(air->quat.z), R3(air->quat.w) }));
		o.set("sp", JN(R2(v->def.kind == "heli" ? air->spool : air->power))); o.set("gr", JN(air->grounded ? 1 : 0));
	} else {
		o.set("pr", JArr({ R3(v->groundPitch), R3(v->groundRoll) })); o.set("st", JN(R2(v->steerAngle)));
		o.set("br", JN(v->input.brake > 0.1 || v->input.handbrake ? 1 : 0)); o.set("sr", JN(v->sirenOn ? 1 : 0));
	}
	if (v->tb) { const Quat& q = v->tb->q; o.set("tq", JArr({ R3(q.x), R3(q.y), R3(q.z), R3(q.w) })); } // (tumbling after a crash)
	if (const Tank* tk = dynamic_cast<const Tank*>(v)) { o.set("tu", JN(R2(tk->turretYaw))); o.set("gp", JN(R2(tk->gunPitch))); }
	return o;
}

void NetSystem::send() {
	if (!online()) return;
	Json st = buildState();
	// stay well inside the room's 4 KiB presence limit: shed the oldest moments first
	Json* ev = nullptr;
	for (auto& kv : st.o) if (kv.first == "ev") ev = &kv.second;
	while (ev && !ev->a.empty() && st.dump().size() > 3600) ev->a.erase(ev->a.begin());
	transport->send(st);
}

// ---- incoming
void NetSystem::onPeer(const std::string& id, const Json& st, bool guest) {
	V3 p0;
	if (!st.isObj() || JNum(st.get("v"), -1) != PROTO || !Vec3(st.get("p"), p0)) return;
	auto it = peers.find(id);
	Peer* P = it == peers.end() ? nullptr : it->second.get();
	const Json* apv = st.get("ap");
	const std::string apKey = (apv ? apv->dump() : std::string("{}")) + "|" + (st.get("c") ? st.get("c")->dump() : std::string());
	const bool rebuilt = P && P->apKey != apKey; // new look / colour: rebuild the avatar quietly
	if (rebuilt) { removePeer(id, false); P = nullptr; }
	const bool fresh = !P;
	if (!P) P = addPeer(id, st, guest, apKey);
	P->name = CleanText(JStrAt(st, "n"), 16);
	if (P->name.empty()) P->name = "Player";
	const Json* c = st.get("c");
	P->col = PLAYER_COLORS[(c && c->integer() && c->n >= 0 ? (int)c->n : 0) % 8];
	P->st = st;
	P->last = nowMs;
	P->buf.push_back({ nowMs, st });
	if (P->buf.size() > 10) P->buf.pop_front();
	if (fresh) {
		// don't replay moments from before we met
		if (const Json* ev = st.get("ev"); ev && ev->isArr()) for (const Json& e : ev->a) if (e.isArr() && !e.a.empty() && e.a[0].integer()) P->lastEv = std::max(P->lastEv, e.a[0].n);
		if (!rebuilt) sys("<b style=\"color:" + Hex(P->col) + "\">" + Esc(P->name) + "</b> joined.");
	} else moments(*P, st.get("ev"));
}

NetSystem::Peer* NetSystem::addPeer(const std::string& id, const Json& st, bool guest, const std::string& apKey) {
	Game& g = game;
	// everyone wears their player colour, so you can tell people apart at a glance
	Appearance look = SanitizeAppearance(st.get("ap"));
	const Json* c = st.get("c");
	const uint32_t col = PLAYER_COLORS[(c && c->integer() && c->n >= 0 ? (int)c->n : 0) % 8];
	look.shirt = col;
	if (look.shirtType == "jacket") look.jacketColor = look.shirt;
	auto P = std::make_unique<Peer>();
	P->id = id; P->st = st; P->guest = guest; P->apKey = apKey; P->last = nowMs; P->col = col;
	P->avatar = g.makeCharacter<RemoteAvatar>(*this, id, look);
	V3 p0; Vec3(st.get("p"), p0);
	P->avatar->setPosition(p0.x, p0.y, p0.z);
	Blip b; b.x = p0.x; b.z = p0.z; b.icon = "person"; b.color = col; b.label = "Player";
	P->blip = g.addBlip(b);
	Peer* raw = P.get();
	peers[id] = std::move(P);
	return raw;
}

void NetSystem::removePeer(const std::string& id, bool announce) {
	if (game.pets) game.pets->dropRemote(id);
	auto it = peers.find(id);
	if (it == peers.end()) return;
	std::unique_ptr<Peer> P = std::move(it->second);
	peers.erase(it);
	RemoteAvatar* av = P->avatar.get();
	if (av && av->vehicle) av->vehicle->takeOut(av);
	if (av) av->remove();
	if (P->veh && !P->veh->removed) releaseProxy(P->veh);
	if (P->blip) game.removeBlip(P->blip);
	if (announce) sys("<b style=\"color:" + Hex(P->col) + "\">" + Esc(P->name) + "</b> left.");
}

void NetSystem::moments(Peer& P, const Json* ev) {
	if (!ev || !ev->isArr()) return;
	Game& g = game;
	const std::string me = selfId();
	for (const Json& e : ev->a) {
		if (!e.isArr() || e.a.empty() || !e.a[0].integer() || e.a[0].n <= P.lastEv) continue;
		P.lastEv = e.a[0].n;
		const std::string k = e.a.size() > 1 && e.a[1].isStr() ? e.a[1].s : std::string();
		auto Str = [&](size_t i) { return i < e.a.size() && e.a[i].isStr() ? e.a[i].s : std::string(); };
		auto Num = [&](size_t i, double d = 0) { return i < e.a.size() ? JNum(&e.a[i], d) : d; };
		auto Truthy = [&](size_t i) { return i < e.a.size() && JTruthy(&e.a[i]); };
		if (k == "sh") {
			const WeaponDef* def = FindWeapon(Str(2));
			V3 from, to;
			if (!def || !Vec3At(&e, 3, from) || !Vec3At(&e, 6, to)) continue;
			if ((to - from).length() > 400) continue;
			if (g.effects) { g.effects->tracer(from, to); g.effects->muzzleFlash(from, (to - from).normalized(), def->id == "shotgun"); }
			if (g.audio) { SoundOpts o; o.gun = true; g.audio->playAt(def->sound, from, 0.85, o); }
			g.events.gunshot.emit(P.avatar.get(), from, def->id);
		} else if (k == "ex") {
			V3 pos;
			if (!Vec3At(&e, 2, pos)) continue;
			const double vis = Clamp(Num(6, Num(5, 5) * 0.75), 0.75, 32), size = vis / 6.75;
			if (g.effects) g.effects->explosion(pos, vis);
			if (g.audio) { SoundOpts o; o.size = size; g.audio->playAt("explosion", pos, std::min(1.6, 0.7 + size * 0.3), o); }
			const V3 pd = g.player->vehicle ? g.player->vehicle->pos : g.player->pos;
			g.rig.addShake(Clamp(1.2 - (pos - pd).length() / (60 * std::max(1.0, std::sqrt(size))), 0, 1.2));
		} else if (k == "hit") {
			if (me.empty() || Str(2) != me) continue;
			const Json* imp = e.a.size() > 6 ? &e.a[6] : nullptr;
			takeHit(P, Clamp(Num(3), 0, 400), Truthy(4), Truthy(5), imp, Str(7));
		} else if (k == "vh") {
			if (me.empty() || Str(2) != me || !pvp) continue;
			Vehicle* v = g.player->vehicle;
			if (v && g.player->seat == 0 && v->remoteOwner.empty()) v->damage(Clamp(Num(3), 0, 2000), P.avatar.get());
		} else if (k == "nh" || k == "nv" || k == "tk") {
			// (shared pedestrians and traffic: npcsync.js, not ported yet)
		} else if (k == "ch") {
			const std::string text = CleanText(Str(2), 140);
			if (!text.empty()) { ChatLine m; m.name = P.name; m.col = P.col; m.text = Esc(text); pushChat(std::move(m)); }
		} else if (k == "k") {
			const std::string kid = Str(2);
			std::string kname; uint32_t kcol = 0;
			if (kid == me && !me.empty()) { kname = name; kcol = PLAYER_COLORS[color % 8]; }
			else if (auto it = peers.find(kid); it != peers.end()) { kname = it->second->name; kcol = it->second->col; }
			const WeaponDef* wd = FindWeapon(Str(3));
			const std::string w = wd ? wd->name : "explosion";
			if (!kname.empty()) sys("<b style=\"color:" + Hex(kcol) + "\">" + Esc(kname) + "</b> <span class=\"kf\">" + Esc(w) + "</span> <b style=\"color:" + Hex(P.col) + "\">" + Esc(P.name) + "</b>");
		}
	}
}

void NetSystem::takeHit(Peer& P, double dmg, bool head, bool kd, const Json* imp, const std::string& weapon) {
	Player& p = *game.player;
	if (!pvp || p.dead || !dmg) return;
	const bool wasDead = p.dead;
	const WeaponDef* wd = FindWeapon(weapon);
	DamageInfo info;
	info.type = wd ? (wd->type == "melee" ? "melee" : "bullet") : weapon == "vehicle" ? "vehicle" : "explosion";
	info.source = P.avatar.get(); info.weapon = weapon; info.headMul = 1;
	if (kd && !p.vehicle) { info.knockdown = true; info.hasImpulse = true; V3 iv; info.impulse = Vec3(imp, iv) ? iv : V3(0, 3, 0); }
	p.takeDamage(dmg, info);
	if (info.type == "bullet" && game.effects) game.effects->blood(p.chestPos(), V3(std::sin(p.yaw), 0.2, std::cos(p.yaw)), head ? 12 : 6);
	if (p.dead && !wasDead) { Json e = Json::Array(); e.push(JS("k")); e.push(JS(P.id)); e.push(JS(weapon)); emit(std::move(e)); }
}

// ---- frame
void NetSystem::tick(double dt) {
	nowMs += dt * 1000;
	if (!online()) return;
	sendT -= dt;
	const double rate = game.paused ? 3 : SEND_HZ;
	if (sendT <= 0) { sendT = 1 / rate; send(); }
	std::vector<Peer*> list;
	for (auto& kv : peers) list.push_back(kv.second.get());
	for (Peer* P : list) pose(*P, nowMs, dt);
	syncClock(dt);
}

NetSystem::Pick NetSystem::sample(const Peer& P, double now) const {
	const double T = now - DELAY;
	const auto& b = P.buf;
	size_t i = b.size() - 1;
	while (i > 0 && b[i - 1].t > T) i--;
	const Sample& B = b[i];
	const Sample* A = i > 0 ? &b[i - 1] : nullptr;
	if (!A || B.t <= T) {
		// past the newest state: extrapolate a little
		return { &B.st, &B.st, 0, Clamp((T - B.t) / 1000, 0, 0.25) };
	}
	return { &A->st, &B.st, Clamp((T - A->t) / std::max(1.0, B.t - A->t), 0, 1), 0 };
}

void NetSystem::pose(Peer& P, double now, double dt) {
	Game& g = game;
	RemoteAvatar* av = P.avatar.get();
	if (!av || P.buf.empty()) return;
	if (g.pets) {
		std::vector<double> pt;
		if (const Json* j = P.st.get("pt"); j && j->isArr()) for (const Json& x : j->a) pt.push_back(x.isNum() ? x.n : NaN());
		g.pets->poseRemote(P.id, pt, dt); // (their pet)
	}
	const Pick s = sample(P, now);
	const Json& a = *s.a; const Json& b = *s.b;
	V3 ap, bp; Vec3(a.get("p"), ap); Vec3(b.get("p"), bp);
	const bool jump = Hypot(ap.x - bp.x, ap.z - bp.z) > 40;
	const double kk = jump ? 1 : s.k;
	V3 vl; if (!Vec3(b.get("vl"), vl)) vl = V3();
	const double x = Lerp(ap.x, bp.x, kk) + vl.x * s.ext, y = Lerp(ap.y, bp.y, kk) + vl.y * s.ext * 0.5, z = Lerp(ap.z, bp.z, kk) + vl.z * s.ext;
	const double yaw = JNum(a.get("y")) + WrapAngle(JNum(b.get("y")) - JNum(a.get("y"))) * kk;
	if (P.blip) { P.blip->x = x; P.blip->z = z; P.blip->color = P.col; }
	// vehicle
	const Json& st = P.st;
	const Json* vs = st.get("vh");
	const bool hasVs = vs && vs->isObj();
	if (hasVs && !JTruthy(vs->get("tr"))) poseVehicle(P, *vs, a.get("vh"), b.get("vh"), kk, s.ext, dt);
	else if (av->vehicle) av->vehicle->takeOut(av);
	if (!hasVs && P.veh) { parkProxy(P, P.veh); P.oldVeh = P.veh; P.veh = nullptr; }
	if (av->vehicle && av->vehicle->removed) { av->vehicle = nullptr; av->seat = -1; }
	const Json* A = st.get("a");
	auto An = [&](size_t i) { return A && A->isArr() && i < A->a.size() ? JNum(&A->a[i]) : 0.0; };
	auto At = [&](size_t i) { return A && A->isArr() && i < A->a.size() && JTruthy(&A->a[i]); };
	if (!av->vehicle) {
		// on foot (or riding the train: shown where they are)
		const bool down = JTruthy(st.get("rg")) || JTruthy(st.get("d"));
		av->setDown(down, JTruthy(st.get("d")));
		if (!av->ragdolling) { av->pos = V3(x, y, z); av->yaw = yaw; }
		AnimState& as = av->animState;
		as.speed = Clamp(An(0), 0, 12); as.moveAngle = An(1); as.grounded = At(2); as.aim = At(3); as.aimPitch = Clamp(An(4), -1.5, 1.5);
		as.crouch = At(5); as.swim = At(6); as.handsUp = At(7); as.vy = An(9);
		av->aiming = At(3); av->aimPitch = as.aimPitch; av->swimming = At(6); av->crouching = At(5);
		av->vel = vl;
	} else {
		av->setDown(false, JTruthy(st.get("d")));
		// drive-bys: aiming from a seat (a passenger leans out of the window)
		av->aiming = At(3); av->aimPitch = Clamp(An(4), -1.5, 1.5);
		av->hasAimYaw = av->aiming; av->aimYaw = av->aiming ? An(10) : 0;
	}
	const std::string w = JStrAt(st, "w");
	av->setWeapon(w.empty() ? "fist" : w);
	av->animState.weapon = av->holdType();
	if (const Json* ac = st.get("ac"); ac && ac->isArr() && ac->a.size() >= 2 && JNum(&ac->a[1], -1) != P.actN) {
		P.actN = JNum(&ac->a[1], -1);
		if (ac->a[0].isStr() && !av->ragdolling) av->anim->play(ac->a[0].s);
	}
	av->health = Clamp(JNumAt(st, "hp", 100), 0, 200);
	av->update(dt);
}

void NetSystem::poseVehicle(Peer& P, const Json& vs, const Json* va, const Json* vb, double k, double ext, double dt) {
	Game& g = game;
	RemoteAvatar* av = P.avatar.get();
	if (const Json* ref = vs.get("ref"); ref && ref->isStr()) {
		// riding in someone's vehicle: ours, or another player's stand-in
		const std::string me = selfId();
		Vehicle* host = nullptr;
		if (ref->s == me && !me.empty()) host = g.player->vehicle;
		else if (auto it = peers.find(ref->s); it != peers.end()) host = it->second->veh;
		const int seat = (int)Clamp(JNumAt(vs, "s", 1), 1, 3);
		const int seats = host && host->model ? (int)host->model->seats.size() : 2;
		if (!host || seat >= seats) { if (av->vehicle) av->vehicle->takeOut(av); return; }
		if (av->vehicle != host || av->seat != seat) {
			if (av->vehicle) av->vehicle->takeOut(av);
			if (!host->occupants[seat]) host->putIn(av, seat);
		}
		return;
	}
	const std::string type = JStrAt(vs, "t");
	V3 vp;
	if (!FindVehicle(type) || !Vec3(vs.get("p"), vp)) return;
	Vehicle* v = P.veh;
	if (!v || v->removed || v->type != type || (v->exploded && !JTruthy(vs.get("x")))) {
		if (v && !v->removed) parkProxy(P, v);
		// the car they got into is usually one we already show: the one they left here. Take that over rather
		// than spawning a second copy.
		auto nearOk = [&](Vehicle* o) {
			if (!o || o->removed || o->type != type || Hypot(o->pos.x - vp.x, o->pos.z - vp.z) >= 9) return false;
			for (const auto& c : o->occupants) if (c && (c->isPlayer || (c->remote && !c->npcProxy))) return false;
			return true;
		};
		Vehicle* cand = nearOk(P.oldVeh) ? P.oldVeh : nullptr;
		if (cand) {
			v = cand;
			for (const auto& o : v->occupants) if (o && o->npcProxy) v->takeOut(o.get());
			v->npcRemote = false; v->parkedOf.clear();
		} else {
			SpawnOpts o;
			if (const Json* c = vs.get("c"); c && c->integer()) { o.hasColor = true; o.color = (uint32_t)c->n; }
			o.hasY = true; o.y = vp.y; o.persistent = true;
			v = g.vehicles.spawn(type, vp.x, vp.z, JNumAt(vs, "y"), o);
			if (!v) return;
		}
		v->remote = true;
		v->remoteOwner = P.id;
		v->persistent = true;
		v->locked = true;
		if (!IsSet(v->mass0)) v->mass0 = v->mass;
		v->mass = 1e6; // (immovable for local collisions: its owner drives it)
		v->ai = nullptr;
		P.veh = v;
	}
	const Json& A = va && va->isObj() && va->get("p") ? *va : vs;
	const Json& B = vb && vb->isObj() && vb->get("p") ? *vb : vs;
	V3 pa, pb; Vec3(A.get("p"), pa); Vec3(B.get("p"), pb);
	const bool jump = Hypot(pa.x - pb.x, pa.z - pb.z) > 60;
	const double kk = jump ? 1 : k;
	V3 vl; if (!Vec3(B.get("vl"), vl)) vl = V3();
	v->pos = V3(Lerp(pa.x, pb.x, kk) + vl.x * ext, Lerp(pa.y, pb.y, kk) + vl.y * ext, Lerp(pa.z, pb.z, kk) + vl.z * ext);
	v->yaw = JNumAt(A, "y") + WrapAngle(JNumAt(B, "y") - JNumAt(A, "y")) * kk;
	v->vel = vl;
	v->health = Clamp(JNumAt(vs, "hp", 1000), 0, 5000);
	const int seat = (int)Clamp(JNumAt(vs, "s"), 0, 3);
	if (av->vehicle != v || av->seat != seat) {
		if (av->vehicle) av->vehicle->takeOut(av);
		if (v->occupants[seat] && v->occupants[seat].get() != av) { Character* o = v->occupants[seat].get(); if (!o->isPlayer) v->takeOut(o); }
		if (!v->occupants[seat]) v->putIn(av, seat);
	}
	if (!v->netT) v->netT = std::make_unique<Vehicle::NetPose>();
	Vehicle::NetPose& T = *v->netT;
	T.A = A; T.B = B; T.k = kk; T.sp = JNumAt(vs, "sp"); T.gr = JTruthy(vs.get("gr"));
	const Json* pr = B.get("pr");
	T.pr[0] = pr && pr->isArr() && pr->a.size() >= 2 ? JNum(&pr->a[0]) : 0; T.pr[1] = pr && pr->isArr() && pr->a.size() >= 2 ? JNum(&pr->a[1]) : 0;
	T.st = JNumAt(vs, "st"); T.br = JTruthy(vs.get("br")); T.sr = JTruthy(vs.get("sr")); T.tu = JNumAt(vs, "tu"); T.gp = JNumAt(vs, "gp");
	const Json* tq = B.get("tq");
	T.hasTq = tq && tq->isArr() && tq->a.size() == 4 && tq->a[0].finite() && tq->a[1].finite() && tq->a[2].finite() && tq->a[3].finite();
	if (T.hasTq) T.tq = Quat(tq->a[0].n, tq->a[1].n, tq->a[2].n, tq->a[3].n);
	if (JTruthy(vs.get("x")) && !v->exploded) {
		// (their client deals the damage: here it's only the look)
		Combat* c = dynamic_cast<Combat*>(g.combat);
		const bool old = c ? c->visualOnly : false;
		if (c) c->visualOnly = true;
		v->lastDamager = nullptr;
		v->explode();
		if (c) c->visualOnly = old;
	}
	netStep(v, dt);
}

// kinematic update for a stand-in vehicle (the vehicle manager skips remote vehicles' physics)
void NetSystem::netStep(Vehicle* v, double dt) {
	if (!v->netT) return;
	const Vehicle::NetPose& T = *v->netT;
	if (AirVehicle* air = dynamic_cast<AirVehicle*>(v)) {
		auto q4 = [](const Json& s, Quat& q) {
			const Json* j = s.get("q");
			if (!j || !j->isArr() || j->a.size() != 4) return false;
			for (const Json& x : j->a) if (!x.finite()) return false;
			q = Quat(j->a[0].n, j->a[1].n, j->a[2].n, j->a[3].n);
			q.normalize();
			return true;
		};
		Quat qa, qb;
		const bool hasA = q4(T.A, qa), hasB = q4(T.B, qb);
		if (hasA || hasB) air->quat = Quat::Slerp(hasA ? qa : qb, hasB ? qb : qa, T.k);
		if (v->def.kind == "heli") air->spool = Clamp(T.sp, 0, 1); else { air->power = Clamp(T.sp, 0, 1); air->spool = std::max(air->spool, air->power); }
		air->grounded = T.gr;
	} else {
		v->groundPitch = Clamp(T.pr[0], -0.8, 0.8); v->groundRoll = Clamp(T.pr[1], -0.8, 0.8);
		v->steerAngle = Clamp(T.st, -1, 1);
		v->input.brake = T.br ? 1 : 0; v->input.handbrake = false; v->input.throttle = 0.3;
		v->sirenOn = T.sr;
		const double fwd = v->vel.x * std::sin(v->yaw) + v->vel.z * std::cos(v->yaw);
		v->wheelRot += fwd * dt / (v->def.wheelR > 0 ? v->def.wheelR : 0.35);
		if (Tank* tk = dynamic_cast<Tank*>(v)) { tk->turretYaw = T.tu; tk->gunPitch = T.gp; }
		v->airborne = false;
		if (T.hasTq) { v->netQ = T.tq; v->netQ.normalize(); v->hasNetQ = true; } else v->hasNetQ = false;
		v->updateVisual(dt);
	}
}

// they got out: keep showing the car where they left it
void NetSystem::parkProxy(Peer& P, Vehicle* v) {
	for (int i = 0; i < 4; i++) { auto o = v->occupants[i]; if (o && o->remote) v->takeOut(o.get()); }
	v->netT.reset(); v->vel = V3();
	if (AirVehicle* air = dynamic_cast<AirVehicle*>(v)) { air->grounded = true; air->power = 0; }
	v->parkedOf = P.id; v->parkedT = nowMs;
}

void NetSystem::releaseProxy(Vehicle* v) {
	for (int i = 0; i < 4; i++) { auto o = v->occupants[i]; if (o && o->remote) v->takeOut(o.get()); }
	v->remote = false; v->remoteOwner.clear(); v->locked = false; v->persistent = false; v->netT.reset();
	v->netId = 0; v->npcRemote = false; v->parkedOf.clear(); // (a peer's id means nothing to us)
	if (IsSet(v->mass0)) v->mass = v->mass0;
	v->vel = V3();
	if (AirVehicle* air = dynamic_cast<AirVehicle*>(v)) { air->grounded = true; air->power = 0; }
}

// follow the host's clock and weather (the player who has been here longest) in free roam
void NetSystem::syncClock(double dt) {
	Game& g = game;
	clockT -= dt;
	if (clockT > 0) return;
	clockT = 2;
	const Peer* host = nullptr;
	double hj = joinedAt;
	std::string hid = selfId();
	for (const auto& kv : peers) {
		const double j = JNumAt(kv.second->st, "j", std::numeric_limits<double>::infinity());
		if (j < hj || (j == hj && kv.second->id < hid)) { hj = j; hid = kv.second->id; host = kv.second.get(); }
	}
	const bool follow = host && !(g.missions && g.missions->active) && g.freeRoam;
	g.env.weatherLocked = follow;
	if (!follow) return;
	const double tod = JNumAt(host->st, "tod", g.env.hours);
	const double diff = std::fabs(std::fmod(tod - g.env.hours + 36, 24.0) - 12);
	if (12 - diff > 0.12) g.env.setTime(tod);
	const std::string wx = JStrAt(host->st, "wx");
	if ((wx == "clear" || wx == "cloudy" || wx == "rain" || wx == "storm" || wx == "fog") && wx != g.env.weather) g.env.setWeather(wx);
}

void NetSystem::sys(const std::string& html) { ChatLine m; m.sys = true; m.text = html; pushChat(std::move(m)); }
void NetSystem::pushChat(ChatLine m) {
	m.t = nowMs;
	chat.push_back(std::move(m));
	if (chat.size() > 60) chat.pop_front();
}

std::vector<NetSystem::RosterRow> NetSystem::roster() const {
	const Game& g = game;
	const V3 me = g.player->vehicle ? g.player->vehicle->pos : g.player->pos;
	std::vector<RosterRow> rows;
	for (const auto& kv : peers) {
		const Peer& P = *kv.second;
		const RemoteAvatar* av = P.avatar.get();
		if (!av) continue;
		const V3 pos = av->vehicle ? av->vehicle->pos : av->pos;
		RosterRow r;
		r.id = P.id; r.name = P.name; r.col = P.col; r.guest = P.guest;
		r.dist = Hypot(pos.x - me.x, pos.z - me.z); r.where = g.map.ZoneName(pos.x, pos.z);
		r.hp = JNumAt(P.st, "hp", 100); r.dead = JTruthy(P.st.get("d")); r.pvp = JTruthy(P.st.get("pvp")); r.paused = JTruthy(P.st.get("pz"));
		r.story = JStrAt(P.st, "m") == "s";
		const Json* vh = P.st.get("vh");
		if (vh && vh->isObj()) {
			if (const VehicleDef* d = FindVehicle(JStrAt(*vh, "t")); d && !vh->get("ref")) r.vehicle = d->name;
			else if (vh->get("ref")) r.vehicle = "passenger";
		}
		r.x = pos.x; r.z = pos.z;
		rows.push_back(r);
	}
	std::sort(rows.begin(), rows.end(), [](const RosterRow& a, const RosterRow& b) { return a.dist < b.dist; });
	return rows;
}

} // namespace atg
