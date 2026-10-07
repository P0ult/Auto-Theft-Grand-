#include "Missions.h"
#include "Combat.h"
#include "Game.h"
#include "Gameplay.h"
#include "Hud.h"
#include "Police.h"
#include "Rail.h"

namespace atg {
namespace {
size_t ScriptTextLength(const std::string& text) {
	size_t n = 0; for (unsigned char c : text) if (c < 0x80 || c >= 0xc0) n += c >= 0xf0 ? 2 : 1;
	return n; // JavaScript string.length counts UTF-16 code units, not UTF-8 bytes
}
std::string RewardText(double value) {
	std::string s = std::to_string((int)value);
	for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, ",");
	return "$" + s;
}
}

RouteDriver::RouteDriver(Game& g, Vehicle* v, const V3& to, const RouteDriverOpts& opts)
	: LaneDriver(g, v, nullptr, true), dest(to), flee(opts.flee), arriveR(opts.arriveR) {
	fixedCruise = true; cruise = opts.speed; ignoreLights = opts.ignoreLights;
	LaneStart st;
	const double yaw = flee ? v->yaw : std::atan2(dest.x - v->pos.x, dest.z - v->pos.z);
	if (!NearestLane(net, v->pos.x, v->pos.z, yaw, true, st)) return;
	const auto& e = net.edges[st.e];
	if (!flee && e.lanesF > 0 && e.lanesB > 0) {
		const auto r = net.FindRoute(v->pos.x, v->pos.z, dest.x, dest.z);
		if (r && !r->seq.empty()) {
			const int dir = r->seq[0].node == e.b ? 0 : 1;
			if (dir != st.dir && v->speedAbs() < 3) { st.dir = dir; st.lane = 0; st.s = e.len - st.s; }
		}
	}
	if (!flee && opts.snap && v->speedAbs() < 2) {
		const auto pts = net.LanePath(e, st.dir, st.lane);
		int bi = 0; double bd = kInf;
		for (int i = 0; i < (int)pts.size() - 1; i++) { const double d = Dist2(pts[i].x, pts[i].z, v->pos.x, v->pos.z); if (d < bd) { bd = d; bi = i; } }
		if (pts.size() > 1) {
			const auto& a = pts[bi]; const auto& b = pts[Min((int)pts.size() - 1, bi + 1)];
			const double lyaw = std::atan2(b.x - a.x, b.z - a.z);
			if (std::fabs(WrapAngle(lyaw - v->yaw)) > kPi / 2) { v->yaw = lyaw; v->pos.x = a.x; v->pos.z = a.z; }
		}
	}
	start(st);
}

bool RouteDriver::chooseNext(const LanePath& cur, Exit& out) {
	const auto opts = options(cur);
	if (opts.empty()) return false;
	if (flee) {
		const V3 p = game.player->vehicle ? game.player->vehicle->pos : game.player->pos;
		double bd = -kInf; out = opts[0];
		for (const auto& o : opts) { const auto& e = net.edges[o.e]; const auto& far = net.nodes[o.dir == 0 ? e.b : e.a]; const double d = Hypot(far.x - p.x, far.z - p.z) + Rand() * 40; if (d > bd) { bd = d; out = o; } }
		return true;
	}
	const auto& node = net.nodes[cur.node];
	for (int attempt = 0; attempt < 2; attempt++) {
		if (!route) route = net.FindRoute(node.x, node.z, dest.x, dest.z);
		if (route) {
			const auto& seq = route->seq;
			int k = -1; for (int i = 0; i < (int)seq.size(); i++) if (seq[i].node == cur.node) { k = i; break; }
			int next = -1;
			if (k >= 0 && k + 1 < (int)seq.size()) next = seq[k + 1].edge;
			else if (k < 0 && !seq.empty()) { const auto& e = net.edges[seq[0].edge]; if (e.a == cur.node || e.b == cur.node) next = e.id; }
			if (next >= 0) for (const auto& o : opts) if (o.e == next) { out = o; return true; }
			if (k >= 0 && k == (int)seq.size() - 1) for (const auto& o : opts) if (o.e == route->goal.e) { out = o; return true; }
		}
		route.reset();
	}
	double bd = kInf; out = opts[0];
	for (const auto& o : opts) { const auto& e = net.edges[o.e]; const auto& far = net.nodes[o.dir == 0 ? e.b : e.a]; const double d = Hypot(far.x - dest.x, far.z - dest.z) + Rand() * 5; if (d < bd) { bd = d; out = o; } }
	return true;
}

