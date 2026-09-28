// Everything outside Los Soles that isn't terrain or road (port of src/world/countryside.js and
// northcity.js's populateNorthCity): the towns (buildings lined up along their curving streets), San
// Aurelio's frontage, farmsteads, the airfield, the Sol Line stations, Fort Carver, power lines along the
// highways and the vegetation scatter.
#include "CityMap.h"

namespace atg {

using C3 = std::array<double, 3>;
using Palette = std::vector<C3>;

namespace {

struct Ctx {
	CityMap& map;
	RNG rng;
	Heightfield& hf;
	RoadNet& net;
	std::vector<Footprint> footprints;
	Ctx(CityMap& m) : map(m), rng(9001), hf(m.hf), net(m.roads) {}
};

double SlopeAt(const Heightfield& hf, double x, double z) {
	const double e = 4;
	const double dx = hf.Sample(x + e, z) - hf.Sample(x - e, z), dz = hf.Sample(x, z + e) - hf.Sample(x, z - e);
	return Hypot(dx, dz) / (2 * e);
}
void Corners(const Footprint& r, V2 out[4]) {
	const double s = std::sin(r.yaw), c = std::cos(r.yaw);
	const double L[4][2] = { { -r.hx, -r.hz }, { r.hx, -r.hz }, { r.hx, r.hz }, { -r.hx, r.hz } };
	for (int k = 0; k < 4; k++) out[k] = { r.cx + L[k][0] * c + L[k][1] * s, r.cz - L[k][0] * s + L[k][1] * c };
}
bool RectsOverlap(const Footprint& a, const Footprint& b) {
	double axes[4][2];
	int n = 0;
	for (const Footprint* r : { &a, &b }) { const double s = std::sin(r->yaw), c = std::cos(r->yaw); axes[n][0] = c; axes[n][1] = -s; n++; axes[n][0] = s; axes[n][1] = c; n++; }
	const double dx = b.cx - a.cx, dz = b.cz - a.cz;
	for (const auto& ax : axes) {
		auto proj = [&](const Footprint& r) { const double s = std::sin(r.yaw), c = std::cos(r.yaw); return std::fabs(c * ax[0] - s * ax[1]) * r.hx + std::fabs(s * ax[0] + c * ax[1]) * r.hz; };
		if (std::fabs(dx * ax[0] + dz * ax[1]) > proj(a) + proj(b)) return false;
	}
	return true;
}
bool RectCircle(const Footprint& r, double x, double z, double rad) {
	const double s = std::sin(r.yaw), c = std::cos(r.yaw), dx = x - r.cx, dz = z - r.cz;
	const double lx = dx * c - dz * s, lz = dx * s + dz * c;
	const double qx = Clamp(lx, -r.hx, r.hx), qz = Clamp(lz, -r.hz, r.hz);
	return Hypot(lx - qx, lz - qz) < rad;
}

struct PlaceOpts { double margin = 2, roadMargin = 1.5, maxDrop = 3.5; };
bool CanPlace(Ctx& ctx, Footprint& r, const PlaceOpts& o = PlaceOpts()) {
	Footprint grown = r; grown.hx += o.margin; grown.hz += o.margin;
	for (const Footprint& f : ctx.footprints) if (RectsOverlap(grown, f)) return false;
	V2 pts[5]; Corners(r, pts); pts[4] = { r.cx, r.cz };
	double lo = kInf, hi = -kInf;
	for (const V2& p : pts) {
		if (ctx.net.OnRoad(p.x, p.z, o.roadMargin).valid()) return false;
		const double h = ctx.hf.Sample(p.x, p.z);
		if (h < 0.4) return false;
		if (Hypot(p.x - LAKE.x, p.z - LAKE.z) < LAKE.r + 10) return false;
		lo = Min(lo, h); hi = Max(hi, h);
	}
	for (int k = 0; k < 4; k++) { const V2 a = pts[k], b = pts[(k + 1) % 4]; if (ctx.net.OnRoad((a.x + b.x) / 2, (a.z + b.z) / 2, o.roadMargin).valid()) return false; }
	if (hi - lo > o.maxDrop) return false;
	for (const RNode& n : ctx.net.nodes) {
		if (n.hasGrid || n.e.empty()) continue;
		const double rr = (n.kind == ENode::RB ? n.rbR + 12 : n.r + 4) + Max(r.hx, r.hz);
		if (std::fabs(n.x - r.cx) < rr && std::fabs(n.z - r.cz) < rr && Hypot(n.x - r.cx, n.z - r.cz) < rr) return false;
	}
	r.y0 = lo - 0.25; r.yTop = hi;
	return true;
}

struct AddOpts {
	bool hasTint = false; C3 tint{ 1, 1, 1 };
	double seed = NaN();
	std::string roof, kind, name, sign, district;
	double floorH = 0, cell = 0;
	bool noCollide = false;
	bool hasFront = false; double front[2] = { 0, 0 };
	AddOpts& Tint(const C3& t) { hasTint = true; tint = t; return *this; }
	AddOpts& Front(double fx, double fz) { hasFront = true; front[0] = fx; front[1] = fz; return *this; }
};
int AddBld(Ctx& ctx, const Footprint& r, double height, int style, const AddOpts& o) {
	BldOpts b; b.rot = r.yaw; b.y0 = r.y0;
	if (o.hasTint) b.Tint(o.tint);
	b.seed = IsSet(o.seed) ? o.seed : ctx.rng.Next();
	b.roof = o.roof.empty() ? "flat" : o.roof; b.floorH = o.floorH; b.cell = o.cell; b.kind = o.kind.empty() ? "building" : o.kind;
	b.name = o.name; b.sign = o.sign; b.noCollide = o.noCollide; b.base = r.y0;
	const int bi = ctx.map.AddBuilding(nullptr, r.cx - r.hx, r.cz - r.hz, r.cx + r.hx, r.cz + r.hz, height + (r.yTop - r.y0), style, b);
	if (bi >= 0) {
		Building& B = ctx.map.buildings[bi];
		B.district = o.district.empty() ? "country" : o.district;
		B.hasFront = o.hasFront; B.front[0] = o.front[0]; B.front[1] = o.front[1];
		B.foundation = r.yTop - r.y0;
	}
	ctx.footprints.push_back(r);
	return bi;
}
Prop MkProp(const char* type, double x, double z, double rot = 0, double y = NaN(), double scale = 1) { Prop p; p.type = type; p.x = x; p.z = z; p.rot = rot; p.y = y; p.scale = scale; return p; }

// ------------------------------------------------------------------ towns
struct TownStyle {
	std::string district;
	int style; std::string roof; int floors[2]; Palette tint;
	std::vector<std::pair<const char*, const char*>> shops;
};
const TownStyle& StyleOf(const std::string& key) {
	static const std::map<std::string, TownStyle> S = {
		{ "fern", { "country", 3, "gable", { 1, 2 }, { { 1, 0.95, 0.85 }, { 0.92, 0.88, 0.78 }, { 0.85, 0.9, 0.95 }, { 1, 0.85, 0.8 }, { 0.95, 0.95, 0.9 }, { 0.8, 0.88, 0.78 } },
			{ { "FERN CREEK DINER", "Fern Creek Diner" }, { "GENERAL STORE", "Olsen General Store" }, { "FEED & SEED", "Feed & Seed" }, { "SALOON", "Rusty Spur Saloon" }, { "HARDWARE", "Hank's Hardware" } } } },
		{ "pine", { "forest", 2, "gable", { 1, 2 }, { { 0.75, 0.55, 0.4 }, { 0.62, 0.48, 0.36 }, { 0.7, 0.62, 0.5 }, { 0.55, 0.42, 0.32 } },
			{ { "LODGE", "Pine Hollow Lodge" }, { "BAIT & TACKLE", "Bait & Tackle" }, { "CAFE", "Timberline Cafe" } } } },
		{ "dry", { "desert", 3, "flat", { 1, 1 }, { { 0.95, 0.78, 0.6 }, { 0.9, 0.72, 0.55 }, { 1, 0.85, 0.7 }, { 0.85, 0.65, 0.5 }, { 0.95, 0.9, 0.8 } },
			{ { "MOTEL", "Desert Rose Motel" }, { "CANTINA", "Cantina Los Muertos" }, { "TRADING POST", "Trading Post" }, { "GUNS", "Dry Wells Guns & Ammo" } } } },
		{ "mirador", { "lake", 2, "gable", { 1, 2 }, { { 0.86, 0.74, 0.58 }, { 0.95, 0.93, 0.88 }, { 0.72, 0.6, 0.48 }, { 0.8, 0.86, 0.9 }, { 0.9, 0.82, 0.7 } },
			{ { "MARINA", "Mirador Marina" }, { "LAKEVIEW HOTEL", "Lakeview Hotel" }, { "BOAT RENTAL", "Mirador Boat Rental" }, { "ICE CREAM", "Two Scoops" }, { "BAR & GRILL", "The Loon Bar & Grill" } } } },
		{ "hale", { "harbor", 3, "gable", { 1, 2 }, { { 0.55, 0.72, 0.85 }, { 0.9, 0.35, 0.3 }, { 0.95, 0.85, 0.45 }, { 0.95, 0.95, 0.93 }, { 0.45, 0.62, 0.55 }, { 0.85, 0.6, 0.45 } },
			{ { "FISH MARKET", "Hale Fish Market" }, { "THE ANCHOR", "The Anchor Pub" }, { "OYSTER BAR", "Pearl Oyster Bar" }, { "CHANDLERY", "Hale Chandlery" }, { "BAIT SHOP", "Hooked Bait Shop" } } } },
		{ "gull", { "gull", 3, "gable", { 1, 2 }, { { 0.95, 0.95, 0.92 }, { 0.6, 0.78, 0.9 }, { 0.95, 0.88, 0.6 }, { 0.85, 0.55, 0.5 }, { 0.7, 0.85, 0.8 } },
			{ { "SURF & SAND", "Gull Bay Surf Shop" }, { "CLAM SHACK", "The Clam Shack" }, { "MOTEL", "Seagull Motel" }, { "ICE CREAM", "Salt & Sugar" }, { "BAIT", "Gull Bay Bait" } } } },
		{ "ridge", { "ridge", 2, "gable", { 1, 2 }, { { 0.8, 0.62, 0.48 }, { 0.68, 0.55, 0.42 }, { 0.9, 0.85, 0.75 }, { 0.6, 0.5, 0.42 } },
			{ { "RIDGE INN", "Cedar Ridge Inn" }, { "OUTFITTERS", "Summit Outfitters" }, { "DINER", "Pass Diner" }, { "GAS & GO", "Ridge Gas & Go" } } } },
		{ "timber", { "forest", 2, "gable", { 1, 2 }, { { 0.7, 0.5, 0.36 }, { 0.58, 0.44, 0.33 }, { 0.66, 0.56, 0.44 }, { 0.5, 0.38, 0.3 }, { 0.78, 0.66, 0.5 } },
			{ { "SAWMILL", "Timberline Sawmill Co." }, { "TAVERN", "The Axe & Anvil" }, { "LUMBER", "Northwoods Lumber" }, { "GENERAL STORE", "Timberline General" } } } },
		{ "seco", { "seco", 3, "flat", { 1, 2 }, { { 1, 0.97, 0.9 }, { 0.97, 0.9, 0.78 }, { 0.95, 0.8, 0.62 }, { 0.85, 0.92, 0.95 }, { 1, 0.88, 0.8 } },
			{ { "LA SIRENA", "Cantina La Sirena" }, { "TACOS", "Tacos El Faro" }, { "SURF SHOP", "Seco Surf" }, { "HOTEL DEL MAR", "Hotel del Mar" }, { "MERCADO", "Mercado Seco" } } } },
	};
	return S.at(key);
}

void GasStation(Ctx& ctx, const Footprint& r, double fx, double fz, const std::string& district) {
	CityMap& map = ctx.map; RNG& rng = ctx.rng;
	const double s = std::sin(r.yaw), c = std::cos(r.yaw);
	auto L = [&](double lx, double lz) { return V2{ r.cx + lx * c + lz * s, r.cz - lx * s + lz * c }; };
	const V2 back = L(r.hx - 4.2, 0);
	{
		BldOpts o; o.rot = r.yaw; o.y0 = r.y0; o.Tint(0.95, 0.95, 0.95); o.seed = rng.Next(); o.roof = "flat"; o.floorH = 4.4; o.name = "Gas Station"; o.sign = "GAS";
		const int bi = map.AddBuilding(nullptr, back.x - 4, back.z - 7, back.x + 4, back.z + 7, 4.4 + (r.yTop - r.y0), 5, o);
		if (bi >= 0) { map.buildings[bi].hasFront = true; map.buildings[bi].front[0] = fx; map.buildings[bi].front[1] = fz; }
	}
	const V2 cc = L(-3.5, 0);
	{ BldOpts o; o.rot = r.yaw; o.y0 = r.yTop + 4.6; o.Tint(0.85, 0.1, 0.1); o.seed = rng.Next(); o.roof = "flat"; o.noCollide = true; map.AddBuilding(nullptr, cc.x - 5, cc.z - 9, cc.x + 5, cc.z + 9, 0.7, 4, o); }
	for (double lz : { -6.0, 0.0, 6.0 }) { const V2 p = L(-3.5, lz); map.props.push_back(MkProp("pump", p.x, p.z, r.yaw + kPi / 2, r.yTop)); }
	for (double lz : { -8.5, 8.5 }) for (double lx : { -8.0, 1.0 }) { const V2 p = L(lx, lz); Prop pr = MkProp("post", p.x, p.z, 0, r.yTop); pr.h = 4.6; map.props.push_back(pr); }
	map.padSurfaces.push_back({ r.cx, r.cz, r.hx + 3, r.hz + 2, r.yaw, r.yTop + 0.03, "concrete" });
	for (int k = 0; k < 2; k++) { const V2 p = L(-2, -10 + k * 20); ParkingSpot sp; sp.x = p.x; sp.z = p.z; sp.rot = r.yaw; sp.district = district; sp.lot = true; map.parkingSpots.push_back(sp); }
	ctx.footprints.push_back(r);
}

void HarborPier(Ctx& ctx) {
	CityMap& map = ctx.map;
	double x = 1062; const double z = -945;
	while (x < 1400 && ctx.hf.Sample(x, z) > 1.5) x += 4;
	const double x0 = x - 12, x1 = x + 90;
	{ BldOpts o; o.y0 = 2.1; o.Tint(0.45, 0.33, 0.22); o.seed = 0.2; o.roof = "flat"; o.kind = "pier"; o.name = "Hale Pier"; const int bi = map.AddBuilding(nullptr, x0, z - 3.5, x1, z + 3.5, 0.5, 4, o); map.buildings[bi].district = "harbor"; }
	for (double px = x0 + 6; px < x1; px += 10) for (int side : { -1, 1 }) { Prop p = MkProp("post", px, z + side * 3.2, 0, -2); p.h = 4.7; map.props.push_back(p); }
	const double boats[3][2] = { { x + 25, z + 12 }, { x + 50, z - 13 }, { x + 72, z + 12 } };
	for (const auto& b : boats) map.props.push_back(MkProp("boat", b[0], b[1], kPi / 2 + std::fmod(b[0], 3) * 0.1, -1.2));
	Landmark L; L.x = x + 40; L.z = z; L.y = 2.6; map.landmarks["halePier"] = L;
}
void LakeDocks(Ctx& ctx) {
	CityMap& map = ctx.map;
	const double y = LAKE.y + 0.5;
	const double docks[2][3] = { { -1300, -1880, 34 }, { -1305, -1935, 26 } };
	for (const auto& d : docks) {
		const double cx = d[0], cz = d[1], len = d[2];
		BldOpts o; o.y0 = y - 0.35; o.Tint(0.5, 0.38, 0.26); o.seed = 0.4; o.roof = "flat"; o.kind = "pier";
		const int bi = map.AddBuilding(nullptr, cx - len, cz - 2, cx, cz + 2, 0.35, 4, o);
		map.buildings[bi].district = "lake";
		map.props.push_back(MkProp("boat", cx - len * 0.6, cz + 5, kPi / 2, LAKE.y - 0.45, 0.6));
	}
}

void TownFill(Ctx& ctx, const std::string& key, const TownRoads& t) {
	CityMap& map = ctx.map; RNG& rng = ctx.rng; RoadNet& net = ctx.net; Heightfield& hf = ctx.hf;
	const Town& T = TownByKey(key.c_str());
	const TownStyle& S = StyleOf(key);
	TownArea area0; area0.district = S.district; area0.name = T.name; area0.x = T.x; area0.z = T.z; area0.r = T.r + 40; area0.town = true;
	map.townAreas.push_back(area0);
	const size_t areaIdx = map.townAreas.size() - 1;
	size_t shopIdx = 0;
	std::vector<WalkNode>& walk = map.walkNodes;
	std::vector<int> edges; std::set<int> seen;
	for (const RoadBuilt* r : t.roads) for (int eid : r->edges) if (!net.edges[eid].removed && seen.insert(eid).second) edges.push_back(eid);
	for (int eid : net.EdgesIn(T.x - T.r, T.z - T.r, T.x + T.r, T.z + T.r)) {
		const REdge& e = net.edges[eid];
		if (!e.hasGrid && e.type != ERoad::Freeway && e.type != ERoad::Ramp && e.type != ERoad::Rail && seen.insert(eid).second) edges.push_back(eid);
	}
	bool gasDone = false;
	static const Palette shopTints = { { 0.95, 0.9, 0.8 }, { 0.9, 0.8, 0.7 }, { 0.85, 0.88, 0.92 }, { 1, 0.95, 0.85 } };
	for (int eid : edges) {
		const REdge& e = net.edges[eid];
		if (e.type == ERoad::Dirt && key != "pine") continue;
		std::vector<int> sideNodes[2];
		for (double s = 6; s < e.len - 6; s += 22) {
			const EdgePoint q = net.At(e, s);
			const double d = Hypot(q.x - T.x, q.z - T.z);
			if (d > T.r + 30) continue;
			for (int side : { -1, 1 }) {
				const double off = (side < 0 ? e.wL : e.wR) + 2.2;
				const double x = q.x - q.tz * off * side, z = q.z + q.tx * off * side;
				if (net.OnRoad(x, z, 0.3).valid()) continue;
				WalkNode wn; wn.id = (int)walk.size(); wn.x = x; wn.z = z; wn.town = key;
				walk.push_back(wn);
				map.townAreas[areaIdx].nodeIds.push_back(wn.id);
				std::vector<int>& list = sideNodes[side < 0 ? 0 : 1];
				if (!list.empty()) { const int p = list.back(); if (Hypot(walk[p].x - x, walk[p].z - z) < 40) { walk[p].links.push_back(wn.id); walk[wn.id].links.push_back(p); } }
				list.push_back(wn.id);
			}
		}
		for (size_t k = 0; k < (std::min)(sideNodes[0].size(), sideNodes[1].size()); k += 3) {
			const int a = sideNodes[0][k], b = sideNodes[1][k];
			walk[a].links.push_back(b); walk[b].links.push_back(a);
			walk[a].cross = { b }; walk[b].cross = { a };
		}
		double s = 10 + rng.Range(0, 8);
		while (s < e.len - 10) {
			const EdgePoint q = net.At(e, s);
			const double dCenter = Hypot(q.x - T.x, q.z - T.z);
			if (dCenter > T.r) { s += 20; continue; }
			const bool core = dCenter < T.r * 0.45;
			const int side = rng.Chance(0.5) ? 1 : -1;
			for (int sd : { side, -side }) {
				const bool shop = core && rng.Chance(key == "hale" || key == "mirador" ? 0.7 : 0.55) && shopIdx < S.shops.size();
				const bool gas = !gasDone && dCenter > T.r * 0.4 && e.T->cls >= 1 && rng.Chance(0.25);
				const double w = gas ? 26 : shop ? rng.Range(14, 20) : rng.Range(9, 13);
				const double dpt = gas ? 22 : shop ? rng.Range(12, 16) : rng.Range(8, 11);
				const double setback = gas ? 5 : shop ? 3.5 : rng.Range(5, 9);
				const double off = (sd < 0 ? e.wL : e.wR) + setback + dpt / 2;
				const double rx = -q.tz * sd, rz = q.tx * sd;
				const double cx = q.x + rx * off, cz = q.z + rz * off;
				const double yaw = std::atan2(q.tx, q.tz);
				Footprint r; r.cx = cx; r.cz = cz; r.hx = dpt / 2; r.hz = w / 2; r.yaw = yaw;
				PlaceOpts po; po.margin = gas ? 3 : 1.5;
				if (!CanPlace(ctx, r, po)) continue;
				if (gas) { GasStation(ctx, r, -rx, -rz, S.district); gasDone = true; continue; }
				if (shop) {
					const auto& sh = S.shops[shopIdx++];
					const int floors = key == "dry" ? 1 : rng.Int(1, 2);
					AddOpts o; o.Tint(rng.Pick(shopTints)); o.roof = key == "pine" ? "gable" : "flat"; o.floorH = 4.2; o.name = sh.second; o.sign = sh.first; o.Front(-rx, -rz); o.district = S.district;
					AddBld(ctx, r, floors * 4.2 + 0.6, 5, o);
					for (int k = -1; k <= 1; k += 2) if (rng.Chance(0.5)) {
						ParkingSpot p; p.x = cx - rx * (dpt / 2 + 2.2) + q.tx * k * 4; p.z = cz - rz * (dpt / 2 + 2.2) + q.tz * k * 4; p.rot = yaw; p.district = S.district; p.lot = true;
						map.parkingSpots.push_back(p);
					}
				} else {
					const int floors = rng.Int(S.floors[0], S.floors[1]);
					AddOpts o; o.Tint(rng.Pick(S.tint)); o.roof = S.roof; o.floorH = 3.1; o.cell = 3.0; o.kind = "house"; o.Front(-rx, -rz); o.district = S.district;
					AddBld(ctx, r, floors * 3.1 + 0.4, S.style, o);
					if (rng.Chance(0.5)) {
						ParkingSpot p; p.x = cx - rx * (dpt / 2 + 2.6) + q.tx * (w / 2 + 2); p.z = cz - rz * (dpt / 2 + 2.6) + q.tz * (w / 2 + 2); p.rot = yaw + kPi / 2 * sd; p.district = S.district; p.driveway = true;
						map.parkingSpots.push_back(p);
					}
					if (key != "dry" && rng.Chance(0.6)) {
						const char* type = key == "pine" ? "tree" : rng.Chance(0.3) ? "palm" : "tree";
						const double rot = rng.Range(0, 6.28);
						map.props.push_back(MkProp(type, cx + q.tx * (w / 2 + 3), cz + q.tz * (w / 2 + 3), rot, NaN(), rng.Range(0.9, 1.3)));
					}
				}
			}
			if (core && rng.Chance(0.7)) {
				const double off = e.wR + 1.2;
				const double x = q.x - q.tz * off, z = q.z + q.tx * off;
				map.props.push_back(MkProp("streetlight", x, z, std::atan2(q.tz, -q.tx) + kPi, hf.Sample(x, z)));
			}
			s += rng.Range(18, 26);
		}
	}
	if (key == "hale") HarborPier(ctx);
	if (key == "mirador") LakeDocks(ctx);
	std::vector<int> ids = map.townAreas[areaIdx].nodeIds;
	for (size_t k = 0; k < ids.size(); k++) {
		WalkNode& a = walk[ids[k]];
		if (a.links.size() > 1) continue;
		int best = -1; double bd = 45;
		for (int id : ids) {
			if (id == a.id || std::find(a.links.begin(), a.links.end(), id) != a.links.end()) continue;
			const WalkNode& b = walk[id];
			const double d = Hypot(a.x - b.x, a.z - b.z);
			if (d < bd) { bd = d; best = id; }
		}
		if (best >= 0) { a.links.push_back(best); walk[best].links.push_back(a.id); }
	}
	std::vector<int> keep;
	for (int id : ids) if (!walk[id].links.empty()) keep.push_back(id);
	map.townAreas[areaIdx].nodeIds = keep;
}

// ------------------------------------------------------------------ San Aurelio's frontage
void PopulateNorthCity(Ctx& ctx) {
	CityMap& map = ctx.map; RoadNet& net = ctx.net; Heightfield& hf = ctx.hf;
	if (!map.roadInfo.hasNcity) return;
	const NCityInfo& nc = map.roadInfo.ncity;
	RNG rng(5151);
	const double y0 = NCITY.y;
	const double FC = 48;
	struct PairHash { size_t operator()(const std::pair<int64_t, int64_t>& p) const { return std::hash<int64_t>()(p.first * 1000003 + p.second); } };
	std::unordered_map<std::pair<int64_t, int64_t>, std::vector<Footprint>, PairHash> FH;
	std::unordered_map<std::pair<int64_t, int64_t>, std::vector<int>, PairHash> NH;
	auto cellK = [&](double v) { return (int64_t)std::floor(v / FC); };
	auto addFP = [&](const Footprint& r) {
		const double R = Hypot(r.hx, r.hz);
		for (int64_t gx = cellK(r.cx - R); gx <= cellK(r.cx + R); gx++) for (int64_t gz = cellK(r.cz - R); gz <= cellK(r.cz + R); gz++) FH[{ gx, gz }].push_back(r);
		ctx.footprints.push_back(r);
	};
	for (const RNode& n : net.nodes) if (n.ncity && !n.e.empty()) NH[{ cellK(n.x), cellK(n.z) }].push_back(n.id);
	auto freeRect = [&](const Footprint& r, double m) {
		const double R = Hypot(r.hx, r.hz) + m;
		Footprint g = r; g.hx += m; g.hz += m;
		for (int64_t gx = cellK(r.cx - R); gx <= cellK(r.cx + R); gx++) for (int64_t gz = cellK(r.cz - R); gz <= cellK(r.cz + R); gz++) {
			auto a = FH.find({ gx, gz });
			if (a != FH.end()) for (const Footprint& f : a->second) if (std::fabs(f.cx - r.cx) < R + f.hx + f.hz && std::fabs(f.cz - r.cz) < R + f.hx + f.hz && RectsOverlap(g, f)) return false;
			auto ns = NH.find({ gx, gz });
			if (ns != NH.end()) for (int nid : ns->second) { const RNode& n = net.nodes[nid]; const double rad = n.kind == ENode::RB ? n.rbR + 5.4 + 4.2 : n.r + 4.2; if (RectCircle(r, n.x, n.z, rad)) return false; }
		}
		const double s = std::sin(r.yaw), c = std::cos(r.yaw);
		const int L[9][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 }, { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 }, { 0, 0 } };
		for (const auto& l : L) {
			const double x = r.cx + l[0] * r.hx * c + l[1] * r.hz * s, z = r.cz - l[0] * r.hx * s + l[1] * r.hz * c;
			if (net.OnRoad(x, z, 3.9).valid()) return false;
			if (NcEdgeDist(x, z) > 20) return false;
		}
		return true;
	};
	struct BOpts { C3 tint{ 1, 1, 1 }; bool hasTint = false; double y0 = NaN(), base = NaN(); std::string roof, kind, name, sign; double floorH = 0, cell = 0; bool hasFront = false; double front[2] = { 0, 0 }; };
	auto addB = [&](const Footprint& r, double height, int style, const BOpts& o) {
		BldOpts b; b.rot = r.yaw; b.y0 = IsSet(o.y0) ? o.y0 : y0 - 0.3; if (o.hasTint) b.Tint(o.tint);
		b.seed = rng.Next(); b.roof = o.roof.empty() ? "flat" : o.roof; b.floorH = o.floorH; b.cell = o.cell; b.kind = o.kind.empty() ? "building" : o.kind;
		b.name = o.name; b.sign = o.sign; b.base = IsSet(o.base) ? o.base : y0 - 0.3;
		const int bi = map.AddBuilding(nullptr, r.cx - r.hx, r.cz - r.hz, r.cx + r.hx, r.cz + r.hz, height, style, b);
		if (bi >= 0) { Building& B = map.buildings[bi]; B.district = "aurelio"; B.hasFront = o.hasFront; B.front[0] = o.front[0]; B.front[1] = o.front[1]; B.foundation = 0.3; }
		return bi;
	};
	static const Palette warmP = { { 1, 0.95, 0.86 }, { 0.96, 0.9, 0.8 }, { 0.92, 0.86, 0.78 }, { 1, 0.9, 0.78 }, { 0.88, 0.84, 0.8 }, { 0.95, 0.93, 0.9 } };
	static const Palette glassP = { { 0.75, 0.85, 0.95 }, { 0.7, 0.8, 0.85 }, { 0.85, 0.9, 0.95 }, { 0.65, 0.75, 0.8 }, { 0.8, 0.8, 0.85 } };
	static const Palette brickP = { { 0.95, 0.75, 0.65 }, { 0.85, 0.65, 0.55 }, { 1, 0.85, 0.72 }, { 0.8, 0.7, 0.65 }, { 0.9, 0.8, 0.72 } };
	static const Palette pastelP = { { 1, 0.93, 0.82 }, { 0.93, 0.97, 0.95 }, { 1, 0.86, 0.78 }, { 0.9, 0.93, 1 }, { 1, 0.96, 0.75 }, { 0.95, 0.85, 0.9 }, { 0.86, 0.95, 0.86 } };
	auto warm = [&]() { return rng.Pick(warmP); };
	auto glass = [&]() { return rng.Pick(glassP); };
	auto brick = [&]() { return rng.Pick(brickP); };
	auto pastel = [&]() { return rng.Pick(pastelP); };
	auto rOf = [](double x, double z) { return Hypot(x - NCITY.x, z - NCITY.z); };
	auto tinted = [](const C3& t) { BOpts o; o.tint = t; o.hasTint = true; return o; };

