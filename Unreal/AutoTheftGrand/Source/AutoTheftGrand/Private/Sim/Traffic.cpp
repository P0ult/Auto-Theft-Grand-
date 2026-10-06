#include "Traffic.h"
#include "Game.h"
#include "Peds.h"
#include "Rail.h"
#include "RoadNet.h"

namespace atg {

namespace {
constexpr double STOP_GAP = 3; // metres left to the bumper of a stopped car ahead
}

ESignal SignalState(double t, int axis) {
	const double c = std::fmod(std::fmod(t, 34) + 34, 34);
	if (axis == 0) return c < 13 ? ESignal::Green : c < 16 ? ESignal::Yellow : ESignal::Red;
	return c >= 17 && c < 30 ? ESignal::Green : c >= 30 && c < 33 ? ESignal::Yellow : ESignal::Red;
}

LanePath MakePath(std::vector<V3> pts) {
	LanePath p;
	p.pts = std::move(pts);
	const size_t n = p.pts.size();
	p.cum.assign(n, 0);
	for (size_t i = 1; i < n; i++) p.cum[i] = p.cum[i - 1] + Hypot(p.pts[i].x - p.pts[i - 1].x, p.pts[i].z - p.pts[i - 1].z);
	p.len = n ? p.cum[n - 1] : 0;
	p.vmax.assign(n, 99.f);
	for (size_t i = 1; i + 1 < n; i++) {
		const V3 &a = p.pts[i - 1], &b = p.pts[i], &c = p.pts[i + 1];
		const double h1 = std::atan2(b.x - a.x, b.z - a.z), h2 = std::atan2(c.x - b.x, c.z - b.z);
		const double dAng = std::fabs(WrapAngle(h2 - h1));
		const double ds = (p.cum[i + 1] - p.cum[i - 1]) / 2;
		if (dAng > 1e-3 && ds > 0.01) { const double R = ds / dAng; p.vmax[i] = (float)std::sqrt(5.2 * R); }
	}
	return p;
}

V3 SampleOn(const LanePath& path, double s) {
	const auto& pts = path.pts; const auto& cum = path.cum;
	if (pts.empty()) return V3();
	if (s <= 0) return pts[0];
	if (s >= path.len) return pts.back();
	size_t lo = 0, hi = cum.size() - 1;
	while (hi - lo > 1) { const size_t m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
	double den = cum[hi] - cum[lo]; if (den == 0) den = 1;
	const double t = (s - cum[lo]) / den;
	return pts[lo].lerp(pts[hi], t);
}

static std::vector<V3> ToV3(const Line& l) { std::vector<V3> o; o.reserve(l.size()); for (const P3& p : l) o.push_back(V3(p.x, p.y, p.z)); return o; }

bool NearestLane(const RoadNet& net, double x, double z, double yaw, bool hasYaw, LaneStart& out, const std::function<bool(const REdge&)>& filter, double y) {
	const EdgeHit c = net.Closest(x, z, [&](const REdge& e) { return !e.removed && (e.lanesF > 0 || e.lanesB > 0) && (!filter || filter(e)); }, 300, y);
	if (!c.valid()) return false;
	const REdge& e = net.edges[c.e];
	const EdgePoint t = net.At(e, c.s);
	const double eyaw = std::atan2(t.tx, t.tz);
	int dir;
	if (e.lanesB == 0) dir = 0; else if (e.lanesF == 0) dir = 1;
	else if (hasYaw) dir = std::fabs(WrapAngle(yaw - eyaw)) < kPi / 2 ? 0 : 1;
	else dir = c.lat >= 0 ? 0 : 1;
	const int n = dir == 0 ? e.lanesF : e.lanesB;
	const double latTravel = dir == 0 ? c.lat : -c.lat;
	int lane = 0; double bd = kInf;
	for (int k = 0; k < n; k++) { const double d = std::fabs(net.LaneOffset(e, dir, k) - latTravel); if (d < bd) { bd = d; lane = k; } }
	out.e = c.e; out.dir = dir; out.lane = lane; out.s = dir == 0 ? c.s : e.len - c.s;
	return true;
}

// ==================================================================== LaneDriver
LaneDriver::LaneDriver(Game& g, Vehicle* v, const LaneStart* st, bool noSnap) : game(g), veh(v), net(g.map.roads), cruiseFactor(Rand(0.82, 1.05)) {
	if (st) start(*st); else if (!noSnap) resnap();
}

std::shared_ptr<LanePath> LaneDriver::lanePath(int ei, int dir, int lane) {
	const REdge& e = net.edges[ei];
	auto p = std::make_shared<LanePath>(MakePath(ToV3(net.LanePath(e, dir, lane))));
	p->e = ei; p->dir = dir; p->lane = lane; p->node = dir == 0 ? e.b : e.a; p->speed = e.speed;
	return p;
}

void LaneDriver::start(const LaneStart& st) {
	paths.clear();
	auto p = lanePath(st.e, st.dir, st.lane);
	paths.push_back(p);
	s = project(*p, veh->pos.x, veh->pos.z, 0, p->len);
	ensurePaths();
}

std::vector<LaneDriver::Exit> LaneDriver::options(const LanePath& cur) {
	const RNode& n = net.nodes[cur.node];
	auto exits = [&]() { std::vector<Exit> out; for (int id : n.e) { const REdge& e = net.edges[id]; if (e.a == n.id && e.lanesF > 0) out.push_back({ id, 0 }); if (e.b == n.id && e.lanesB > 0) out.push_back({ id, 1 }); } return out; };
	std::vector<Exit> ex;
	for (const Exit& o : exits()) if (!(o.e == cur.e && o.dir != cur.dir) && !net.edges[o.e].removed) ex.push_back(o);
	if (ex.empty()) { for (const Exit& o : exits()) if (!net.edges[o.e].removed) ex.push_back(o); return ex; }
	const REdge& ce = net.edges[cur.e];
	const bool rightmost = cur.lane >= (cur.dir == 0 ? ce.lanesF : ce.lanesB) - 1;
	if (n.kind == ENode::Split) {
		std::vector<Exit> main;
		for (const Exit& o : ex) if (net.edges[o.e].type == ce.type) main.push_back(o);
		if (!rightmost && !main.empty()) return main;
	}
	return ex;
}

double LaneDriver::heading(int ei, int dir, bool atStart) const {
	const REdge& e = net.edges[ei];
	const EdgePoint t = net.At(e, atStart ? (dir == 0 ? 0.5 : e.len - 0.5) : (dir == 0 ? e.len - 0.5 : 0.5));
	return dir == 0 ? std::atan2(t.tx, t.tz) : std::atan2(-t.tx, -t.tz);
}

bool LaneDriver::chooseNext(const LanePath& cur, Exit& out) {
	const std::vector<Exit> ex = options(cur);
	if (ex.empty()) return false;
	const double hIn = heading(cur.e, cur.dir, false);
	const REdge& ce = net.edges[cur.e];
	std::vector<double> w;
	double tot = 0;
	for (const Exit& o : ex) {
		const double d = std::fabs(WrapAngle(heading(o.e, o.dir, true) - hIn));
		double k = d < 0.5 ? 3 : d < 2.2 ? 1 : 0.15;
		const REdge& oe = net.edges[o.e];
		if (oe.type == ERoad::Dirt) k *= 0.3;
		if (ce.type == ERoad::Freeway && oe.type == ERoad::Ramp) k = 0.9;
		w.push_back(k); tot += k;
	}
	double r = Rand() * tot;
	for (size_t i = 0; i < ex.size(); i++) { r -= w[i]; if (r <= 0) { out = ex[i]; return true; } }
	out = ex[0];
	return true;
}

int LaneDriver::laneFor(const LanePath& cur, const Exit& o) {
	const REdge& oe = net.edges[o.e];
	const int n = o.dir == 0 ? oe.lanesF : oe.lanesB;
	if (net.nodes[cur.node].kind == ENode::Merge || net.edges[cur.e].type == ERoad::Ramp) return n - 1;
	if (oe.type == ERoad::Ramp) return 0;
	return (int)Clamp(cur.lane, 0, n - 1);
}

std::shared_ptr<LanePath> LaneDriver::turnPath(const std::shared_ptr<LanePath>& from, const std::shared_ptr<LanePath>& to, int nodeId) {
	const RNode& node = net.nodes[nodeId];
	const V3 A = from->pts.back(), B = to->pts.front();
	const V3 a2 = from->pts[from->pts.size() >= 2 ? from->pts.size() - 2 : 0], b2 = to->pts[to->pts.size() > 1 ? 1 : 0];
	double hax = A.x - a2.x, haz = A.z - a2.z; double la = Hypot(hax, haz); if (la == 0) la = 1; hax /= la; haz /= la;
	double hbx = b2.x - B.x, hbz = b2.z - B.z; double lb = Hypot(hbx, hbz); if (lb == 0) lb = 1; hbx /= lb; hbz /= lb;
	const double d = Hypot(B.x - A.x, B.z - A.z);
	std::vector<V3> pts;
	if (d < 0.6) pts = { A, B };
	else if (node.kind == ENode::RB) {
		const double R = node.rbR;
		const double ta = std::atan2(A.z - node.z, A.x - node.x), tb = std::atan2(B.z - node.z, B.x - node.x);
		const double a0 = ta - 0.45;
		double a1 = tb + 0.45;
		while (a1 > a0) a1 -= kTau;
		while (a0 - a1 > kTau) a1 += kTau;
		pts.push_back(A);
		const int steps = (int)Max(4, std::ceil((a0 - a1) / 0.25));
		for (int i = 0; i <= steps; i++) { const double a = a0 + (a1 - a0) * i / steps; pts.push_back(V3(node.x + std::cos(a) * R, node.y + 0.05, node.z + std::sin(a) * R)); }
		pts.push_back(B);
	} else if (from->e == to->e) {
		const double rx = -haz, rz = hax;
		const double w = Max(6, d);
		const double cx = (A.x + B.x) / 2 + hax * 4, cz = (A.z + B.z) / 2 + haz * 4;
		pts.push_back(A);
		for (int k = 0; k <= 10; k++) {
			const double t = k / 10.0, ang = t * kPi;
			pts.push_back(V3(cx - rx * std::cos(ang) * w / 2 * -1 + hax * std::sin(ang) * 4, A.y, cz - rz * std::cos(ang) * w / 2 * -1 + haz * std::sin(ang) * 4));
		}
		pts.push_back(B);
	} else {
		const double k = Clamp(d * 0.42, 1, 40);
		const double P1x = A.x + hax * k, P1z = A.z + haz * k, P2x = B.x - hbx * k, P2z = B.z - hbz * k;
		const int n = (int)Max(3, Min(16, std::ceil(d / 3)));
		for (int i = 0; i <= n; i++) {
			const double t = (double)i / n, u = 1 - t;
			const double x = u * u * u * A.x + 3 * u * u * t * P1x + 3 * u * t * t * P2x + t * t * t * B.x;
			const double z = u * u * u * A.z + 3 * u * u * t * P1z + 3 * u * t * t * P2z + t * t * t * B.z;
			pts.push_back(V3(x, A.y + (B.y - A.y) * t, z));
		}
	}
	auto p = std::make_shared<LanePath>(MakePath(pts));
	p->turn = true; p->node = nodeId; p->from = from; p->to = to;
	return p;
}

void LaneDriver::ensurePaths() {
	int guard = 0;
	while (paths.size() < 4 && guard++ < 6) {
		auto last = paths.back();
		if (last->turn) break;
		Exit o;
		if (!chooseNext(*last, o)) break;
		const int lane = laneFor(*last, o);
		auto nxt = lanePath(o.e, o.dir, lane);
		paths.push_back(turnPath(last, nxt, last->node));
		paths.push_back(nxt);
	}
}

double LaneDriver::project(const LanePath& path, double x, double z, double s0, double s1) {
	const auto& pts = path.pts; const auto& cum = path.cum;
	double best = kInf, bs = s0;
	size_t i0 = 0, i1 = pts.size() ? pts.size() - 1 : 0;
	while (i0 + 1 < pts.size() && cum[i0 + 1] < s0) i0++;
	while (i1 > 0 && cum[i1 - 1] > s1) i1--;
	for (size_t i = i0; i < i1; i++) {
		const V3& a = pts[i]; const V3& b = pts[i + 1];
		const double dx = b.x - a.x, dz = b.z - a.z; double L2 = dx * dx + dz * dz; if (L2 == 0) L2 = 1e-6;
		const double t = Clamp(((x - a.x) * dx + (z - a.z) * dz) / L2, 0, 1);
		const double px = a.x + dx * t, pz = a.z + dz * t;
		const double d = (x - px) * (x - px) + (z - pz) * (z - pz);
		if (d < best) { best = d; bs = cum[i] + std::sqrt(L2) * t; }
	}
	lat = std::sqrt(best);
	return bs;
}

V3 LaneDriver::pointAhead(double ahead) {
	double ss = s + ahead;
	size_t k = 0;
	while (k + 1 < paths.size() && ss > paths[k]->len) { ss -= paths[k]->len; k++; }
	return SampleOn(*paths[k], ss);
}

double LaneDriver::curveLimit(double dist) {
	double lim = 99, ss = s, acc = 0;
	for (size_t k = 0; k < paths.size() && acc < dist; k++) {
		const LanePath& p = *paths[k];
		for (size_t i = 0; i < p.pts.size(); i++) {
			if (p.cum[i] < ss) continue;
			const double d = acc + p.cum[i] - ss;
			if (d > dist) break;
			lim = Min(lim, std::sqrt((double)p.vmax[i] * p.vmax[i] + 2 * 4 * d));
		}
		acc += p.len - ss; ss = 0;
	}
	return lim;
}

void LaneDriver::aheadLine(double maxD) {
	Vehicle* v = veh;
	Line& L = line;
	int n = 0; double acc = 0;
	auto put = [&](double x, double y, double z) {
		if ((int)L.x.size() <= n) { L.x.push_back(0); L.y.push_back(0); L.z.push_back(0); L.s.push_back(0); }
		L.x[n] = x; L.y[n] = y; L.z[n] = z; L.s[n] = acc; n++;
	};
	put(v->pos.x, v->pos.y, v->pos.z);
	double ss = s;
	for (size_t k = 0; k < paths.size() && acc < maxD && !hasBypass; k++, ss = 0) {
		const auto& pts = paths[k]->pts; const auto& cum = paths[k]->cum;
		for (size_t i = 0; i < pts.size() && acc < maxD; i++) {
			if (cum[i] <= ss + 0.5) continue;
			const V3& q = pts[i];
			const double d = Hypot(q.x - L.x[n - 1], q.z - L.z[n - 1]);
			if (d < 0.5) continue;
			acc += d; put(q.x, q.y, q.z);
		}
	}
	if (acc < maxD) {
		double dx = std::sin(v->yaw), dz = std::cos(v->yaw);
		if (n > 1) { dx = L.x[n - 1] - L.x[n - 2]; dz = L.z[n - 1] - L.z[n - 2]; double l = Hypot(dx, dz); if (l == 0) l = 1; dx /= l; dz /= l; }
		const double r = maxD - acc + 6, x = L.x[n - 1], z = L.z[n - 1];
		acc += r; put(x + dx * r, L.y[n - 1], z + dz * r);
	}
	L.n = n;
}

double LaneDriver::obstacleAhead(double maxD) {
	Vehicle* v = veh;
	const double fx = std::sin(v->yaw), fz = std::cos(v->yaw);
	double best = maxD;
	obsObj = nullptr;
	aheadLine(maxD + 6);
	const Line& L = line;
	const int N = L.n;
	auto consider = [&](double x, double z, double y, double rad, Vehicle* o) {
		if ((x - v->pos.x) * fx + (z - v->pos.z) * fz < 0) return;
		double bl = kInf, ba = 0; int bi = 0;
		for (int i = 0; i < N - 1; i++) {
			const double ax = L.x[i], az = L.z[i], dx = L.x[i + 1] - ax, dz = L.z[i + 1] - az; double l2 = dx * dx + dz * dz; if (l2 == 0) l2 = 1;
			const double t = Clamp(((x - ax) * dx + (z - az) * dz) / l2, 0, 1);
			const double la = Hypot(x - ax - dx * t, z - az - dz * t);
			if (la < bl) { bl = la; ba = L.s[i] + t * std::sqrt(l2); bi = i; }
		}
		if (std::fabs(y - L.y[bi]) > 3.5) return;
		double side = rad;
		if (o) {
			const double dx = L.x[bi + 1] - L.x[bi], dz = L.z[bi + 1] - L.z[bi]; double l = Hypot(dx, dz); if (l == 0) l = 1;
			const double cf = std::fabs((std::sin(o->yaw) * dx + std::cos(o->yaw) * dz) / l), cs = std::sqrt(Max(0, 1 - cf * cf));
			rad = cf * o->hz + cs * o->hx; side = cs * o->hz + cf * o->hx;
		}
		if (bl < v->hx + side + 0.25 && ba - v->hz - rad < best) { best = ba - v->hz - rad; obsObj = o; }
	};
	Vehicle* skip = hasBypass ? bypass.v.get() : nullptr;
	for (const auto& op : game.vehicles.list) {
		Vehicle* o = op.get();
		if (o == v || o->removed || o == skip) continue;
		if (std::fabs(o->pos.x - v->pos.x) > maxD + 12 || std::fabs(o->pos.z - v->pos.z) > maxD + 12) continue;
		consider(o->pos.x, o->pos.z, o->pos.y, 0, o);
	}
	Player& pl = *game.player;
	if (!pl.vehicle) consider(pl.pos.x, pl.pos.z, pl.pos.y, 0.5, nullptr);
	if (game.peds && !(impatient > 0)) for (const auto& p : game.peds->list) {
		if (p->vehicle || p->dead || p->ragdolling) continue;
		if (std::fabs(p->pos.x - v->pos.x) > maxD + 4 || std::fabs(p->pos.z - v->pos.z) > maxD + 4) continue;
		consider(p->pos.x, p->pos.z, p->pos.y, 0.4, nullptr);
	}
	return best;
}

double LaneDriver::junctionControl(const LanePath& cur, double remain, double speed) {
	const RNode& n = net.nodes[cur.node];
	if (panic > 0) return 99;
	if (IsSet(n.sig)) {
		if (ignoreLights) return 99;
		const REdge& ce = net.edges[cur.e];
		int axis;
		if (ce.hasGrid) axis = ce.grid.di != 0 ? 1 : 0;
		else { const double h = heading(cur.e, cur.dir, false); axis = std::fabs(std::sin(h)) > std::fabs(std::cos(h)) ? 1 : 0; }
		const ESignal state = SignalState(game.env.uTime + n.sig, axis);
		if (state != ESignal::Green && remain > -0.5) {
			const double stopDist = remain - 0.5;
			if (state == ESignal::Red || stopDist > speed * 0.9) return Max(0, stopDist * 0.6);
		}
		return 99;
	}
	if (remain > 30) return 99;
	const auto& vlist = game.vehicles.list;
	if (n.kind == ENode::RB) {
		const V3& last = cur.pts.back();
		const double ta = std::atan2(last.z - n.z, last.x - n.x);
		for (const auto& op : vlist) {
			Vehicle* o = op.get();
			if (o == veh || o->removed) continue;
			const double d = Hypot(o->pos.x - n.x, o->pos.z - n.z);
			if (d < n.rbR - 4.5 || d > n.rbR + 4.5) continue;
			const double da = WrapAngle(std::atan2(o->pos.z - n.z, o->pos.x - n.x) - ta);
			if ((o->speedAbs() >= 0.5 && da > -0.2 && da < 1.5) || (da > -0.95 && da < 0.35)) return Max(0, (remain - 0.5) * 0.6);
		}
		return 11;
	}
	const REdge& ce = net.edges[cur.e];
	if (n.kind == ENode::Merge || (ce.type == ERoad::Ramp && n.kind != ENode::X)) {
		for (const auto& op : vlist) {
			Vehicle* o = op.get();
			if (o == veh || o->removed) continue;
			const double d = Hypot(o->pos.x - n.x, o->pos.z - n.z);
			if (d < 30 && o->speedAbs() > 3 && Hypot(o->pos.x - veh->pos.x, o->pos.z - veh->pos.z) < 22) return Max(6, speed * 0.7);
		}
		return 99;
	}
	if (n.kind == ENode::X && !n.hasGrid) {
		const bool minor = ce.T->cls < n.maxCls;
		auto it = game.nodeBusy.find(n.id);
		Vehicle* h = it != game.nodeBusy.end() ? it->second.get() : nullptr;
		bool busy = false;
		if (h && h != veh && !h->removed && !h->isWrecked()) {
			LaneDriver* hd = dynamic_cast<LaneDriver*>(h->ai.get());
			busy = hd && !hd->paths.empty() && hd->paths[0]->node == n.id && !(hd->jam > 20) && Hypot(h->pos.x - n.x, h->pos.z - n.z) < n.r + 30;
		}
		if (busy) return Max(0, (remain - 0.5) * 0.6);
		if (remain < 12 && exitBlocked(paths.size() > 2 ? paths[2].get() : nullptr)) return Max(0, (remain - 0.5) * 0.6);
		if (minor && !ignoreLights) {
			bool clear = true;
			for (const auto& op : vlist) {
				Vehicle* o = op.get();
				if (o == veh || o->removed || o->speedAbs() < 1) continue;
				if (Hypot(o->pos.x - n.x, o->pos.z - n.z) < n.r + 26) { clear = false; break; }
			}
			if (!clear || (remain > 1.2 && wait < 0.6)) return Max(0, (remain - 0.4) * 0.5);
		}
		if (remain < 3 + Max(0, speed) * 0.6) game.nodeBusy[n.id] = Ref<Vehicle>(veh);
		return 9;
	}
	return 99;
}

bool LaneDriver::exitBlocked(const LanePath* lane) {
	if (!lane || lane->turn || lane->pts.empty()) return false;
	const V3 A = lane->pts[0], B = SampleOn(*lane, Min(lane->len, 10));
	const double dx = B.x - A.x, dz = B.z - A.z; double l2 = dx * dx + dz * dz; if (l2 == 0) l2 = 1;
	for (const auto& op : game.vehicles.list) {
		Vehicle* o = op.get();
		if (o == veh || o->removed || o->speedAbs() > 2.5 || std::fabs(o->pos.y - A.y) > 3) continue;
		const double t = Clamp(((o->pos.x - A.x) * dx + (o->pos.z - A.z) * dz) / l2, 0, 1);
		if (Hypot(o->pos.x - A.x - dx * t, o->pos.z - A.z - dz * t) < 2.2) return true;
	}
	return false;
}

void LaneDriver::update(double dt) {
	Vehicle* v = veh;
	if (!v->driver() || v->isWrecked() || paths.empty()) return;
	VehInput& inp = v->input;
	const double speed = v->speed();
	panic = Max(0, panic - dt);
	std::shared_ptr<LanePath> cur = paths[0];
	s = project(*cur, v->pos.x, v->pos.z, s - 4, s + 30);
	while (s >= cur->len - 0.05 && paths.size() > 1) {
		if (cur->turn) { auto it = game.nodeBusy.find(cur->node); if (it != game.nodeBusy.end() && it->second.get() == v) game.nodeBusy.erase(it); }
		s -= cur->len;
		paths.erase(paths.begin());
		cur = paths[0];
		if (cur->turn && net.nodes[cur->node].kind == ENode::X && !net.nodes[cur->node].hasGrid) game.nodeBusy[cur->node] = Ref<Vehicle>(v);
		ensurePaths();
		s = Max(0, s);
	}
	const LanePath* lane = cur->turn ? cur->to.get() : cur.get();
	const double baseSpeed = fixedCruise ? cruise : (lane ? lane->speed : 14) * cruiseFactor;
	double desired = baseSpeed * (panic > 0 ? 1.4 : 1);
	desired = Min(desired, curveLimit(28 + Max(0, speed) * 2.2));
	waitingLight = false;
	if (!cur->turn) {
		const double remain = cur->len - s;
		const double cap = junctionControl(*cur, remain, speed);
		desired = Min(desired, cap);
		if (cap < 0.5 && std::fabs(speed) < 0.6) wait += dt; else if (cap > 5) wait = 0;
		waitingLight = cap < 2;
	}
	double obs = obstacleAhead(22 + Max(0, speed) * 1.2);
	if (game.rail) obs = Min(obs, game.rail->crossingAhead(v->pos.x, v->pos.z, std::sin(v->yaw), std::cos(v->yaw), 20 + Max(0, speed) * 1.5));
	if (obs < 20 + speed) desired = Min(desired, Max(0, Min((obs - STOP_GAP) * 0.9, std::sqrt(2 * 5 * Max(0, obs - STOP_GAP)))));
	if (obs < STOP_GAP) desired = 0;
	const double look = 5 + std::fabs(speed) * 0.45;
	const V3 T = pointAhead(look);
	double tx = T.x, tz = T.z;
	if (hasBypass) {
		const double hx = std::sin(v->yaw), hz = std::cos(v->yaw);
		tx += -hz * bypass.side * 3.3; tz += hx * bypass.side * 3.3;
		desired = Min(desired, 6);
	}
	double lx, lz; v->worldToLocal(tx, tz, lx, lz);
	const double ang = std::atan2(lx, Max(0.5, lz));
	inp.steer = Clamp(ang / (v->def.steer * 0.8), -1, 1);
	inp.handbrake = false;
	const double err = desired - speed;
	if (err > 0.5) { inp.throttle = Clamp(err * 0.25, 0.15, 1); inp.brake = 0; }
	else if (err < -1) { inp.throttle = 0; inp.brake = speed > 0.5 ? Clamp(-err * 0.2, 0.2, 1) : 0; }
	else { inp.throttle = desired > 0.5 ? 0.15 : 0; inp.brake = desired < 0.3 && speed > 0.3 ? 0.6 : 0; }
	if (desired < 0.3 && std::fabs(speed) < 0.5) { inp.throttle = 0; inp.brake = 0; inp.handbrake = true; }
	if (obs < 4 && desired < 1) {
		blockedTime += dt;
		if (blockedTime > 2.5 && honkTimer <= 0 && Dist2(v->pos.x, v->pos.z, game.player->pos.x, game.player->pos.z) < 30 * 30) {
			honkTimer = Rand(1.5, 4);
			game.soundAt("horn", v->pos, 0.7);
		}
	} else blockedTime = 0;
	impatient = Max(0, impatient - dt);
	if (blockedTime > 9) { impatient = 3; blockedTime = 0; }
	if (blockedTime > 5 && !cur->turn) {
		const REdge& ce = net.edges[cur->e];
		const int n = cur->dir == 0 ? ce.lanesF : ce.lanesB;
		if (n > 1) { blockedTime = 0; LaneStart st; st.e = cur->e; st.dir = cur->dir; st.lane = (cur->lane + 1) % n; start(st); }
	}
	honkTimer -= dt;
	Vehicle* ob = obsObj;
	LaneDriver* oa = ob ? dynamic_cast<LaneDriver*>(ob->ai.get()) : nullptr;
	queued = obs < 6 && oa && oa != this && (oa->waitingLight || oa->queued) && !ob->isWrecked();
	if (std::fabs(speed) < 1 && !waitingLight && !queued) jam += dt; else jam = Max(0, jam - dt * 2);
	obsD = obs;
	const bool inQueue = oa && oa->obsD < 6 && oa->obsObj && oa->obsObj->driver() && !oa->obsObj->isWrecked();
	const bool dead = ob && (ob->isWrecked() || !ob->driver() || ob->driver()->dead || (ob->driver()->isPlayer && ob->speedAbs() < 0.5) || (oa && !inQueue && (oa->jam > 4 || oa->stuck > 1.5)));
	const bool nearJct = cur->turn || cur->len - s < 35;
	if (!hasBypass && obs < 7 && ob && ob->speedAbs() < 0.6 && dead && !queued && !nearJct) bypassT += dt; else if (!hasBypass) bypassT = Max(0, bypassT - dt);
	if (bypassT > 2.5 && !hasBypass && ob) {
		const REdge& ce = net.edges[cur->e];
		const int lanesRight = !cur->turn ? (cur->dir == 0 ? ce.lanesF : ce.lanesB) : 1;
		const bool twoWay = !cur->turn && ce.lanesF > 0 && ce.lanesB > 0;
		double olx, olz; v->worldToLocal(ob->pos.x, ob->pos.z, olx, olz);
		double side = olx > 0 ? -1 : 1;
		if (twoWay && lanesRight == 1) side = -1;
		else if (!cur->turn && lanesRight > 1) side = cur->lane == 0 ? 1 : -1;
		hasBypass = true; bypass = { Ref<Vehicle>(ob), side, 0 };
		bypassT = 0;
	}
	if (hasBypass) {
		bypass.t += dt;
		Vehicle* bv = bypass.v.get();
		double blx = 0, blz = 0;
		if (bv) v->worldToLocal(bv->pos.x, bv->pos.z, blx, blz);
		if (!bv || bv->removed || blz < -(v->hz + bv->hz + 2) || bypass.t > 9) hasBypass = false;
	}
	if (std::fabs(speed) < 0.5 && desired > 3) stuck += dt; else if (!(stuck > 3)) stuck = 0;
	if (stuck > 3) {
		inp.brake = 1; inp.throttle = 0; inp.handbrake = false; inp.steer = -inp.steer;
		if (stuck > 5) { stuck = 0; resnap(); }
		else stuck += dt;
	}
	if (std::fabs(T.y - v->pos.y) > 4 && !v->airborne) offLevel += dt; else offLevel = 0;
	if (lat > 14 || offLevel > 1.2) { offLevel = 0; resnap(); }
}

void LaneDriver::resnap() {
	LaneStart st;
	if (NearestLane(net, veh->pos.x, veh->pos.z, veh->yaw, true, st, nullptr, veh->pos.y)) start(st);
}

// ==================================================================== Traffic
Traffic::Traffic(Game& g) : game(g), maxCars(g.quality.traffic) {
	game.events.gunshot.on([this](Character*, V3 p, const std::string&) {
		for (auto& c : cars) if (Vehicle* v = c.get()) if (auto* ai = dynamic_cast<LaneDriver*>(v->ai.get())) if (Dist2(v->pos.x, v->pos.z, p.x, p.z) < 40 * 40) { ai->panic = 8; ai->ignoreLights = true; }
	});
}

std::string Traffic::pickType(const std::string& district) {
	static const std::map<std::string, std::map<std::string, double>> MULT = {
		{ "downtown", { { "taxi", 4 }, { "zenith", 2 }, { "kestrel", 2 }, { "buffalo", 2 }, { "baller", 2 } } },
		{ "hood", { { "bouncer", 4 }, { "brawler", 2 }, { "hauler", 2 } } },
		{ "docks", { { "boxer", 5 }, { "parcel", 4 }, { "hauler", 3 } } },
		{ "hills", { { "zenith", 4 }, { "kestrel", 4 }, { "summit", 3 }, { "tempest", 3 }, { "baller", 3 } } },
		{ "beach", { { "kestrel", 2 }, { "bouncer", 2 } } },
		{ "corona", { { "bouncer", 3 }, { "hauler", 2 } } },
		{ "country", { { "hauler", 6 }, { "summit", 3 }, { "boxer", 2 }, { "taxi", 0.1 } } },
		{ "desert", { { "hauler", 5 }, { "summit", 3 }, { "brawler", 2 }, { "taxi", 0.1 } } },
		{ "forest", { { "summit", 5 }, { "hauler", 4 }, { "taxi", 0.1 } } },
	};
	auto m = MULT.find(district);
	std::vector<std::pair<std::string, double>> pool;
	for (const auto& e : TrafficPool()) {
		// (motorbikes join the traffic once bikes.js is ported)
		if (!VehicleManager::Supported(*FindVehicle(e.first))) continue;
		double k = 1;
		if (m != MULT.end()) { auto it = m->second.find(e.first); if (it != m->second.end()) k = it->second; }
		pool.push_back({ e.first, e.second * k });
	}
	RNG rng((uint32_t)(int64_t)std::floor(Rand() * 1e9));
	return rng.Weighted(pool);
}

Vehicle* Traffic::spawnCar(const LaneStart& st, double s0, const std::string& forced) {
	const RoadNet& net = game.map.roads;
	const LanePath p = MakePath(ToV3(net.LanePath(net.edges[st.e], st.dir, st.lane)));
	const V3 P = SampleOn(p, s0), Q = SampleOn(p, s0 + 2);
	const double yaw = std::atan2(Q.x - P.x, Q.z - P.z);
	const std::string type = forced.empty() ? pickType(game.map.DistrictAt(P.x, P.z)) : forced;
	const double y = game.collision->surfaceHeight(P.x, P.z, P.y + 0.6);
	SpawnOpts o; o.hasY = true; o.y = y;
	Vehicle* v = game.vehicles.spawn(type, P.x, P.z, yaw, o);
	if (!v) return nullptr;
	v->lastGroundY = y;
	if (game.peds) {
		Ped* driver = game.peds->spawnPed(P.x, P.z);
		v->putIn(driver, 0);
	}
	v->ai = std::make_shared<LaneDriver>(game, v, &st);
	v->traffic = true;
	const double sp = Min(game.map.roads.edges[st.e].speed * 0.8, 20);
	v->vel.set(std::sin(yaw) * sp, 0, std::cos(yaw) * sp);
	if (Rand() < 0.15 && game.peds) { Ped* pa = game.peds->spawnPed(P.x, P.z); v->putIn(pa, 1); }
	cars.push_back(Ref<Vehicle>(v));
	return v;
}

void Traffic::populate(int count) {
	if (count < 0) count = maxCars;
	const bool saved = ignoreView;
	ignoreView = true;
	auto alive = [&]() { int n = 0; for (auto& c : cars) if (c.get() && !c->removed) n++; return n; };
	for (int k = 0; k < count * 2 && alive() < Min(count, maxCars); k++) trySpawn(35);
	ignoreView = saved;
}

bool Traffic::SampleLane(Game& game, double cx, double cz, double rMin, double rMax, Sample& out, const std::function<bool(const REdge&)>& filter) {
	const RoadNet& net = game.map.roads;
	for (int tries = 0; tries < 12; tries++) {
		const double a = Rand() * kTau, r = Rand(rMin, rMax);
		const double x = cx + std::cos(a) * r, z = cz + std::sin(a) * r;
		const EdgeHit c = net.Closest(x, z, [&](const REdge& e) { return !e.removed && e.type != ERoad::Dirt && (!filter || filter(e)); }, 90);
		if (!c.valid()) continue;
		const REdge& e = net.edges[c.e];
		std::vector<int> dirs;
		if (e.lanesF > 0) dirs.push_back(0);
		if (e.lanesB > 0) dirs.push_back(1);
		if (dirs.empty()) continue;
		const int dir = RandPick(dirs);
		const int lane = RandInt(0, (dir == 0 ? e.lanesF : e.lanesB) - 1);
		const Line pts = net.LanePath(e, dir, lane);
		if (pts.size() < 2) continue;
		const LanePath p = MakePath(ToV3(pts));
		if (p.len < 8) continue;
		const double s0 = Rand(3, Max(4, p.len - 6));
		const V3 P = SampleOn(p, s0);
		const double d = Hypot(P.x - cx, P.z - cz);
		if (d < rMin * 0.8 || d > rMax * 1.2) continue;
		out.start.e = c.e; out.start.dir = dir; out.start.lane = lane; out.s0 = s0; out.x = P.x; out.y = P.y; out.z = P.z;
		return true;
	}
	return false;
}

void Traffic::trySpawn(double minDist) {
	Player& pl = *game.player;
	Vehicle* pv = pl.vehicle;
	const V3 p = pv ? pv->pos : pl.pos;
	const double fast = pv ? Clamp(pv->speedAbs() / 30, 0, 1) : 0;
	double cx = p.x, cz = p.z;
	if (pv && fast > 0.3) { cx += pv->vel.x * 3; cz += pv->vel.z * 3; }
	const double rMax = 190 + fast * 110;
	Sample smp;
	if (!SampleLane(game, cx, cz, minDist, rMax, smp)) return;
	const double d2 = Dist2(smp.x, smp.z, p.x, p.z);
	if (d2 < minDist * minDist || d2 > (rMax + 40) * (rMax + 40)) return;
	if (!ignoreView && game.rig.inView(V3(smp.x, 1, smp.z)) && d2 < 140 * 140) return;
	for (const auto& v : game.vehicles.list) if (Dist2(v->pos.x, v->pos.z, smp.x, smp.z) < 12 * 12 && std::fabs(v->pos.y - smp.y) < 4) return;
	const REdge& e = game.map.roads.edges[smp.start.e];
	const bool rural = !game.map.IsOnCityStreet(smp.x, smp.z, 4) && e.type != ERoad::Freeway && !e.ncity;
	if (rural && Rand() < 0.45) return;
	spawnCar(smp.start, smp.s0);
}

double Traffic::density() const {
	const double h = game.env.hours;
	return h < 5 || h > 23 ? 0.5 : h < 7 ? 0.7 : 1;
}

void Traffic::update(double dt) {
	Player& pl = *game.player;
	const V3 p = pl.vehicle ? pl.vehicle->pos : pl.pos;
	spawnTimer -= dt;
	std::vector<Ref<Vehicle>> alive;
	for (auto& c : cars) if (c.get() && !c->removed) alive.push_back(c);
	cars = alive;
	const bool airborne = pl.vehicle && pl.vehicle->def.aircraft && pl.vehicle->altitude() > 40;
	if (spawnTimer <= 0 && (int)alive.size() < maxCars * density() && !game.disableAmbient && !airborne) { spawnTimer = 0.3; trySpawn(); }
	for (auto& c : alive) {
		Vehicle* v = c.get();
		if (!v || v->removed) continue;
		if (v->ai && v->driver() && !v->driver()->isPlayer && !v->driver()->dead) v->ai->update(dt);
		else if ((v->driver() && v->driver()->isPlayer) || !v->driver()) { v->ai.reset(); v->traffic = false; }
		const double d2 = Dist2(v->pos.x, v->pos.z, p.x, p.z);
		LaneDriver* ai = dynamic_cast<LaneDriver*>(v->ai.get());
		if ((d2 > 320 * 320 || (v->isWrecked() && d2 > 100 * 100)) && !v->persistent && pl.vehicle != v) despawn(v);
		else if (ai && !v->persistent && ((ai->jam > 18 && d2 > 35 * 35) || ai->jam > 60) && !game.rig.inView(V3(v->pos.x, 1, v->pos.z))) despawn(v);
	}
}

void Traffic::despawn(Vehicle* v) {
	for (auto& o : v->occupants) if (o && !o->isPlayer) { auto keep = o; if (game.peds) game.peds->remove(keep.get()); else keep->remove(); }
	for (auto& o : v->occupants) o.reset();
	game.vehicles.remove(v);
}

} // namespace atg