void RouteDriver::update(double dt) {
	if (!flee && Hypot(dest.x - veh->pos.x, dest.z - veh->pos.z) < arriveR) { arrived = true; veh->input.throttle = 0; veh->input.brake = 1; veh->input.handbrake = veh->speedAbs() < 1; return; }
	LaneDriver::update(dt);
}

void RaceDriver::update(double dt) {
	Vehicle& v = *veh;
	if (!v.driver() || v.isWrecked() || done || points.empty()) { v.input.throttle = 0; v.input.brake = 1; return; }
	const V3 tp = points[idx]; const double d = Hypot(tp.x - v.pos.x, tp.z - v.pos.z);
	if (d < 14) { idx++; if (idx >= (int)points.size()) { done = true; return; } }
	const V3 next = points[Min(idx, (int)points.size() - 1)], after = points[Min(idx + 1, (int)points.size() - 1)];
	const double k = Clamp(1 - d / 40, 0, 0.6);
	double lx, lz; v.worldToLocal(next.x + (after.x - next.x) * k, next.z + (after.z - next.z) * k, lx, lz);
	const double ang = std::atan2(lx, Max(0.5, lz)); double steer = ang / (v.def.steer * 0.7);
	auto probe = [&](double a) { RayHit h; RayOpts o; o.ignoreProps = true; return game.collision->raycast(v.pos.x, v.pos.y + 0.8, v.pos.z, std::sin(v.yaw + a), 0, std::cos(v.yaw + a), 16, h, o) ? h.t : 16; };
	if (probe(0.3) < 7) steer -= 0.7; if (probe(-0.3) < 7) steer += 0.7;
	for (const auto& other : game.vehicles.list) {
		if (other.get() == &v) continue;
		double ox, oz; v.worldToLocal(other->pos.x, other->pos.z, ox, oz);
		if (oz > 0 && oz < 14 && std::fabs(ox) < 2.4) steer += ox > 0 ? -0.6 : 0.6;
	}
	v.input.steer = Clamp(steer, -1, 1);
	const double turn = std::fabs(ang), want = (turn > 0.9 ? 12 : turn > 0.5 ? 20 : v.def.top * 0.95) * (0.8 + skill * 0.25), sp = v.speed();
	if (rev > 0) { rev -= dt; v.input.throttle = 0; v.input.brake = 1; v.input.steer = -v.input.steer; return; }
	if (std::fabs(sp) < 1.2) { stuck += dt; if (stuck > 1.6) { stuck = 0; rev = 1; } } else stuck = 0;
	v.input.throttle = sp < want ? 1 : 0; v.input.brake = sp > want + 4 ? 0.6 : 0;
	v.input.handbrake = turn > 1 && sp > 14;
}

double RaceDriver::progress() const { if (points.empty()) return 0; const V3& tp = points[Min(idx, (int)points.size() - 1)]; return idx * 1000 - Hypot(tp.x - veh->pos.x, tp.z - veh->pos.z); }

MissionContext::MissionContext(Missions& e, const MissionDef& d) : engine(e), game(e.game), def(d), noSpray(d.noSpray) {}
Player& MissionContext::player() const { return *game.player; }

MissionContext::Until MissionContext::until(std::function<bool(double)> checkFn, const UntilOpts& opts) {
	auto w = std::make_shared<Waiter>(); std::weak_ptr<Waiter> weak(w); const double startTime = t;
	w->predicate = [this, checkFn = std::move(checkFn), opts, startTime, weak](double dt) {
		if (opts.timeout && t - startTime > opts.timeout) {
			if (!opts.resolveTimeout) throw MissionFail(opts.timeoutReason);
			if (auto state = weak.lock()) state->timeout = true;
			return true;
		}
		return checkFn(dt);
	};
	return Until{ this, w };
}
MissionContext::Until MissionContext::until(std::function<bool()> fn, const UntilOpts& opts) { return until([fn](double) { return fn(); }, opts); }
MissionContext::Until MissionContext::wait(double sec) { const double end = t + sec; return until([this, end]() { return t >= end; }); }