	// ---- landmarks first
	Footprint plaza; plaza.cx = NCITY.x; plaza.cz = NCITY.z; plaza.hx = 26; plaza.hz = 26;
	addFP(plaza);
	map.props.push_back(MkProp("fountain", NCITY.x, NCITY.z, 0, y0 + 0.3));
	for (int k = 0; k < 8; k++) { const double a = k / 8.0 * kTau; map.props.push_back(MkProp("palm", NCITY.x + std::cos(a) * 12.5, NCITY.z + std::sin(a) * 12.5, a, y0 + 0.3, 1.1)); }
	map.ncSupers = NcSuperblocks();
	for (const NcSuper& sb : map.ncSupers) {
		const double yaw = std::atan2(std::cos(sb.th), -std::sin(sb.th));
		const double hz = Min(80, sb.span * 0.36), hx = Min(58, sb.depth * 0.3);
		Footprint r; r.cx = sb.x; r.cz = sb.z; r.hx = hx; r.hz = hz; r.yaw = yaw;
		addFP(r);
		if (sb.kind == "park") {
			map.padSurfaces.push_back({ sb.x, sb.z, hx + 2, hz + 2, yaw, y0 + 0.05, "grass" });
			map.padSurfaces.push_back({ sb.x, sb.z, 2.2, hz + 2, yaw, y0 + 0.07, "path" });
			map.padSurfaces.push_back({ sb.x, sb.z, hx + 2, 2.2, yaw, y0 + 0.07, "path" });
			map.props.push_back(MkProp("fountain", sb.x, sb.z, 0, y0 + 0.1));
			const double s = std::sin(yaw), c = std::cos(yaw);
			for (int k = 0; k < 34; k++) {
				const double lx = rng.Range(-hx, hx), lz = rng.Range(-hz, hz);
				if (std::fabs(lx) < 5 || std::fabs(lz) < 5) continue;
				const char* type = rng.Chance(0.2) ? "palm" : "tree";
				const double rot = rng.Range(0, 6.28);
				map.props.push_back(MkProp(type, sb.x + lx * c + lz * s, sb.z - lx * s + lz * c, rot, y0 + 0.05, rng.Range(0.9, 1.4)));
			}
			for (int k = -3; k <= 3; k++) if (k) for (int sd : { -1, 1 }) {
				const double lx = sd * 3.4, lz = k * hz / 4;
				map.props.push_back(MkProp("bench", sb.x + lx * c + lz * s, sb.z - lx * s + lz * c, yaw + (sd > 0 ? -kPi / 2 : kPi / 2), y0 + 0.07));
			}
			Landmark L; L.x = sb.x; L.z = sb.z; L.name = "Bayview Park"; map.landmarks["aurPark"] = L;
		} else {
			map.padSurfaces.push_back({ sb.x, sb.z, hx + 3, hz + 3, yaw, y0 + 0.05, "plaza" });
			Footprint a = r; a.hx = hx - 6; a.hz = hz - 8;
			BOpts o = tinted({ 0.85, 0.87, 0.92 }); o.roof = "ac"; o.name = "Aurelio Arena"; o.floorH = 6;
			addB(a, 24, 0, o);
			Landmark L; L.x = sb.x; L.z = sb.z; L.name = "Aurelio Arena"; map.landmarks["aurArena"] = L;
		}
	}

