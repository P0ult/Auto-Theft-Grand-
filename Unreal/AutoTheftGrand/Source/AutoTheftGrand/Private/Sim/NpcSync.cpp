#include "NpcSync.h"
#include "Animator.h"
#include "Game.h"
#include "Peds.h"
#include "Player.h"
#include "Ragdoll.h"
#include "Rail.h"
#include "Train.h"
#include "Vehicle.h"
#include "Weapons.h"
#include "VehicleDefs.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace atg {

namespace {
constexpr double SEND_HZ = 6;
constexpr double SHARE_R = 260;     // NPCs this close to another player are shared with them
constexpr double SHOW_R = 360;      // draw others' NPCs this close to us
constexpr size_t MAX_CARS = 18, MAX_PEDS = 24;
constexpr double DELAY = 260;       // ms of interpolation delay (sends are 170 ms apart)
const char* const HAIR[] = { "short", "bald", "afro", "cap", "buzz", "long", "ponytail", "bun" };
const char* const SHIRT[] = { "tee", "long", "tank", "jacket" };
constexpr double kInfinity = std::numeric_limits<double>::infinity();

double R1(double x) { return std::floor(x * 10 + 0.5) / 10; }
Json JN(double v) { return Json::Number(v); }
Json JS(const std::string& v) { return Json::String(v); }
// col(): a colour from someone else's data, or grey
uint32_t Col(const Json* x) { return x && x->integer() && x->n >= 0 && x->n <= 0xffffff ? (uint32_t)x->n : 0x888888; }
bool IsOne(const Json* x) { return x && x->type == Json::Num && x->n == 1; }
int IndexOf(const std::string& s, const char* const* list, int n) { for (int i = 0; i < n; i++) if (s == list[i]) return i; return -1; }
const char* Pick(const Json* x, const char* const* list, int n, const char* alt) {
	return x && x->integer() && x->n >= 0 && x->n < n ? list[(int)x->n] : alt;
}
int VehicleIndex(const std::string& type) {
	const auto& D = VehicleDefs();
	for (size_t i = 0; i < D.size(); i++) if (D[i].id == type) return (int)i;
	return -1;
}
const VehicleDef* VehicleAt(double i) {
	const auto& D = VehicleDefs();
	return std::isfinite(i) && i >= 0 && i < (double)D.size() && i == std::floor(i) ? &D[(size_t)i] : nullptr;
}
int WeaponIndex(const std::string& id) {
	const auto& W = WeaponOrder();
	for (size_t i = 0; i < W.size(); i++) if (W[i] == id) return (int)i;
	return -1;
}
// a record from the world channel: every field a number (someone else's data)
bool Record(const Json& arr, size_t from, size_t n, std::vector<double>& out) {
	if (from + n > arr.a.size()) return false;
	out.resize(n);
	for (size_t i = 0; i < n; i++) { const Json& x = arr.a[from + i]; if (x.type != Json::Num || !std::isfinite(x.n)) return false; out[i] = x.n; }
	return out[0] == std::floor(out[0]);
}
V3 PosOf(const Character* c) { return c->vehicle ? c->vehicle->pos : c->pos; }
} // namespace

// ------------------------------------------------------------------ appearance <-> compact array
Json PackLook(const Appearance& a) {
	Json q = Json::Array();
	for (double x : { a.female ? 1.0 : 0.0, (double)a.skin, (double)a.hair, (double)IndexOf(a.hairStyle, HAIR, 8), (double)a.shirt, (double)IndexOf(a.shirtType, SHIRT, 4),
			(double)a.pants, a.shorts ? 1.0 : 0.0, (double)a.shoes, (double)a.hat, JsRound((a.build ? a.build : 1) * 100), JsRound((a.height ? a.height : 1) * 100),
			a.glasses ? 1.0 : 0.0, a.beard ? 1.0 : 0.0, (double)a.jacketColor, (double)a.bandana,
			a.hasUniform ? (double)a.uniformShirt : -1.0, a.hasUniform ? (double)a.uniformPants : -1.0, a.hasUniform ? (double)a.uniformHat : -1.0 })
		q.push(JN(x));
	return q;
}

bool UnpackLook(const Json* q, Appearance& a) {
	if (!q || !q->isArr() || q->a.size() < 16) return false;
	auto at = [&](size_t i) { return &q->a[i]; };
	auto opt = [&](size_t i) -> int64_t { return at(i)->type == Json::Num && at(i)->n == -1 ? -1 : (int64_t)Col(at(i)); };
	a = Appearance();
	a.female = IsOne(at(0)); a.skin = Col(at(1)); a.hair = Col(at(2)); a.hairStyle = Pick(at(3), HAIR, 8, "short"); a.shirt = Col(at(4));
	a.shirtType = Pick(at(5), SHIRT, 4, "tee"); a.pants = Col(at(6)); a.shorts = IsOne(at(7)); a.shoes = Col(at(8)); a.hat = opt(9);
	a.build = Clamp(JNum(at(10), 100) / 100, 0.85, 1.25); a.height = Clamp(JNum(at(11), 100) / 100, 0.9, 1.1);
	a.glasses = IsOne(at(12)); a.beard = IsOne(at(13)); a.jacketColor = Col(at(14)); a.bandana = opt(15);
	a.hasUniform = false;
	if (q->a.size() >= 19 && !(at(16)->type == Json::Num && at(16)->n == -1)) {
		a.hasUniform = true; a.uniformShirt = Col(at(16)); a.uniformPants = Col(at(17)); a.uniformHat = opt(18);
	}
	return true;
}