std::function<void()> MissionContext::tick(std::function<void(double)> fn) {
	const int id = ++nextFn; tickers.push_back({ id, std::move(fn) });
	return [this, id]() { std::erase_if(tickers, [id](const auto& p) { return p.first == id; }); };
}
std::function<void()> MissionContext::failIf(std::function<bool()> fn, const std::string& reason) { return failIf(std::function<std::string()>([fn, reason]() { return fn() ? reason : ""; })); }
std::function<void()> MissionContext::failIf(std::function<std::string()> fn) {
	const int id = ++nextFn; fails.push_back({ id, std::move(fn) });
	return [this, id]() { std::erase_if(fails, [id](const auto& p) { return p.first == id; }); };
}
std::function<void()> MissionContext::timer(double sec, const std::string& reason) {
	const double end = t + sec;
	auto stop = tick([this, end](double) { if (game.hud) game.hud->setTimer(end - t); });
	auto unfail = failIf([this, end]() { return t > end; }, reason);
	return [this, stop, unfail]() { stop(); unfail(); if (game.hud) game.hud->setTimer(NaN()); };
}

void MissionContext::reject(std::exception_ptr error) { pendingError = error; }
void MissionContext::abort() { aborted = true; reject(std::make_exception_ptr(MissionAbort())); }
void MissionContext::poll(double dt) {
	t += dt;
	const auto ticks = tickers; for (const auto& fn : ticks) fn.second(dt);
	const auto failFns = fails;
	for (const auto& f : failFns) {
		std::string reason; try { reason = f.second(); } catch (...) {}
		if (!reason.empty()) { reject(std::make_exception_ptr(MissionFail(reason))); break; }
	}
	if (pendingError) {
		const auto ws = std::move(waiters); waiters.clear();
		for (const auto& w : ws) if (w->handle) w->handle.resume();
		return;
	}
	for (int i = (int)waiters.size() - 1; i >= 0; i--) {
		auto w = waiters[i]; bool done = false;
		try { done = w->predicate(dt); } catch (...) { w->error = std::current_exception(); done = true; }
		if (done) { waiters.erase(waiters.begin() + i); w->handle.resume(); }
	}
}

void MissionContext::help(const std::string& text, double dur) { if (game.hud) game.hud->help(text, dur); }
void MissionContext::objective(const std::string& text) { if (game.hud) game.hud->objective(text); }
MissionTask MissionContext::say(const std::string& speaker, const std::string& text, double dur) {
	const double d = IsSet(dur) ? dur : Clamp(1.6 + ScriptTextLength(text) * 0.052, 2.2, 7);
	if (game.hud) game.hud->subtitle(text, speaker, d + 0.2);
	Ref<Character> spk = speakers[speaker];
	if (spk && !spk->dead) spk->animState.talking = true;
	const double end = t + d;
	co_await until([this, end]() { return t >= end || (inCutscene && game.input.hit("skip")); });
	if (spk && !spk->dead) spk->animState.talking = false;
}
MissionTask MissionContext::lines(std::vector<Dialogue> list) { for (const auto& line : list) co_await say(line.speaker, line.text, line.duration); }

MissionTask MissionContext::cutscene(std::function<MissionTask()> fn) {
	inCutscene = true; game.cutscene = true;
	if (game.hud) game.hud->letterbox(true);
	if (game.policeSys) game.policeSys->enabled = false;
	player().moveTargetX = player().moveTargetZ = 0; player().aiming = false;
	struct Guard {
		MissionContext& m;
		~Guard() {
			m.inCutscene = false; m.game.cutscene = false;
			if (m.game.hud) m.game.hud->letterbox(false);
			m.game.rig.clearCinematic(); if (m.game.policeSys) m.game.policeSys->enabled = true;
			for (const auto& kv : m.speakers) if (kv.second && !kv.second->dead) kv.second->animState.talking = false;
		}
	} guard{ *this };
	co_await fn();
}
void MissionContext::shot(const V3& from, const V3& to, double fov, bool snap) { game.rig.setCinematic(from, to, fov); if (snap) { game.rig.camPos = from; game.rig.fov = fov; game.rig.update(0, nullptr, player()); } }
void MissionContext::twoShot(Character* a, Character* b, double side, double dist, double height) {
	const V3 mid((a->pos.x + b->pos.x) / 2, (a->pos.y + b->pos.y) / 2 + height, (a->pos.z + b->pos.z) / 2);
	const double dx = b->pos.x - a->pos.x, dz = b->pos.z - a->pos.z, len = Hypot(dx, dz), l = len ? len : 1;
	shot(mid + V3(-dz / l * side * dist, 0.15, dx / l * side * dist), mid, 45);
}
void MissionContext::overShoulder(Character* from, Character* to, double dist) {
	const V3 d = V3(to->pos.x - from->pos.x, 0, to->pos.z - from->pos.z).normalized(), side(-d.z, 0, d.x);
	shot(from->pos + V3(0, 1.7, 0) - d * dist + side * 0.55, to->pos + V3(0, 1.55, 0), 40);
}
void MissionContext::face(Character* a, Character* b) { a->faceTowards(b->pos.x, b->pos.z, 0); }