	// ---- frontage along every street
	static const std::pair<const char*, const char*> SIGNS[] = { { "AURELIO HOTEL", "Hotel Aurelio" }, { "CAFE ROMA", "Cafe Roma" }, { "BAYVIEW DINER", "Bayview Diner" }, { "PHARMACY", "Mission Pharmacy" },
		{ "BOOKS", "Old Harbor Books" }, { "PIZZA", "Nonna's Pizza" }, { "BANK", "Pacific Trust Bank" }, { "CINEMA", "The Aurelio Cinema" }, { "MARKET", "Northgate Market" },
		{ "BAR", "The Lantern Bar" }, { "TATTOO", "Anchor Tattoo" }, { "BAKERY", "Panaderia Sol" }, { "GYM", "Iron Bay Gym" }, { "JEWELER", "Castell Jewelers" } };
	const size_t nSigns = sizeof(SIGNS) / sizeof(SIGNS[0]);
	size_t signI = 0; int towers = 0;
	bool cathedralDone = false;
	for (int eid : nc.edges) {
		const REdge& e = net.edges[eid];
		const RNode &na = net.nodes[e.a], &nb = net.nodes[e.b];
		auto clr = [](const RNode& n) { return n.kind == ENode::RB ? n.rbR + 10 : n.r + 3; };
		const double sA = clr(na), sB = e.len - clr(nb);
		for (int side : { -1, 1 }) {
			double s = sA + rng.Range(0, 3);
			while (s < sB - 6) {
				EdgePoint q = net.At(e, s);
				const double r0 = rOf(q.x, q.z);
				const bool core = r0 < 205, inner = !core && r0 < 360;
				const double w0 = core ? rng.Range(20, 34) : inner ? rng.Range(14, 26) : rng.Range(10, 20);
				const double d0 = core ? rng.Range(26, 40) : inner ? rng.Range(18, 30) : rng.Range(12, 22);
				bool found = false; Footprint r; double w = 0, rx = 0, rz = 0;
				const double tries[5][2] = { { 1, 1 }, { 0.62, 1 }, { 1, 0.55 }, { 0.62, 0.55 }, { 0.45, 0.4 } };
				for (const auto& kk : tries) {
					w = w0 * kk[0]; const double d = d0 * kk[1];
					if (s + w > sB) continue;
					q = net.At(e, s + w / 2);
					const double tx = q.tx, tz = q.tz;
					rx = -tz * side; rz = tx * side;
					const double off = Max(e.wL, e.wR) + 4.3 + d / 2 + (w * w) / (8 * 110);
					Footprint cand; cand.cx = q.x + rx * off; cand.cz = q.z + rz * off; cand.hx = d / 2; cand.hz = w / 2; cand.yaw = std::atan2(tx, tz);
					if (freeRect(cand, 0.6)) { r = cand; found = true; break; }
				}
				if (!found) { s += 4; continue; }
				BOpts fo; fo.hasFront = true; fo.front[0] = -rx; fo.front[1] = -rz;
				const double rc = rOf(r.cx, r.cz);
				if (core) {
					const double H = Lerp(175, 40, Clamp(rc / 215, 0, 1)) * rng.Range(0.55, 1.1) + (towers == 2 ? 70 : 0);
					towers++;
					const int st = rng.Weighted<int>({ { 1, 5 }, { 0, 3 }, { 6, 2 } });
					const C3 tint = st == 1 ? glass() : warm();
					const double podH = rng.Range(9, 15);
					if (H < 50 || rng.Chance(0.3)) {
						BOpts o = fo; o.tint = tint; o.hasTint = true; o.roof = rng.Chance(0.4) ? "antenna" : "helipad"; o.floorH = st == 1 ? 3.8 : 3.4;
						addB(r, H, st, o);
					} else {
						const int pst = rng.Chance(0.5) ? 5 : st;
						{ BOpts o = fo; o.tint = tint; o.hasTint = true; o.floorH = 4.2; addB(r, podH, pst, o); }
						const double ins = Min(r.hx, r.hz) * rng.Range(0.18, 0.3);
						Footprint t1 = r; t1.hx = r.hx - ins; t1.hz = r.hz - ins;
						const double top = y0 - 0.3 + podH;
						if (rng.Chance(0.5)) {
							const double h1 = (H - podH) * rng.Range(0.6, 0.8), in2 = Min(t1.hx, t1.hz) * 0.2;
							{ BOpts o = tinted(tint); o.y0 = top; o.base = y0 - 0.3; o.roof = "flat"; addB(t1, h1, st, o); }
							Footprint t2 = t1; t2.hx = t1.hx - in2; t2.hz = t1.hz - in2;
							{ BOpts o = tinted(tint); o.y0 = top + h1; o.base = y0 - 0.3; o.roof = rng.Chance(0.5) ? "spire" : "helipad"; addB(t2, H - podH - h1, st, o); }
						} else {
							BOpts o = tinted(tint); o.y0 = top; o.base = y0 - 0.3; o.roof = rng.Chance(0.5) ? "antenna" : "helipad";
							addB(t1, H - podH, st, o);
						}
					}
				} else if (inner) {
					const double th = std::fmod(std::fmod(std::atan2(r.cz - NCITY.z, r.cx - NCITY.x), kTau) + kTau, kTau);
					if (!cathedralDone && th > 2.6 && th < 3.7 && w > 20) {
						cathedralDone = true;
						BOpts o = fo; o.tint = { 0.93, 0.9, 0.84 }; o.hasTint = true; o.roof = "spire"; o.name = "Cathedral of San Aurelio"; o.floorH = 6.5;
						addB(r, 26, 6, o);
						Landmark L; L.x = r.cx; L.z = r.cz; L.name = "Cathedral of San Aurelio"; map.landmarks["aurCathedral"] = L;
					} else {
						const bool shop = rng.Chance(0.35) && signI < nSigns;
						const int st = shop ? 5 : rng.Weighted<int>({ { 2, 4 }, { 6, 2 }, { 0, 2 }, { 5, 2 }, { 3, 1 } });
						const int floors = rng.Int(3, 9);
						const double fh = st == 5 ? 4.2 : 3.4;
						const C3 tint = st == 2 ? brick() : warm();
						if (shop) {
							const auto& sg = SIGNS[signI++];
							BOpts o = fo; o.tint = tint; o.hasTint = true; o.sign = sg.first; o.name = sg.second; o.floorH = fh; o.roof = "ac";
							addB(r, floors * fh + 0.6, st, o);
						} else {
							BOpts o = fo; o.tint = tint; o.hasTint = true; o.floorH = fh; o.roof = rng.Chance(0.35) ? "ac" : "flat";
							addB(r, floors * fh + (st == 5 ? 0.6 : 0), st, o);
						}
					}
				} else {
					const bool house = rng.Chance(0.3) && e.type == ERoad::Road;
					if (house) {
						const double h = rng.Int(1, 2) * 3.1 + 0.4;
						BOpts o = fo; o.tint = pastel(); o.hasTint = true; o.roof = "gable"; o.floorH = 3.1; o.cell = 3.0; o.kind = "house";
						addB(r, h, 3, o);
					} else {
						const bool shop = rng.Chance(0.3) && signI < nSigns;
						const int st = shop ? 5 : rng.Weighted<int>({ { 3, 4 }, { 5, 3 }, { 2, 2 } });
						const double fh = st == 5 ? 4.2 : 3.3;
						const int floors = rng.Int(2, 5);
						if (shop) {
							const auto& sg = SIGNS[signI++];
							BOpts o = fo; o.tint = pastel(); o.hasTint = true; o.sign = sg.first; o.name = sg.second; o.floorH = fh;
							addB(r, floors * fh + 0.6, 5, o);
						} else {
							BOpts o = fo; o.tint = st == 2 ? brick() : pastel(); o.hasTint = true; o.floorH = fh; o.roof = rng.Chance(0.4) ? "ac" : "flat";
							addB(r, floors * fh, st, o);
						}
					}
				}
				addFP(r);
				s += w + (rng.Chance(core ? 0.1 : 0.25) ? rng.Range(2, 6) : 0.4);
			}
		}
	}
	// ---- back lots
	const double B = 16;
	for (int64_t gx = (int64_t)std::floor((NCITY.x - 620) / B); gx <= (int64_t)std::ceil((NCITY.x + 620) / B); gx++) for (int64_t gz = (int64_t)std::floor((NCITY.z - 620) / B); gz <= (int64_t)std::ceil((NCITY.z + 620) / B); gz++) {
		const double x = gx * B + rng.Range(-3, 3);
		const double z = gz * B + rng.Range(-3, 3);
		if (NcEdgeDist(x, z) > -10) continue;
		const EdgeHit c = net.Closest(x, z, [](const REdge& e) { return e.ncity; }, 80);
		if (!c.valid() || c.d < 22) continue;
		const EdgePoint q = net.At(net.edges[c.e], c.s);
		const double yaw = std::atan2(q.tx, q.tz);
		const double rc = rOf(x, z);
		const double k = rng.Next();
		const double sz = rc < 175 ? rng.Range(10, 16) : rng.Range(8, 13);
		Footprint r; r.cx = x; r.cz = z; r.hx = sz; r.hz = sz * rng.Range(0.8, 1.3); r.yaw = yaw;
		if (!freeRect(r, 1.5)) continue;
		if (k < 0.55) {
			const int floors = rc < 175 ? rng.Int(5, 12) : rc < 335 ? rng.Int(2, 5) : rng.Int(1, 3);
			const int st = rng.Pick(std::vector<int>{ 0, 2, 3, 2 });
			BOpts o; o.tint = rng.Chance(0.5) ? warm() : brick(); o.hasTint = true; o.roof = rng.Chance(0.5) ? "ac" : "flat"; o.floorH = 3.4;
			addB(r, floors * 3.4, st, o);
		} else if (k < 0.82) {
			map.padSurfaces.push_back({ x, z, r.hx, r.hz, yaw, y0 + 0.04, "asphalt" });
			const double s = std::sin(yaw), cc = std::cos(yaw);
			for (int qq = -1; qq <= 1; qq += 2) if (rng.Chance(0.7)) { ParkingSpot p; p.x = x + qq * r.hx * 0.45 * cc; p.z = z - qq * r.hx * 0.45 * s; p.rot = yaw; p.district = "aurelio"; p.lot = true; map.parkingSpots.push_back(p); }
		} else {
			map.padSurfaces.push_back({ x, z, r.hx, r.hz, yaw, y0 + 0.04, "grass" });
			for (int qq = 0; qq < 3; qq++) {
				const double px = x + rng.Range(-r.hx, r.hx) * 0.7;
				const double pz = z + rng.Range(-r.hz, r.hz) * 0.7;
				const double rot = rng.Range(0, 6.28);
				map.props.push_back(MkProp("tree", px, pz, rot, y0 + 0.04, rng.Range(0.8, 1.2)));
			}
			if (rng.Chance(0.5)) map.props.push_back(MkProp("bench", x, z, yaw, y0 + 0.04));
		}
		addFP(r);
	}

