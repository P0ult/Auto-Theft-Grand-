// Port of src/world/roadnet.js. See RoadNet.h.
#include "RoadNet.h"
#include "WorldGen.h"
#include <map>
#include <set>
#include <unordered_set>

namespace atg {

static const RoadType kTypes[] = {
	{ "street", 2, 2, 3.5, 0.15, 10, 10, 13.5, 0, 1, false },
	{ "freeway", 2, 0, 3.7, -3.7, 4.8, 5.9, 29, 1, 3, true },
	{ "ramp", 1, 0, 4.0, -2.0, 3.3, 3.6, 17, 2, 2, true },
	{ "highway", 1, 1, 3.6, 0.12, 5.5, 5.5, 24, 3, 2, false },
	{ "road", 1, 1, 3.3, 0.1, 4.5, 4.5, 15, 4, 1, false },
	{ "avenue", 2, 2, 3.3, 0.25, 7.2, 7.2, 15, 8, 2, false },
	{ "dirt", 1, 1, 2.8, 0.0, 3.4, 3.4, 11, 5, 0, false },
	{ "rail", 0, 0, 0, 0, 3.3, 3.3, 0, 7, -1, false },
};
const RoadType& RT(ERoad t) { return kTypes[(int)t]; }
const char* RoadTypeName(ERoad t) { return kTypes[(int)t].name; }

// ------------------------------------------------------------------ polyline helpers
template <typename PT>
static Line CatmullT(const std::vector<PT>& ctrl, double spacing) {
	Line out;
	const int n0 = (int)ctrl.size();
	auto P = [&](int i) -> const PT& { return ctrl[(size_t)Clamp(i, 0, n0 - 1)]; };
	for (int i = 0; i < n0 - 1; i++) {
		const PT &p0 = P(i - 1), &p1 = P(i), &p2 = P(i + 1), &p3 = P(i + 2);
		const double segLen = Hypot(p2.x - p1.x, p2.z - p1.z);
		const int n = (int)Max(1, std::ceil(segLen / spacing));
		for (int k = 0; k < n; k++) {
			const double t = (double)k / n, t2 = t * t, t3 = t2 * t;
			auto f = [&](double a, double b, double c, double d) { return 0.5 * ((2 * b) + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3); };
			P3 q; q.x = f(p0.x, p1.x, p2.x, p3.x); q.z = f(p0.z, p1.z, p2.z, p3.z); q.y = 0;
			out.push_back(q);
		}
	}
	P3 last; last.x = ctrl.back().x; last.z = ctrl.back().z; last.y = 0;
	out.push_back(last);
	return out;
}
Line Catmull(const std::vector<V2>& ctrl, double spacing) { return CatmullT(ctrl, spacing); }
Line Catmull(const Line& ctrl, double spacing) { return CatmullT(ctrl, spacing); }

std::vector<double> CumLen(const Line& pts) {
	std::vector<double> c(pts.size());
	if (pts.empty()) return c;
	c[0] = 0;
	for (size_t i = 1; i < pts.size(); i++) c[i] = c[i - 1] + Hypot(pts[i].x - pts[i - 1].x, pts[i].z - pts[i - 1].z);
	return c;
}

P3 PointAt(const Line& pts, const std::vector<double>& cum, double s) {
	if (s <= 0) return pts[0];
	const double L = cum.back();
	if (s >= L) return pts.back();
	int lo = 0, hi = (int)cum.size() - 1;
	while (hi - lo > 1) { const int m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
	double den = cum[hi] - cum[lo]; if (den == 0) den = 1;
	const double t = (s - cum[lo]) / den;
	const P3 &a = pts[lo], &b = pts[hi];
	P3 r; r.x = a.x + (b.x - a.x) * t; r.z = a.z + (b.z - a.z) * t; r.y = a.y + (b.y - a.y) * t;
	return r;
}

Line Resample(const Line& pts, double spacing) {
	const std::vector<double> cum = CumLen(pts);
	const double L = cum.back();
	const int n = (int)Max(1, JsRound(L / spacing));
	Line out;
	for (int k = 0; k <= n; k++) out.push_back(PointAt(pts, cum, L * k / n));
	return out;
}

V2 TangentAt(const Line& pts, int i) {
	const P3& a = pts[(size_t)(std::max)(0, i - 1)];
	const P3& b = pts[(size_t)(std::min)((int)pts.size() - 1, i + 1)];
	const double dx = b.x - a.x, dz = b.z - a.z;
	double l = Hypot(dx, dz); if (l == 0) l = 1;
	return { dx / l, dz / l };
}

Line OffsetLine(const Line& pts, double off) {
	Line out(pts.size());
	for (size_t i = 0; i < pts.size(); i++) {
		const V2 t = TangentAt(pts, (int)i);
		out[i].x = pts[i].x - t.z * off; out[i].z = pts[i].z + t.x * off; out[i].y = pts[i].y;
	}
	return out;
}

ProjectHit Project(const Line& pts, const std::vector<double>& cum, double x, double z) {
	double best = kInf, bs = 0, bl = 0; int bi = 0;
	for (size_t i = 0; i + 1 < pts.size(); i++) {
		const P3 &a = pts[i], &b = pts[i + 1];
		const double dx = b.x - a.x, dz = b.z - a.z;
		double L2 = dx * dx + dz * dz; if (L2 == 0) L2 = 1;
		const double t = Clamp(((x - a.x) * dx + (z - a.z) * dz) / L2, 0, 1);
		const double px = a.x + dx * t, pz = a.z + dz * t;
		const double d = (x - px) * (x - px) + (z - pz) * (z - pz);
		if (d < best) { best = d; bs = cum[i] + (cum[i + 1] - cum[i]) * t; bi = (int)i; bl = ((x - a.x) * -dz + (z - a.z) * dx) / std::sqrt(L2); }
	}
	ProjectHit h; h.s = bs; h.d = std::sqrt(best); h.i = bi; h.lat = -bl;
	return h;
}

std::optional<CrossHit> Intersect(const Line& A, const std::vector<double>& cumA, const Line& B, const std::vector<double>& cumB) {
	for (size_t i = 0; i + 1 < A.size(); i++) {
		const P3& p = A[i];
		const double r0 = A[i + 1].x - p.x, r1 = A[i + 1].z - p.z;
		for (size_t j = 0; j + 1 < B.size(); j++) {
			const P3& q = B[j];
			const double s0 = B[j + 1].x - q.x, s1 = B[j + 1].z - q.z;
			const double den = r0 * s1 - r1 * s0;
			if (std::fabs(den) < 1e-9) continue;
			const double t = ((q.x - p.x) * s1 - (q.z - p.z) * s0) / den;
			const double u = ((q.x - p.x) * r1 - (q.z - p.z) * r0) / den;
			if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
				const double segA = cumA[i + 1] - cumA[i], segB = cumB[j + 1] - cumB[j];
				return CrossHit{ cumA[i] + segA * t, cumB[j] + segB * u, p.x + r0 * t, p.z + r1 * t };
			}
		}
	}
	return std::nullopt;
}

template <typename C>
static int NearestIdxT(const C& cum, double s) {
	int lo = 0, hi = (int)cum.size() - 1;
	while (hi - lo > 1) { const int m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
	return s - cum[lo] < cum[hi] - s ? lo : hi;
}
int NearestIdx(const std::vector<double>& cum, double s) { return NearestIdxT(cum, s); }
int NearestIdx(const std::vector<float>& cum, double s) { return NearestIdxT(cum, s); }

// ------------------------------------------------------------------ profile solver
Profile SolveProfile(const Line& pts, const std::function<double(double, double)>& terrain, const ProfileOpts& o) {
	const int n = (int)pts.size();
	const std::vector<double> cum = CumLen(pts);
	std::vector<float> raw(n);
	for (int i = 0; i < n; i++) { const double t = terrain(pts[i].x, pts[i].z); raw[i] = (float)(t < 0.3 ? Max(t, o.waterY) : Max(t, o.minY)); }
	const double win = o.window;
	std::vector<float> y(n);
	int lo = 0, hi = 0; double sum = 0;
	for (int i = 0; i < n; i++) {
		while (hi < n && cum[hi] - cum[i] <= win / 2) { sum += raw[hi]; hi++; }
		while (cum[i] - cum[lo] > win / 2) { sum -= raw[lo]; lo++; }
		y[i] = (float)(sum / (hi - lo));
	}
	const double maxCut = o.maxCut;
	for (int i = 0; i < n; i++) y[i] = (float)Max(y[i], raw[i] - maxCut);
	std::vector<ProfilePin> pins = o.pins;
	if (IsSet(o.y0)) pins.push_back({ 0, o.y0, NaN() });
	if (IsSet(o.y1)) pins.push_back({ cum[n - 1], o.y1, NaN() });
	const double g = o.maxGrade;
	std::vector<float> add(n, 0.0f);
	for (const ProfilePin& p : pins) {
		const int i0 = NearestIdx(cum, p.s);
		const double c = p.y - y[i0];
		const double R = Max(Max(IsSet(p.r) ? p.r : 0, 30), std::fabs(c) / g * 1.35);
		for (int i = 0; i < n; i++) {
			const double d = std::fabs(cum[i] - p.s);
			if (d < R) { const double t = 1 - d / R; add[i] = (float)(add[i] + c * t * t * (3 - 2 * t)); }
		}
	}
	for (int i = 0; i < n; i++) y[i] = (float)((double)y[i] + (double)add[i]);
	std::vector<uint8_t> fixed(n, 0);
	if (o.fixed) for (int i = 0; i < n; i++) { const double f = o.fixed(pts[i].x, pts[i].z, cum[i]); if (IsSet(f)) { y[i] = (float)f; fixed[i] = 1; } }
	for (const ProfilePin& p : pins) { const int i0 = NearestIdx(cum, p.s); y[i0] = (float)p.y; fixed[i0] = 1; }
	for (int it = 0; it < 4; it++) {
		for (int i = 1; i < n; i++) if (!fixed[i]) { const double ds = cum[i] - cum[i - 1]; y[i] = (float)Clamp(y[i], y[i - 1] - g * ds, y[i - 1] + g * ds); }
		for (int i = n - 2; i >= 0; i--) if (!fixed[i]) { const double ds = cum[i + 1] - cum[i]; y[i] = (float)Clamp(y[i], y[i + 1] - g * ds, y[i + 1] + g * ds); }
	}
	std::vector<uint8_t> hard(n, 0);
	if (o.fixed) for (int i = 0; i < n; i++) if (IsSet(o.fixed(pts[i].x, pts[i].z, cum[i]))) hard[i] = 1;
	const double gMax = g * 1.45;
	for (int it = 0; it < 400; it++) {
		bool bad = false;
		for (int i = 1; i < n; i++) {
			const double ds = cum[i] - cum[i - 1], dy = (double)y[i] - (double)y[i - 1];
			if (std::fabs(dy) <= gMax * ds) continue;
			bad = true;
			const double ex = (std::fabs(dy) - gMax * ds) * (dy > 0 ? 1 : dy < 0 ? -1 : 0);
			if (hard[i] && hard[i - 1]) continue;
			if (hard[i]) y[i - 1] = (float)(y[i - 1] + ex);
			else if (hard[i - 1]) y[i] = (float)(y[i] - ex);
			else { y[i - 1] = (float)(y[i - 1] + ex / 2); y[i] = (float)(y[i] - ex / 2); }
		}
		if (!bad) break;
	}
	Profile pr; pr.y = std::move(y); pr.cum = cum;
	return pr;
}

// ------------------------------------------------------------------ graph
RNode& RoadNet::AddNode(double x, double z, double y, const NodeOpts& o) {
	RNode n;
	n.id = (int)nodes.size(); n.x = x; n.z = z; n.y = y;
	n.kind = o.kind; n.r = o.r; n.sig = o.sig; n.hasGrid = o.hasGrid; n.gi = o.gi; n.gj = o.gj; n.rbR = o.rbR; n.name = o.name; n.noStop = o.noStop; n.rail = o.rail;
	nodes.push_back(n);
	return nodes.back();
}

REdge& RoadNet::AddEdge(int a, int b, const Line& pts, ERoad type, const EdgeOpts& o) {
	const RoadType& T = RT(type);
	const int n = (int)pts.size();
	REdge e;
	e.p.resize((size_t)n * 3); e.cum.assign(n, 0.0f);
	for (int i = 0; i < n; i++) {
		e.p[i * 3] = (float)pts[i].x; e.p[i * 3 + 1] = (float)pts[i].y; e.p[i * 3 + 2] = (float)pts[i].z;
		if (i) e.cum[i] = (float)((double)e.cum[i - 1] + Hypot(pts[i].x - pts[i - 1].x, pts[i].z - pts[i - 1].z));
	}
	e.id = (int)edges.size(); e.a = a; e.b = b; e.type = type; e.T = &T;
	e.len = e.cum[n - 1]; e.n = n;
	e.lanesF = o.hasLanes ? o.lanesF : T.lanesF; e.lanesB = o.hasLanes ? o.lanesB : T.lanesB;
	e.render = o.render; e.hasGrid = o.hasGrid; e.grid = o.grid; e.name = o.name; e.deck.assign(n, 0);
	e.speed = IsSet(o.speed) ? o.speed : T.speed; e.wL = IsSet(o.wL) ? o.wL : T.wL; e.wR = IsSet(o.wR) ? o.wR : T.wR;
	e.barrierL = o.barrierL; e.barrierR = o.barrierR; e.under = o.under; e.city = o.city;
	e.noBarrierA = o.noBarrierA; e.noBarrierB = o.noBarrierB; e.trimA = o.trimA; e.trimB = o.trimB; e.base = o.base;
	edges.push_back(std::move(e));
	REdge& E = edges.back();
	nodes[a].e.push_back(E.id); if (b != a) nodes[b].e.push_back(E.id);
	Index(E);
	return edges.back();
}

void RoadNet::Index(const REdge& e) {
	const double c = cell;
	const double pad = Max(e.wL, e.wR) + 2;
	for (int i = 0; i < e.n - 1; i++) {
		const double x0 = Min(e.p[i * 3], e.p[i * 3 + 3]) - pad, x1 = Max(e.p[i * 3], e.p[i * 3 + 3]) + pad;
		const double z0 = Min(e.p[i * 3 + 2], e.p[i * 3 + 5]) - pad, z1 = Max(e.p[i * 3 + 2], e.p[i * 3 + 5]) + pad;
		for (int64_t gx = (int64_t)std::floor(x0 / c); gx <= (int64_t)std::floor(x1 / c); gx++) for (int64_t gz = (int64_t)std::floor(z0 / c); gz <= (int64_t)std::floor(z1 / c); gz++) {
			std::vector<Seg>& arr = grid[Key(gx, gz)];
			if (!arr.empty() && arr.back().e == e.id && arr.back().i1 == i) arr.back().i1 = i + 1;
			else arr.push_back({ e.id, i, i + 1 });
		}
	}
}

void RoadNet::RemoveEdge(int eid) {
	REdge& e = edges[eid];
	e.removed = true;
	for (int nid : { e.a, e.b }) {
		std::vector<int>& L = nodes[nid].e;
		auto it = std::find(L.begin(), L.end(), eid);
		if (it != L.end()) L.erase(it);
	}
	for (auto& kv : grid) {
		std::vector<Seg>& arr = kv.second;
		for (int i = (int)arr.size() - 1; i >= 0; i--) if (arr[i].e == eid) arr.erase(arr.begin() + i);
	}
}

std::vector<int> RoadNet::EdgesIn(double x0, double z0, double x1, double z1) const {
	const double c = cell;
	std::vector<int> out; std::unordered_set<int> seen;
	for (int64_t gx = (int64_t)std::floor(x0 / c); gx <= (int64_t)std::floor(x1 / c); gx++) for (int64_t gz = (int64_t)std::floor(z0 / c); gz <= (int64_t)std::floor(z1 / c); gz++) {
		auto it = grid.find(Key(gx, gz));
		if (it == grid.end()) continue;
		for (const Seg& r : it->second) if (seen.insert(r.e).second) out.push_back(r.e);
	}
	return out;
}

EdgeHit RoadNet::Closest(double x, double z, const std::function<bool(const REdge&)>& filter, double maxD, double y) const {
	EdgeHit best; bool have = false;
	auto tryRange = [&](const REdge& e, int i0, int i1) {
		for (int i = i0; i < i1; i++) {
			const double ax = e.p[i * 3], az = e.p[i * 3 + 2], bx = e.p[i * 3 + 3], bz = e.p[i * 3 + 5];
			const double dx = bx - ax, dz = bz - az;
			double L2 = dx * dx + dz * dz; if (L2 == 0) L2 = 1;
			const double t = Clamp(((x - ax) * dx + (z - az) * dz) / L2, 0, 1);
			const double px = ax + dx * t, pz = az + dz * t;
			double d = Hypot(x - px, z - pz);
			if (IsSet(y)) { const double ey = e.p[i * 3 + 1] + ((double)e.p[i * 3 + 4] - e.p[i * 3 + 1]) * t; d += Max(0, std::fabs(ey - y) - 3) * 12; }
			if (d < maxD && (!have || d < best.d)) {
				const double L = std::sqrt(L2);
				const double lat = ((x - ax) * -dz + (z - az) * dx) / L;
				have = true;
				best.e = e.id; best.s = e.cum[i] + L * t; best.d = d; best.lat = -lat;
				best.y = e.p[i * 3 + 1] + ((double)e.p[i * 3 + 4] - e.p[i * 3 + 1]) * t; best.i = i; best.t = t;
			}
		}
	};
	const double c = cell;
	const double R = Min(maxD, 400);
	for (int64_t gx = (int64_t)std::floor((x - R) / c); gx <= (int64_t)std::floor((x + R) / c); gx++) for (int64_t gz = (int64_t)std::floor((z - R) / c); gz <= (int64_t)std::floor((z + R) / c); gz++) {
		auto it = grid.find(Key(gx, gz));
		if (it == grid.end()) continue;
		for (const Seg& r : it->second) { const REdge& e = edges[r.e]; if (!filter || filter(e)) tryRange(e, r.i0, r.i1); }
	}
	return best;
}

RoadHit RoadNet::OnRoad(double x, double z, double margin) const {
	RoadHit h;
	auto it = grid.find(Key((int64_t)std::floor(x / cell), (int64_t)std::floor(z / cell)));
	if (it == grid.end()) return h;
	for (const Seg& r : it->second) {
		const REdge& e = edges[r.e];
		if (e.hasGrid) continue;
		for (int i = r.i0; i < r.i1; i++) {
			const double ax = e.p[i * 3], az = e.p[i * 3 + 2], bx = e.p[i * 3 + 3], bz = e.p[i * 3 + 5];
			const double dx = bx - ax, dz = bz - az;
			double L2 = dx * dx + dz * dz; if (L2 == 0) L2 = 1;
			const double t = Clamp(((x - ax) * dx + (z - az) * dz) / L2, 0, 1);
			const double px = ax + dx * t, pz = az + dz * t;
			const double lat = ((x - px) * -dz + (z - pz) * dx) / std::sqrt(L2);
			const double w = lat > 0 ? e.wL : e.wR;
			if (std::fabs(lat) <= w + margin) {
				h.e = e.id; h.i = i; h.t = t; h.y = e.p[i * 3 + 1] + ((double)e.p[i * 3 + 4] - e.p[i * 3 + 1]) * t; h.deck = e.deck[i] || e.deck[i + 1];
				return h;
			}
		}
	}
	return h;
}

EdgePoint RoadNet::At(const REdge& e, double s) const {
	s = Clamp(s, 0, e.len);
	int lo = 0, hi = e.n - 1;
	while (hi - lo > 1) { const int m = (lo + hi) >> 1; if (e.cum[m] <= s) lo = m; else hi = m; }
	double den = (double)e.cum[hi] - e.cum[lo]; if (den == 0) den = 1;
	const double t = (s - e.cum[lo]) / den;
	const double ax = e.p[lo * 3], ay = e.p[lo * 3 + 1], az = e.p[lo * 3 + 2], bx = e.p[hi * 3], by = e.p[hi * 3 + 1], bz = e.p[hi * 3 + 2];
	const double dx = bx - ax, dz = bz - az;
	double l = Hypot(dx, dz); if (l == 0) l = 1;
	EdgePoint o; o.x = ax + dx * t; o.y = ay + (by - ay) * t; o.z = az + dz * t; o.tx = dx / l; o.tz = dz / l;
	return o;
}

double RoadNet::LaneOffset(const REdge& e, int dir, int lane) const {
	const int nl = dir == 0 ? e.lanesF : e.lanesB;
	lane = (int)Clamp(lane, 0, nl - 1);
	return e.T->off0 + e.T->laneW * (lane + 0.5);
}

double RoadNet::Clearance(const RNode& n) const {
	if (n.kind == ENode::Via || n.kind == ENode::Split || n.kind == ENode::Merge) return 0;
	if (n.kind == ENode::RB) return n.rbR + 9;
	return n.r;
}

Line RoadNet::LanePath(const REdge& e, int dir, int lane, double trimStart, double trimEnd) const {
	const RNode& na = nodes[dir == 0 ? e.a : e.b];
	const RNode& nb = nodes[dir == 0 ? e.b : e.a];
	const double off = LaneOffset(e, dir, lane);
	const double tA = dir == 0 ? e.trimA : e.trimB, tB = dir == 0 ? e.trimB : e.trimA;
	double s0 = IsSet(trimStart) ? trimStart : IsSet(tA) ? tA : Clearance(na) + (na.kind == ENode::X || na.kind == ENode::RB ? 0.5 : 0);
	double s1 = e.len - (IsSet(trimEnd) ? trimEnd : IsSet(tB) ? tB : Clearance(nb) + (nb.kind == ENode::X ? 7.5 : nb.kind == ENode::RB ? 1 : 0));
	if (s1 - s0 < 2) { const double m = (s0 + s1) / 2; s0 = Max(0, m - 1); s1 = Min(e.len, m + 1); }
	Line out;
	const double step = 5;
	const int n = (int)Max(1, std::ceil((s1 - s0) / step));
	for (int k = 0; k <= n; k++) {
		const double s = s0 + (s1 - s0) * k / n;
		const double ss = dir == 0 ? s : e.len - s;
		const EdgePoint q = At(e, ss);
		double tx = q.tx, tz = q.tz;
		if (dir == 1) { tx = -tx; tz = -tz; }
		P3 p; p.x = q.x - tz * off; p.y = q.y; p.z = q.z + tx * off;
		out.push_back(p);
	}
	return out;
}

std::optional<Route> RoadNet::FindRoute(double fromX, double fromZ, double toX, double toZ, const std::function<bool(const REdge&)>& filter, bool anyDir) const {
	auto drivable = [&](const REdge& e) { return e.type != ERoad::Rail && (!filter || filter(e)); };
	const EdgeHit start = Closest(fromX, fromZ, drivable);
	const EdgeHit goal = Closest(toX, toZ, drivable);
	if (!start.valid() || !goal.valid()) return std::nullopt;
	std::unordered_map<int, double> g;
	struct Came { int from, via; };
	std::unordered_map<int, Came> came;
	std::vector<std::pair<double, int>> open;
	auto h = [&](const RNode& n) { return Hypot(n.x - toX, n.z - toZ); };
	auto push = [&](int id, double cost, int from, int via) {
		auto it = g.find(id);
		if (it != g.end() && it->second <= cost) return;
		g[id] = cost; came[id] = { from, via };
		open.push_back({ cost + h(nodes[id]), id });
	};
	const REdge& se = edges[start.e];
	if (se.lanesF > 0 || anyDir) push(se.b, se.len - start.s, -1, se.id);
	if (se.lanesB > 0 || anyDir) push(se.a, start.s, -1, se.id);
	const REdge& ge = edges[goal.e];
	int found = -1, iter = 0;
	while (!open.empty() && iter++ < 20000) {
		size_t bi = 0;
		for (size_t i = 1; i < open.size(); i++) if (open[i].first < open[bi].first) bi = i;
		const int id = open[bi].second;
		open[bi] = open.back(); open.pop_back();
		if (id == ge.a || id == ge.b) { found = id; break; }
		const RNode& n = nodes[id];
		const double gc = g[id];
		for (int eid : n.e) {
			const REdge& e = edges[eid];
			int to; bool ok;
			if (e.a == id) { to = e.b; ok = e.lanesF > 0 || anyDir; } else { to = e.a; ok = e.lanesB > 0 || anyDir; }
			if (!ok || to == id) continue;
			const double pen = e.type == ERoad::Dirt ? 1.6 : e.type == ERoad::Freeway ? 0.75 : e.type == ERoad::Ramp ? 0.9 : 1;
			push(to, gc + e.len * pen, id, eid);
		}
	}
	if (found < 0) return std::nullopt;
	Route r; r.start = start; r.goal = goal;
	int cur = found;
	while (came.count(cur)) { const Came c = came[cur]; r.seq.push_back({ cur, c.via }); if (c.from == -1) break; cur = c.from; }
	std::reverse(r.seq.begin(), r.seq.end());
	return r;
}

std::vector<V2> RoadNet::RoutePolyline(const Route* r, double fromX, double fromZ, double toX, double toZ) const {
	if (!r) return { { fromX, fromZ }, { toX, toZ } };
	std::vector<V2> pts = { { fromX, fromZ } };
	int prevNode = -1;
	for (const RouteStep& st : r->seq) {
		const REdge& e = edges[st.edge];
		const bool forward = e.b == st.node;
		if (prevNode < 0) {
			const double s0 = r->start.s, s1 = forward ? e.len : 0;
			const int n = (int)Max(1, std::ceil(std::fabs(s1 - s0) / 25));
			for (int k = 0; k <= n; k++) { const EdgePoint q = At(e, s0 + (s1 - s0) * k / n); pts.push_back({ q.x, q.z }); }
		} else {
			const int n = (int)Max(1, std::ceil(e.len / 25));
			for (int k = 0; k <= n; k++) { const EdgePoint q = At(e, forward ? e.len * k / n : e.len * (1 - (double)k / n)); pts.push_back({ q.x, q.z }); }
		}
		prevNode = st.node;
	}
	const REdge& ge = edges[r->goal.e];
	if (prevNode >= 0) {
		const bool fromA = ge.a == prevNode;
		const double s0 = fromA ? 0 : ge.len, s1 = r->goal.s;
		const int n = (int)Max(1, std::ceil(std::fabs(s1 - s0) / 25));
		for (int k = 0; k <= n; k++) { const EdgePoint q = At(ge, s0 + (s1 - s0) * k / n); pts.push_back({ q.x, q.z }); }
	}
	pts.push_back({ toX, toZ });
	return pts;
}

// ------------------------------------------------------------------ terrain shaping
static double FillMax(ERoad t) {
	switch (t) {
	case ERoad::Freeway: return 7.5; case ERoad::Ramp: return 6.5; case ERoad::Highway: return 7.5; case ERoad::Road: return 6.5;
	case ERoad::Avenue: return 6.5; case ERoad::Dirt: return 16; case ERoad::Rail: return 9; default: return 6;
	}
}

void ShapeTerrain(RoadNet& net, Heightfield& hf, const std::function<double(double, double)>& groundAt) {
	std::vector<REdge*> list;
	for (REdge& e : net.edges) if (!e.removed && !e.hasGrid) list.push_back(&e);
	auto avgY = [](const REdge* e) { double s = 0; for (int i = 0; i < e->n; i++) s += e->p[i * 3 + 1]; return s / e->n; };
	std::vector<double> avg(net.edges.size(), 0);
	for (REdge* e : list) avg[e->id] = avgY(e);
	std::stable_sort(list.begin(), list.end(), [&](const REdge* a, const REdge* b) { return avg[b->id] - avg[a->id] < 0; });
	std::vector<float>& h = hf.h;
	std::vector<float> low(h.size(), std::numeric_limits<float>::infinity());
	for (REdge* ep : list) {
		REdge& e = *ep;
		const std::vector<float>& p = e.p;
		for (int i = 0; i < e.n; i++) {
			const double x = p[i * 3], y = p[i * 3 + 1], z = p[i * 3 + 2];
			const double cg = groundAt(x, z);
			if (IsSet(cg)) { e.deck[i] = y - cg > 0.14 ? 1 : 0; continue; }
			const double g = hf.Sample(x, z);
			e.deck[i] = (y - g > FillMax(e.type) || g < 0.4) ? 1 : 0;
		}
		const std::vector<uint8_t> d0 = e.deck;
		for (int i = 0; i < e.n; i++) {
			const bool prev = i > 0 && d0[i - 1], next = i + 1 < e.n && d0[i + 1];
			if (d0[i] && !(prev || next) && e.n > 2) e.deck[i] = 0;
		}
		const double core = Max(e.wL, e.wR) + 2.2, blend = (e.type == ERoad::Freeway || e.type == ERoad::Ramp) ? 22 : 16;
		// (insertion-ordered like the JavaScript Map; the order only matters for which entry wins, and
		// every cell has one entry)
		std::unordered_map<int, std::pair<double, double>> coreY;
		std::vector<int> coreOrder;
		for (int i = 0; i < e.n - 1; i++) {
			if (e.deck[i] && e.deck[i + 1]) continue;
			const double ax = p[i * 3], ay = p[i * 3 + 1], az = p[i * 3 + 2], bx = p[i * 3 + 3], by = p[i * 3 + 4], bz = p[i * 3 + 5];
			if (IsSet(groundAt(ax, az)) && IsSet(groundAt(bx, bz))) continue;
			const double R = core + blend;
			const double x0 = Min(ax, bx) - R, x1 = Max(ax, bx) + R, z0 = Min(az, bz) - R, z1 = Max(az, bz) + R;
			const double dx = bx - ax, dz = bz - az;
			double L2 = dx * dx + dz * dz; if (L2 == 0) L2 = 1;
			const bool spanA = i > 0 && e.deck[i] && e.deck[i - 1], spanB = i + 2 < e.n && e.deck[i + 1] && e.deck[i + 2];
			hf.ForRect(x0, z0, x1, z1, [&](int k, double x, double z) {
				if (IsSet(groundAt(x, z))) return;
				const double tr = ((x - ax) * dx + (z - az) * dz) / L2, t = Clamp(tr, 0, 1);
				const double d = Hypot(x - ax - dx * t, z - az - dz * t);
				if (d > R) return;
				const double ty = ay + (by - ay) * (((tr < 0 && spanA) || (tr > 1 && spanB)) ? tr : t) - 0.07;
				const double w = d <= core ? 1 : 1 - Smooth(core, R, d);
				const double cur = h[k];
				const double nv = cur + (ty - cur) * w;
				if (d <= core) {
					auto it = coreY.find(k);
					if (it == coreY.end()) { coreY[k] = { d, ty }; coreOrder.push_back(k); }
					else if (d < it->second.first) it->second = { d, ty };
				} else h[k] = (float)nv;
			});
		}
		for (int k : coreOrder) { const double ty = coreY[k].second; h[k] = (float)ty; if (ty < low[k]) low[k] = (float)ty; }
	}
	for (RNode& n : net.nodes) {
		if (n.hasGrid || n.city || n.dead) continue;
		if (IsSet(groundAt(n.x, n.z))) continue;
		const double r = n.kind == ENode::RB ? n.rbR + 6 : n.kind == ENode::X ? n.r + 1 : n.kind == ENode::End ? 6 : 0;
		if (r == 0) continue;
		Pad pad; pad.x = n.x; pad.z = n.z; pad.r = r; pad.y = n.y - 0.07; pad.blend = 16;
		hf.PadIt(pad);
	}
	for (size_t k = 0; k < h.size(); k++) if (low[k] < h[k]) h[k] = low[k];
	for (REdge* ep : list) {
		REdge& e = *ep;
		for (int i = 0; i < e.n; i++) {
			const double x = e.p[i * 3], y = e.p[i * 3 + 1], z = e.p[i * 3 + 2];
			const double cg = groundAt(x, z);
			e.deck[i] = IsSet(cg) ? (y - cg > 0.14 ? 1 : 0) : (y - hf.Sample(x, z) > 1.6 ? 1 : 0);
		}
	}
}

} // namespace atg