Ped* MissionContext::ped(double x, double z, const MissionPedOpts& opts) {
	PedOpts o = opts; o.persistent = true;
	if (opts.defaultScript && o.gang.empty()) { o.brain = "script"; o.state = "idle"; }
	Ped* p = game.peds->spawnPed(x, z, o);
	p->invincible = opts.invincible; peds.push_back(std::static_pointer_cast<Ped>(p->shared_from_this())); return p;
}
Ped* MissionContext::enemy(double x, double z, const MissionEnemyOpts& opts) {
	PedOpts o = opts; o.persistent = true; o.brain = "gang"; o.state = opts.guard ? "guard" : "attack";
	Ped* p = game.peds->spawnPed(x, z, o); p->threat = &player(); p->missionEnemy = true;
	if (opts.guard) { p->hasGuardFace = true; p->guardFace = IsSet(opts.face) ? opts.face : Rand() * 6.28; }
	peds.push_back(std::static_pointer_cast<Ped>(p->shared_from_this()));
	if (opts.blip) blipEntity(p, 0xff3030, "dot", true); if (opts.arrow) targetArrow(p);
	return p;
}
void MissionContext::targetArrow(Ped* p) { p->targetArrow = true; p->targetArrowPhase = Rand() * 6; }
Vehicle* MissionContext::car(const std::string& type, double x, double z, double yaw, const SpawnOpts& opts) {
	SpawnOpts o = opts; o.persistent = true;
	Vehicle* v = game.vehicles.spawn(type, x, z, yaw, o); cars.push_back(v->shared_from_this()); return v;
}
Ped* MissionContext::driver(Vehicle* v, const MissionPedOpts& opts, int seat) { Ped* p = ped(v->pos.x, v->pos.z, opts); v->putIn(p, seat); return p; }
Ped* MissionContext::follower(Ped* p, int slot) { p->brain = "civilian"; p->state = "follow"; p->follow = &player(); p->followSlot = slot; p->persistent = true; return p; }
Marker* MissionContext::marker(double x, double z, const MarkerOpts& opts) { Marker* m = game.pickupsSys->addMarker(x, z, opts); for (const auto& q : game.pickupsSys->markers) if (q.get() == m) { markers.push_back(q); break; } return m; }
void MissionContext::removeMarker(Marker* m) { game.pickupsSys->removeMarker(m); }
std::shared_ptr<Blip> MissionContext::blipEntity(Character* c, uint32_t color, const std::string& icon, bool small) {
	Blip b; b.color = color; b.icon = icon; b.small = small; b.character = c; auto out = game.addBlip(b); blips.push_back(out);
	Ref<Character> weak(c);
	auto upd = [this, weak, out](double) {
		Character* ent = weak.get(); if (!ent || ent->removed || ent->dead) { game.removeBlip(out); return; }
		const V3 p = ent->vehicle ? ent->vehicle->pos : ent->ragdolling && ent->ragdoll ? ent->ragdoll->center() : ent->pos; out->x = p.x; out->z = p.z;
	}; upd(0); tick(upd); return out;
}
std::shared_ptr<Blip> MissionContext::blipEntity(Vehicle* v, uint32_t color, const std::string& icon, bool small) {
	Blip b; b.color = color; b.icon = icon; b.small = small; b.vehicle = v; auto out = game.addBlip(b); blips.push_back(out); Ref<Vehicle> weak(v);
	auto upd = [this, weak, out](double) { if (!weak || weak->removed || weak->isWrecked()) { game.removeBlip(out); return; } out->x = weak->pos.x; out->z = weak->pos.z; };
	upd(0); tick(upd); return out;
}
void MissionContext::unblip(const std::shared_ptr<Blip>& b) { game.removeBlip(b); }
void MissionContext::gps(double x, double z) { if (game.hud) game.hud->routeTo(V2(x, z)); }
void MissionContext::gpsOff() { if (game.hud) game.hud->routeTo(std::nullopt); }