	// ---- pavements: walk graph, lamps, trees, bins, hydrants, bus stops
	std::vector<WalkNode>& walk = map.walkNodes;
	std::vector<TownArea> areas; std::map<std::pair<int64_t, int64_t>, int> areaIdx;
	auto cellOf = [&](double x, double z) -> TownArea& {
		const std::pair<int64_t, int64_t> k{ (int64_t)std::floor(x / 140), (int64_t)std::floor(z / 140) };
		auto it = areaIdx.find(k);
		if (it == areaIdx.end()) {
			TownArea a; a.x = (k.first + 0.5) * 140; a.z = (k.second + 0.5) * 140;
			const NcDistrict nd = NcDistrictAt(a.x, a.z);
			a.district = nd.key ? nd.key : "aurelio"; a.name = NCITY.name; a.r = 100; a.town = true;
			areas.push_back(a);
			it = areaIdx.emplace(k, (int)areas.size() - 1).first;
		}
		return areas[it->second];
	};
	struct EndRef { int wid; int e; };
	std::vector<std::pair<int, std::vector<EndRef>>> endNodes;
	auto pushEnd = [&](int nid, int wid, int e) {
		for (auto& kv : endNodes) if (kv.first == nid) { kv.second.push_back({ wid, e }); return; }
		endNodes.push_back({ nid, { { wid, e } } });
	};
	const double yWalk = y0 + 0.12;
	for (int eid : nc.edges) {
		const REdge& e = net.edges[eid];
		const RNode &na = net.nodes[e.a], &nb = net.nodes[e.b];
		auto clr = [](const RNode& n) { return n.kind == ENode::RB ? n.rbR + 6.5 : n.r + 1; };
		const double s0 = clr(na) + 1, s1 = e.len - clr(nb) - 1;
		if (s1 - s0 < 4) continue;
		const double off = Max(e.wL, e.wR) + e.walk * 0.55;
		const int n = (int)Max(1, JsRound((s1 - s0) / 18));
		std::vector<int> lists[2];
		for (int qn = 0; qn <= n; qn++) {
			const double s = s0 + (s1 - s0) * qn / n;
			const EdgePoint q = net.At(e, s);
			for (int li = 0; li < 2; li++) {
				const int side = li == 0 ? -1 : 1;
				const double x = q.x - q.tz * off * side, z = q.z + q.tx * off * side;
				WalkNode wn; wn.id = (int)walk.size(); wn.x = x; wn.z = z; wn.town = "aurelio"; wn.y = yWalk;
				walk.push_back(wn);
				cellOf(x, z).nodeIds.push_back(wn.id);
				std::vector<int>& L = lists[li];
				if (!L.empty()) { const int p = L.back(); walk[p].links.push_back(wn.id); walk[wn.id].links.push_back(p); }
				L.push_back(wn.id);
			}
		}
		for (size_t qi : { (size_t)0, lists[0].size() - 1 }) {
			const int a = lists[0][qi], b = lists[1][qi];
			walk[a].links.push_back(b); walk[b].links.push_back(a); walk[a].cross = { b }; walk[b].cross = { a };
		}
		pushEnd(e.a, lists[0].front(), eid); pushEnd(e.a, lists[1].front(), eid);
		pushEnd(e.b, lists[0].back(), eid); pushEnd(e.b, lists[1].back(), eid);
		const double lampStep = e.type == ERoad::Avenue ? 30 : 38;
		int flip = 1;
		const double w = Max(e.wL, e.wR);
		for (double s = s0 + 4; s < s1 - 2; s += lampStep) {
			EdgePoint q = net.At(e, s);
			flip = -flip;
			const int side = e.type == ERoad::Avenue ? flip : 1;
			const double lx = q.x - q.tz * (w + 0.55) * side, lz = q.z + q.tx * (w + 0.55) * side;
			map.props.push_back(MkProp("streetlight", lx, lz, std::atan2(side * q.tz, -side * q.tx), yWalk));
			if (e.type == ERoad::Avenue) for (int sd : { -1, 1 }) {
				q = net.At(e, s + lampStep / 2);
				const double o = w + e.walk - 1.0;
				const char* type = rOf(q.x, q.z) > 380 ? "palm" : "tree";
				const double rot = rng.Range(0, 6.28);
				Prop p = MkProp(type, q.x - q.tz * o * sd, q.z + q.tx * o * sd, rot, yWalk, rng.Range(0.85, 1.15));
				p.street = true;
				map.props.push_back(p);
			}
		}
		auto put = [&](const char* type, double s, double o, bool face) {
			const EdgePoint q = net.At(e, s);
			const int sd = rng.Chance(0.5) ? 1 : -1;
			map.props.push_back(MkProp(type, q.x - q.tz * (w + o) * sd, q.z + q.tx * (w + o) * sd, face ? std::atan2(sd * q.tz, -sd * q.tx) + kPi : 0, yWalk));
		};
		if (rng.Chance(0.6)) { const double s = rng.Range(s0 + 3, s1 - 3); put("hydrant", s, 0.8, false); }
		if (rng.Chance(0.55)) { const double s = rng.Range(s0 + 3, s1 - 3); put("trashcan", s, 1.0, false); }
		if (e.type == ERoad::Avenue && e.len > 90 && rng.Chance(0.45)) put("busstop", (s0 + s1) / 2, e.walk - 1.1, true);
		if (rng.Chance(0.25)) { const double s = rng.Range(s0 + 3, s1 - 3); put("bench", s, e.walk - 0.8, true); }
	}
	for (auto& kv : endNodes) {
		const std::vector<EndRef>& list = kv.second;
		for (const EndRef& a : list) {
			const EndRef* best = nullptr; double bd = 30;
			for (const EndRef& b : list) {
				if (b.e == a.e) continue;
				const double d = Hypot(walk[a.wid].x - walk[b.wid].x, walk[a.wid].z - walk[b.wid].z);
				if (d < bd) { bd = d; best = &b; }
			}
			if (best && std::find(walk[a.wid].links.begin(), walk[a.wid].links.end(), best->wid) == walk[a.wid].links.end()) { walk[a.wid].links.push_back(best->wid); walk[best->wid].links.push_back(a.wid); }
		}
	}
	for (TownArea& a : areas) {
		std::vector<int> keep; for (int id : a.nodeIds) if (!walk[id].links.empty()) keep.push_back(id);
		a.nodeIds = keep;
		if (!a.nodeIds.empty()) map.townAreas.push_back(a);
	}
	{ Landmark L; L.x = NCITY.x + 30; L.z = NCITY.z + 32; L.name = NCITY.name; map.landmarks["aurelio"] = L; }
	auto he = nc.ends.find("harbor");
	if (he != nc.ends.end()) { Landmark L; L.x = net.nodes[he->second].x + 14; L.z = net.nodes[he->second].z - 60; L.name = "Harbor Drive"; map.landmarks["aurHarbor"] = L; }
	for (const RoadBuilt* rb : { nc.harborN, nc.harborS }) {
		if (!rb) continue;
		for (int eid : rb->edges) {
			const REdge& e = net.edges[eid];
			for (double s = 12; s < e.len - 8; s += 24) {
				const EdgePoint q = net.At(e, s);
				for (int sd : { -1, 1 }) {
					const double o = Max(e.wL, e.wR) + 2.2, x = q.x - q.tz * o * sd, z = q.z + q.tx * o * sd;
					const double rot = rng.Range(0, 6.28);
					map.props.push_back(MkProp("palm", x, z, rot, hf.Sample(x, z), rng.Range(0.95, 1.25)));
				}
				if (((int)(s / 24)) % 2 == 0) {
					const double o = Max(e.wL, e.wR) + 1.0, x = q.x + q.tz * o, z = q.z - q.tx * o;
					map.props.push_back(MkProp("streetlight", x, z, std::atan2(-q.tz, q.tx), hf.Sample(x, z)));
				}
			}
		}
	}
}

// ------------------------------------------------------------------ farms
void Farms(Ctx& ctx) {
	CityMap& map = ctx.map; RNG& rng = ctx.rng; RoadNet& net = ctx.net; Heightfield& hf = ctx.hf;
	struct Cand { int e; double s, x, z, tx, tz; bool used = false; };
	std::vector<Cand> cands;
	for (const REdge& e : net.edges) {
		if (e.removed || e.hasGrid || e.type == ERoad::Freeway || e.type == ERoad::Ramp) continue;
		for (double s = 40; s < e.len - 40; s += 90) {
			const EdgePoint p = net.At(e, s);
			if (FarmMask(p.x, p.z) < 0.4 && !(e.type == ERoad::Dirt && RegionWeights(p.x, p.z).country > 0.6)) continue;
			cands.push_back({ e.id, s, p.x, p.z, p.tx, p.tz });
		}
	}
	static const Palette houseP = { { 1, 0.97, 0.9 }, { 0.95, 0.92, 0.85 }, { 0.85, 0.9, 0.95 } };
	int made = 0;
	for (int i = 0; i < (int)cands.size() && made < 14; i++) {
		Cand& c = cands[(size_t)std::floor(Hash2(i, 55) * cands.size())];
		if (c.used) continue;
		c.used = true;
		const REdge& e = net.edges[c.e];
		const int side = Hash2(i, 9) < 0.5 ? 1 : -1;
		const double rx = -c.tz * side, rz = c.tx * side;
		const double yaw = std::atan2(c.tx, c.tz);
		const double off = side < 0 ? e.wL : e.wR;
		Footprint house; house.cx = c.x + rx * (off + 14); house.cz = c.z + rz * (off + 14); house.hx = 5.5; house.hz = 7; house.yaw = yaw;
		PlaceOpts hp; hp.margin = 3;
		if (!CanPlace(ctx, house, hp)) continue;
		{
			const double h = rng.Chance(0.5) ? 6.6 : 3.5;
			AddOpts o; o.Tint(rng.Pick(houseP)); o.roof = "gable"; o.floorH = 3.1; o.cell = 3; o.kind = "house"; o.Front(-rx, -rz);
			AddBld(ctx, house, h, 3, o);
		}
		Footprint barn; barn.cx = house.cx + rx * 30 + c.tx * 16; barn.cz = house.cz + rz * 30 + c.tz * 16; barn.hx = 9; barn.hz = 13; barn.yaw = yaw;
		PlaceOpts bp; bp.margin = 2; bp.maxDrop = 5;
		if (CanPlace(ctx, barn, bp)) { AddOpts o; o.Tint({ 0.78, 0.22, 0.16 }); o.roof = "gable"; o.floorH = 7.5; o.cell = 5; o.kind = "barn"; AddBld(ctx, barn, 7.5, 4, o); }
		const double sx = house.cx + rx * 30 - c.tx * 6, sz = house.cz + rz * 30 - c.tz * 6;
		if (!net.OnRoad(sx, sz, 3).valid()) map.props.push_back(MkProp("silo", sx, sz, 0, hf.Sample(sx, sz)));
		for (int k = 0; k < 5; k++) {
			const double a1 = rng.Range(18, 45), a2 = rng.Range(-30, 30);
			const double hx = house.cx + rx * a1 + c.tx * a2;
			const double b1 = rng.Range(18, 45), b2 = rng.Range(-30, 30);
			const double hz = house.cz + rz * b1 + c.tz * b2;
			if (!net.OnRoad(hx, hz, 3).valid()) { const double rot = rng.Range(0, 6.28); map.props.push_back(MkProp("haybale", hx, hz, rot, hf.Sample(hx, hz))); }
		}
		for (int k = -3; k <= 3; k++) {
			const double fx = c.x + rx * (off + 3.5) + c.tx * k * 12, fz = c.z + rz * (off + 3.5) + c.tz * k * 12;
			if (std::abs(k) <= 0 || net.OnRoad(fx, fz, 1).valid()) continue;
			const double y = hf.Sample(fx, fz);
			Fence f; f.rot = yaw; f.cx = fx; f.cz = fz; f.hx = 0.06; f.hz = 6; f.h = 1.2; f.type = "wood"; f.y = y - 0.15; f.x0 = fx; f.z0 = fz; f.x1 = fx; f.z1 = fz;
			map.fences.push_back(f);
		}
		ParkingSpot p; p.x = house.cx - rx * 9 + c.tx * 9; p.z = house.cz - rz * 9 + c.tz * 9; p.rot = yaw; p.district = "country"; p.driveway = true; p.rural = true;
		map.parkingSpots.push_back(p);
		made++;
	}
}

void RailStations(Ctx& ctx) {
	CityMap& map = ctx.map; Heightfield& hf = ctx.hf;
	if (!map.roadInfo.hasRail) return;
	const RailInfo& rail = map.roadInfo.rail;
	map.stations.clear();
	for (const RailStation& st : rail.stations) {
		const double rx = -st.tz, rz = st.tx;
		const bool big = st.key == "union";
		const double hx = big ? 9 : 6, hz = big ? 26 : 11;
		const double off = 6.75 + 3 + hx;
		Footprint r; r.cx = st.x + rx * off; r.cz = st.z + rz * off; r.hx = hx; r.hz = hz; r.yaw = st.yaw;
		PlaceOpts po; po.margin = 0.5; po.maxDrop = 6; po.roadMargin = 0.5;
		if (CanPlace(ctx, r, po)) {
			std::string up = st.name; for (char& ch : up) ch = (char)std::toupper((unsigned char)ch);
			AddOpts o; o.Tint(big ? C3{ 0.9, 0.84, 0.72 } : C3{ 0.85, 0.72, 0.58 }); o.roof = big ? "flat" : "gable"; o.floorH = big ? 6 : 6.5; o.name = st.name; o.sign = up; o.Front(-rx, -rz); o.district = "station";
			AddBld(ctx, r, big ? 12 : 6.5, big ? 0 : 5, o);
		}
		for (int k = -2; k <= 2; k++) { ParkingSpot p; p.x = r.cx + rx * (hx + 6) + st.tx * k * 4; p.z = r.cz + rz * (hx + 6) + st.tz * k * 4; p.rot = st.yaw + kPi / 2; p.district = "station"; p.lot = true; map.parkingSpots.push_back(p); }
		Landmark lm; lm.name = st.name; lm.x = st.x + rx * 4.25; lm.z = st.z + rz * 4.25; lm.y = st.y + 1.05; lm.vals["s"] = st.s; lm.vals["rot"] = st.yaw;
		map.stations.push_back(lm);
		map.landmarks["station_" + st.key] = lm;
	}
	for (const RailCrossing& c : rail.crossings) {
		if (c.kind != "level") continue;
		const REdge& e = ctx.net.edges[c.edge];
		int best = 0; double bd = kInf;
		for (int i = 0; i < e.n; i++) { const double d = Hypot(e.X(i) - c.x, e.Z(i) - c.z); if (d < bd) { bd = d; best = i; } }
		const EdgePoint t = ctx.net.At(e, e.cum[best]);
		for (int side : { -1, 1 }) {
			const double off = (side < 0 ? e.wL : e.wR) + 1.4;
			const double x = c.x - t.tz * off * side + t.tx * 5 * side, z = c.z + t.tx * off * side + t.tz * 5 * side;
			map.props.push_back(MkProp("crossbuck", x, z, std::atan2(t.tx, t.tz) + (side > 0 ? kPi : 0), hf.Sample(x, z)));
		}
	}
}

void Airfield(Ctx& ctx) {
	CityMap& map = ctx.map;
	const AirfieldDef& A = AIRFIELD;
	const double y = ctx.hf.Sample(A.x, A.z);
	map.padSurfaces.push_back({ A.x, A.z, 15, A.len / 2, 0, y + 0.05, "runway" });
	map.padSurfaces.push_back({ A.x - 34, A.z - 150, 18, 26, 0, y + 0.05, "apron" });
	Footprint hg; hg.cx = A.x - 45; hg.cz = A.z - 150; hg.hx = 11; hg.hz = 14; hg.yaw = kPi / 2;
	PlaceOpts po; po.margin = 0.5; po.maxDrop = 6;
	if (CanPlace(ctx, hg, po)) { AddOpts o; o.Tint({ 0.75, 0.78, 0.8 }); o.roof = "flat"; o.floorH = 8; o.cell = 6; o.kind = "hangar"; o.name = A.name; o.sign = "AIRFIELD"; AddBld(ctx, hg, 8, 4, o); }
	map.props.push_back(MkProp("windsock", A.x + 24, A.z - A.len / 2 + 40, 0, y));
	map.fixedVehicles.push_back({ "skipper", A.x - 22, A.z - 150, kPi, y + 0.05, 180 });
	Landmark L; L.x = A.x; L.z = A.z; L.y = y; map.landmarks["airfield"] = L;
}

void BaseFill(Ctx& ctx) {
	CityMap& map = ctx.map; Heightfield& hf = ctx.hf;
	const BaseDef& Bd = BASE;
	const double y = hf.Sample((Bd.minX + Bd.maxX) / 2, (Bd.minZ + Bd.maxZ) / 2);
	auto P = [&](double x, double z, double hx, double hz, double yaw = 0) { Footprint f; f.cx = x; f.cz = z; f.hx = hx; f.hz = hz; f.yaw = yaw; f.y0 = y - 0.25; f.yTop = y; return f; };
	auto bld = [&](const Footprint& r, double h, int style, AddOpts o) { o.district = "base"; return AddBld(ctx, r, h, style, o); };
	const double runZ = -4520, runX0 = -5230, runX1 = -3950;
	map.padSurfaces.push_back({ (runX0 + runX1) / 2, runZ, 26, (runX1 - runX0) / 2, kPi / 2, y + 0.05, "runway" });
	map.padSurfaces.push_back({ -4575, -4400, 12, 575, kPi / 2, y + 0.045, "taxiway" });
	for (double x : { -5140.0, -4575.0, -4010.0 }) map.padSurfaces.push_back({ x, -4460, 12, 48, 0, y + 0.042, "taxiway" });
	map.padSurfaces.push_back({ -4650, -4270, 350, 118, 0, y + 0.04, "apron" });
	const double hangarXs[] = { -4900, -4780, -4660, -4540, -4420 };
	for (int i = 0; i < 5; i++) { AddOpts o; o.Tint({ 0.62, 0.66, 0.6 }); o.roof = "flat"; o.floorH = 17; o.cell = 7; o.kind = "hangar"; if (i == 0) o.name = "Fort Carver"; if (i == 2) o.sign = "FORT CARVER"; bld(P(hangarXs[i], -4125, 26, 22), 17, 4, o); }
	{ AddOpts o; o.Tint({ 0.85, 0.85, 0.8 }); o.roof = "flat"; o.floorH = 3.6; bld(P(-4300, -4180, 6, 6), 26, 0, o); }
	{ BldOpts o; o.y0 = y + 26; o.Tint(0.4, 0.6, 0.7); o.seed = 0.4; o.roof = "antenna"; const int bi = map.AddBuilding(nullptr, -4308, -4188, -4292, -4172, 5, 1, o); map.buildings[bi].district = "base"; }
	for (int k = 0; k < 4; k++) { AddOpts o; o.Tint({ 0.78, 0.72, 0.6 }); o.roof = "flat"; o.floorH = 3.5; bld(P(-5150 + k * 90, -3960, 36, 9), 7, 2, o); }
	{ AddOpts o; o.Tint({ 0.8, 0.8, 0.74 }); o.roof = "antenna"; o.floorH = 3.4; o.name = "Fort Carver HQ"; bld(P(-4200, -3960, 26, 14), 10, 0, o); }
	{ AddOpts o; o.Tint({ 0.55, 0.6, 0.5 }); o.roof = "flat"; o.floorH = 8; o.cell = 6; o.kind = "hangar"; bld(P(-4020, -3950, 30, 10), 8, 4, o); }
	map.padSurfaces.push_back({ -4070, -4150, 110, 60, 0, y + 0.04, "dirtpad" });
	for (double x : { -4720.0, -4640.0, -4560.0 }) map.padSurfaces.push_back({ x, -4020, 13, 13, 0, y + 0.05, "helipad" });
	for (int k = 0; k < 4; k++) map.props.push_back(MkProp("fueltank", -5230 + (k % 2) * 22, -4230 + (k / 2) * 22, 0, y));
	map.props.push_back(MkProp("radar", -5250, -3930, 0, y));
	auto fence = [&](double x0, double z0, double x1, double z1) {
		const double len = Hypot(x1 - x0, z1 - z0);
		const int n = (int)std::ceil(len / 24);
		for (int k = 0; k < n; k++) {
			const double ax = Lerp(x0, x1, (double)k / n), az = Lerp(z0, z1, (double)k / n), bx = Lerp(x0, x1, (double)(k + 1) / n), bz = Lerp(z0, z1, (double)(k + 1) / n);
			const double cx = (ax + bx) / 2, cz = (az + bz) / 2;
			if (x0 == x1 && std::fabs(cz - Bd.gateZ) < 16 && x0 == Bd.maxX) continue;
			Fence f; f.x0 = Min(ax, bx) - (x0 == x1 ? 0.06 : 0); f.z0 = Min(az, bz) - (z0 == z1 ? 0.06 : 0); f.x1 = Max(ax, bx) + (x0 == x1 ? 0.06 : 0); f.z1 = Max(az, bz) + (z0 == z1 ? 0.06 : 0);
			f.h = 4; f.type = "chain"; f.y = hf.Sample(cx, cz) - 0.15; f.tall = true;
			map.fences.push_back(f);
		}
	};
	fence(Bd.minX, Bd.minZ, Bd.maxX, Bd.minZ); fence(Bd.minX, Bd.maxZ, Bd.maxX, Bd.maxZ); fence(Bd.minX, Bd.minZ, Bd.minX, Bd.maxZ); fence(Bd.maxX, Bd.minZ, Bd.maxX, Bd.maxZ);
	const double towers[6][2] = { { Bd.minX + 6, Bd.minZ + 6 }, { Bd.maxX - 6, Bd.minZ + 6 }, { Bd.minX + 6, Bd.maxZ - 6 }, { Bd.maxX - 6, Bd.maxZ - 6 }, { Bd.maxX - 8, Bd.gateZ - 24 }, { Bd.maxX - 8, Bd.gateZ + 24 } };
	for (const auto& t : towers) {
		const double x = t[0], z = t[1];
		BldOpts o; o.y0 = y + 7; o.Tint(0.5, 0.55, 0.45); o.seed = 0.3; o.roof = "flat"; o.noCollide = true;
		const int bi = map.AddBuilding(nullptr, x - 2.5, z - 2.5, x + 2.5, z + 2.5, 3.2, 4, o);
		map.buildings[bi].district = "base";
		const double os[4][2] = { { -2, -2 }, { 2, -2 }, { 2, 2 }, { -2, 2 } };
		for (const auto& q : os) map.props.push_back(MkProp("stilt", x + q[0], z + q[1], 0, y));
	}
	map.props.push_back(MkProp("boothbar", Bd.maxX + 6, Bd.gateZ + 8, 0, y));
	map.padSurfaces.push_back({ Bd.maxX - 20, Bd.gateZ, 14, 30, kPi / 2, y + 0.03, "concrete" });
	for (int k = 0; k < 6; k++) map.props.push_back(MkProp("sandbags", -4130 + k * 22, -4225, 0, y));
	std::vector<FixedVehicle>& FV = map.fixedVehicles;
	FV.push_back({ "raptor", -4900, -4230, kPi, y, 240 });
	FV.push_back({ "raptor", -4780, -4230, kPi, y, 240 });
	FV.push_back({ "hercules", -4620, -4250, kPi, y, 300 });
	FV.push_back({ "warhawk", -4720, -4020, kPi, y + 0.05, 240 });
	FV.push_back({ "skylark", -4640, -4020, kPi, y + 0.05, 200 });
	FV.push_back({ "mammoth", -4120, -4150, -kPi / 2, y, 240 });
	FV.push_back({ "mammoth", -4060, -4150, -kPi / 2, y, 240 });
	FV.push_back({ "ranger", -4000, -4130, -kPi / 2, y, 120 });
	FV.push_back({ "ranger", -4000, -4170, -kPi / 2, y, 120 });
	FV.push_back({ "barracks", -4180, -4150, -kPi / 2, y, 160 });
	Landmark L; L.x = (Bd.minX + Bd.maxX) / 2; L.z = (Bd.minZ + Bd.maxZ) / 2; L.y = y; L.pts["gate"] = { Bd.maxX + 20, Bd.gateZ, NaN() };
	L.vals = { { "runwayX0", runX0 }, { "runwayX1", runX1 }, { "runwayZ", runZ } };
	map.landmarks["base"] = L;
	for (const Building& b : map.buildings) if (b.name == "All Saints General") { FixedVehicle f{ "skylark", (b.x0 + b.x1) / 2, (b.z0 + b.z1) / 2, 0, b.y1 + 0.3, 200 }; f.roof = true; FV.push_back(f); break; }
}

void PowerLines(Ctx& ctx) {
	RoadNet& net = ctx.net;
	for (const REdge& e : net.edges) {
		if (e.removed || e.hasGrid || (e.type != ERoad::Highway && e.type != ERoad::Road)) continue;
		if (e.name == "Main Street" || e.base) continue;
		const double off = e.wR + 7;
		for (double s = 20; s < e.len - 20; s += 55) {
			const EdgePoint p = net.At(e, s);
			if (CityDist(p.x, p.z) < 60) continue;
			const double x = p.x - p.tz * off, z = p.z + p.tx * off;
			if (net.OnRoad(x, z, 2).valid()) continue;
			const double y = ctx.hf.Sample(x, z);
			if (y < 0.5 || std::fabs(y - p.y) > 6) continue;
			ctx.map.props.push_back(MkProp("powerpole", x, z, std::atan2(p.tx, p.tz), y));
		}
	}
}

void VegetationFill(Ctx& ctx) {
	CityMap& map = ctx.map; Heightfield& hf = ctx.hf; RoadNet& net = ctx.net;
	Vegetation& V = map.vegetation;
	const double WminX = -5550, WmaxX = 1500, WminZ = -5350, WmaxZ = 1300;
	const double fc = 64;
	std::unordered_map<int64_t, std::vector<const Footprint*>> FG;
	auto key = [](int64_t gx, int64_t gz) { return gx * 1000003 + gz; };
	for (const Footprint& f : ctx.footprints) {
		const double R = f.hx + f.hz + 4;
		for (int64_t gx = (int64_t)std::floor((f.cx - R) / fc); gx <= (int64_t)std::floor((f.cx + R) / fc); gx++) for (int64_t gz = (int64_t)std::floor((f.cz - R) / fc); gz <= (int64_t)std::floor((f.cz + R) / fc); gz++) FG[key(gx, gz)].push_back(&f);
	}
	auto blocked = [&](double x, double z, double r) {
		if (CityDist(x, z) < 12) return true;
		if (NcEdgeDist(x, z) < 15) return true;
		if (z < -2000 && x > 300 && x > CoastX(z) - 230) return true;
		if (net.OnRoad(x, z, r + 2).valid()) return true;
		auto it = FG.find(key((int64_t)std::floor(x / fc), (int64_t)std::floor(z / fc)));
		if (it != FG.end()) for (const Footprint* f : it->second) if (std::fabs(f->cx - x) < f->hx + f->hz + r + 3 && std::fabs(f->cz - z) < f->hx + f->hz + r + 3) return true;
		if (x > BASE.minX - 30 && x < BASE.maxX + 30 && z > BASE.minZ - 30 && z < BASE.maxZ + 30) return true;
		if (std::fabs(x - AIRFIELD.x) < 90 && std::fabs(z - AIRFIELD.z) < AIRFIELD.len / 2 + 60) return true;
		return false;
	};
	auto push = [&](std::vector<float>& list, double x, double z, double r3, double scale, double sink = 0.1) {
		const double y = hf.Sample(x, z);
		list.push_back((float)x); list.push_back((float)(y - sink)); list.push_back((float)z); list.push_back((float)(r3 * 6.283)); list.push_back((float)scale);
	};
	auto townNear = [](double x, double z) { for (const Town& t : TOWNS) if (Hypot(x - t.x, z - t.z) < t.r + 20) return true; return false; };
	for (const ScatterPt& p : Scatter(WminX, WminZ, WmaxX, WmaxZ, 15, 11, [&](double x, double z) {
		const Regions w = RegionWeights(x, z);
		if (w.mountain < 0.25) return 0.0;
		const double h = hf.Sample(x, z);
		if (h < 1.5 || h > 560) return 0.0;
		const double dens = Smooth(0.25, 0.7, w.mountain) * (0.35 + 0.65 * Smooth(0.35, 0.6, FbmN(x * 0.003, z * 0.003, 2))) * (1 - Smooth(420, 560, h));
		return dens * 0.85;
	})) {
		if (SlopeAt(hf, p.x, p.z) > 0.75 || blocked(p.x, p.z, 0.5) || Hypot(p.x - LAKE.x, p.z - LAKE.z) < LAKE.r + 8) continue;
		push(V.pine, p.x, p.z, p.r3, 0.75 + p.r1 * 0.7);
	}
	for (const ScatterPt& p : Scatter(WminX, WminZ, WmaxX, WmaxZ, 34, 23, [&](double x, double z) {
		const Regions w = RegionWeights(x, z);
		const double dC = CityDist(x, z);
		const double ringHills = dC > 20 && dC < 1300 ? 0.55 : 0;
		return Max(Max(w.country * 0.45, ringHills), w.mountain * 0.2) * (1 - FarmMask(x, z)) * (0.3 + 0.9 * Smooth(0.4, 0.65, FbmN(x * 0.004 + 5, z * 0.004, 2)));
	})) {
		const double h = hf.Sample(p.x, p.z);
		if (h < 1.2 || SlopeAt(hf, p.x, p.z) > 0.6 || blocked(p.x, p.z, 0.6)) continue;
		if (townNear(p.x, p.z) && p.r2 < 0.6) continue;
		if (p.r1 < 0.62) push(V.oak, p.x, p.z, p.r3, 0.8 + p.r2 * 0.8);
		else push(V.bush, p.x, p.z, p.r3, 0.7 + p.r2 * 0.9, 0.2);
	}
	for (const ScatterPt& p : Scatter(-2400, -1800, -1300, 900, 28, 31, [](double x, double z) { return RiverDist(x, z) < 70 && RiverDist(x, z) > 22 ? 0.35 : 0.0; })) {
		if (hf.Sample(p.x, p.z) < 0.5 || blocked(p.x, p.z, 0.5)) continue;
		push(V.palm, p.x, p.z, p.r3, 0.9 + p.r1 * 0.5);
	}
	for (const ScatterPt& p : Scatter(WminX, WminZ, WmaxX, WmaxZ, 30, 41, [](double x, double z) { return RegionWeights(x, z).desert * 0.55; })) {
		const double h = hf.Sample(p.x, p.z);
		if (h < 1 || blocked(p.x, p.z, 0.8)) continue;
		const double sl = SlopeAt(hf, p.x, p.z);
		if (p.r1 < 0.34) { if (sl < 0.5) push(V.cactus, p.x, p.z, p.r3, 0.7 + p.r2 * 0.7, 0.2); }
		else if (p.r1 < 0.62) push(V.rock, p.x, p.z, p.r3, 0.6 + p.r2 * 1.8, 0.4);
		else if (p.r1 < 0.68) { if (sl < 0.5) push(V.deadtree, p.x, p.z, p.r3, 0.8 + p.r2 * 0.5); }
		else if (sl < 0.6) push(V.bush, p.x, p.z, p.r3, 0.5 + p.r2 * 0.6, 0.25);
	}
	for (const ScatterPt& p : Scatter(WminX, WminZ, WmaxX, WmaxZ, 60, 51, [](double x, double z) { return RegionWeights(x, z).mountain * 0.4; })) {
		if (hf.Sample(p.x, p.z) < 2 || blocked(p.x, p.z, 1)) continue;
		push(V.rock, p.x, p.z, p.r3, 0.8 + p.r2 * 2.2, 0.5);
	}
}

} // namespace

