// Port of src/world/roadlayout.js (and the skatepark pads). See RoadLayout.h.
#include "RoadLayout.h"

namespace atg {

const std::vector<double> XS = { -830, -730, -630, -535, -440, -345, -255, -165, -75, 15, 105, 195, 285, 380, 480, 580, 680, 775, 870 };
const std::vector<double> ZS = { -770, -665, -560, -455, -350, -245, -140, -40, 60, 160, 260, 360, 450, 540, 625 };
const std::set<std::string> REMOVED_SEGMENTS = { "h:12,10", "v:7,3", "v:3,0", "v:6,6" };
const std::vector<SuperBlockDef> SUPERBLOCKS = {
	{ "h:12,10", { { 12, 9 }, { 12, 10 } }, "stadium", "Los Soles Coliseum" },
	{ "v:7,3", { { 6, 3 }, { 7, 3 } }, "mall", "Market Street Mall" },
	{ "v:3,0", { { 2, 0 }, { 3, 0 } }, "golf", "Vistawood Country Club" },
	{ "v:6,6", { { 5, 6 }, { 6, 6 } }, "park", "Glen Park" },
};
const std::vector<std::pair<int, int>> CITY_ROUNDABOUTS = { { 3, 2 }, { 15, 3 }, { 6, 11 }, { 11, 12 }, { 16, 5 }, { 13, 9 } };

const std::vector<SkateparkDef> SKATEPARKS = { { "santaluz", "Santa Luz Skatepark", -440, 663, 0, 30, 13, 0.05 } };
std::vector<Pad> SkateparkPads() {
	std::vector<Pad> out;
	for (const SkateparkDef& p : SKATEPARKS) {
		Pad d; d.key = std::string("skate_") + p.key; d.minX = p.x - p.hx - 2; d.maxX = p.x + p.hx + 2; d.minZ = p.z - p.hz - 2; d.maxZ = p.z + p.hz + 2; d.y = p.y; d.blend = 18;
		out.push_back(d);
	}
	return out;
}

TownRoads& RoadInfo::Town(const std::string& key) {
	for (auto& kv : towns) if (kv.first == key) return kv.second;
	towns.push_back({ key, TownRoads() });
	return towns.back().second;
}

const std::vector<RouteDef>& ROUTES() {
	static std::vector<RouteDef> R;
	if (!R.empty()) return R;
	auto T = [](const char* k) -> const Town& { return TownByKey(k); };
	const V2 cityW{ -830, 540 }, cityN{ 15, -770 }, cityE{ 870, -455 };
	const V2 jDw{ -3700, -2480 };
	auto mk = [&](const char* key, const char* type, RouteEnd e0, RouteEnd e1, std::vector<V2> ctrl, double grade = NaN(), double width = NaN()) {
		RouteDef d; d.key = key; d.type = type; d.ends[0] = e0; d.ends[1] = e1; d.ctrl = std::move(ctrl); d.grade = grade; d.width = width;
		R.push_back(d);
	};
	using E = RouteEnd;
	mk("coast", "highway", E::AtY(0), E::Pad("pine"), { { cityW.x - 10, cityW.z }, { -950, 545 }, { -1100, 560 }, { -1250, 520 }, { -1360, 400 }, { -1400, 220 }, { -1380, 60 }, { -1350, -60 }, { -1330, -200 }, { -1300, -450 }, { -1290, -700 }, { -1330, -950 }, { -1420, -1200 }, { -1520, -1450 }, { -1560, -1640 }, { -1380, -1700 }, { T("mirador").x, T("mirador").z }, { -1180, -1980 }, { -1150, -2100 }, { -950, -2250 }, { -650, -2330 }, { T("pine").x - 25, T("pine").z } });
	mk("fernN", "road", E::Pad("fern"), E::None(), { { T("fern").x, T("fern").z - 25 }, { -2525, -400 }, { -2520, -560 }, { -2540, -700 }, { -2600, -900 }, { -2680, -1100 } });
	mk("vista", "road", E::AtY(0), E::Pad("pine"), { { cityN.x, cityN.z - 10 }, { 30, -830 }, { 70, -900 }, { 95, -1000 }, { 40, -1120 }, { -80, -1200 }, { -60, -1320 }, { 80, -1430 }, { 45, -1560 }, { -110, -1700 }, { -200, -1900 }, { -260, -2100 }, { T("pine").x, T("pine").z + 23 } }, 0.1);
	mk("bay", "highway", E::AtY(0), E::Pad("pine"), { { cityE.x + 10, cityE.z }, { 950, -520 }, { 1000, -700 }, { T("hale").x, T("hale").z }, { 990, -1250 }, { 950, -1550 }, { 880, -1850 }, { 720, -2100 }, { 450, -2280 }, { 150, -2345 }, { T("pine").x + 23, T("pine").z } });
	mk("freeway", "freeway", E::Pad("dry"), E::AtY(0), { jDw, { -3665, -2330 }, { -3560, -1900 }, { -3380, -1480 }, { -3120, -1080 }, { -2830, -770 }, { -2520, -560 }, { -2300, -400 }, { -2100, -230 }, { -1860, -150 }, { -1600, -100 }, { -1350, -60 }, { -1180, -25 }, { -1000, 4 }, { -880, 10 }, { -840, 10 }, { -600, 10 }, { -300, 10 }, { -100, 10 }, { 15, 10 } }, 0.04, 30);
	mk("fernS", "road", E::Pad("fern"), E::None(), { { T("fern").x, T("fern").z + 25 }, { -2530, -60 }, { -2560, 150 }, { -2590, 380 }, { -2620, CoastZ(-2620) - 60 } });
	mk("fernW", "road", E::Pad("fern"), E::None(), { { T("fern").x - 25, T("fern").z }, { -2700, -230 }, { -2850, -150 }, { -2955, 40 }, { AIRFIELD.x - 40, AIRFIELD.z - 55 } });
	mk("fernE", "road", E::Pad("fern"), E::None(), { { T("fern").x + 25, T("fern").z }, { -2380, -180 }, { -2230, -60 }, { -2090, 40 }, { -1900, 180 }, { -1700, 240 }, { -1500, 262 }, { -1398, 250 } });
	mk("dryE", "road", E::Pad("dry"), E::None(), { { T("dry").x + 25, T("dry").z }, { -3780, -2480 }, jDw });
	mk("dryE2", "road", E::None(), E::None(), { jDw, { -3550, -2470 }, { -3350, -2500 }, { -3150, -2560 } });
	mk("dryW", "road", E::Pad("dry"), E::None(), { { T("dry").x - 25, T("dry").z }, { -3980, -2470 }, { -4200, -2450 }, { -4500, -2380 }, { -4800, -2300 } });
	mk("costa", "road", E::None(), E::Pad("seco"), { { AIRFIELD.x - 40, AIRFIELD.z - 55 }, { -3150, 320 }, { -3400, 390 }, { -3700, 440 }, { T("seco").x + 22, T("seco").z } });
	mk("seco", "highway", E::Pad("seco"), E::None(), { { T("seco").x, T("seco").z - 22 }, { -4020, 200 }, { -4100, -300 }, { -4180, -900 }, { -4150, -1450 }, { -4080, -1900 }, { -4150, -2250 }, { -4200, -2445 } });
	mk("baseRd", "highway", E::Pad("dry"), E::Pad("base"), { { T("dry").x, T("dry").z - 25 }, { -3875, -2650 }, { -3900, -2900 }, { -3960, -3250 }, { -3930, -3600 }, { -3800, -3860 }, { -3700, -3960 }, { -3720, BASE.gateZ }, { BASE.maxX - 2, BASE.gateZ } });
	for (const RouteDef& d : NcRoutes()) if (d.key != "harborDr") R.push_back(d);
	return R;
}
static const RouteDef& RouteByKey(const char* key) { for (const RouteDef& d : ROUTES()) if (d.key == key) return d; return ROUTES()[0]; }

static double Interp(const std::vector<double>& cum, const std::vector<float>& arr, double s) {
	if (s <= 0) return arr[0];
	if (s >= cum.back()) return arr.back();
	int lo = 0, hi = (int)cum.size() - 1;
	while (hi - lo > 1) { const int m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
	double den = cum[hi] - cum[lo]; if (den == 0) den = 1;
	const double t = (s - cum[lo]) / den;
	return arr[lo] + ((double)arr[hi] - arr[lo]) * t;
}

// A side road's profile is solved on its own: ease its end to the height of the junction it joins.
static void EaseToJunction(REdge& e, bool atStart, double y) {
	const int n = e.n, i0 = atStart ? 0 : n - 1;
	const double dy = y - e.p[i0 * 3 + 1];
	if (std::fabs(dy) < 0.02) return;
	const double D = Min(e.len * 0.9, Max(30, std::fabs(dy) / 0.07));
	for (int i = 0; i < n; i++) {
		const double d = atStart ? e.cum[i] : e.len - e.cum[i];
		if (d >= D) continue;
		const double t = 1 - d / D;
		e.p[i * 3 + 1] = (float)(e.p[i * 3 + 1] + dy * t * t * (3 - 2 * t));
	}
}

namespace {
struct RoadOpts {
	int start = -1, end = -1;
	double spacing = 6, window = NaN(), maxGrade = NaN(), maxCut = NaN(), minY = NaN();
	std::string name;
	struct Split { double x, z; ENode kind = ENode::X; double r = 8, rbR = 0; std::string name; };
	std::vector<Split> splits;
	bool base = false;
};

struct Layout {
	Heightfield& hf;
	RoadNet& net;
	RoadInfo& info;
	Layout(Heightfield& h, RoadNet& n, RoadInfo& i) : hf(h), net(n), info(i) {}

	double terrain(double x, double z) const { return hf.Sample(x, z); }
	double avgTerrain(double x, double z, double r = 14) const {
		double s = 0; int n = 0;
		for (int a = 0; a < 8; a++) { s += terrain(x + std::cos(a * 0.785) * r, z + std::sin(a * 0.785) * r); n++; }
		return (s / n + terrain(x, z)) / 2;
	}
	int mk(double x, double z, NodeOpts o, double y = NaN()) { return net.AddNode(x, z, IsSet(y) ? y : avgTerrain(x, z), o).id; }
	RNode& N(int id) { return net.nodes[id]; }
	REdge& E(int id) { return net.edges[id]; }

	RoadBuilt* road(const std::vector<V2>& ctrl, ERoad type, const RoadOpts& o) {
		Line pts = Catmull(ctrl, o.spacing);
		double run[2] = { 0, 0 };
		auto runIn = [&](int nid, bool atEnd) {
			const RNode& n = N(nid);
			const P3 q = atEnd ? pts.back() : pts.front();
			const double d = Hypot(q.x - n.x, q.z - n.z);
			if (d < 1) return;
			const int k = (int)std::ceil(d / o.spacing);
			Line add;
			for (int i = 0; i < k; i++) { P3 p; p.x = n.x + (q.x - n.x) * i / k; p.z = n.z + (q.z - n.z) * i / k; add.push_back(p); }
			if (atEnd) { std::reverse(add.begin(), add.end()); pts.insert(pts.end(), add.begin(), add.end()); }
			else { add.insert(add.end(), pts.begin(), pts.end()); pts = std::move(add); }
			run[atEnd ? 1 : 0] = d;
		};
		if (o.start >= 0) runIn(o.start, false);
		if (o.end >= 0) runIn(o.end, true);
		const double len = CumLen(pts).back();
		ProfileOpts po;
		if (o.start >= 0) po.pins.push_back({ run[0], N(o.start).y, NaN() });
		if (o.end >= 0) po.pins.push_back({ len - run[1], N(o.end).y, NaN() });
		const double startY = o.start >= 0 ? N(o.start).y : 0, endY = o.end >= 0 ? N(o.end).y : 0;
		const bool hasS = o.start >= 0, hasE = o.end >= 0;
		const double r0 = run[0], r1 = run[1];
		po.fixed = [=](double, double, double s) { return hasS && s <= r0 + 0.01 ? startY : hasE && s >= len - r1 - 0.01 ? endY : NaN(); };
		po.window = IsSet(o.window) ? o.window : (type == ERoad::Dirt ? 50 : type == ERoad::Road ? 80 : 160);
		po.maxGrade = IsSet(o.maxGrade) ? o.maxGrade : (type == ERoad::Dirt ? 0.14 : type == ERoad::Road ? 0.1 : 0.075);
		po.maxCut = IsSet(o.maxCut) ? o.maxCut : (type == ERoad::Dirt ? 4 : 12);
		if (IsSet(o.minY)) po.minY = o.minY;
		const Profile prof = SolveProfile(pts, [this](double x, double z) { return terrain(x, z); }, po);
		const std::vector<double>& cum = prof.cum;
		for (size_t i = 0; i < pts.size(); i++) pts[i].y = prof.y[i];
		struct Stop { double s; int node; int k; };
		std::vector<Stop> stops = { { 0, o.start, -1 }, { cum.back(), o.end, -1 } };
		for (const RoadOpts::Split& sp : o.splits) {
			const ProjectHit pr = Project(pts, cum, sp.x, sp.z);
			const int k = NearestIdx(cum, pr.s);
			NodeOpts no; no.kind = sp.kind; no.r = sp.r; no.rbR = sp.rbR; no.name = sp.name;
			const int node = net.AddNode(pts[k].x, pts[k].z, pts[k].y, no).id;
			stops.push_back({ cum[k], node, k });
		}
		std::stable_sort(stops.begin(), stops.end(), [](const Stop& a, const Stop& b) { return a.s - b.s < 0; });
		NodeOpts endo; endo.kind = ENode::End; endo.r = 0;
		if (stops.front().node < 0) stops.front().node = net.AddNode(pts[0].x, pts[0].z, pts[0].y, endo).id;
		if (stops.back().node < 0) stops.back().node = net.AddNode(pts.back().x, pts.back().z, pts.back().y, endo).id;
		info.store.emplace_back();
		RoadBuilt* rb = &info.store.back();
		for (size_t q = 0; q + 1 < stops.size(); q++) {
			const int k0 = stops[q].k >= 0 ? stops[q].k : NearestIdx(cum, stops[q].s);
			const int k1 = stops[q + 1].k >= 0 ? stops[q + 1].k : NearestIdx(cum, stops[q + 1].s);
			if (k1 <= k0) continue;
			const Line seg(pts.begin() + k0, pts.begin() + k1 + 1);
			EdgeOpts eo; eo.name = o.name; eo.base = o.base;
			rb->edges.push_back(net.AddEdge(stops[q].node, stops[q + 1].node, seg, type, eo).id);
		}
		rb->pts = pts; rb->cum = cum;
		for (const Stop& s : stops) rb->nodes.push_back(s.node);
		return rb;
	}

	static Line EdgeLocal(const REdge& e) {
		Line L;
		for (int i = 0; i < e.n; i++) { P3 p; p.x = e.X(i); p.z = e.Z(i); p.y = e.Y(i); L.push_back(p); }
		return L;
	}

	int splitAtPoint(RoadBuilt* r, double px, double pz, const std::string& name) {
		const ProjectHit pr = Project(r->pts, r->cum, px, pz);
		const int k = NearestIdx(r->cum, pr.s);
		const P3 pt = r->pts[k];
		for (size_t ei = 0; ei < r->edges.size(); ei++) {
			const int eid = r->edges[ei];
			const REdge& e = E(eid);
			const int a = e.a, b = e.b;
			const double sa = Project(r->pts, r->cum, e.X(0), e.Z(0)).s, sb = Project(r->pts, r->cum, e.X(e.n - 1), e.Z(e.n - 1)).s;
			if (pr.s <= sa || pr.s >= sb) continue;
			NodeOpts no; no.kind = ENode::X; no.r = 9; no.name = name;
			const int n = net.AddNode(pt.x, pt.z, pt.y, no).id;
			const Line local = EdgeLocal(E(eid));
			const std::vector<double> lc = CumLen(local);
			const int kk = NearestIdx(lc, Project(local, lc, pt.x, pt.z).s);
			const ERoad type = E(eid).type; const std::string ename = E(eid).name;
			net.RemoveEdge(eid);
			EdgeOpts eo; eo.name = ename;
			const int e1 = net.AddEdge(a, n, Line(local.begin(), local.begin() + kk + 1), type, eo).id;
			const int e2 = net.AddEdge(n, b, Line(local.begin() + kk, local.end()), type, eo).id;
			r->edges.erase(r->edges.begin() + ei);
			r->edges.insert(r->edges.begin() + ei, { e1, e2 });
			return n;
		}
		return -1;
	}

	void joinTo(RoadBuilt* r, RoadBuilt* side, bool atStart = true) {
		const P3 p = atStart ? side->pts.front() : side->pts.back();
		const int n = splitAtPoint(r, p.x, p.z, "jct");
		if (n < 0) return;
		const int eid = atStart ? side->edges.front() : side->edges.back();
		const int endNode = atStart ? E(eid).a : E(eid).b, farNode = atStart ? E(eid).b : E(eid).a;
		Line pts = EdgeLocal(E(eid));
		if (!atStart) std::reverse(pts.begin(), pts.end());
		const std::vector<double> cum = CumLen(pts);
		const double s = Project(pts, cum, N(n).x, N(n).z).s;
		int k = 1;
		while (k < (int)pts.size() - 2 && cum[k] < s + 3) k++;
		Line np; { P3 q; q.x = N(n).x; q.z = N(n).z; q.y = pts[k].y; np.push_back(q); }
		np.insert(np.end(), pts.begin() + k, pts.end());
		if (!atStart) std::reverse(np.begin(), np.end());
		const ERoad type = E(eid).type; const std::string ename = E(eid).name;
		net.RemoveEdge(eid);
		N(endNode).dead = N(endNode).e.empty();
		EdgeOpts eo; eo.name = ename;
		const int e2 = atStart ? net.AddEdge(n, farNode, np, type, eo).id : net.AddEdge(farNode, n, np, type, eo).id;
		auto it = std::find(side->edges.begin(), side->edges.end(), eid);
		*it = e2;
		EaseToJunction(E(e2), atStart, N(n).y);
	}

	int findNode(const std::string& name, bool rbOnly) const {
		for (const RNode& q : net.nodes) if (q.name == name && (!rbOnly || q.kind == ENode::RB)) return q.id;
		return -1;
	}

	void build();
};

RoadOpts RO(int start, int end, const std::string& name) { RoadOpts o; o.start = start; o.end = end; o.name = name; return o; }

void Layout::build() {
	// ------------------------------------------------------------------ city grid
	for (auto& ij : CITY_ROUNDABOUTS) info.cityRb.insert(std::to_string(ij.first) + "," + std::to_string(ij.second));
	std::vector<std::vector<int>> G(XS.size());
	for (int i = 0; i < (int)XS.size(); i++) for (int j = 0; j < (int)ZS.size(); j++) {
		const bool rb = info.cityRb.count(std::to_string(i) + "," + std::to_string(j)) > 0;
		NodeOpts o; o.kind = rb ? ENode::RB : ENode::X; o.r = 10; o.rbR = 7.6; o.sig = rb ? NaN() : CellPhase(i, j); o.hasGrid = true; o.gi = i; o.gj = j;
		G[i].push_back(net.AddNode(XS[i], ZS[j], 0, o).id);
	}
	auto gridEdge = [&](int a, int b, GridRef g) {
		Line L(2); L[0].x = N(a).x; L[0].z = N(a).z; L[1].x = N(b).x; L[1].z = N(b).z;
		EdgeOpts eo; eo.render = false; eo.hasGrid = true; eo.grid = g; eo.city = true;
		net.AddEdge(a, b, L, ERoad::Street, eo);
	};
	struct SpliceDef { double z; const char* name; };
	std::map<std::string, std::vector<SpliceDef>> splices = { { "4,7", { { -10, "WB ramps" }, { 30, "EB ramps" } } }, { "9,7", { { 10, "Sol Freeway" } } } };
	std::map<std::string, int> spliceNodes;
	for (int i = 0; i < (int)XS.size(); i++) for (int j = 0; j < (int)ZS.size() - 1; j++) {
		if (REMOVED_SEGMENTS.count("v:" + std::to_string(i) + "," + std::to_string(j))) continue;
		auto it = splices.find(std::to_string(i) + "," + std::to_string(j));
		if (it == splices.end()) { gridEdge(G[i][j], G[i][j + 1], { i, j, 0, 1 }); continue; }
		int prev = G[i][j];
		for (const SpliceDef& s : it->second) {
			NodeOpts o; o.kind = ENode::X; o.r = 10; o.sig = CellPhase(i + 20, j + 7); o.name = s.name;
			RNode& n = net.AddNode(XS[i], s.z, 0, o);
			n.city = true;
			const int nid = n.id;
			char key[32]; snprintf(key, sizeof key, "%d,%d", i, (int)s.z);
			spliceNodes[key] = nid;
			gridEdge(prev, nid, { i, j, 0, 1 });
			prev = nid;
		}
		gridEdge(prev, G[i][j + 1], { i, j, 0, 1 });
	}
	for (int j = 0; j < (int)ZS.size(); j++) for (int i = 0; i < (int)XS.size() - 1; i++) {
		if (REMOVED_SEGMENTS.count("h:" + std::to_string(i) + "," + std::to_string(j))) continue;
		gridEdge(G[i][j], G[i + 1][j], { i, j, 1, 0 });
	}

	// ------------------------------------------------------------------ key junctions
	auto rbOpts = [](double rbR, double r, const char* name) { NodeOpts o; o.kind = ENode::RB; o.rbR = rbR; o.r = r; o.name = name; return o; };
	const Town &TF = TownByKey("fern"), &TD = TownByKey("dry"), &TP = TownByKey("pine"), &TM = TownByKey("mirador"), &TH = TownByKey("hale"), &TS = TownByKey("seco");
	const int rbFern = mk(TF.x, TF.z, rbOpts(17, 25, "Fern Creek"));
	const int rbDry = mk(TD.x, TD.z, rbOpts(17, 25, "Dry Wells"));
	const int rbPine = mk(TP.x, TP.z, rbOpts(15, 23, "Pine Hollow"));
	info.rbs.push_back(rbFern); info.rbs.push_back(rbDry); info.rbs.push_back(rbPine);
	NodeOpts jo; jo.kind = ENode::X; jo.r = 12; jo.name = "Sol Freeway west end";
	const int jDw = mk(-3700, -2480, jo);

	// ------------------------------------------------------------------ crossroads (built before the freeway)
	const int cityW = G[0][13], cityN = G[9][0], cityE = G[18][3];
	RoadOpts co = RO(cityW, rbPine, "Coast Highway");
	{ RoadOpts::Split s; s.x = -1398; s.z = 250; s.name = "Farm Road jct"; s.r = 9; co.splits.push_back(s); }
	{ RoadOpts::Split s; s.x = TM.x; s.z = TM.z; s.kind = ENode::RB; s.rbR = 15; s.r = 23; s.name = "Mirador"; co.splits.push_back(s); }
	RoadBuilt* coast = road(RouteByKey("coast").ctrl, ERoad::Highway, co);
	RoadBuilt* fernN = road(RouteByKey("fernN").ctrl, ERoad::Road, RO(rbFern, -1, "Main Street"));
	RoadOpts vo = RO(cityN, rbPine, "Vistawood Drive"); vo.maxGrade = 0.11;
	RoadBuilt* vista = road(RouteByKey("vista").ctrl, ERoad::Road, vo);
	RoadOpts bo = RO(cityE, rbPine, "Bayshore Road");
	{ RoadOpts::Split s; s.x = TH.x; s.z = TH.z; s.kind = ENode::RB; s.rbR = 15; s.r = 23; s.name = "Port Hale"; bo.splits.push_back(s); }
	RoadBuilt* bay = road(RouteByKey("bay").ctrl, ERoad::Highway, bo);

	// ------------------------------------------------------------------ freeway centreline + profile
	const Line fwC = Resample(Catmull(RouteByKey("freeway").ctrl, 4), 6);
	const std::vector<double> fwCum = CumLen(fwC);
	const auto crossCoast = Intersect(fwC, fwCum, coast->pts, coast->cum);
	const auto crossFern = Intersect(fwC, fwCum, fernN->pts, fernN->cum);
	auto yAt = [](const RoadBuilt* r, double s) { return PointAt(r->pts, r->cum, s).y; };
	auto cityProfile = [](double x) {
		if (x <= -200) return FW.y;
		if (x <= -75) return Lerp(FW.y, 6.6, Smooth(-200, -75, x));
		if (x <= 2) return Lerp(6.6, 0.16, (x + 75) / 77);
		return 0.12;
	};
	ProfileOpts fo; fo.window = 260; fo.maxGrade = 0.045; fo.maxCut = 18; fo.y0 = N(jDw).y;
	fo.fixed = [&](double x, double, double) { return x > -842 ? cityProfile(x) : NaN(); };
	if (crossCoast) fo.pins.push_back({ crossCoast->sa, yAt(coast, crossCoast->sb) + 8.6, NaN() });
	if (crossFern) fo.pins.push_back({ crossFern->sa, yAt(fernN, crossFern->sb) + 8.6, NaN() });
	const Profile fwProf = SolveProfile(fwC, [this](double x, double z) { return terrain(x, z); }, fo);
	const std::vector<float>& fwY = fwProf.y;
	struct FwAt { double x, z, y, tx, tz; };
	auto fwAt = [&](double s) { const P3 p = PointAt(fwC, fwCum, s); const int k = NearestIdx(fwCum, s); const V2 t = TangentAt(fwC, k); return FwAt{ p.x, p.z, Interp(fwCum, fwY, s), t.x, t.z }; };
	auto frame = [&](double s, double lat) { const FwAt f = fwAt(s); return V2{ f.x - f.tz * lat, f.z + f.tx * lat }; };
	auto sOfX = [&](double x) { return Project(fwC, fwCum, x, 10).s; };

	struct Station { double s; ENode kind; std::string key; };
	std::vector<Station> EB, WB;
	auto addIc = [&](double sc, const std::string& name) {
		EB.push_back({ sc - 380, ENode::Split, name + ":EBoff" }); EB.push_back({ sc + 380, ENode::Merge, name + ":EBon" });
		WB.push_back({ sc + 380, ENode::Split, name + ":WBoff" }); WB.push_back({ sc - 380, ENode::Merge, name + ":WBon" });
	};
	if (crossFern) addIc(crossFern->sa, "fern");
	if (crossCoast) addIc(crossCoast->sa, "coast");
	EB.push_back({ sOfX(-620), ENode::Split, "city:EBoff" }); EB.push_back({ sOfX(-190), ENode::Merge, "city:EBon" });
	WB.push_back({ sOfX(-260), ENode::Split, "city:WBoff" }); WB.push_back({ sOfX(-690), ENode::Merge, "city:WBon" });

	const int jDt = spliceNodes["9,10"];
	auto cw = [&](double side) {
		Line out;
		for (size_t i = 0; i < fwC.size(); i++) { const V2 t = TangentAt(fwC, (int)i); const double o = side * FW.carriage; P3 p; p.x = fwC[i].x - t.z * o; p.z = fwC[i].z + t.x * o; p.y = fwY[i]; out.push_back(p); }
		return out;
	};
	const Line ebPts = cw(1);
	Line wbPts = cw(-1); std::reverse(wbPts.begin(), wbPts.end());
	const double L = fwCum.back();
	std::vector<double> wbCum; for (double c : fwCum) wbCum.push_back(L - c);
	std::reverse(wbCum.begin(), wbCum.end());
	// (insertion order kept for the barrier pass below)
	std::vector<std::pair<std::string, int>> stationNodes;
	auto stationNode = [&](const std::string& key) -> int { for (auto& kv : stationNodes) if (kv.first == key) return kv.second; return -1; };
	auto buildCarriage = [&](const Line& pts, const std::vector<double>& cumArr, const std::vector<Station>& stations, int start, int end, bool isWB) {
		std::vector<Station> st = stations;
		for (Station& q : st) q.s = isWB ? L - q.s : q.s;
		std::stable_sort(st.begin(), st.end(), [](const Station& a, const Station& b) { return a.s - b.s < 0; });
		int prevNode = start, prevK = 0;
		std::vector<int> out;
		for (const Station& q : st) {
			const int k = NearestIdx(cumArr, q.s);
			NodeOpts no; no.kind = q.kind; no.r = 0; no.name = "Sol Freeway";
			const int n = net.AddNode(pts[k].x, pts[k].z, pts[k].y, no).id;
			stationNodes.push_back({ q.key, n });
			EdgeOpts eo; eo.name = "Sol Freeway"; eo.barrierL = 1; eo.barrierR = 1; eo.city = pts[prevK].x > -842;
			out.push_back(net.AddEdge(prevNode, n, Line(pts.begin() + prevK, pts.begin() + k + 1), ERoad::Freeway, eo).id);
			prevNode = n; prevK = k;
		}
		EdgeOpts eo; eo.name = "Sol Freeway"; eo.barrierL = 1; eo.barrierR = 1;
		out.push_back(net.AddEdge(prevNode, end, Line(pts.begin() + prevK, pts.end()), ERoad::Freeway, eo).id);
		return out;
	};
	info.freewayEB = buildCarriage(ebPts, fwCum, EB, jDw, jDt, false);
	info.freewayWB = buildCarriage(wbPts, wbCum, WB, jDt, jDw, true);
	info.fwPts = fwC; info.fwCum = fwCum; info.fwY = fwY;
	for (auto& kv : stationNodes) {
		const std::string& key = kv.first;
		const int node = kv.second;
		const bool off = key.find("off") != std::string::npos, on = key.find("on") != std::string::npos;
		for (int eid : N(node).e) {
			REdge& e = E(eid);
			if (e.type != ERoad::Freeway) continue;
			if (e.a == node && off) e.noBarrierA = 75;
			if (e.b == node && on) e.noBarrierB = 75;
			if (e.b == node && off) e.noBarrierB = 5;
			if (e.a == node && on) e.noBarrierA = 5;
		}
	}

	// ------------------------------------------------------------------ ramps
	auto fwYnear = [&](double x, double z) { return Interp(fwCum, fwY, Project(fwC, fwCum, x, z).s); };
	auto rampEdge = [&](int fromNode, int toNode, const std::vector<V2>& ctrl, const std::function<double(double, double, double, double)>& profileFn, const std::string& name, double trimA) {
		Line pts = Resample(Catmull(ctrl, 3), 4);
		const std::vector<double> cum = CumLen(pts);
		for (size_t i = 0; i < pts.size(); i++) pts[i].y = profileFn(pts[i].x, pts[i].z, cum[i], cum.back());
		EdgeOpts eo; eo.name = name; eo.barrierL = 1; eo.barrierR = 1; eo.trimA = trimA;
		return net.AddEdge(fromNode, toNode, pts, ERoad::Ramp, eo).id;
	};
	auto rampY = [&](double toY, double adjLen) {
		return [=, &fwYnear](double x, double z, double s, double len) {
			const double fy = fwYnear(x, z);
			if (s < adjLen) return fy;
			const double t = Smooth(adjLen, len, s);
			return Lerp(fy, toY, t);
		};
	};
	struct Diamond { CrossHit c; P3 jE, jW; };
	auto diamond = [&](RoadBuilt* cr) {
		const CrossHit c = *Intersect(fwC, fwCum, cr->pts, cr->cum);
		const FwAt fA = fwAt(c.sa);
		const P3 p1 = PointAt(cr->pts, cr->cum, c.sb - 70), p2 = PointAt(cr->pts, cr->cum, c.sb + 70);
		auto side = [&](const P3& p) { return (p.x - fA.x) * -fA.tz + (p.z - fA.z) * fA.tx; };
		const bool e1 = side(p1) > 0;
		return Diamond{ c, e1 ? p1 : p2, e1 ? p2 : p1 };
	};
	auto icRamps = [&](double sc, int jE, int jW, const std::string& name) {
		const int ebOff = stationNode(name + ":EBoff"), ebOn = stationNode(name + ":EBon"), wbOff = stationNode(name + ":WBoff"), wbOn = stationNode(name + ":WBon");
		const double a = FW.adj;
		auto off = [&](int st, int j, double sgn, const std::string& nm) {
			const double s0 = st == ebOff ? sc - 380 : sc + 380;
			const double dir = sgn > 0 ? 1 : -1;
			const std::vector<V2> ctrl = { frame(s0, sgn * a), frame(s0 + dir * 60, sgn * a), frame(s0 + dir * 170, sgn * (a + 14)), frame(sc - dir * 110, sgn * (a + 34)), { N(j).x, N(j).z } };
			return rampEdge(st, j, ctrl, rampY(N(j).y, 60), nm, 55);
		};
		auto on = [&](int st, int j, double sgn, const std::string& nm) {
			const double s1 = st == ebOn ? sc + 380 : sc - 380;
			const double dir = sgn > 0 ? 1 : -1;
			const std::vector<V2> ctrl = { { N(j).x, N(j).z }, frame(sc + dir * 110, sgn * (a + 34)), frame(s1 - dir * 170, sgn * (a + 14)), frame(s1 - dir * 60, sgn * a), frame(s1, sgn * a) };
			Line pts = Resample(Catmull(ctrl, 3), 4);
			const std::vector<double> cum = CumLen(pts);
			const double len = cum.back();
			const double jy = N(j).y;
			for (size_t i = 0; i < pts.size(); i++) pts[i].y = cum[i] > len - 60 ? fwYnear(pts[i].x, pts[i].z) : Lerp(jy, fwYnear(pts[i].x, pts[i].z), Smooth(0, len - 60, cum[i]));
			EdgeOpts eo; eo.name = nm; eo.barrierL = 1; eo.barrierR = 1; eo.trimB = 55;
			return net.AddEdge(j, st, pts, ERoad::Ramp, eo).id;
		};
		if (ebOff >= 0) off(ebOff, jE, 1, name + " exit");
		if (ebOn >= 0) on(ebOn, jE, 1, name + " on-ramp");
		if (wbOff >= 0) off(wbOff, jW, -1, name + " exit");
		if (wbOn >= 0) on(wbOn, jW, -1, name + " on-ramp");
	};
	std::optional<Diamond> coastIc, fernIc;
	if (crossCoast) coastIc = diamond(coast);
	if (crossFern) fernIc = diamond(fernN);
	auto addIcRamps = [&](const Diamond& d, RoadBuilt* r, double sc, const std::string& name) {
		const int nE = splitAtPoint(r, d.jE.x, d.jE.z, name + " ramps");
		const int nW = splitAtPoint(r, d.jW.x, d.jW.z, name + " ramps");
		if (nE < 0 || nW < 0) return;
		info.interchanges.push_back({ name, d.c.x, d.c.z });
		icRamps(sc, nE, nW, name);
	};
	if (coastIc) addIcRamps(*coastIc, coast, crossCoast->sa, "coast");
	if (fernIc) addIcRamps(*fernIc, fernN, crossFern->sa, "fern");

	// city diamond at XS[4] (Rosewood): ramps land on the street at z = 30 (EB) and z = -10 (WB)
	{
		const int jEB = spliceNodes["4,30"], jWB = spliceNodes["4,-10"];
		typedef std::vector<std::pair<double, double>> Keys;
		auto kf = [](const Keys& keys, double x) {
			if (x <= keys[0].first) return keys[0].second;
			for (size_t i = 0; i + 1 < keys.size(); i++) if (x <= keys[i + 1].first) return Lerp(keys[i].second, keys[i + 1].second, (x - keys[i].first) / (keys[i + 1].first - keys[i].first));
			return keys.back().second;
		};
		auto mkR = [&](int from, int to, const std::vector<V2>& ctrl, const Keys& keys, const std::string& nm, double trimA, double trimB) {
			Line pts = Resample(Catmull(ctrl, 3), 4);
			for (P3& p : pts) p.y = kf(keys, p.x);
			EdgeOpts eo; eo.name = nm; eo.barrierL = 1; eo.barrierR = 1; eo.city = true; eo.trimA = trimA; eo.trimB = trimB;
			net.AddEdge(from, to, pts, ERoad::Ramp, eo);
		};
		const double y9 = FW.y, adj = FW.adj;
		mkR(stationNode("city:EBoff"), jEB, { { -620, 10 + adj }, { -560, 10 + adj }, { -520, 27 }, { -480, 29.6 }, { -455, 30 }, { -440, 30 } }, { { -620, y9 }, { -560, y9 - 0.1 }, { -535, 7.4 }, { -458, 0.16 }, { -440, 0.12 } }, "Rosewood exit", 55, NaN());
		mkR(jEB, stationNode("city:EBon"), { { -440, 30 }, { -425, 30 }, { -400, 29.6 }, { -355, 27.6 }, { -300, 25.8 }, { -250, 10 + adj }, { -190, 10 + adj } }, { { -440, 0.12 }, { -425, 0.16 }, { -355, 7.0 }, { -320, 8.4 }, { -260, y9 }, { -190, cityProfile(-190) } }, "Rosewood on-ramp", NaN(), 55);
		mkR(stationNode("city:WBoff"), jWB, { { -260, 10 - adj }, { -320, 10 - adj }, { -360, -7 }, { -400, -9.6 }, { -425, -10 }, { -440, -10 } }, { { -440, 0.12 }, { -425, 0.16 }, { -345, 7.3 }, { -320, y9 }, { -260, y9 } }, "Rosewood exit", 55, NaN());
		mkR(jWB, stationNode("city:WBon"), { { -440, -10 }, { -455, -10 }, { -480, -9.6 }, { -525, -7.6 }, { -580, -5.8 }, { -630, 10 - adj }, { -690, 10 - adj } }, { { -690, y9 }, { -620, y9 }, { -560, 8.6 }, { -525, 7.0 }, { -455, 0.16 }, { -440, 0.12 } }, "Rosewood on-ramp", NaN(), 55);
		info.interchanges.push_back({ "Rosewood", -440, 10 });
	}

	// ------------------------------------------------------------------ Fern Creek
	const int fernJ = findNode("Farm Road jct", false);
	RoadBuilt* fernS = road(RouteByKey("fernS").ctrl, ERoad::Road, RO(rbFern, -1, "Main Street"));
	RoadBuilt* fernW = road(RouteByKey("fernW").ctrl, ERoad::Road, RO(rbFern, -1, "Airfield Road"));
	std::vector<V2> feCtrl(RouteByKey("fernE").ctrl.begin(), RouteByKey("fernE").ctrl.end() - 1); feCtrl.push_back({ N(fernJ).x, N(fernJ).z });
	RoadBuilt* fernE = road(feCtrl, ERoad::Road, RO(rbFern, fernJ, "Farm Road"));
	{ TownRoads& t = info.Town("fern"); t.center = rbFern; t.roads = { fernN, fernS, fernW, fernE }; }
	RoadBuilt* fs1 = road({ { -2525, -330 }, { -2620, -330 }, { -2700, -350 } }, ERoad::Road, RO(-1, -1, "Oak Street"));
	RoadBuilt* fs2 = road({ { -2530, -120 }, { -2440, -110 }, { -2360, -130 } }, ERoad::Road, RO(-1, -1, "Elm Street"));
	RoadBuilt* fs3 = road({ { -2545, 40 }, { -2650, 60 }, { -2720, 20 } }, ERoad::Road, RO(-1, -1, "Mill Lane"));
	joinTo(fernN, fs1); joinTo(fernS, fs2); joinTo(fernS, fs3);
	{ TownRoads& t = info.Town("fern"); t.roads.push_back(fs1); t.roads.push_back(fs2); t.roads.push_back(fs3); }
	road({ { -2680, -1100 }, { -2900, -1180 }, { -3100, -1100 } }, ERoad::Dirt, RO(E(fernN->edges.back()).b, -1, "Farm Track"));
	RoadBuilt* farm2 = road({ { -2556, -770 }, { -2430, -752 }, { -2300, -748 }, { -2205, -800 }, { -2150, -900 }, { -2100, -1000 } }, ERoad::Dirt, RO(-1, -1, "River Track"));
	joinTo(fernN, farm2);

	// ------------------------------------------------------------------ Dry Wells + base road
	RoadBuilt* dryE = road(RouteByKey("dryE").ctrl, ERoad::Road, RO(rbDry, jDw, "Main Street"));
	road(RouteByKey("dryE2").ctrl, ERoad::Road, RO(jDw, -1, "Desert Road"));
	RoadBuilt* dryW = road(RouteByKey("dryW").ctrl, ERoad::Road, RO(rbDry, -1, "Main Street"));
	NodeOpts go; go.kind = ENode::X; go.r = 8; go.name = "Fort Carver gate";
	const int baseGate = mk(BASE.maxX - 2, BASE.gateZ, go);
	RoadBuilt* baseRd = road(RouteByKey("baseRd").ctrl, ERoad::Highway, RO(rbDry, baseGate, "Carver Road"));
	RoadBuilt* dryN = road({ { N(rbDry).x, N(rbDry).z + 25 }, { -3860, -2330 }, { -3780, -2150 }, { -3600, -2050 } }, ERoad::Dirt, RO(rbDry, -1, "Mesa Track"));
	RoadBuilt* dryS1 = road({ { -3860, -2600 }, { -3960, -2610 }, { -4050, -2580 } }, ERoad::Road, RO(-1, -1, "Adobe Street"));
	joinTo(baseRd, dryS1);
	{ TownRoads& t = info.Town("dry"); t.center = rbDry; t.roads = { dryE, dryW, baseRd, dryN, dryS1 }; }
	RoadOpts mo = RO(-1, -1, "Mesa Track"); mo.maxGrade = 0.3;
	RoadBuilt* mesaTrack = road({ { -4500, -2380 }, { -4520, -2650 }, { -4580, -2900 }, { -4480, -3180 } }, ERoad::Dirt, mo);
	joinTo(dryW, mesaTrack);

	// ------------------------------------------------------------------ Pine Hollow
	RoadOpts po = RO(rbPine, -1, "Logging Track"); po.maxGrade = 0.25; po.window = 30; po.maxCut = 3;
	RoadBuilt* pineN = road({ { N(rbPine).x, N(rbPine).z - 23 }, { -320, -2440 }, { -400, -2520 }, { -520, -2560 } }, ERoad::Dirt, po);
	RoadBuilt* pineS1 = road({ { -300, -2250 }, { -200, -2260 }, { -110, -2240 } }, ERoad::Road, RO(-1, -1, "Cedar Lane"));
	joinTo(vista, pineS1, true);
	{ TownRoads& t = info.Town("pine"); t.center = rbPine; t.roads = { pineN, pineS1, coast, vista, bay }; }

	// ------------------------------------------------------------------ Mirador
	const int rbMir = findNode("Mirador", true);
	if (rbMir >= 0) {
		info.rbs.push_back(rbMir);
		RoadBuilt* lakeDr = road({ { N(rbMir).x - 18, N(rbMir).z + 12 }, { -1300, -1760 }, { -1330, -1830 }, { -1320, -1900 }, { -1280, -1960 } }, ERoad::Road, RO(rbMir, -1, "Lakeshore Drive"));
		RoadOpts so = RO(rbMir, -1, "Summit Road"); so.maxGrade = 0.13;
		RoadBuilt* summit = road({ { N(rbMir).x + 20, N(rbMir).z - 4 }, { -1150, -1805 }, { -1070, -1830 }, { -990, -1840 } }, ERoad::Road, so);
		RoadBuilt* pier = road({ { -1310, -1880 }, { -1250, -1880 }, { -1215, -1905 } }, ERoad::Road, RO(-1, -1, "Marina Way"));
		joinTo(lakeDr, pier, true);
		TownRoads& t = info.Town("mirador"); t.center = rbMir; t.roads = { lakeDr, summit, pier };
	}

	// ------------------------------------------------------------------ Port Hale
	const int rbHale = findNode("Port Hale", true);
	if (rbHale >= 0) {
		info.rbs.push_back(rbHale);
		RoadBuilt* harbor = road({ { N(rbHale).x + 20, N(rbHale).z }, { 1040, -950 }, { 1062, -945 } }, ERoad::Road, RO(rbHale, -1, "Harbor Road"));
		RoadOpts cl = RO(rbHale, -1, "Cliff Street"); cl.maxGrade = 0.14;
		RoadBuilt* cliff = road({ { N(rbHale).x - 20, N(rbHale).z }, { 950, -975 }, { 900, -1000 }, { 860, -1040 } }, ERoad::Road, cl);
		RoadBuilt* quay = road({ { 1020, -1060 }, { 1035, -1000 }, { 1040, -950 } }, ERoad::Road, RO(-1, -1, "Quay Street"));
		joinTo(bay, quay, true);
		joinTo(harbor, quay, false);
		TownRoads& t = info.Town("hale"); t.center = rbHale; t.roads = { harbor, cliff, quay, bay };
	}

	// ------------------------------------------------------------------ Puerto Seco
	const int rbSeco = mk(TS.x, TS.z, rbOpts(16, 24, "Puerto Seco"));
	info.rbs.push_back(rbSeco);
	{
		const int airEnd = E(fernW->edges.back()).b;
		RoadBuilt* costa = road(RouteByKey("costa").ctrl, ERoad::Road, RO(airEnd, rbSeco, "Costa Road"));
		RoadBuilt* seco = road(RouteByKey("seco").ctrl, ERoad::Highway, RO(rbSeco, -1, "Seco Highway"));
		joinTo(dryW, seco, false);
		RoadBuilt* mayor = road({ { N(rbSeco).x, N(rbSeco).z + 22 }, { -3995, 560 }, { -3990, 650 } }, ERoad::Road, RO(rbSeco, -1, "Calle Mayor"));
		RoadBuilt* sol = road({ { N(rbSeco).x - 22, N(rbSeco).z }, { -4110, 480 }, { -4220, 505 } }, ERoad::Road, RO(rbSeco, -1, "Calle del Sol"));
		RoadBuilt* mar = road({ { -4110, 480 }, { -4120, 580 }, { -4100, 660 } }, ERoad::Road, RO(-1, -1, "Calle del Mar"));
		joinTo(sol, mar, true);
		TownRoads& t = info.Town("seco"); t.center = rbSeco; t.roads = { costa, seco, mayor, sol, mar };
	}

	// ------------------------------------------------------------------ San Aurelio and the north-east
	{
		NCityInfo& nc = BuildNorthCity(net, info);
		const std::vector<RouteDef> NR = NcRoutes();
		const Town &TG = TownByKey("gull"), &TR = TownByKey("ridge"), &TT = TownByKey("timber");
		const int jA = splitAtPoint(bay, 880, -1850, "Aurelio Hwy jct");
		RoadOpts ho = RO(jA, nc.ends["bay"], "Aurelio Highway");
		{ RoadOpts::Split s; s.x = TG.x; s.z = TG.z; s.kind = ENode::RB; s.rbR = 15; s.r = 23; s.name = "Gull Bay"; ho.splits.push_back(s); }
		RoadBuilt* hwy = road(std::vector<V2>(NR[0].ctrl.begin(), NR[0].ctrl.end() - 1), ERoad::Highway, ho);
		const int jR = splitAtPoint(bay, 300, -2322, "Ridge Road jct");
		RoadOpts ro = RO(jR, nc.ends["ridge"], "Ridge Road"); ro.maxGrade = 0.11;
		{ RoadOpts::Split s; s.x = TR.x; s.z = TR.z; s.kind = ENode::RB; s.rbR = 15; s.r = 23; s.name = "Cedar Ridge"; ro.splits.push_back(s); }
		RoadBuilt* ridge = road(std::vector<V2>(NR[1].ctrl.begin(), NR[1].ctrl.end() - 1), ERoad::Road, ro);
		const int rbTimber = mk(TT.x, TT.z, rbOpts(15, 23, "Timberline"));
		std::vector<V2> tc(NR[2].ctrl.begin() + 1, NR[2].ctrl.end() - 1); tc.push_back({ N(rbTimber).x + 23, N(rbTimber).z });
		RoadOpts to = RO(nc.ends["timber"], rbTimber, "Timber Road"); to.maxGrade = 0.11;
		RoadBuilt* timber = road(tc, ERoad::Road, to);
		const int he = nc.ends["harbor"];
		const double hx = N(he).x, hz = N(he).z;
		RoadBuilt* harborN = road({ { hx + 14, hz - 30 }, { hx + 22, hz - 150 }, { hx + 20, hz - 290 }, { hx + 2, hz - 400 } }, ERoad::Road, RO(he, -1, "Harbor Drive"));
		RoadBuilt* harborS = road({ { hx + 14, hz + 30 }, { hx + 16, hz + 170 }, { hx + 6, hz + 340 }, { hx - 30, hz + 500 }, { hx - 80, hz + 580 } }, ERoad::Road, RO(he, -1, "Harbor Drive"));
		joinTo(hwy, harborS, false);
		const int rbGull = findNode("Gull Bay", true), rbRidge = findNode("Cedar Ridge", true);
		info.rbs.push_back(rbGull); info.rbs.push_back(rbRidge); info.rbs.push_back(rbTimber);
		RoadBuilt* gBeach = road({ { N(rbGull).x + 23, N(rbGull).z }, { 1100, -2768 }, { 1150, -2790 } }, ERoad::Road, RO(rbGull, -1, "Beach Road"));
		RoadBuilt* gLane = road({ { N(rbGull).x - 23, N(rbGull).z }, { 950, -2748 }, { 880, -2715 } }, ERoad::Road, RO(rbGull, -1, "Gull Lane"));
		RoadBuilt* gDock = road({ { 1100, -2768 }, { 1105, -2700 }, { 1120, -2650 } }, ERoad::Road, RO(-1, -1, "Dock Street"));
		joinTo(gBeach, gDock, true);
		{ TownRoads& t = info.Town("gull"); t.center = rbGull; t.roads = { hwy, gBeach, gLane, gDock }; }
		RoadOpts rs = RO(rbRidge, -1, "Summit Lane"); rs.maxGrade = 0.13;
		RoadBuilt* rSummit = road({ { N(rbRidge).x - 23, N(rbRidge).z }, { -120, -3188 }, { -210, -3172 } }, ERoad::Road, rs);
		RoadOpts rl = RO(rbRidge, -1, "Lodge Road"); rl.maxGrade = 0.13;
		RoadBuilt* rLodge = road({ { N(rbRidge).x + 23, N(rbRidge).z }, { 45, -3192 }, { 130, -3165 } }, ERoad::Road, rl);
		{ TownRoads& t = info.Town("ridge"); t.center = rbRidge; t.roads = { ridge, rSummit, rLodge }; }
		RoadOpts tm = RO(rbTimber, -1, "Mill Road"); tm.maxGrade = 0.13;
		RoadBuilt* tMill = road({ { N(rbTimber).x, N(rbTimber).z + 23 }, { -648, -4300 }, { -670, -4215 } }, ERoad::Road, tm);
		RoadOpts tl = RO(rbTimber, -1, "Lake Road"); tl.maxGrade = 0.13;
		RoadBuilt* tLake = road({ { N(rbTimber).x, N(rbTimber).z - 23 }, { -630, -4470 }, { -610, -4550 } }, ERoad::Road, tl);
		RoadOpts tg = RO(rbTimber, -1, "Logging Road"); tg.maxGrade = 0.2;
		RoadBuilt* tLog = road({ { N(rbTimber).x - 23, N(rbTimber).z }, { -760, -4400 }, { -900, -4440 }, { -1060, -4420 } }, ERoad::Dirt, tg);
		{ TownRoads& t = info.Town("timber"); t.center = rbTimber; t.roads = { timber, tMill, tLake, tLog }; }
		NCityInfo& nci = info.ncity;
		nci.hwy = hwy; nci.ridge = ridge; nci.timber = timber; nci.harborN = harborN; nci.harborS = harborS;
	}

	// ------------------------------------------------------------------ Sol Line railway
	BuildRailway(net, info, fwC, fwCum, [this](double x, double z) { return terrain(x, z); }, hf);

	// ------------------------------------------------------------------ base interior roads
	const double bY = hf.Sample((BASE.minX + BASE.maxX) / 2, (BASE.minZ + BASE.maxZ) / 2);
	RoadOpts bmo = RO(baseGate, -1, "Fort Carver"); bmo.base = true;
	RoadBuilt* baseMain = road({ { N(baseGate).x, N(baseGate).z }, { -4000, BASE.gateZ }, { -4300, BASE.gateZ }, { -4700, BASE.gateZ }, { -5100, BASE.gateZ } }, ERoad::Road, bmo);
	info.baseGate = baseGate; info.baseRoad = baseMain; info.baseY = bY;

	// ------------------------------------------------------------------ node clearances from the widest road
	for (RNode& n : net.nodes) {
		if (n.hasGrid || n.city) continue;
		if (n.kind == ENode::X) { double w = 6; for (int eid : n.e) { const REdge& e = E(eid); w = Max(Max(w, e.wL), e.wR); } n.r = Max(n.r, w + 3); }
	}
	for (RNode& n : net.nodes) { int c = 0; for (int eid : n.e) c = (std::max)(c, E(eid).T->cls); n.maxCls = c; }
	// ------------------------------------------------------------------ level through junctions
	std::vector<const RailCrossing*> crossings;
	if (info.hasRail) for (const RailCrossing& c : info.rail.crossings) if (c.kind == "level") crossings.push_back(&c);
	for (RNode& n : net.nodes) {
		if (n.dead || n.hasGrid || n.city || (n.kind != ENode::X && n.kind != ENode::RB)) continue;
		const double flat = n.kind == ENode::RB ? n.rbR + 6.5 : n.r + 1.5;
		for (int eid : n.e) {
			REdge& e = E(eid);
			if (e.removed || e.type == ERoad::Rail || e.a == e.b) continue;
			auto along = [&](int i) { return e.a == n.id ? (double)e.cum[i] : e.len - e.cum[i]; };
			double grade = 0;
			for (int i = 1; i < e.n; i++) if (Min(along(i), along(i - 1)) < flat + 60) grade = Max(grade, std::fabs((double)e.p[i * 3 + 1] - e.p[i * 3 - 2]) / Max(0.1, (double)e.cum[i] - e.cum[i - 1]));
			const double Gr = Max(0.12, grade * 1.25), ease = 8;
			for (int i = 0; i < e.n; i++) {
				const double x = e.p[i * 3], z = e.p[i * 3 + 2];
				bool nearCross = false;
				for (const RailCrossing* c : crossings) if (Hypot(c->x - x, c->z - z) < 20) { nearCross = true; break; }
				if (nearCross) continue;
				const double u = Max(0, along(i) - flat);
				const double room = u < ease ? Gr * u * u / (2 * ease) : Gr * (u - ease / 2);
				e.p[i * 3 + 1] = (float)(n.y + Clamp(e.p[i * 3 + 1] - n.y, -room, room));
			}
		}
	}
}
} // namespace

void BuildRoadNetwork(Heightfield& hf, RoadNet& net, RoadInfo& info) {
	Layout L(hf, net, info);
	L.build();
}

} // namespace atg