MissionTask MissionContext::goTo(double x, double z, const GoToOpts& opts) {
	MarkerOpts o; o.radius = IsSet(opts.radius) ? opts.radius : opts.vehicle ? 4 : 1.6; o.color = opts.color; o.label = opts.label;
	o.vehicleOnly = opts.vehicle; o.footOnly = opts.onFoot; o.icon = "dot"; o.height = opts.height; o.hasY = IsSet(opts.y); o.y = opts.y;
	Marker* m = marker(x, z, o);
	if (!opts.text.empty()) objective(opts.text); gps(x, z);
	bool entered = false; m->onEnter = [&entered](Marker*) { entered = true; };
	co_await until([this, &entered, opts]() {
		if (!entered) return false;
		if (opts.inCar && player().vehicle != opts.inCar) { entered = false; help(opts.inCarMsg); return false; }
		if (opts.slow && player().vehicle && player().vehicle->speedAbs() > 6) return false;
		if (opts.condition && !opts.condition()) { entered = false; return false; }
		return true;
	});
	removeMarker(m); gpsOff(); game.sound("checkpoint");
	if (opts.stop && player().vehicle) player().vehicle->input.brake = 1;
}
MissionTask MissionContext::getIn(Vehicle* v, const std::string& text) {
	if (!text.empty()) objective(text); auto b = blipEntity(v, 0x4aa3ff, "car"); gps(v->pos.x, v->pos.z);
	co_await until([this, v]() { return player().vehicle == v && !game.vehicles.isBusy(&player()); }); unblip(b); gpsOff();
}
MissionTask MissionContext::killAll(std::vector<Ped*> list, const std::string& text, const std::string& counter) {
	if (!text.empty()) objective(text); const int total = (int)list.size();
	co_await until([this, list, counter, total]() {
		int left = 0; for (const auto* p : list) if (!p->dead && !p->removed) left++;
		if (!counter.empty() && game.hud) game.hud->setCounter(counter, std::to_string(total - left) + "/" + std::to_string(total));
		return left == 0;
	}); if (!counter.empty() && game.hud) game.hud->setCounter("", "");
}
MissionTask MissionContext::loseWanted(const std::string& text) { if (game.police && game.police->wantedLevel()) { objective(text); co_await until([this]() { return !game.police->wantedLevel(); }); } }
MissionTask MissionContext::airRing(double x, double z, double alt, double radius, std::optional<V3> next, const std::string& text) {
	const V3 c(x, Max(game.map.GroundHeight(x, z), 0) + alt, z);
	currentRing = AirRing{ c, radius, next ? std::atan2(next->x - x, next->z - z) : 0 };
	Blip mark; mark.x = x; mark.z = z; mark.color = 0xffd23f; mark.icon = "flag"; auto b = game.addBlip(mark); blips.push_back(b);
	if (!text.empty()) objective(text);
	struct RingCleanup { MissionContext& ctx; std::shared_ptr<Blip> blip; ~RingCleanup() { ctx.currentRing.reset(); ctx.game.removeBlip(blip); } } ringGuard{ *this, b };
	co_await until([this, c, radius]() { Vehicle* v = player().vehicle; const V3 p = v ? v->def.aircraft ? v->cgPoint() : v->pos : player().pos; return p.distanceTo(c) < radius * 1.15; });
	game.sound("checkpoint");
}
void MissionContext::wanted(int level) { if (game.policeSys) game.policeSys->setLevel(Max(game.policeSys->level, level)); }
std::function<void()> MissionContext::keepAlive(Character* c, const std::string& reason) { Ref<Character> weak(c); return failIf([weak]() { return !weak || weak->dead; }, reason); }
std::function<void()> MissionContext::keepAlive(Vehicle* v, const std::string& reason) { Ref<Vehicle> weak(v); return failIf([weak]() { return !weak || weak->isWrecked(); }, reason); }
void MissionContext::cash(double amount) { player().money += amount; if (game.hud) game.hud->moneyFlash(amount); }
double MissionContext::distTo(const V3& pos) const { const V3 pp = player().vehicle ? player().vehicle->pos : player().pos; return Hypot(pp.x - pos.x, pp.z - pos.z); }
double MissionContext::distTo(Character* c) const { return distTo(c->vehicle ? c->vehicle->pos : c->pos); }
double MissionContext::distTo(Vehicle* v) const { return distTo(v->pos); }
std::function<void()> MissionContext::driveBy(Ped* p, Character* target, double range) {
	Ref<Ped> weak(p); Ref<Character> tg(target); double fireT = Rand(0.5, 1.5);
	return tick([this, weak, tg, range, fireT](double dt) mutable {
		Ped* shooter = weak.get(); if (!shooter || shooter->dead || !shooter->vehicle) return;
		Character* victim = tg ? tg.get() : &player(); const V3 tp = victim->vehicle ? victim->vehicle->pos : victim->pos;
		const double d = Hypot(tp.x - shooter->vehicle->pos.x, tp.z - shooter->vehicle->pos.z); shooter->aiming = d < range; fireT -= dt;
		if (d < range && fireT <= 0) {
			fireT = Rand(0.35, 1); const V3 from = shooter->vehicle->pos + V3(0, 1.3, 0), to(tp.x + Rand(-1.5, 1.5), tp.y + 1, tp.z + Rand(-1.5, 1.5)), dir = (to - from).normalized();
			CombatHit hit; auto* combat = dynamic_cast<Combat*>(game.combat); const bool h = combat && combat->raycast(from.x, from.y, from.z, dir.x, dir.y, dir.z, 60, shooter, hit);
			if (game.effects) { game.effects->tracer(from, h ? hit.point : from + dir * 60); game.effects->muzzleFlash(from, dir, false); }
			WeaponDef wd; wd.id = "smg"; wd.damage = 9; if (h) combat->applyHit(hit, wd, shooter, dir);
			SoundOpts so; so.gun = true; game.soundAt("smg", from, 0.9, so);
		}
	});
}

