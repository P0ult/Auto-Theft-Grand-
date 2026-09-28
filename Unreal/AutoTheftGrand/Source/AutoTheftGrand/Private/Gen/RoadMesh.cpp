// The non-grid road network's geometry (port of src/world/roadmesh.js): road ribbons with painted markings
// (the material paints them from the per-vertex channels), junction pads, roundabouts with planted islands,
// San Aurelio's raised pavements, and for elevated sections concrete decks, jersey barriers and pillars;
// plus the railway's rails, sleepers (instanced) and station platforms.
#include "WorldMeshes.h"

namespace atg {

namespace {
constexpr double CHUNK = 400;
int MarkOf(ERoad t) {
	switch (t) {
	case ERoad::Freeway: return 1; case ERoad::Ramp: return 2; case ERoad::Highway: return 3; case ERoad::Road: return 4; case ERoad::Dirt: return 5;
	case ERoad::Street: return 0; case ERoad::Rail: return 7; case ERoad::Avenue: return 8; default: return 4;
	}
}
constexpr int MARK_JUNCTION = 6;

struct Row { double s, x, y, z, tx, tz; bool deck; };
struct Side { Row r; double lx, lz, Rx, Rz, yl, yr; };

struct Builder {
	const CityMap& map;
	const RoadNet& net;
	RoadMeshes out;
	explicit Builder(const CityMap& m) : map(m), net(m.roads) {}

	RoadChunk& chunk(double x, double z) {
		const std::pair<int, int> k{ (int)std::floor(x / CHUNK), (int)std::floor(z / CHUNK) };
		auto it = out.chunks.find(k);
		if (it == out.chunks.end()) {
			RoadChunk c; c.cx = (k.first + 0.5) * CHUNK; c.cz = (k.second + 0.5) * CHUNK;
			c.conc.Rough(0.92); c.conc.Signal(-1); // concrete grime
			c.rails.Rough(0.32);
			it = out.chunks.emplace(k, std::move(c)).first;
		}
		return it->second;
	}
	double ground(double x, double z) const { return map.GroundHeight(x, z); }

	void trims(const REdge& e, double& t0, double& t1) const {
		auto t = [&](const RNode& n, double x, double z) -> double {
			if (n.hasGrid || n.city) return Max(0, 10.2 - Hypot(x - n.x, z - n.z));
			if (n.kind == ENode::X) return n.r;
			if (n.kind == ENode::RB) return n.rbR + 5.4;
			return 0;
		};
		t0 = t(net.nodes[e.a], e.X(0), e.Z(0));
		t1 = t(net.nodes[e.b], e.X(e.n - 1), e.Z(e.n - 1));
	}