double FarmMask(double x, double z) {
	const Regions w = RegionWeights(x, z);
	if (w.country < 0.55) return 0;
	const double dC = CityDist(x, z);
	if (dC < 1000) return 0;
	const double n = FbmN(x * 0.0016 + 40, z * 0.0016 - 13, 2);
	double m = Smooth(0.42, 0.52, n) * Smooth(0.55, 0.85, w.country) * Smooth(1000, 1400, dC);
	for (const Town& t : TOWNS) m *= Smooth(t.r * 0.9, t.r * 1.4, Hypot(x - t.x, z - t.z));
	m *= Smooth(60, 140, RiverDist(x, z));
	m *= Smooth(LAKE.r + 60, LAKE.r + 200, Hypot(x - LAKE.x, z - LAKE.z));
	if (m > 0) m *= Smooth(200, 450, NcEdgeDist(x, z));
	return m;
}

std::vector<ScatterPt> Scatter(double x0, double z0, double x1, double z1, double spacing, int seed, const std::function<double(double, double)>& density) {
	std::vector<ScatterPt> out;
	for (int gz = (int)std::floor(z0 / spacing); gz <= (int)std::floor(z1 / spacing); gz++) {
		for (int gx = (int)std::floor(x0 / spacing); gx <= (int)std::floor(x1 / spacing); gx++) {
			const double r1 = Hash2(gx * 7 + seed, gz * 13 - seed), r2 = Hash2(gx * 11 - seed, gz * 5 + seed * 3), r3 = Hash2(gx + seed * 7, gz - seed);
			const double x = (gx + r1) * spacing, z = (gz + r2) * spacing;
			if (x < x0 || x > x1 || z < z0 || z > z1) continue;
			if (density(x, z) > r3) out.push_back({ x, z, r1, r2, r3 });
		}
	}
	return out;
}

void PopulateCountryside(CityMap& map) {
	Ctx ctx(map);
	map.townAreas.clear();
	map.padSurfaces.clear();
	map.vegetation = Vegetation();
	map.fixedVehicles.clear();
	for (const auto& kv : map.roadInfo.towns) TownFill(ctx, kv.first, kv.second);
	PopulateNorthCity(ctx);
	Farms(ctx);
	Airfield(ctx);
	RailStations(ctx);
	BaseFill(ctx);
	PowerLines(ctx);
	VegetationFill(ctx);
}

} // namespace atg