void MissionContext::cleanup(bool passed) {
	(void)passed;
	for (const auto& fn : cleanupFns) fn(); cleanupFns.clear();
	if (game.hud) { game.hud->setTimer(NaN()); game.hud->setCounter("", ""); game.hud->setBar(nullptr); game.hud->clearObjective(); gpsOff(); }
	for (const auto& m : markers) game.pickupsSys->removeMarker(m.get());
	for (const auto& b : blips) game.removeBlip(b);
	for (const auto& p : peds) {
		p->targetArrow = false;
		if (p->removed || p->missionKeep) continue;
		p->persistent = false; p->invincible = false;
		if (p->brain == "script") { p->brain = "civilian"; p->state = "wander"; p->node = -1; p->scriptThink = nullptr; }
		if (p->state == "follow") { p->state = "wander"; p->follow = nullptr; }
	}
	for (const auto& v : cars) if (!v->removed && !v->missionKeep) { v->persistent = false; v->locked = false; if (v->ai && !v->traffic) v->ai.reset(); }
	game.cutscene = false; if (game.hud) game.hud->letterbox(false); game.rig.clearCinematic();
	if (game.policeSys) game.policeSys->enabled = true;
	engine.maxWanted = NaN(); engine.noBust = false; game.missionNoSpray = false; game.missionActive = false;
	if (game.rail && game.rail->held) game.rail->releaseLine();
}