	static int IdxAt(const std::vector<float>& cum, double s) {
		int lo = 0, hi = (int)cum.size() - 1;
		while (hi - lo > 1) { const int m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
		return lo;
	}

	void edge(const REdge& e) {
		const int n = e.n;
		double t0, t1; trims(e, t0, t1);
		const double s0 = t0, s1 = e.len - t1;
		if (s1 - s0 < 0.5) return;
		const int type = MarkOf(e.type);
		const RNode &na = net.nodes[e.a], &nb = net.nodes[e.b];
		int flags = 0;
		if (e.ncity) {
			if (na.kind == ENode::X && na.e.size() > 2) flags |= 4;
			if (nb.kind == ENode::X && nb.e.size() > 2) flags |= 8;
		} else {
			if (na.kind == ENode::X && !na.city && na.maxCls > e.T->cls) flags |= 1;
			if (nb.kind == ENode::X && !nb.city && nb.maxCls > e.T->cls) flags |= 2;
		}
		std::vector<double> st = { s0 };
		for (int i = 1; i < n - 1; i++) if (e.cum[i] > s0 + 0.3 && e.cum[i] < s1 - 0.3) st.push_back(e.cum[i]);
		st.push_back(s1);
		std::vector<Row> rows;
		for (double s : st) {
			const EdgePoint q = net.At(e, s);
			const int i = IdxAt(e.cum, s);
			const bool deck = e.deck[i] || e.deck[(std::min)(n - 1, i + 1)];
			rows.push_back({ s, q.x, q.y, q.z, q.tx, q.tz, deck });
		}
		for (size_t k = 1; k + 1 < rows.size(); k++) {
			const Row &a = rows[k - 1], &b = rows[k + 1];
			const double dx = b.x - a.x, dz = b.z - a.z; double l = Hypot(dx, dz); if (l == 0) l = 1;
			rows[k].tx = dx / l; rows[k].tz = dz / l;
		}
		const double wL = e.wL, wR = e.wR;
		const bool rail = e.type == ERoad::Rail;
		const double lift = rail ? 0.005 : 0.035;
		auto inCross = [&](double s) { if (!rail) return false; for (const RailCrossZone& c : e.crossings) if (std::fabs(s - c.s) < c.halfW) return true; return false; };
		const double wMax = Max(wL, wR);
		bool havePrev = false; Side prev{};
		for (const Row& r : rows) {
			const double rx = -r.tz, rz = r.tx;
			const double lx = r.x - rx * wL, lz = r.z - rz * wL, Rx = r.x + rx * wR, Rz = r.z + rz * wR;
			const double y = r.y + lift;
			double yl = y, yr = y;
			if (e.type == ERoad::Dirt && !r.deck) { yl = Max(y - 0.25, Min(y + 0.25, ground(lx, lz) + 0.05)); yr = Max(y - 0.25, Min(y + 0.25, ground(Rx, Rz) + 0.05)); }
			const Side cur{ r, lx, lz, Rx, Rz, yl, yr };
			if (havePrev) {
				RoadChunk& c = chunk((prev.r.x + r.x) / 2, (prev.r.z + r.z) / 2);
				MeshBuf& A = c.road;
				if (!inCross((prev.r.s + r.s) / 2)) {
					// (both ends of a piece share one v offset so the dashes run on across it)
					const double off = 36 * std::floor(prev.r.s / 36);
					auto rv = [&](double x, double yy, double z, double u, double s) {
						A.Set(1, Min(s, 60), Min(e.len - s, 60)); A.Set(2, type, flags); A.Set(3, wMax, 0);
						return A.V(x, yy, z, 0, 1, 0, u, s - off);
					};
					const uint32_t i0 = rv(prev.lx, prev.yl, prev.lz, -wL, prev.r.s), i1 = rv(prev.Rx, prev.yr, prev.Rz, wR, prev.r.s);
					const uint32_t i2 = rv(cur.Rx, cur.yr, cur.Rz, wR, r.s), i3 = rv(cur.lx, cur.yl, cur.lz, -wL, r.s);
					A.QuadAuto(i0, i1, i2, i3);
				}
				const bool deck = prev.r.deck && r.deck;
				if (deck) deckSegment(c.conc, e, prev, cur);
				else if (sags(prev, cur)) deckEnd(c.conc, e, prev, cur);
				const bool rawL = e.barrierL == 1, rawR = e.barrierR == 1;
				const bool bL = rawL && (e.type == ERoad::Freeway || deck), bR = (rawR && deck) || (e.type == ERoad::Freeway && rawR && deck);
				auto noB = [&](double s) { return (e.noBarrierA > 0 && s < e.noBarrierA) || (e.noBarrierB > 0 && s > e.len - e.noBarrierB); };
				auto taper = [&](double s) { return e.type == ERoad::Ramp && ((IsSet(e.trimA) && e.trimA != 0 && s < e.trimA + 8) || (IsSet(e.trimB) && e.trimB != 0 && s > e.len - e.trimB - 8)); };
				if (bL && !(e.type == ERoad::Ramp && noB(r.s)) && !taper(r.s) && !taper(prev.r.s)) barrier(c.conc, prev.lx, prev.yl, prev.lz, cur.lx, cur.yl, cur.lz, -1);
				if ((bR || (deck && e.type != ERoad::Dirt)) && (e.type == ERoad::Ramp || (!noB(r.s) && !noB(prev.r.s)))) barrier(c.conc, prev.Rx, prev.yr, prev.Rz, cur.Rx, cur.yr, cur.Rz, 1);
			}
			prev = cur; havePrev = true;
		}
		if (e.walk > 0) pavements(e);
		// pillars under deck runs
		double acc = 14;
		for (size_t k = 1; k < rows.size(); k++) {
			const Row &a = rows[k - 1], &b = rows[k];
			if (!(a.deck && b.deck)) { acc = 14; continue; }
			acc += b.s - a.s;
			if (acc < 26) continue;
			const double g = ground(b.x, b.z);
			if (b.y - DECK_H - g < 2.2) { acc = 20; continue; }
			if (map.IsOnCityStreet(b.x, b.z, 3)) continue;
			const RoadHit h = net.OnRoad(b.x, b.z, 1.5);
			if (h.valid() && h.e != e.id) continue;
			acc = 0;
			pillar(chunk(b.x, b.z).conc, e, b, g);
		}
	}

	static void colorOf(MeshBuf& A, const double c[3]) { A.Color(c[0], c[1], c[2]); }

	void deckSegment(MeshBuf& A, const REdge& e, const Side& p, const Side& q) {
		const double col[3] = { 0.46, 0.45, 0.43 };
		colorOf(A, col);
		const double yb0l = p.yl - DECK_H, yb0r = p.yr - DECK_H, yb1l = q.yl - DECK_H, yb1r = q.yr - DECK_H;
		const double ln[3] = { p.r.tz, 0, -p.r.tx };
		uint32_t a = A.V(p.lx, p.yl, p.lz, ln[0], 0, ln[2]), b = A.V(q.lx, q.yl, q.lz, ln[0], 0, ln[2]);
		uint32_t c = A.V(q.lx, yb1l, q.lz, ln[0], 0, ln[2]), d = A.V(p.lx, yb0l, p.lz, ln[0], 0, ln[2]);
		A.QuadAuto(a, d, c, b);
		const double rn[3] = { -p.r.tz, 0, p.r.tx };
		a = A.V(p.Rx, p.yr, p.Rz, rn[0], 0, rn[2]); b = A.V(q.Rx, q.yr, q.Rz, rn[0], 0, rn[2]);
		c = A.V(q.Rx, yb1r, q.Rz, rn[0], 0, rn[2]); d = A.V(p.Rx, yb0r, p.Rz, rn[0], 0, rn[2]);
		A.QuadAuto(a, b, c, d);
		const double uc[3] = { 0.4, 0.4, 0.38 };
		colorOf(A, uc);
		a = A.V(p.lx, yb0l, p.lz, 0, -1, 0); b = A.V(p.Rx, yb0r, p.Rz, 0, -1, 0);
		c = A.V(q.Rx, yb1r, q.Rz, 0, -1, 0); d = A.V(q.lx, yb1l, q.lz, 0, -1, 0);
		A.QuadAuto(a, b, c, d);
		// low solid ramps in the city: fill the gap under a low deck down to the ground with the side walls
		const double ga = ground(p.r.x, p.r.z), gb = ground(q.r.x, q.r.z);
		if (p.r.y - DECK_H - ga < 2.4 || q.r.y - DECK_H - gb < 2.4) {
			const double fl[3] = { 0.43, 0.42, 0.4 };
			colorOf(A, fl);
			for (int side : { -1, 1 }) {
				const double px = side < 0 ? p.lx : p.Rx, pz = side < 0 ? p.lz : p.Rz, qx = side < 0 ? q.lx : q.Rx, qz = side < 0 ? q.lz : q.Rz;
				const double* nn = side < 0 ? ln : rn;
				const double y0 = side < 0 ? yb0l : yb0r, y1 = side < 0 ? yb1l : yb1r;
				const double g0 = ground(px, pz) - 0.2, g1 = ground(qx, qz) - 0.2;
				const uint32_t i0 = A.V(px, y0, pz, nn[0], 0, nn[2]), i1 = A.V(qx, y1, qz, nn[0], 0, nn[2]), i2 = A.V(qx, g1, qz, nn[0], 0, nn[2]), i3 = A.V(px, g0, pz, nn[0], 0, nn[2]);
				A.QuadAuto(i0, i1, i2, i3);
			}
		}
		(void)e;
	}

	bool sags(const Side& p, const Side& q) const {
		for (int k = 0; k <= 4; k++) {
			const double t = k / 4.0;
			const double L[3][6] = { { p.lx, p.lz, p.yl, q.lx, q.lz, q.yl }, { p.r.x, p.r.z, p.r.y, q.r.x, q.r.z, q.r.y }, { p.Rx, p.Rz, p.yr, q.Rx, q.Rz, q.yr } };
			for (const auto& l : L) if (ground(l[0] + (l[3] - l[0]) * t, l[1] + (l[4] - l[1]) * t) < l[2] + (l[5] - l[2]) * t - 0.2) return true;
		}
		return false;
	}

	void deckEnd(MeshBuf& A, const REdge& e, const Side& p, const Side& q) {
		const double col[3] = { 0.45, 0.44, 0.42 };
		colorOf(A, col);
		for (int side : { -1, 1 }) {
			const double px = side < 0 ? p.lx : p.Rx, pz = side < 0 ? p.lz : p.Rz, qx = side < 0 ? q.lx : q.Rx, qz = side < 0 ? q.lz : q.Rz;
			const double y0 = side < 0 ? p.yl : p.yr, y1 = side < 0 ? q.yl : q.yr;
			const double g0 = ground(px, pz) - 0.3, g1 = ground(qx, qz) - 0.3;
			if (g0 > y0 - 0.3 && g1 > y1 - 0.3) continue;
			const double nx = side * -p.r.tz, nz = side * p.r.tx;
			const uint32_t a = A.V(px, y0, pz, nx, 0, nz), b = A.V(qx, y1, qz, nx, 0, nz);
			const uint32_t c = A.V(qx, Min(g1, y1 - 0.2), qz, nx, 0, nz), d = A.V(px, Min(g0, y0 - 0.2), pz, nx, 0, nz);
			if (side < 0) A.QuadAuto(a, d, c, b); else A.QuadAuto(a, b, c, d);
		}
		(void)e;
	}

	void barrier(MeshBuf& A, double x0, double y0, double z0, double x1, double y1, double z1, int side) {
		const double col[3] = { 0.52, 0.51, 0.49 };
		colorOf(A, col);
		const double dx = x1 - x0, dz = z1 - z0; double l = Hypot(dx, dz); if (l == 0) l = 1;
		const double rx = -dz / l * side, rz = dx / l * side;
		const double inset = 0.35;
		const double pts[4][2] = { { -0.3, 0 }, { -0.1, 0.85 }, { 0.1, 0.85 }, { 0.3, 0 } };
		const double NRM[3][2] = { { -0.97, 0.23 }, { 0, 1 }, { 0.97, 0.23 } };
		auto P = [&](double x, double y, double z, double o, double h, double res[3]) { res[0] = x - rx * (inset - o); res[1] = y + h; res[2] = z - rz * (inset - o); };
		for (int k = 0; k < 3; k++) {
			double a0[3], b0[3], a1[3], b1[3];
			P(x0, y0, z0, pts[k][0], pts[k][1], a0); P(x0, y0, z0, pts[k + 1][0], pts[k + 1][1], b0);
			P(x1, y1, z1, pts[k][0], pts[k][1], a1); P(x1, y1, z1, pts[k + 1][0], pts[k + 1][1], b1);
			const double Nx = rx * NRM[k][0], Ny = NRM[k][1], Nz = rz * NRM[k][0];
			const uint32_t i0 = A.V(a0[0], a0[1], a0[2], Nx, Ny, Nz), i1 = A.V(b0[0], b0[1], b0[2], Nx, Ny, Nz);
			const uint32_t i2 = A.V(b1[0], b1[1], b1[2], Nx, Ny, Nz), i3 = A.V(a1[0], a1[1], a1[2], Nx, Ny, Nz);
			A.QuadAuto(i0, i1, i2, i3);
		}
	}

	static void box(MeshBuf& A, double cx, double cz, double hx, double hz, double yaw, double y0, double y1, const double col[3], bool top = false) {
		A.Color(col[0], col[1], col[2]);
		const double s = std::sin(yaw), c = std::cos(yaw);
		auto P = [&](double lx, double lz) { return V2{ cx + lx * c + lz * s, cz - lx * s + lz * c }; };
		const V2 cs[4] = { P(-hx, -hz), P(hx, -hz), P(hx, hz), P(-hx, hz) };
		for (int k = 0; k < 4; k++) {
			const V2 p = cs[k], q = cs[(k + 1) % 4];
			double nx = (p.x + q.x) / 2 - cx, nz = (p.z + q.z) / 2 - cz;
			double l = Hypot(nx, nz); if (l == 0) l = 1; nx /= l; nz /= l;
			const uint32_t a = A.V(p.x, y0, p.z, nx, 0, nz), b = A.V(q.x, y0, q.z, nx, 0, nz), cc = A.V(q.x, y1, q.z, nx, 0, nz), d = A.V(p.x, y1, p.z, nx, 0, nz);
			A.QuadAuto(a, b, cc, d);
		}
		if (top) {
			uint32_t ids[4], idb[4];
			for (int k = 0; k < 4; k++) ids[k] = A.V(cs[k].x, y1, cs[k].z, 0, 1, 0);
			A.QuadAuto(ids[0], ids[3], ids[2], ids[1]);
			for (int k = 0; k < 4; k++) idb[k] = A.V(cs[k].x, y0, cs[k].z, 0, -1, 0);
			A.QuadAuto(idb[0], idb[1], idb[2], idb[3]);
		}
	}

	void pillar(MeshBuf& A, const REdge& e, const Row& r, double g) {
		const double col[3] = { 0.44, 0.43, 0.41 };
		const double top = r.y - DECK_H;
		const double w = Max(e.wL, e.wR);
		const double rx = -r.tz, rz = r.tx;
		std::vector<double> cols; if (w > 5) { cols.push_back(-w * 0.45); cols.push_back(w * 0.45); } else cols.push_back(0);
		for (double o : cols) box(A, r.x + rx * o, r.z + rz * o, 0.75, 0.75, std::atan2(r.tx, r.tz), g - 1, top - 0.6, col);
		box(A, r.x, r.z, 0.7, w * 0.9, std::atan2(r.tx, r.tz), top - 0.7, top, col, true);
	}

	static std::vector<V2> ConvexHull(std::vector<V2> pts) {
		std::stable_sort(pts.begin(), pts.end(), [](const V2& a, const V2& b) { const double d = a.x - b.x; return d != 0 ? d < 0 : a.z - b.z < 0; });
		auto cross = [](const V2& o, const V2& a, const V2& b) { return (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x); };
		std::vector<V2> lower, upper;
		for (const V2& p : pts) { while (lower.size() >= 2 && cross(lower[lower.size() - 2], lower.back(), p) <= 0) lower.pop_back(); lower.push_back(p); }
		for (int i = (int)pts.size() - 1; i >= 0; i--) { const V2& p = pts[i]; while (upper.size() >= 2 && cross(upper[upper.size() - 2], upper.back(), p) <= 0) upper.pop_back(); upper.push_back(p); }
		upper.pop_back(); lower.pop_back();
		lower.insert(lower.end(), upper.begin(), upper.end());
		return lower;
	}

	void junction(const RNode& n) {
		std::vector<V2> pts;
		for (int eid : n.e) {
			const REdge& e = net.edges[eid];
			if (e.removed) continue;
			const bool atA = e.a == n.id;
			const double s = atA ? Min(n.r, e.len) : Max(0, e.len - n.r);
			const EdgePoint q = net.At(e, s);
			const double rx = -q.tz, rz = q.tx;
			pts.push_back({ q.x - rx * e.wL, q.z - rz * e.wL }); pts.push_back({ q.x + rx * e.wR, q.z + rz * e.wR });
		}
		if (pts.size() < 3) return;
		const std::vector<V2> hull = ConvexHull(pts);
		const double y = n.y + 0.035;
		MeshBuf& A = chunk(n.x, n.z).road;
		A.Set(1, 60, 60); A.Set(2, MARK_JUNCTION, 0); A.Set(3, 0, 0);
		const uint32_t c = A.V(n.x, y, n.z, 0, 1, 0, 0, 0);
		std::vector<uint32_t> ids;
		for (const V2& p : hull) ids.push_back(A.V(p.x, y, p.z, 0, 1, 0, p.x - n.x, p.z - n.z));
		for (size_t k = 0; k < ids.size(); k++) A.Tri(c, ids[(k + 1) % ids.size()], ids[k]);
	}

	// ---- San Aurelio's raised pavements
	double pvStart(const REdge& e, bool atA) const {
		const RNode& n = net.nodes[atA ? e.a : e.b];
		return n.kind == ENode::RB ? n.rbR + 5.4 + e.walk : n.kind == ENode::X && n.e.size() > 1 ? n.r : 0;
	}
	struct PvPt { double ix, iz, ox, oz, y, rx, rz; };
	void pvQuad(MeshBuf& A, const PvPt& a, const PvPt& b, double TOP) {
		const double col[3] = { 0.64, 0.63, 0.6 }, kerb[3] = { 0.78, 0.77, 0.74 };
		colorOf(A, col);
		A.QuadAuto(A.V(a.ix, a.y + TOP, a.iz, 0, 1, 0), A.V(a.ox, a.y + TOP, a.oz, 0, 1, 0), A.V(b.ox, b.y + TOP, b.oz, 0, 1, 0), A.V(b.ix, b.y + TOP, b.iz, 0, 1, 0));
		const double nx = -a.rx, nz = -a.rz;
		colorOf(A, kerb);
		A.QuadAuto(A.V(a.ix, a.y - 0.03, a.iz, nx, 0, nz), A.V(b.ix, b.y - 0.03, b.iz, nx, 0, nz), A.V(b.ix, b.y + TOP, b.iz, nx, 0, nz), A.V(a.ix, a.y + TOP, a.iz, nx, 0, nz));
		colorOf(A, col);
		A.QuadAuto(A.V(a.ox, a.y - 0.5, a.oz, -nx, 0, -nz), A.V(a.ox, a.y + TOP, a.oz, -nx, 0, -nz), A.V(b.ox, b.y + TOP, b.oz, -nx, 0, -nz), A.V(b.ox, b.y - 0.5, b.oz, -nx, 0, -nz));
	}
	void pavements(const REdge& e) {
		const double W = e.walk, TOP = 0.12;
		const double s0 = pvStart(e, true), s1 = e.len - pvStart(e, false);
		if (s1 - s0 < 0.5) return;
		std::vector<double> st = { s0 };
		for (int i = 1; i < e.n - 1; i++) if (e.cum[i] > s0 + 0.3 && e.cum[i] < s1 - 0.3) st.push_back(e.cum[i]);
		st.push_back(s1);
		struct R { double s, x, y, z, tx, tz; };
		std::vector<R> rows;
		for (double s : st) { const EdgePoint q = net.At(e, s); rows.push_back({ s, q.x, q.y + 0.035, q.z, q.tx, q.tz }); }
		for (size_t k = 1; k + 1 < rows.size(); k++) { const R &a = rows[k - 1], &b = rows[k + 1]; const double dx = b.x - a.x, dz = b.z - a.z; double l = Hypot(dx, dz); if (l == 0) l = 1; rows[k].tx = dx / l; rows[k].tz = dz / l; }
		for (int side : { -1, 1 }) {
			const double w = side < 0 ? e.wL : e.wR;
			std::vector<PvPt> pts;
			for (const R& r : rows) { const double rx = -r.tz * side, rz = r.tx * side; pts.push_back({ r.x + rx * w, r.z + rz * w, r.x + rx * (w + W), r.z + rz * (w + W), r.y, rx, rz }); }
			for (size_t k = 1; k < pts.size(); k++) pvQuad(chunk((rows[k - 1].x + rows[k].x) / 2, (rows[k - 1].z + rows[k].z) / 2).conc, pts[k - 1], pts[k], TOP);
		}
	}
	void pavementCorners(const RNode& n) {
		const double TOP = 0.12, y = n.y + 0.035;
		struct Arm { double a, dir; PvPt p, m; double W, w, x, z, dx, dz; };
		std::vector<Arm> arms;
		for (int eid : n.e) {
			const REdge& e = net.edges[eid];
			if (e.removed || e.walk <= 0) continue;
			const bool atA = e.a == n.id;
			const double s = pvStart(e, atA);
			const EdgePoint q = net.At(e, atA ? Min(s, e.len) : Max(0, e.len - s));
			double dx = q.tx, dz = q.tz;
			if (!atA) { dx = -dx; dz = -dz; }
			const double px = -dz, pz = dx;
			const double w = Max(e.wL, e.wR), W = e.walk;
			auto mk = [&](double sg) { return PvPt{ q.x + px * w * sg, q.z + pz * w * sg, q.x + px * (w + W) * sg, q.z + pz * (w + W) * sg, 0, 0, 0 }; };
			arms.push_back({ std::atan2(q.z - n.z, q.x - n.x), std::atan2(dz, dx), mk(1), mk(-1), W, w, q.x, q.z, dx, dz });
		}
		if (arms.empty()) return;
		std::stable_sort(arms.begin(), arms.end(), [](const Arm& p, const Arm& q) { return p.dir - q.dir < 0; });
		MeshBuf& A = chunk(n.x, n.z).conc;
		if (n.kind == ENode::RB) { pavementRing(A, n, arms.size(), [&](size_t k) { return std::make_pair(arms[k].dir, arms[k].w); }, [&]() { double W = 0; for (const Arm& a : arms) W = Max(W, a.W); return W; }(), y, TOP); return; }
		if (arms.size() < 2) return;
		const double col[3] = { 0.64, 0.63, 0.6 }, kerb[3] = { 0.78, 0.77, 0.74 };
		for (size_t k = 0; k < arms.size(); k++) {
			const Arm &a = arms[k], &b = arms[(k + 1) % arms.size()];
			auto ang = [&](const PvPt& q) { return std::atan2(q.iz - n.z, q.ix - n.x); };
			auto wrapD = [](double x) { while (x < -kPi) x += kTau; while (x > kPi) x -= kTau; return x; };
			const PvPt sa = wrapD(ang(a.p) - a.dir) > 0 ? a.p : a.m, sb = wrapD(ang(b.p) - b.dir) < 0 ? b.p : b.m;
			double gap = b.dir - a.dir; if (k == arms.size() - 1) gap += kTau;
			if (arms.size() == 1 || gap < 0.05) continue;
			const double mid = a.dir + gap / 2, R = Hypot(sa.ox - n.x, sa.oz - n.z);
			const double f = gap > kPi * 0.9 ? 0.3 : 1;
			const double cx = n.x + std::cos(mid) * R * f, cz = n.z + std::sin(mid) * R * f;
			colorOf(A, col);
			const uint32_t t0 = A.V(sa.ix, y + TOP, sa.iz, 0, 1, 0), t1 = A.V(sa.ox, y + TOP, sa.oz, 0, 1, 0), t2 = A.V(cx, y + TOP, cz, 0, 1, 0);
			const uint32_t t3 = A.V(sb.ox, y + TOP, sb.oz, 0, 1, 0), t4 = A.V(sb.ix, y + TOP, sb.iz, 0, 1, 0);
			if (gap < kPi * 0.9) { A.QuadAuto(t0, t1, t2, t4); A.QuadAuto(t4, t2, t3, t3); }
			else A.QuadAuto(t0, t1, t3, t4);
			double nx = n.x - (sa.ix + sb.ix) / 2, nz = n.z - (sa.iz + sb.iz) / 2; double l = Hypot(nx, nz); if (l == 0) l = 1; nx /= l; nz /= l;
			colorOf(A, kerb);
			A.QuadAuto(A.V(sa.ix, y - 0.03, sa.iz, nx, 0, nz), A.V(sb.ix, y - 0.03, sb.iz, nx, 0, nz), A.V(sb.ix, y + TOP, sb.iz, nx, 0, nz), A.V(sa.ix, y + TOP, sa.iz, nx, 0, nz));
		}
	}
	template <typename F>
	void pavementRing(MeshBuf& A, const RNode& n, size_t nArms, F arm, double W, double y, double TOP) {
		const double R0 = n.rbR + 5.4, R1 = R0 + W;
		std::vector<std::array<double, 2>> gaps;
		for (size_t k = 0; k < nArms; k++) { const auto aw = arm(k); const double h = std::asin(Min(0.99, aw.second / R0)) + 0.02; gaps.push_back({ aw.first - h, aw.first + h }); }
		for (size_t k = 0; k < gaps.size(); k++) {
			const double a0 = gaps[k][1]; double a1 = gaps[(k + 1) % gaps.size()][0]; if (k == gaps.size() - 1) a1 += kTau;
			if (a1 - a0 < 0.02) continue;
			const int seg = (int)Max(2, std::ceil((a1 - a0) * R1 / 3));
			bool have = false; PvPt prev{};
			for (int q = 0; q <= seg; q++) {
				const double t = a0 + (a1 - a0) * q / seg, c = std::cos(t), s = std::sin(t);
				const PvPt cur{ n.x + c * R0, n.z + s * R0, n.x + c * R1, n.z + s * R1, y, c, s };
				if (have) pvQuad(A, prev, cur, TOP);
				prev = cur; have = true;
			}
		}
	}

	void roundabout(const RNode& n) {
		MeshBuf& A = chunk(n.x, n.z).road;
		const double R = n.rbR, rin = R - 5.2, rout = R + 5.4;
		const double y = n.y + 0.035;
		const int seg = 48;
		A.Set(1, 60, 60); A.Set(2, MARK_JUNCTION, 0); A.Set(3, 0, 0);
		std::vector<std::array<uint32_t, 2>> ring;
		for (int k = 0; k <= seg; k++) {
			const double t = (double)k / seg * kTau, cx = std::cos(t), cz = std::sin(t);
			ring.push_back({ A.V(n.x + cx * rin, y, n.z + cz * rin, 0, 1, 0), A.V(n.x + cx * rout, y, n.z + cz * rout, 0, 1, 0) });
		}
		for (int k = 0; k < seg; k++) A.QuadAuto(ring[k][0], ring[k + 1][0], ring[k + 1][1], ring[k][1]);
		island(n.x, n.z, y, rin);
	}

	void island(double nx, double nz, double y, double rin) {
		const int seg = 48;
		MeshBuf& C = chunk(nx, nz).conc;
		const double curbCol[3] = { 0.72, 0.71, 0.68 }, grass[3] = { 0.2, 0.34, 0.12 };
		const double top = y + 0.28;
		colorOf(C, grass);
		const uint32_t cc = C.V(nx, top, nz, 0, 1, 0);
		std::vector<std::array<uint32_t, 3>> rim;
		for (int k = 0; k <= seg; k++) {
			const double t = (double)k / seg * kTau, cx = std::cos(t), cz = std::sin(t);
			colorOf(C, grass);
			const uint32_t a = C.V(nx + cx * (rin - 0.3), top, nz + cz * (rin - 0.3), 0, 1, 0);
			colorOf(C, curbCol);
			const uint32_t b = C.V(nx + cx * rin, top, nz + cz * rin, cx, 0.3, cz), c = C.V(nx + cx * rin, y - 0.1, nz + cz * rin, cx, 0, cz);
			rim.push_back({ a, b, c });
		}
		for (int k = 0; k < seg; k++) {
			C.Tri(cc, rim[k + 1][0], rim[k][0]);
			C.QuadAuto(rim[k][1], rim[k + 1][1], rim[k + 1][2], rim[k][2]);
		}
	}

	void railway(const RailInfo& rail) {
		const double L = rail.length, Gg = 0.7175;
		auto inCross = [&](double s) { for (const RailCrossing& c : rail.crossings) if (c.kind == "level" && std::fabs(s - c.s) < c.halfW - 1) return true; return false; };
		for (double s = 0.4; s < L; s += 0.68) {
			if (inCross(s)) continue;
			const RailPoint t = RailAt(rail, s);
			out.sleepers.push_back({ t.x, t.y + 0.12, t.z, std::atan2(t.tx, t.tz) });
		}
		const RailLoop& loop = rail.loop;
		if (loop.edge >= 0) for (double s = loop.s0 + 0.2; s < loop.s1; s += 0.68) {
			if (std::fabs(LoopOffsetAt(loop, s)) < 2.7) continue;
			const RailPoint t = RailAtTrack(rail, s, 1);
			out.sleepers.push_back({ t.x, t.y + 0.12, t.z, std::atan2(t.tx, t.tz) });
		}
		auto tan = [](const Line& pts, int i) {
			const P3& a = pts[(size_t)(std::max)(0, i - 1)]; const P3& b = pts[(size_t)(std::min)((int)pts.size() - 1, i + 1)];
			const double dx = b.x - a.x, dz = b.z - a.z; double l = Hypot(dx, dz); if (l == 0) l = 1;
			return V2{ dx / l, dz / l };
		};
		auto drawRails = [&](const Line& pts) {
			for (size_t i = 0; i + 1 < pts.size(); i++) {
				const P3 &a = pts[i], &b = pts[i + 1];
				MeshBuf& A = chunk(a.x, a.z).rails;
				const V2 ta = tan(pts, (int)i), tb = tan(pts, (int)i + 1);
				for (int side : { -1, 1 }) {
					const double ax = a.x - ta.z * Gg * side, az = a.z + ta.x * Gg * side, bx = b.x - tb.z * Gg * side, bz = b.z + tb.x * Gg * side;
					const double y1a = a.y + 0.36, y1b = b.y + 0.36, y0a = a.y + 0.2, y0b = b.y + 0.2, w = 0.036;
					const double nx = -ta.z, nz = ta.x;
					A.Color(0.82, 0.8, 0.78);
					uint32_t i0 = A.V(ax - nx * w, y1a, az - nz * w, 0, 1, 0), i1 = A.V(ax + nx * w, y1a, az + nz * w, 0, 1, 0);
					uint32_t i2 = A.V(bx + nx * w, y1b, bz + nz * w, 0, 1, 0), i3 = A.V(bx - nx * w, y1b, bz - nz * w, 0, 1, 0);
					A.QuadAuto(i0, i1, i2, i3);
					A.Color(0.55, 0.52, 0.5);
					for (int sd : { -1, 1 }) {
						const double ox = nx * w * sd, oz = nz * w * sd;
						i0 = A.V(ax + ox, y0a, az + oz, nx * sd, 0, nz * sd); i1 = A.V(bx + ox, y0b, bz + oz, nx * sd, 0, nz * sd);
						i2 = A.V(bx + ox, y1b, bz + oz, nx * sd, 0, nz * sd); i3 = A.V(ax + ox, y1a, az + oz, nx * sd, 0, nz * sd);
						A.QuadAuto(i0, i1, i2, i3);
					}
				}
			}
		};
		drawRails(rail.pts);
		if (loop.edge >= 0) {
			Line lp;
			for (double s = loop.s0; s <= loop.s1 + 0.01; s += 2) { const RailPoint t = RailAtTrack(rail, Min(s, loop.s1), 1); P3 p; p.x = t.x; p.z = t.z; p.y = t.y; lp.push_back(p); }
			drawRails(lp);
		}
		for (const RailStation& st : rail.stations) {
			const double rx = -st.tz, rz = st.tx, yaw = st.yaw;
			const double cx = st.x + rx * 4.25, cz = st.z + rz * 4.25;
			MeshBuf& A = chunk(cx, cz).conc;
			const double top = st.y + 1.05;
			const double pc[3] = { 0.58, 0.57, 0.54 }, yl[3] = { 0.85, 0.7, 0.12 }, post[3] = { 0.3, 0.32, 0.34 }, roof[3] = { 0.42, 0.44, 0.46 };
			box(A, cx, cz, 2.5, 56, yaw, st.y - 0.6, top, pc, true);
			box(A, st.x + rx * 2.1, st.z + rz * 2.1, 0.14, 56, yaw, top - 0.004, top + 0.004, yl, true);
			const double posts[4][2] = { { -1.6, -9 }, { 1.6, -9 }, { -1.6, 9 }, { 1.6, 9 } };
			for (const auto& q : posts) {
				const double px = cx + q[0] * std::cos(yaw) + q[1] * std::sin(yaw), pz = cz - q[0] * std::sin(yaw) + q[1] * std::cos(yaw);
				box(A, px, pz, 0.09, 0.09, yaw, top, top + 3.1, post);
			}
			box(A, cx, cz, 2.3, 10.5, yaw, top + 3.1, top + 3.3, roof, true);
		}
	}

	void build() {
		for (const REdge& e : net.edges) if (!e.removed && e.render && !e.hasGrid) edge(e);
		for (const RNode& n : net.nodes) if (!n.dead && !n.e.empty() && !n.hasGrid && !n.city) {
			if (n.kind == ENode::X && n.e.size() > 1) junction(n);
			else if (n.kind == ENode::RB) roundabout(n);
			if (n.ncity) pavementCorners(n);
		}
		for (const RNode& n : net.nodes) if (n.hasGrid && n.kind == ENode::RB) island(n.x, n.z, 0.03, 5.6);
		if (map.roadInfo.hasRail) railway(map.roadInfo.rail);
	}
};
} // namespace

RoadMeshes BuildRoadMeshes(const CityMap& map) {
	Builder b(map);
	b.build();
	return std::move(b.out);
}

} // namespace atg