// ------------------------------------------------------------------ a stand-in pedestrian
NpcProxy::NpcProxy(Game& g, NpcSync& s, const std::string& own, int id, const Appearance& look)
	: Character(g, look, 100, 0, "remote"), sync(s), owner(own) {
	netId = id;
	remote = true; npcProxy = true;
	invincible = false;
}

void NpcProxy::update(double dt) {
	if (removed) return;
	if (vehicle) {
		AnimState& st = animState;
		seatState(st); st.speed = 0; st.grounded = true; st.swim = false; st.weapon = "none";
		anim->update(dt, st);
		updatePose();
		return;
	}
	if (ragdolling) { ragdoll->update(dt); ragdoll->apply(rootMatrix()); updatePose(); return; }
	animState.sit = 0;
	anim->update(dt, animState);
	updatePose();
	orientWeapon();
}

bool NpcProxy::takeDamage(double amount, const DamageInfo& info) {
	if (dead) return false;
	double dmg = amount;
	if (info.part == "head") dmg *= IsSet(info.headMul) ? info.headMul : 4;
	if (info.part == "limb") dmg *= 0.7;
	sync.sendPedHit(*this, dmg, info);
	if (!ragdolling && !vehicle && info.type != "fire") anim->play("flinch");
	return false;
}

// (knockDown: the owner decides; its state tells us)
void NpcProxy::setDown(bool down, bool isDead) {
	if (down && !ragdolling && !vehicle) startRagdoll(V3());
	else if (!down && ragdolling) { ragdolling = false; anim->beginBlend(0.25); }
	dead = isDead;
}

// ------------------------------------------------------------------ NpcSync
NpcSync::NpcSync(NetSystem& n) : net(n), game(n.game) {}

// other players near a point (for spawning budgets and keeping NPCs alive)
int NpcSync::peersNear(double x, double z, double R) const {
	int n = 0;
	for (const auto& kv : net.peers) {
		const RemoteAvatar* a = kv.second->avatar.get();
		if (!a) continue;
		const V3 p = PosOf(a);
		if (Dist2(p.x, p.z, x, z) < R * R) n++;
	}
	return n;
}
// stand-ins near a point (spawning counts them)
int NpcSync::proxyCarsNear(double x, double z, double R) const {
	int n = 0;
	for (const auto& kv : net.peers) if (kv.second->npc) for (const auto& [id, ref] : kv.second->npc->cars) if (Vehicle* v = ref.get()) if (Dist2(v->pos.x, v->pos.z, x, z) < R * R) n++;
	return n;
}
int NpcSync::proxyPedsNear(double x, double z, double R) const {
	int n = 0;
	for (const auto& q : pedList) if (!q->vehicle && Dist2(q->pos.x, q->pos.z, x, z) < R * R) n++;
	return n;
}

int NpcSync::idOf(Character* c) { if (!c->netId) c->netId = nextId++; return c->netId; }
int NpcSync::idOf(Vehicle* v) { if (!v->netId) v->netId = nextId++; return v->netId; }

