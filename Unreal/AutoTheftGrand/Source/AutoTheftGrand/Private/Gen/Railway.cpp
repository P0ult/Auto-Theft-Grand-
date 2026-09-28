// The Sol Line (port of src/world/railway.js): a railway from Union Station (just west of Los Soles) past
// Fern Creek to Dry Wells, running parallel to the Sol Freeway about 150 m to its south / west. It is built
// as rail edges in the road network so terrain shaping, bridges and pillars come for free; road crossings
// are pinned to the road's height (level crossings) or passed under / over where the road is on a bridge.
#include "RoadLayout.h"

namespace atg {

static V2 TangentXZ(const Line& pts, const std::vector<double>& cum, double s) {
	const P3 a = PointAt(pts, cum, Max(0, s - 3)), b = PointAt(pts, cum, Min(cum.back(), s + 3));
	const double dx = b.x - a.x, dz = b.z - a.z;
	double l = Hypot(dx, dz); if (l == 0) l = 1;
	return { dx / l, dz / l };
}
static double InterpY(const std::vector<double>& cum, const std::vector<float>& y, double s) {
	const int i = NearestIdx(cum, s);
	const int j = cum[i] > s ? (std::max)(0, i - 1) : (std::min)((int)cum.size() - 1, i + 1);
	if (i == j) return y[i];
	const double t = (s - cum[i]) / (cum[j] - cum[i]);
	return y[i] + ((double)y[j] - y[i]) * t;
}

double LoopOffsetAt(const RailLoop& loop, double s) {
	if (loop.edge < 0 || s <= loop.s0 || s >= loop.s1) return 0;
	return loop.off * Smooth(loop.s0, loop.s0 + loop.taper, s) * (1 - Smooth(loop.s1 - loop.taper, loop.s1, s));
}

RailPoint RailAt(const RailInfo& rail, double s) {
	const Line& pts = rail.pts; const std::vector<double>& cum = rail.cum;
	s = Clamp(s, 0, cum.back());
	int lo = 0, hi = (int)cum.size() - 1;
	while (hi - lo > 1) { const int m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
	double den = cum[hi] - cum[lo]; if (den == 0) den = 1;
	const double t = (s - cum[lo]) / den;
	const P3 &a = pts[lo], &b = pts[hi];
	const double dx = b.x - a.x, dz = b.z - a.z;
	double l = Hypot(dx, dz); if (l == 0) l = 1;
	return { a.x + dx * t, a.y + (b.y - a.y) * t, a.z + dz * t, dx / l, dz / l, (b.y - a.y) / l };
}

RailPoint RailAtTrack(const RailInfo& rail, double s, int track) {
	RailPoint o = RailAt(rail, s);
	if (!track || rail.loop.edge < 0) return o;
	const double off = LoopOffsetAt(rail.loop, s);
	const double d = LoopOffsetAt(rail.loop, s + 1) - off;
	if (off == 0 && d == 0) return o;
	const double tx = o.tx, tz = o.tz;
	o.x += -tz * off; o.z += tx * off;
	const double nx = tx - tz * d, nz = tz + tx * d;
	double l = Hypot(nx, nz); if (l == 0) l = 1;
	o.tx = nx / l; o.tz = nz / l;
	return o;
}

RailInfo& BuildRailway(RoadNet& net, RoadInfo& info, const Line& fwC, const std::vector<double>& fwCum, const std::function<double(double, double)>& terrain, Heightfield& hf) {
	const Line off = OffsetLine(fwC, RAIL.offset);
	double s0 = 335, s1 = fwCum.back();
	for (size_t i = 0; i < off.size(); i++) if (off[i].x > -965) { s1 = fwCum[i]; break; }
	Line raw;
	for (size_t i = 0; i < off.size(); i++) if (fwCum[i] >= s0 && fwCum[i] <= s1) { P3 p; p.x = off[i].x; p.z = off[i].z; raw.push_back(p); }
	const Line pts2 = Resample(raw, 6);
	const std::vector<double> cum = CumLen(pts2);
	const double L = cum.back();

	// ---- road crossings
	std::vector<RailCrossing> crossings;
	for (const REdge& e : net.edges) {
		if (e.removed || e.hasGrid || e.type == ERoad::Rail) continue;
		Line ep;
		for (int i = 0; i < e.n; i++) { P3 p; p.x = e.X(i); p.z = e.Z(i); p.y = e.Y(i); ep.push_back(p); }
		const std::vector<double> ec = CumLen(ep);
		const auto hit = Intersect(pts2, cum, ep, ec);
		if (!hit) continue;
		const double ry = PointAt(ep, ec, hit->sb).y;
		const double g = terrain(hit->x, hit->z);
		std::string kind = "level"; double y = ry;
		if (ry - g > 5 || e.T->cls >= 2) { kind = "under"; y = ry - 7.4; }
		if (kind == "under" && y < g - 22) { kind = "over"; y = ry + 7.6; }
		const V2 ta = TangentXZ(pts2, cum, hit->sa), tb = TangentXZ(ep, ec, hit->sb);
		const double ang = std::fabs(std::atan2(ta.x, ta.z) - std::atan2(tb.x, tb.z));
		const double skew = Max(0.35, std::fabs(std::sin(ang)));
		crossings.push_back({ hit->sa, hit->x, hit->z, y, ry, kind, e.id, (Max(e.wL, e.wR) + 1.5) / skew, e.name });
	}
	std::stable_sort(crossings.begin(), crossings.end(), [](const RailCrossing& a, const RailCrossing& b) { return a.s - b.s < 0; });

	// ---- stations
	const RailCrossing* fernX = nullptr;
	for (const RailCrossing& c : crossings) if (c.name == "Main Street" && c.kind == "level" && c.x < -2300 && c.x > -2700) { fernX = &c; break; }
	std::vector<RailStation> stations(3);
	stations[0].key = "dry"; stations[0].name = "Dry Wells"; stations[0].s = 110;
	stations[1].key = "fern"; stations[1].name = "Fern Creek"; stations[1].s = fernX ? fernX->s - 150 : L * 0.45;
	stations[2].key = "union"; stations[2].name = "Union Station"; stations[2].s = L - 80;

	// ---- profile
	ProfileOpts po; po.window = 380; po.maxGrade = RAIL.grade; po.maxCut = 26;
	for (const RailCrossing& c : crossings) po.pins.push_back({ c.s, c.y, 60 });
	Profile prof = SolveProfile(pts2, terrain, po);
	std::vector<float>& y = prof.y;
	for (RailStation& st : stations) {
		const int i0 = NearestIdx(cum, st.s);
		st.y = y[i0];
		for (size_t i = 0; i < cum.size(); i++) {
			const double d = std::fabs(cum[i] - st.s);
			if (d < 70) y[i] = (float)st.y;
			else if (d < 190) y[i] = (float)Lerp(st.y, y[i], Smooth(70, 190, d));
		}
	}
	for (const RailCrossing& c : crossings) { const int i = NearestIdx(cum, c.s); y[i] = (float)c.y; }
	std::vector<uint8_t> fixed(cum.size(), 0);
	for (const RailCrossing& c : crossings) fixed[NearestIdx(cum, c.s)] = 1;
	for (const RailStation& st : stations) for (size_t i = 0; i < cum.size(); i++) if (std::fabs(cum[i] - st.s) < 65) fixed[i] = 1;
	const double G = RAIL.grade;
	const int N = (int)cum.size();
	for (int it = 0; it < 6; it++) {
		for (int i = 1; i < N; i++) if (!fixed[i]) { const double ds = cum[i] - cum[i - 1]; y[i] = (float)Clamp(y[i], y[i - 1] - G * ds, y[i - 1] + G * ds); }
		for (int i = N - 2; i >= 0; i--) if (!fixed[i]) { const double ds = cum[i + 1] - cum[i]; y[i] = (float)Clamp(y[i], y[i + 1] - G * ds, y[i + 1] + G * ds); }
	}
	for (int it = 0; it < 40; it++) {
		double worst = 0;
		for (int i = 1; i < N; i++) {
			const double ds = cum[i] - cum[i - 1], dy = (double)y[i] - y[i - 1];
			if (std::fabs(dy) > G * ds * 1.6) {
				worst = Max(worst, std::fabs(dy));
				const double m = ((double)y[i] + y[i - 1]) / 2;
				y[i - 1] = (float)Lerp(y[i - 1], m, 0.5); y[i] = (float)Lerp(y[i], m, 0.5);
			}
		}
		if (worst == 0) break;
	}
	Line pts;
	for (int i = 0; i < N; i++) { P3 p; p.x = pts2[i].x; p.z = pts2[i].z; p.y = y[i]; pts.push_back(p); }

	for (RailStation& st : stations) {
		const int i = NearestIdx(cum, st.s);
		const V2 t = TangentXZ(pts2, cum, st.s);
		st.x = pts2[i].x; st.z = pts2[i].z; st.tx = t.x; st.tz = t.z;
		st.yaw = std::atan2(t.x, t.z);
		Pad pd; pd.x = st.x - t.z * 12; pd.z = st.z + t.x * 12; pd.r = 42; pd.y = st.y - 0.07; pd.blend = 30;
		hf.PadIt(pd);
	}

	// ---- graph edges (split at the middle station)
	RailInfo& rail = info.rail;
	info.hasRail = true;
	const RailStation& mid = stations[1];
	const int cuts[3] = { 0, NearestIdx(cum, mid.s), N - 1 };
	auto railNode = [&](double x, double z, double yy, const char* name) { NodeOpts o; o.kind = ENode::Via; o.r = 0; o.name = name; o.rail = true; return net.AddNode(x, z, yy, o).id; };
	const int nA = railNode(pts[0].x, pts[0].z, pts[0].y, "Sol Line west end");
	const int nM = railNode(mid.x, mid.z, mid.y, (mid.name + " station").c_str());
	const int nB = railNode(pts.back().x, pts.back().z, pts.back().y, "Sol Line east end");
	const int ends[3] = { nA, nM, nB };
	std::vector<int> edges;
	for (int q = 0; q < 2; q++) {
		EdgeOpts eo; eo.name = "Sol Line"; eo.barrierL = 1; eo.barrierR = 1;
		Line seg(pts.begin() + cuts[q], pts.begin() + cuts[q + 1] + 1);
		REdge& e = net.AddEdge(ends[q], ends[q + 1], seg, ERoad::Rail, eo);
		e.rail = true;
		edges.push_back(e.id);
	}
	for (int q = 0; q < 2; q++) {
		REdge& e = net.edges[edges[q]];
		const double sA = cum[cuts[q]], sB = cum[cuts[q + 1]];
		for (const RailCrossing& c : crossings) if (c.kind == "level" && c.s >= sA - 1 && c.s <= sB + 1) e.crossings.push_back({ c.s - sA, c.halfW });
	}
	// ---- passing loop at Fern Creek
	double l0 = mid.s - 240, l1 = mid.s + 125;
	for (const RailCrossing& c : crossings) { if (c.s >= mid.s && c.s - 45 < l1) l1 = c.s - 45; if (c.s < mid.s && c.s + 45 > l0) l0 = c.s + 45; }
	RailLoop loop; loop.s0 = l0; loop.s1 = l1; loop.taper = 55; loop.off = -4.6;
	loop.m0 = loop.s0 + loop.taper; loop.m1 = loop.s1 - loop.taper;
	loop.edge = 0; // (so LoopOffsetAt works while building it)
	Line loopPts;
	for (double sl = l0; sl <= l1 + 0.01; sl += 4) {
		const P3 q = PointAt(pts2, cum, sl); const V2 t = TangentXZ(pts2, cum, sl); const double o = LoopOffsetAt(loop, sl);
		P3 p; p.x = q.x - t.z * o; p.z = q.z + t.x * o; p.y = InterpY(cum, y, sl);
		loopPts.push_back(p);
	}
	const int la = railNode(loopPts[0].x, loopPts[0].z, loopPts[0].y, "Fern Creek loop");
	const int lb = railNode(loopPts.back().x, loopPts.back().z, loopPts.back().y, "Fern Creek loop");
	EdgeOpts lo; lo.name = "Sol Line (Fern Creek loop)"; lo.wL = 4.2; lo.wR = 2.2;
	REdge& le = net.AddEdge(la, lb, loopPts, ERoad::Rail, lo);
	le.rail = true; le.loop = true;
	loop.edge = le.id;

	rail.pts = pts; rail.cum = cum; rail.length = L; rail.stations = stations; rail.crossings = crossings; rail.edges = edges;
	rail.nodes = { nA, nM, nB }; rail.loop = loop;
	return rail;
}

} // namespace atg