Missions::Missions(Game& g) : game(g), maxWanted(g.missionMaxWanted), noBust(g.missionNoBust), gangDensity(g.gangDensity) {
	g.events.playerDied.on([this]() { failActive("You died."); });
	g.events.busted.on([this]() { if (active && !noBust) failActive("You got busted."); });
	g.canEnterVehicle = [this](Vehicle* v) { return canEnterVehicle(v); };
}
std::vector<const MissionDef*> Missions::available() const {
	std::vector<const MissionDef*> out;
	for (const auto& m : story) if (!completed.count(m.id) && std::all_of(m.requiresIds.begin(), m.requiresIds.end(), [this](const auto& id) { return completed.count(id); })) out.push_back(&m);
	return out;
}
void Missions::refreshContacts() {
	for (const auto& m : contactMarkers) game.pickupsSys->removeMarker(m.get()); contactMarkers.clear();
	if (active) return;
	std::set<std::pair<long long, long long>> shown;
	for (const auto* def : available()) {
		if (!def->start) continue;
		const V3 pos = def->start(game.map);
		const auto key = std::make_pair((long long)std::floor(pos.x + 0.5), (long long)std::floor(pos.z + 0.5));
		if (!shown.insert(key).second) continue;
		MarkerOpts o; o.color = 0xffd23f; o.radius = 1.3; o.label = def->title; o.icon = "dot"; o.footOnly = !def->startInCar;
		const std::string id = def->id; o.onEnter = [this, id](Marker*) { start(id); };
		Marker* m = game.pickupsSys->addMarker(pos.x, pos.z, o); m->blip->letter = def->contact; m->blip->color = 0xffd23f;
		for (const auto& p : game.pickupsSys->markers) if (p.get() == m) { contactMarkers.push_back(p); break; }
	}
}
bool Missions::start(const std::string& id) { for (const auto& d : story) if (d.id == id) return start(d); return false; }
bool Missions::start(const MissionDef& def) {
	if (active || !def.run) return false;
	if (def.autoStart) { if (game.police) game.police->clearWanted(); }
	else if (game.police && game.police->wantedLevel() > 0 && !def.allowWanted) { if (game.hud) game.hud->help("Lose your wanted level before starting a mission."); return false; }
	for (const auto& m : contactMarkers) game.pickupsSys->removeMarker(m.get()); contactMarkers.clear();
	active = std::make_shared<MissionContext>(*this, def); game.missionActive = true; game.missionNoSpray = active->noSpray;
	if (game.hud) game.hud->bigMessage(def.title, "title", 3.5);
	game.events.missionStart.emit(def.id);
	active->task = def.run(*active, game); return true;
}
void Missions::update(double dt) {
	if (!active) return;
	try { active->poll(dt); } catch (...) { active->reject(std::current_exception()); active->poll(0); }
	if (active && active->task.done()) finish();
}
void Missions::finish() {
	auto ctx = active;
	if (auto err = ctx->task.error()) {
		try { std::rethrow_exception(err); }
		catch (const MissionAbort&) { ctx->cleanup(false); active.reset(); refreshContacts(); }
		catch (const MissionFail& e) { fail(ctx, e.what()); }
		catch (...) { fail(ctx, "Something went wrong."); }
	} else pass(ctx);
}
void Missions::pass(const std::shared_ptr<MissionContext>& ctx) {
	const MissionDef d = ctx->def;
	ctx->cleanup(true); completed.insert(d.id); active.reset();
	if (d.reward) game.player->money += d.reward;
	game.stats.missions++;
	if (game.hud) game.hud->bigMessage("MISSION PASSED!", "passed", 5, d.reward ? RewardText(d.reward) : "RESPECT +");
	game.sound("passed"); if (!d.log.empty()) log.push_back(d.log); game.events.missionPassed.emit(d.id);
	std::string next; for (const auto* m : available()) if (m->autoStart) { next = m->id; break; }
	game.setTimeout(5.5, [this, d, next]() { if (d.after) d.after(game); if (!next.empty()) start(next); else refreshContacts(); if (autosave) autosave(); });
	if (d.chapterEnd.size() >= 2) game.setTimeout(6, [this, d]() { if (game.hud) game.hud->bigMessage(d.chapterEnd[0], "chapter", 5, d.chapterEnd[1]); });
}
void Missions::fail(const std::shared_ptr<MissionContext>& ctx, const std::string& reason) {
	ctx->cleanup(false); active.reset();
	auto show = [this, reason]() { if (game.hud) game.hud->bigMessage("MISSION FAILED!", "failed", 4.5, reason); game.sound("failed"); };
	if (game.gameplay && (game.gameplay->state == "dead" || game.gameplay->state == "busted" || game.gameplay->state == "respawning")) game.gameplay->afterRespawn = show; else show();
	game.events.missionFailed.emit(ctx->def.id); game.setTimeout(4, [this]() { refreshContacts(); });
}
void Missions::failActive(const std::string& reason) { if (active) active->reject(std::make_exception_ptr(MissionFail(reason))); }
void Missions::abortActive() { if (active) active->abort(); }
void Missions::shutdown() { if (active) { active->task = MissionTask(); active->cleanup(false); active.reset(); } }
bool Missions::canEnterVehicle(Vehicle* v) const { return !active || std::find(active->lockedCars.begin(), active->lockedCars.end(), v) == active->lockedCars.end(); }
void Missions::load(const std::set<std::string>& ids, const std::vector<std::string>& entries) { completed = ids; log = entries; }
} // namespace atg