// ------------------------------------------------------------------ outgoing
Json NpcSync::build() {
	Game& g = game;
	if (net.peers.empty()) return Json();
	std::vector<V3> ppos;
	for (const auto& kv : net.peers) if (kv.second->avatar) ppos.push_back(PosOf(kv.second->avatar.get()));
	auto nearest = [&](const V3& p) { double best = kInfinity; for (const V3& q : ppos) best = std::min(best, Dist2(q.x, q.z, p.x, p.z)); return std::sqrt(best); };
	Player& me = *g.player;
	std::vector<std::pair<double, Vehicle*>> cars;
	std::vector<std::pair<double, Ped*>> peds;
	for (const auto& sp : g.vehicles.list) {
		Vehicle* v = sp.get();
		if (v->removed || v->remote || v->def.train || v == me.vehicle) continue;
		bool skip = false;
		for (const auto& o : v->occupants) if (o && (o->isPlayer || o->remote)) skip = true;
		if (skip) continue;
		const double d = nearest(v->pos);
		if (d < SHARE_R) cars.push_back({ d, v });
	}
	if (g.peds) for (const auto& sp : g.peds->list) {
		Ped* q = sp.get();
		if (q->removed || q->remote || q->shopClerk || (q->vehicle && q->vehicle->remote)) continue; // (every client has its own shop clerks)
		if (q->vehicle && q->seat != 0) continue;
		const V3 pos = q->ragdolling ? q->ragdoll->center() : q->pos;
		const double d = q->vehicle ? nearest(q->vehicle->pos) : nearest(pos);
		if (d < SHARE_R) peds.push_back({ d, q });
	}
	std::stable_sort(cars.begin(), cars.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	std::stable_sort(peds.begin(), peds.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	Json C = Json::Array(), Pd = Json::Array(), S = Json::Array();
	const double now = net.nowMs;
	struct Static { int id; bool car; Vehicle* v; Character* c; Character* drv; };
	std::vector<Static> statics;
	std::set<int> shown;
	for (size_t i = 0; i < cars.size() && i < MAX_CARS; i++) {
		Vehicle* v = cars[i].second;
		const int id = idOf(v); shown.insert(id); byId[id] = Shared{ nullptr, Ref<Vehicle>(v) };
		const double fwd = v->vel.x * std::sin(v->yaw) + v->vel.z * std::cos(v->yaw);
		Character* d = v->driver();
		const int drv = d && !d->isPlayer ? idOf(d) : 0;
		if (drv) { byId[drv] = Shared{ Ref<Character>(d), nullptr }; shown.insert(drv); }
		const Ped* dp = dynamic_cast<const Ped*>(d);
		const int fl = (v->input.brake > 0.1 || v->input.handbrake ? 1 : 0) | (v->sirenOn ? 2 : 0) | (v->exploded ? 4 : 0) | (v->health <= 0 ? 8 : 0) | (std::min(3, dp ? dp->npcWanted : 0) << 4);
		for (double x : { (double)id, JsRound(v->pos.x * 10), JsRound(v->pos.y * 10), JsRound(v->pos.z * 10), JsRound(v->yaw * 100), JsRound(fwd * 10), JsRound(v->steerAngle * 100), (double)fl, (double)drv })
			C.push(JN(x));
		statics.push_back({ id, true, v, nullptr, drv ? d : nullptr });
	}
	for (size_t i = 0; i < peds.size() && i < MAX_PEDS; i++) {
		Ped* q = peds[i].second;
		if (q->vehicle) continue; // (drivers ride along with their car's record)
		const int id = idOf(q); shown.insert(id); byId[id] = Shared{ Ref<Character>(q), nullptr };
		const V3 pos = q->ragdolling ? q->ragdoll->center() : q->pos;
		const AnimState& a = q->animState;
		const int fl = (q->aiming ? 2 : 0) | (a.crouch || q->crouching ? 4 : 0) | (a.handsUp ? 8 : 0) | (a.cower ? 16 : 0) | (q->dead ? 32 : 0) | (q->ragdolling ? 64 : 0)
			| (q->swimming ? 128 : 0) | (a.talking ? 256 : 0) | (std::min(3, q->npcWanted) << 9);
		for (double x : { (double)id, JsRound(pos.x * 10), JsRound(pos.y * 10), JsRound(pos.z * 10), JsRound(q->yaw * 100), JsRound(a.speed * 10), (double)fl, (double)WeaponIndex(q->weapon) })
			Pd.push(JN(x));
		statics.push_back({ id, false, nullptr, q, nullptr });
	}
	// static info (type / colour / look): new ones first, then a rotating few
	auto sentAt = [&](int id) { auto it = sentStatic.find(id); return it == sentStatic.end() ? 0.0 : it->second; };
	std::stable_sort(statics.begin(), statics.end(), [&](const Static& a, const Static& b) { return sentAt(a.id) < sentAt(b.id); });
	double budget = 900;
	for (const Static& st : statics) {
		const double last = sentAt(st.id);
		if (last && now - last < 2500) continue;
		Json rec = Json::Array();
		rec.push(JN(st.id));
		if (st.car) { rec.push(JN(0)); rec.push(JN(VehicleIndex(st.v->type))); rec.push(JN(st.v->color)); rec.push(st.drv ? PackLook(st.drv->appearance) : JN(0)); }
		else { rec.push(JN(1)); rec.push(PackLook(st.c->appearance)); }
		const double len = (double)rec.dump().size();
		if (len > budget) break;
		budget -= len;
		S.push(std::move(rec));
		sentStatic[st.id] = now;
	}
	// forget what's no longer shared
	for (auto it = byId.begin(); it != byId.end();) {
		Character* c = it->second.c.get(); Vehicle* v = it->second.v.get();
		const bool gone = (!c && !v) || (c && c->removed) || (v && v->removed);
		if (!shown.count(it->first) || gone) { sentStatic.erase(it->first); it = byId.erase(it); } else ++it;
	}
	Json out = Json::Object();
	out.set("v", JN(2)); out.set("q", JN(++seq)); out.set("c", std::move(C)); out.set("p", std::move(Pd)); out.set("s", std::move(S));
	Json tr = trainState();
	if (tr.isArr()) out.set("tr", std::move(tr));
	return out;
}

void NpcSync::send(double dt) {
	sendT -= dt;
	if (sendT > 0) return;
	sendT = 1 / SEND_HZ;
	NetTransport* t = net.transport.get();
	if (!t) return;
	Json n = build();
	if (!n.isObj()) { if (sentAny) { sentAny = false; t->sendWorld(Json()); } return; }
	// keep under the 4 KiB presence limit
	auto field = [&](const char* k) -> std::vector<Json>& { for (auto& kv : n.o) if (kv.first == k) return kv.second.a; static std::vector<Json> none; return none; };
	std::vector<Json>& P = field("p");
	std::vector<Json>& C = field("c");
	std::string s = n.dump();
	while (s.size() > 3900 && (!P.empty() || !C.empty())) {
		if (!P.empty()) P.resize(P.size() > 32 ? P.size() - 32 : 0);
		else C.resize(C.size() > 27 ? C.size() - 27 : 0);
		s = n.dump();
	}
	sentAny = true;
	t->sendWorld(n);
}

// ------------------------------------------------------------------ incoming
void NpcSync::onWorld(NetSystem::Peer* P, const Json* n) {
	if (!P) return;
	if (!P->npc) P->npc = std::make_shared<NpcPeer>();
	NpcPeer& N = *P->npc;
	if (!n || !n->isObj() || JNumAt(*n, "v", 0) != 2) { clearPeer(*P); return; }
	NpcPeer::Snap snap;
	snap.t = net.nowMs;
	std::vector<double> r;
	if (const Json* C = n->get("c"); C && C->isArr())
		for (size_t i = 0; i + 8 < C->a.size(); i += 9) if (Record(*C, i, 9, r)) snap.cars[(int)r[0]] = r;
	if (const Json* Pd = n->get("p"); Pd && Pd->isArr())
		for (size_t i = 0; i + 7 < Pd->a.size(); i += 8) if (Record(*Pd, i, 8, r)) snap.peds[(int)r[0]] = r;
	if (const Json* S = n->get("s"); S && S->isArr())
		for (const Json& rec : S->a) if (rec.isArr() && !rec.a.empty() && rec.a[0].integer()) N.statics[(int)rec.a[0].n] = rec;
	if (const Json* tr = n->get("tr"); tr && tr->isArr()) snap.tr = *tr;
	// forget the looks of NPCs that are long gone
	if (N.statics.size() > 300)
		for (auto it = N.statics.begin(); it != N.statics.end();) {
			const int id = it->first;
			if (!snap.cars.count(id) && !snap.peds.count(id) && !N.cars.count(id) && !N.peds.count(id)) it = N.statics.erase(it); else ++it;
		}
	N.train = snap.tr;
	N.buf.push_back(std::move(snap));
	if (N.buf.size() > 8) N.buf.pop_front();
}

void NpcSync::clearPeer(NetSystem::Peer& P) {
	if (!P.npc) return;
	NpcPeer& N = *P.npc;
	for (auto& [id, ref] : N.cars) if (Vehicle* v = ref.get()) dropCar(v);
	for (auto& [id, q] : N.peds) dropPed(q.get());
	N.cars.clear(); N.carGone.clear(); N.peds.clear(); N.buf.clear();
	std::vector<std::shared_ptr<Vehicle>> snap = game.vehicles.list;
	for (const auto& v : snap) if (v->parkedOf == P.id) { v->parkedOf.clear(); net.releaseProxy(v.get()); }
	pedList.erase(std::remove_if(pedList.begin(), pedList.end(), [](const std::shared_ptr<NpcProxy>& q) { return q->removed; }), pedList.end());
}

void NpcSync::dropPed(Character* q) {
	if (!q) return;
	if (q->vehicle) q->vehicle->takeOut(q);
	q->remove();
	q->removed = true;
}

void NpcSync::dropCar(Vehicle* v) {
	// (the stand-in driver goes with the car: left behind it would sit in the road where the car was)
	std::shared_ptr<Character> occ[4];
	for (int i = 0; i < 4; i++) occ[i] = v->occupants[i];
	for (const auto& o : occ) if (o) { if (o->npcProxy) { v->takeOut(o.get()); dropPed(o.get()); } else if (o->remote) v->takeOut(o.get()); }
	if (!v->removed) game.vehicles.remove(v);
}

// A car record from a peer with no stand-in yet: is it one we already have under another hat?
// (the car they're driving, the one they just got out of, or one of our parked cars on the same spot)
Vehicle* NpcSync::adoptCar(NetSystem::Peer& P, int id, const std::string& type, double x, double z, bool& isDriven) {
	isDriven = false;
	Vehicle* pv = P.veh.get();
	if (pv && !pv->removed && pv->type == type && (pv->netId == id || (!pv->netId && Hypot(pv->pos.x - x, pv->pos.z - z) < 5))) { pv->netId = id; isDriven = true; return nullptr; }
	for (const auto& sp : game.vehicles.list) {
		Vehicle* o = sp.get();
		if (o->parkedOf != P.id || o->removed || o->type != type || Hypot(o->pos.x - x, o->pos.z - z) > 6) continue;
		o->parkedOf.clear(); o->npcRemote = true; o->netId = id;
		return o;
	}
	return nullptr;
}

// Parked cars and the army's hardware sit on fixed spots, so two players can each spawn their own copy of the same
// car. The player with the lower id keeps theirs; the other one's goes.
void NpcSync::dedupeStatic(const NetSystem::Peer& P, Vehicle* v) {
	const std::string me = net.selfId();
	if (me.empty() || !(P.id < me)) return;
	std::vector<std::shared_ptr<Vehicle>> snap = game.vehicles.list;
	for (const auto& sp : snap) {
		Vehicle* o = sp.get();
		if (o == v || o->remote || o->removed || !o->parked || o->driver() || o->ownedByPlayer || o->type != v->type) continue;
		if (Hypot(o->pos.x - v->pos.x, o->pos.z - v->pos.z) > 3.5 || std::fabs(o->pos.y - v->pos.y) > 3) continue;
		game.vehicles.remove(o);
	}
}

// ------------------------------------------------------------------ per frame
void NpcSync::update(double dt) {
	Game& g = game;
	const double now = net.nowMs;
	const V3 me = g.player->vehicle ? g.player->vehicle->pos : g.player->pos;
	std::vector<NetSystem::Peer*> plist;
	for (auto& kv : net.peers) plist.push_back(kv.second.get());
	for (NetSystem::Peer* P : plist) {
		if (!P->npc || P->npc->buf.empty()) continue;
		NpcPeer& N = *P->npc;
		const double T = now - DELAY;
		size_t i = N.buf.size() - 1;
		while (i > 0 && N.buf[i - 1].t > T) i--;
		const NpcPeer::Snap& B = N.buf[i];
		const NpcPeer::Snap& A = i > 0 ? N.buf[i - 1] : B;
		const bool same = &A == &B;
		const double k = same ? 0 : Clamp((T - A.t) / std::max(1.0, B.t - A.t), 0, 1);
		const double ext = same ? Clamp((T - B.t) / 1000, 0, 0.3) : 0;
		// cars
		for (const auto& [id, rb] : B.cars) {
			auto fa = A.cars.find(id);
			const std::vector<double>& ra = fa != A.cars.end() ? fa->second : rb;
			const double x = Lerp(ra[1], rb[1], k) / 10, y = Lerp(ra[2], rb[2], k) / 10, z = Lerp(ra[3], rb[3], k) / 10;
			auto fc = N.cars.find(id);
			if (Dist2(x, z, me.x, me.z) > SHOW_R * SHOW_R) {
				if (fc != N.cars.end()) { if (Vehicle* v = fc->second.get()) dropCar(v); N.cars.erase(fc); }
				continue;
			}
			Vehicle* v = fc != N.cars.end() ? fc->second.get() : nullptr;
			if (!v || v->removed) {
				if (N.taken.count(id)) continue; // (we just drove off in it)
				auto st = N.statics.find(id);
				if (st == N.statics.end() || st->second.a.size() < 3 || JNum(&st->second.a[1], -1) != 0) continue;
				const VehicleDef* def = VehicleAt(JNum(&st->second.a[2], -1));
				if (!def) continue;
				bool driven = false;
				Vehicle* adopted = adoptCar(*P, id, def->id, x, z, driven);
				if (driven) continue;
				if (adopted) v = adopted;
				else {
					SpawnOpts o;
					o.hasColor = true; o.color = Col(st->second.at(3)); o.hasY = true; o.y = y; o.persistent = true;
					v = g.vehicles.spawn(def->id, x, z, rb[4] / 100, o);
					if (!v) continue;
					v->remote = true; v->npcRemote = true; v->remoteOwner = P->id; v->netId = id; v->locked = true; v->persistent = true; v->ai = nullptr;
					v->mass0 = v->mass; v->mass = 1e6;
					v->pos = V3(x, y, z);
					dedupeStatic(*P, v);
				}
				N.cars[id] = Ref<Vehicle>(v);
			}
			const double yaw = ra[4] / 100 + WrapAngle(rb[4] / 100 - ra[4] / 100) * k;
			const double fwd = rb[5] / 10;
			v->pos = V3(x + std::sin(yaw) * fwd * ext, y, z + std::cos(yaw) * fwd * ext);
			v->yaw = yaw;
			v->vel = V3(std::sin(yaw) * fwd, 0, std::cos(yaw) * fwd);
			v->steerAngle = Clamp(rb[6] / 100, -1, 1);
			const int fl = (int)rb[7];
			v->input.brake = fl & 1 ? 1 : 0; v->sirenOn = (fl & 2) != 0;
			if ((fl & 4) && !v->exploded) net.proxyExplode(v);
			if ((fl & 8) && !v->exploded) { v->health = 0; v->onFire = true; }
			v->wheelRot += fwd * dt / (v->def.wheelR > 0 ? v->def.wheelR : 0.35);
			v->airborne = false;
			v->updateVisual(dt);
			// the driver
			const int drvId = (int)rb[8];
			Character* cur = v->occupants[0].get();
			if (drvId && (!cur || (cur->npcProxy && cur->netId != drvId))) {
				if (cur) { v->takeOut(cur); dropPed(cur); }
				auto st = N.statics.find(id);
				Appearance look;
				if (st != N.statics.end() && st->second.a.size() > 4 && UnpackLook(&st->second.a[4], look)) {
					auto q = std::make_shared<NpcProxy>(g, *this, P->id, drvId, look);
					q->setPosition(x, y, z);
					v->putIn(q.get(), 0);
					pedList.push_back(q);
				}
			} else if (!drvId && cur && cur->npcProxy) { v->takeOut(cur); dropPed(cur); }
			if (NpcProxy* d = dynamic_cast<NpcProxy*>(v->occupants[0].get())) { d->npcWanted = (fl >> 4) & 3; d->update(dt); }
		}
		for (auto it = N.cars.begin(); it != N.cars.end();) {
			if (!B.cars.count(it->first)) {
				double& gone = N.carGone[it->first];
				gone += dt;
				if (gone > 1.2) { if (Vehicle* v = it->second.get()) dropCar(v); N.carGone.erase(it->first); it = N.cars.erase(it); continue; }
			} else N.carGone[it->first] = 0;
			++it;
		}
		// pedestrians
		const auto& W = WeaponOrder();
		for (const auto& [id, rb] : B.peds) {
			auto fa = A.peds.find(id);
			const std::vector<double>& ra = fa != A.peds.end() ? fa->second : rb;
			const double x = Lerp(ra[1], rb[1], k) / 10, y = Lerp(ra[2], rb[2], k) / 10, z = Lerp(ra[3], rb[3], k) / 10;
			auto fp = N.peds.find(id);
			if (Dist2(x, z, me.x, me.z) > SHOW_R * SHOW_R) {
				if (fp != N.peds.end()) { dropPed(fp->second.get()); N.peds.erase(fp); }
				continue;
			}
			std::shared_ptr<NpcProxy> q = fp != N.peds.end() ? fp->second : nullptr;
			if (!q || q->removed) {
				auto st = N.statics.find(id);
				Appearance look;
				if (st == N.statics.end() || st->second.a.size() < 3 || JNum(&st->second.a[1], -1) != 1 || !UnpackLook(&st->second.a[2], look)) continue;
				q = std::make_shared<NpcProxy>(g, *this, P->id, id, look);
				q->setPosition(x, y, z);
				N.peds[id] = q;
				pedList.push_back(q);
			}
			const int fl = (int)rb[6];
			q->setDown((fl & 96) != 0, (fl & 32) != 0);
			if (!q->ragdolling) { q->pos = V3(x, y, z); q->yaw = ra[4] / 100 + WrapAngle(rb[4] / 100 - ra[4] / 100) * k; }
			AnimState& s = q->animState;
			s.speed = Clamp(rb[5] / 10, 0, 12); s.moveAngle = 0; s.grounded = true;
			s.aim = fl & 2; s.crouch = fl & 4; s.handsUp = fl & 8; s.cower = fl & 16; s.swim = fl & 128; s.talking = fl & 256;
			q->npcWanted = q->dead ? 0 : (fl >> 9) & 3; // (an NPC another player's police are after)
			q->aiming = s.aim; q->crouching = s.crouch; q->swimming = s.swim;
			const double wi = rb[7];
			if (wi >= 0 && wi < (double)W.size()) {
				const std::string& w = W[(size_t)wi];
				if (w != q->weapon) { if (!q->weapons.count(w)) q->giveWeapon(w, 1); q->equip(w); }
			}
			s.weapon = q->holdType();
			q->update(dt);
		}
		for (auto it = N.peds.begin(); it != N.peds.end();) {
			NpcProxy* q = it->second.get();
			if (!B.peds.count(it->first)) { q->gone += dt; if (q->gone > 1.2) { dropPed(q); it = N.peds.erase(it); continue; } }
			else q->gone = 0;
			++it;
		}
	}
	// stand-ins nobody accounts for any more (a driver whose car went, a pedestrian whose owner left)
	std::set<const Character*> live;
	for (auto& kv : net.peers) if (NpcPeer* N = kv.second->npc.get()) {
		for (const auto& [id, q] : N->peds) live.insert(q.get());
		for (auto it = N->taken.begin(); it != N->taken.end();) { if (now - it->second > 6000) it = N->taken.erase(it); else ++it; }
	}
	for (const auto& q : pedList) if (!q->removed && !q->vehicle && !live.count(q.get())) dropPed(q.get());
	pedList.erase(std::remove_if(pedList.begin(), pedList.end(), [](const std::shared_ptr<NpcProxy>& q) { return q->removed; }), pedList.end());
	// cars a player got out of wait a few seconds for their owner to share them as a parked car
	std::vector<std::shared_ptr<Vehicle>> snap = g.vehicles.list;
	for (const auto& v : snap) if (!v->parkedOf.empty() && now - v->parkedT > 4500 && !v->removed) dropCar(v.get());
	applyTrains(dt);
}

// ------------------------------------------------------------------ damage & theft
void NpcSync::sendPedHit(NpcProxy& q, double dmg, const DamageInfo& info) {
	Json e = Json::Array();
	e.push(JS("nh")); e.push(JS(q.owner)); e.push(JN(q.netId)); e.push(JN(R1(dmg)));
	e.push(JN(info.part == "head" ? 1 : 0)); e.push(JN(info.knockdown ? 1 : 0));
	if (info.hasImpulse) { Json imp = Json::Array(); imp.push(JN(R1(info.impulse.x))); imp.push(JN(R1(info.impulse.y))); imp.push(JN(R1(info.impulse.z))); e.push(std::move(imp)); }
	else e.push(Json());
	e.push(JS(!info.weapon.empty() ? info.weapon : info.type));
	net.emit(std::move(e));
}

void NpcSync::sendCarHit(Vehicle* v, double dmg) {
	Json e = Json::Array();
	e.push(JS("nv")); e.push(JS(v->remoteOwner)); e.push(JN(v->netId)); e.push(JN(JsRound(dmg)));
	net.emit(std::move(e));
}

// we took over another client's car (the player got in): it stops being theirs
void NpcSync::takeCar(Vehicle* v) {
	auto it = net.peers.find(v->remoteOwner);
	NetSystem::Peer* P = it != net.peers.end() ? it->second.get() : nullptr;
	Json e = Json::Array();
	e.push(JS("tk")); e.push(JS(v->remoteOwner)); e.push(JN(v->netId));
	net.emit(std::move(e));
	if (P && P->npc) { P->npc->cars.erase(v->netId); P->npc->taken[v->netId] = net.nowMs; }
	// the stand-in driver becomes one of our own pedestrians, thrown out of the car
	Character* drv = v->occupants[0].get();
	if (drv && drv->npcProxy) {
		v->takeOut(drv);
		const Appearance look = drv->appearance;
		dropPed(drv);
		if (game.peds) {
			PedOpts o; o.hasAppearance = true; o.appearance = look;
			if (Ped* ped = game.peds->spawnPed(v->pos.x + 1.5, v->pos.z, o)) { ped->threat = Ref<Character>(game.player.get()); ped->setState("flee"); }
		}
	}
	v->remote = false; v->npcRemote = false; v->remoteOwner.clear(); v->netId = 0; v->locked = false; v->persistent = false;
	if (IsSet(v->mass0)) v->mass = v->mass0;
}

// a moment addressed to us: hits on our NPCs, cars taken
void NpcSync::onMoment(NetSystem::Peer& P, const Json& e) {
	auto Str = [&](size_t i) { return i < e.a.size() && e.a[i].isStr() ? e.a[i].s : std::string(); };
	auto Num = [&](size_t i, double d = 0) { return i < e.a.size() ? JNum(&e.a[i], d) : d; };
	auto Truthy = [&](size_t i) { return i < e.a.size() && JTruthy(&e.a[i]); };
	const std::string k = Str(1), me = net.selfId();
	if (me.empty() || Str(2) != me) return;
	const double oid = Num(3, -1);
	if (oid != std::floor(oid)) return;
	auto f = byId.find((int)oid);
	if (f == byId.end()) return;
	Character* c = f->second.c.get();
	Vehicle* v = f->second.v.get();
	if ((c && c->removed) || (v && v->removed) || (!c && !v)) return;
	Game& g = game;
	if (k == "nh" && c) {
		const std::string w = Str(8);
		const WeaponDef* wd = FindWeapon(w);
		DamageInfo info;
		info.source = P.avatar.get(); info.part = Truthy(5) ? "head" : "torso"; info.headMul = 1; info.weapon = w;
		info.type = wd ? (wd->type == "melee" ? "melee" : "bullet") : (w.empty() ? std::string("bullet") : w);
		if (Truthy(6)) {
			info.knockdown = true; info.hasImpulse = true; info.impulse = V3(0, 3, 0);
			if (e.a.size() > 7 && e.a[7].isArr() && e.a[7].a.size() == 3 && e.a[7].a[0].finite() && e.a[7].a[1].finite() && e.a[7].a[2].finite())
				info.impulse = V3(e.a[7].a[0].n, e.a[7].a[1].n, e.a[7].a[2].n);
		}
		const bool wasDead = c->dead;
		c->takeDamage(Clamp(Num(4), 0, 500), info);
		if (c->dead && !wasDead && !c->ragdolling) c->startRagdoll(info.hasImpulse ? info.impulse : V3(0, 1, 0));
		if (info.type == "bullet" && g.effects) g.effects->blood(c->chestPos(), V3(0, 0.3, 0), Truthy(5) ? 12 : 6);
	} else if (k == "nv" && v) v->damage(Clamp(Num(4), 0, 3000), P.avatar.get());
	else if (k == "tk" && v) {
		// they drove off in it: it's theirs now
		std::shared_ptr<Character> occ[4];
		for (int i = 0; i < 4; i++) occ[i] = v->occupants[i];
		for (const auto& o : occ) if (o && !o->isPlayer) { v->takeOut(o.get()); if (g.peds) g.peds->remove(o.get()); }
		byId.erase(f);
		g.vehicles.remove(v);
	}
}

// ------------------------------------------------------------------ trains
// The host (online longest) runs the timetable; a player at a train's controls drives that one.
bool NpcSync::isHost() const {
	const double hj = net.joinedAt;
	const std::string hid = net.selfId();
	for (const auto& kv : net.peers) {
		const double j = JNumAt(kv.second->st, "j", kInfinity);
		if (j < hj || (j == hj && kv.second->id < hid)) return false;
	}
	return true;
}

Json NpcSync::trainState() const {
	RailSystem* R = game.rail;
	if (!R || !game.freeRoam) return Json();
	const bool host = isHost();
	Json out = Json::Array();
	Vehicle* ts[2] = { R->train.get(), R->freight.get() };
	for (int i = 0; i < 2; i++) {
		Train* t = dynamic_cast<Train*>(ts[i]);
		if (!t || t->removed) continue;
		const bool driving = t->driver() && t->driver()->isPlayer;
		if (host || driving) {
			Json r = Json::Array();
			for (double x : { (double)i, JsRound(t->s * 10), JsRound(t->v * 10), t->dirS, driving ? 1.0 : 0.0, JsRound(std::max(0.0, std::min(t->dwell, 999.0)) * 10) }) r.push(JN(x));
			out.push(std::move(r));
		}
	}
	return out.a.empty() ? Json() : out;
}

void NpcSync::applyTrains(double) {
	Game& g = game;
	RailSystem* R = g.rail;
	if (!R || !g.freeRoam) return;
	Vehicle* ts[2] = { R->train.get(), R->freight.get() };
	struct Want { int prio = 0; const Json* r = nullptr; } want[2];
	for (const auto& kv : net.peers) {
		const NetSystem::Peer& P = *kv.second;
		if (!P.npc || !P.npc->train.isArr()) continue;
		for (const Json& r : P.npc->train.a) {
			if (!r.isArr() || r.a.empty() || r.a[0].type != Json::Num || (r.a[0].n != 0 && r.a[0].n != 1)) continue;
			const int idx = (int)r.a[0].n;
			const int prio = r.a.size() > 4 && JTruthy(&r.a[4]) ? 2 : (peerIsHost(P) ? 1 : 0);
			if (!prio) continue;
			if (!want[idx].r || prio > want[idx].prio) want[idx] = { prio, &r };
		}
	}
	const bool host = isHost();
	for (int i = 0; i < 2; i++) {
		Train* t = dynamic_cast<Train*>(ts[i]);
		if (!t || t->removed) continue;
		const Want& w = want[i];
		if (!w.r || (t->driver() && t->driver()->isPlayer) || (host && w.prio < 2)) { t->hasNetTarget = false; continue; }
		auto at = [&](size_t j, double d = 0) { return j < w.r->a.size() ? JNum(&w.r->a[j], d) : d; };
		t->netTarget.s = at(1) / 10; t->netTarget.v = at(2) / 10; t->netTarget.dirS = at(3) == 1 ? 1 : -1; t->netTarget.dwell = at(5) / 10;
		t->hasNetTarget = std::isfinite(t->netTarget.s) && std::isfinite(t->netTarget.v);
	}
}

bool NpcSync::peerIsHost(const NetSystem::Peer& P) const {
	const double j = JNumAt(P.st, "j", kInfinity);
	if (j > net.joinedAt || (j == net.joinedAt && P.id > net.selfId())) return false;
	for (const auto& kv : net.peers) {
		const NetSystem::Peer& Q = *kv.second;
		if (&Q == &P) continue;
		const double jq = JNumAt(Q.st, "j", kInfinity);
		if (jq < j || (jq == j && Q.id < P.id)) return false;
	}
	return true;
}

} // namespace atg
