// San Aurelio, the second city (port of src/world/northcity.js): a bay city on the north-east coast whose
// streets are not a grid. Three wobbly ring roads circle the Plaza de Aurelio, seven curving avenues run out
// from it, and side streets split the wedges between them, so every block is a different lopsided shape.
// This file has the roads, districts and routes; PopulateNorthCity (the buildings along the streets) is in
// Countryside.cpp with the rest of the out-of-town dressing.
#include "RoadLayout.h"

namespace atg {

const std::vector<NcMainDef> NC_MAIN = {
	{ -0.05, 0.10, 0.3, "Avenida del Mar", "harbor" },
	{ 0.86, 0.12, 1.4, "Bay Avenue", "bay" },
	{ 1.62, 0.09, 2.2, "Mission Street", nullptr },
	{ 2.33, 0.11, 0.9, "Ridge Avenue", "ridge" },
	{ 3.2, 0.10, 2.9, "Timber Avenue", "timber" },
	{ 4.08, 0.13, 1.7, "Cathedral Way", nullptr },
	{ 5.1, 0.10, 0.4, "Northgate Avenue", nullptr },
};
static const char* RING_NAMES[] = { "Plaza Circle", "Crescent Boulevard", "Hillcrest Parkway" };
static const std::vector<const char*> STREETS = { "Alma Street", "Calle Rosa", "Vine Street", "Chapel Lane", "Olive Street", "Harbor Row", "Tanner Street", "Lark Street", "Bell Street",
	"Cypress Lane", "Mercer Street", "Quarry Street", "Pilgrim Way", "Ash Street", "Loma Street", "Fountain Street", "Sparrow Lane", "Orchard Street",
	"Linden Street", "Canal Street", "Poet's Walk", "Cooper Street", "Silver Street", "Dover Street", "Kestrel Lane", "Juniper Street", "Sable Street" };
// side streets left out of the outer band: the big blocks they leave hold the park and the arena
struct SkipKey { int k; double fr; };
static const SkipKey SKIP[] = { { 2, 0.67 }, { 5, 0.34 } };
static bool Skipped(int k, double fr) { for (const SkipKey& s : SKIP) if (s.k == k && s.fr == fr) return true; return false; }

double NcWalkWidth(ERoad t) { return t == ERoad::Avenue ? 3.6 : t == ERoad::Road ? 2.8 : 3; }

double MainTh(int k, double r) {
	const NcMainDef& s = NC_MAIN[k % 7];
	return s.th + (k >= 7 ? kTau : 0) + s.A * std::sin(r / 190 + s.ph) * Min(1, r / 160);
}
static V2 PolarP(double r, double th) { return { NCITY.x + r * std::cos(th), NCITY.z + r * std::sin(th) }; }
static double WrapTau(double a) { return std::fmod(std::fmod(a, kTau) + kTau, kTau); }
// radius at which a spoke (angle as a function of radius) crosses ring j
static double CrossR(const std::function<double(double)>& f, int j) { double r = NCITY.rings[j]; for (int i = 0; i < 16; i++) r = NcRingR(j, f(r)); return r; }

V2 NcSpokeEnd(const std::string& key) {
	int k = -1;
	for (int i = 0; i < (int)NC_MAIN.size(); i++) if (NC_MAIN[i].out && key == NC_MAIN[i].out) { k = i; break; }
	auto f = [k](double r) { return MainTh(k, r); };
	const double r = CrossR(f, 2) + 62;
	return PolarP(r, f(r));
}

namespace {
struct SpokeDef {
	std::function<double(double)> f;
	int j0, j1;
	bool main = false;
	int k = 0;
	ERoad type;
	std::string name;
	double ext = 0, frac = 0;
};
std::vector<SpokeDef> SpokeDefs() {
	std::vector<SpokeDef> out;
	for (int k = 0; k < 7; k++) {
		SpokeDef d; d.f = [k](double r) { return MainTh(k, r); }; d.j0 = -1; d.j1 = 2; d.main = true; d.k = k; d.type = ERoad::Avenue; d.name = NC_MAIN[k].name; d.ext = NC_MAIN[k].out ? 62 : 0;
		out.push_back(d);
	}
	int si = 0;
	auto between = [](int k, double frac, double wob) {
		return [=](double r) { const double a = MainTh(k, r), b = MainTh(k + 1, r); return a + (b - a) * (frac + wob * std::sin(r / 85 + k * 1.7 + frac * 5)); };
	};
	for (int k = 0; k < 7; k++) {
		{ SpokeDef d; d.f = between(k, 0.5, 0.05); d.j0 = 0; d.j1 = 1; d.type = ERoad::Road; d.name = STREETS[si++ % STREETS.size()]; d.k = k; d.frac = 0.5; out.push_back(d); }
		for (double fr : { 0.34, 0.67 }) {
			if (Skipped(k, fr)) continue;
			SpokeDef d; d.f = between(k, fr, 0.04); d.j0 = 1; d.j1 = 2; d.type = ERoad::Road; d.name = STREETS[si++ % STREETS.size()]; d.k = k; d.frac = fr; out.push_back(d);
		}
	}
	return out;
}
} // namespace

std::vector<NcSuper> NcSuperblocks() {
	std::vector<NcSuper> out;
	for (const SkipKey& key : SKIP) {
		const int k = key.k; const double fr = key.fr;
		const double r = (NCITY.rings[1] + NCITY.rings[2]) / 2 + 8;
		const double lo = fr < 0.5 ? 0 : 0.34, hi = fr < 0.5 ? 0.67 : 1;
		const double a = MainTh(k, r), b = MainTh(k + 1, r), th = a + (b - a) * (lo + hi) / 2;
		const double rr = (NcRingR(1, th) + NcRingR(2, th)) / 2;
		const V2 p = PolarP(rr, th);
		out.push_back({ p.x, p.z, th, rr, out.empty() ? "park" : "arena", (b - a) * (hi - lo) * rr, NcRingR(2, th) - NcRingR(1, th) });
	}
	return out;
}

NcDistrict NcDistrictAt(double x, double z) {
	if (NcEdgeDist(x, z) > 60) return { nullptr, nullptr };
	const double dx = x - NCITY.x, dz = z - NCITY.z, r = Hypot(dx, dz), th = WrapTau(std::atan2(dz, dx));
	if (r < NcRingR(0, th) + 8) return { "aurcentro", "Centro" };
	const bool inner = r < NcRingR(1, th) + 8;
	if (th < 0.75 || th > 5.6) return inner ? NcDistrict{ "aurharbor", "Harborside" } : NcDistrict{ "aurbay", "Bayview" };
	if (th < 2.4) return inner ? NcDistrict{ "aurmission", "Mission" } : NcDistrict{ "aurbay", "Bayview" };
	if (th < 3.9) return inner ? NcDistrict{ "aurcathedral", "Cathedral Hill" } : NcDistrict{ "aurheights", "Aurelio Heights" };
	return inner ? NcDistrict{ "aurnorth", "Northgate" } : NcDistrict{ "aurheights", "Aurelio Heights" };
}

NCityInfo& BuildNorthCity(RoadNet& net, RoadInfo& info) {
	const double y = NCITY.y;
	const std::vector<SpokeDef> defs = SpokeDefs();
	struct RingEntry { double th; int node; };
	std::vector<RingEntry> ringNodes[3];
	auto mkNode = [&](double x, double z, const NodeOpts& o) { RNode& n = net.AddNode(x, z, y, o); n.ncity = true; return n.id; };
	NodeOpts co; co.kind = ENode::RB; co.rbR = 24; co.r = 32; co.name = "Plaza de Aurelio";
	const int center = mkNode(NCITY.x, NCITY.z, co);
	info.rbs.push_back(center);
	NCityInfo& nc = info.ncity;
	info.hasNcity = true;
	nc.center = center;
	auto addEdge = [&](int a, int b, const std::vector<V2>& pts, ERoad type, const std::string& name) {
		Line L; for (const V2& q : pts) { P3 p; p.x = q.x; p.z = q.z; p.y = y; L.push_back(p); }
		EdgeOpts eo; eo.name = name;
		REdge& e = net.AddEdge(a, b, L, type, eo);
		e.ncity = true; e.walk = NcWalkWidth(type);
		nc.edges.push_back(e.id);
		return e.id;
	};
	for (const SpokeDef& d : defs) {
		struct Stop { double r; int node; };
		std::vector<Stop> stops;
		if (d.j0 < 0) stops.push_back({ 0, center });
		for (int j = (std::max)(0, d.j0); j <= d.j1; j++) {
			const double r = CrossR(d.f, j), th = d.f(r);
			const V2 p = PolarP(r, th);
			const bool rb = d.main && j == 1;
			NodeOpts o; o.name = d.name;
			if (rb) { o.kind = ENode::RB; o.rbR = 12.5; o.r = 20; } else { o.kind = ENode::X; o.r = 10; }
			const int node = mkNode(p.x, p.z, o);
			if (rb) info.rbs.push_back(node);
			ringNodes[j].push_back({ WrapTau(th), node });
			stops.push_back({ r, node });
		}
		if (d.ext > 0) {
			const double r = stops.back().r + d.ext;
			const V2 p = PolarP(r, d.f(r));
			NodeOpts o; o.kind = ENode::X; o.r = 10; o.name = d.name;
			const int node = mkNode(p.x, p.z, o);
			stops.push_back({ r, node });
			nc.ends[NC_MAIN[d.k].out] = node;
		}
		for (size_t i = 0; i + 1 < stops.size(); i++) {
			const Stop a = stops[i], b = stops[i + 1];
			const int n = (int)Max(2, std::ceil((b.r - a.r) / 6));
			std::vector<V2> pts = { { net.nodes[a.node].x, net.nodes[a.node].z } };
			for (int q = 1; q < n; q++) { const double r = a.r + (b.r - a.r) * q / n; pts.push_back(PolarP(r, d.f(r))); }
			pts.push_back({ net.nodes[b.node].x, net.nodes[b.node].z });
			const int eid = addEdge(a.node, b.node, pts, d.type, d.name);
			if (d.main && stops[i + 1].r > NCITY.rings[2] + 20) net.edges[eid].ext = true;
		}
	}
	for (int j = 0; j < 3; j++) {
		std::vector<RingEntry>& L = ringNodes[j];
		std::stable_sort(L.begin(), L.end(), [](const RingEntry& a, const RingEntry& b) { return a.th - b.th < 0; });
		for (size_t i = 0; i < L.size(); i++) {
			const RingEntry A = L[i], B = L[(i + 1) % L.size()];
			const double a = A.th; double b = B.th; if (b <= a) b += kTau;
			const int n = (int)Max(2, std::ceil((b - a) * NCITY.rings[j] / 6));
			std::vector<V2> pts = { { net.nodes[A.node].x, net.nodes[A.node].z } };
			for (int q = 1; q < n; q++) { const double th = a + (b - a) * q / n; pts.push_back(PolarP(NcRingR(j, th), th)); }
			pts.push_back({ net.nodes[B.node].x, net.nodes[B.node].z });
			addEdge(A.node, B.node, pts, ERoad::Avenue, RING_NAMES[j]);
		}
	}
	return nc;
}

std::vector<RouteDef> NcRoutes() {
	const Town& gull = TownByKey("gull"); const Town& ridge = TownByKey("ridge"); const Town& timber = TownByKey("timber");
	const V2 se = NcSpokeEnd("bay"), sw = NcSpokeEnd("ridge"), w = NcSpokeEnd("timber"), e = NcSpokeEnd("harbor");
	std::vector<RouteDef> out;
	RouteDef a; a.key = "aurelio"; a.type = "highway"; a.ends[0] = RouteEnd::None(); a.ends[1] = RouteEnd::AtY(NCITY.y);
	a.ctrl = { { 905, -1905 }, { CoastX(-2120) - 330, -2120 }, { CoastX(-2440) - 305, -2440 }, { gull.x, gull.z }, { CoastX(-3060) - 300, -3060 }, { CoastX(-3360) - 330, -3360 }, { se.x + 60, se.z + 45 }, se };
	out.push_back(a);
	RouteDef r; r.key = "ridgeRd"; r.type = "road"; r.ends[0] = RouteEnd::None(); r.ends[1] = RouteEnd::AtY(NCITY.y);
	r.ctrl = { { 300, -2335 }, { 262, -2560 }, { 160, -2830 }, { ridge.x, ridge.z }, { -10, -3420 }, { 60, -3620 }, sw };
	out.push_back(r);
	RouteDef t; t.key = "timberRd"; t.type = "road"; t.ends[0] = RouteEnd::AtY(NCITY.y); t.ends[1] = RouteEnd::Pad("timber");
	t.ctrl = { w, { w.x - 90, w.z - 30 }, { -330, -4190 }, { -490, -4300 }, { timber.x, timber.z } };
	out.push_back(t);
	RouteDef h; h.key = "harborDr"; h.type = "road";
	h.ctrl = { { e.x + 20, e.z - 380 }, { e.x + 30, e.z - 200 }, { e.x + 36, e.z }, { e.x + 20, e.z + 200 }, { e.x - 20, e.z + 360 } };
	out.push_back(h);
	return out;
}

} // namespace atg
