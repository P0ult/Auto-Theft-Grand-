// Port of src/world/citymap.js. See CityMap.h. (Evaluation order matters everywhere the seeded RNG is
// drawn: the JavaScript evaluates arguments and object properties left to right, so draws are made into
// locals first here.)
#include "CityMap.h"

namespace atg {

using C3 = std::array<double, 3>;
using Palette = std::vector<C3>;

static const std::map<std::string, std::pair<const char*, const char*>> DISTRICTS = {
	{ "hills", { "Vistawood Hills", "#6c8f4e" } }, { "docks", { "Port Morena", "#7d7f86" } }, { "beach", { "Santa Luz Beach", "#d8c38f" } },
	{ "hood", { "Cedar Row", "#a58a66" } }, { "downtown", { "Downtown", "#8a93a8" } }, { "corona", { "El Corona", "#b88d6a" } },
	{ "westside", { "Rosewood", "#9c8f86" } }, { "midtown", { "Market District", "#9a9488" } }, { "country", { "Verde County", "#8f9a5a" } },
	{ "forest", { "Pinewood Forest", "#4f6b3a" } }, { "desert", { "Tierra Seca Desert", "#c9ad7a" } }, { "base", { "Fort Carver", "#8a8d73" } },
	{ "aurcentro", { "Centro", "#8f96a6" } }, { "aurharbor", { "Harborside", "#8c8a86" } }, { "aurmission", { "Mission", "#a0907c" } },
	{ "aurcathedral", { "Cathedral Hill", "#9a918a" } }, { "aurnorth", { "Northgate", "#958f88" } }, { "aurheights", { "Aurelio Heights", "#9aa08a" } },
	{ "aurbay", { "Bayview", "#a39a86" } }, { "aurelio", { "San Aurelio", "#99948c" } },
};
const char* CityMap::DistrictName(const std::string& key) { auto it = DISTRICTS.find(key); return it == DISTRICTS.end() ? "San Andreas" : it->second.first; }
const char* CityMap::DistrictColor(const std::string& key) { auto it = DISTRICTS.find(key); return it == DISTRICTS.end() ? "#9a9488" : it->second.second; }

static std::string DistrictFor(double cx, double cz) {
	if (cz < -560) return "hills";
	if (cx > 480 && cz > -40) return "docks";
	if (cz > 450 && cx > -300) return "beach";
	if (cx < -255 && cz > 60) return "hood";
	if (cx >= -75 && cx <= 380 && cz >= -560 && cz <= -140) return "downtown";
	if (cx > 380) return "corona";
	if (cx < -255) return "westside";
	return "midtown";
}

static const std::map<std::string, std::string> SPECIAL_BLOCKS = {
	{ "2,10", "home" }, { "3,11", "court" }, { "1,9", "burger" }, { "8,6", "gunshop" }, { "5,4", "spray" }, { "12,7", "spray2" },
	{ "9,4", "hospital" }, { "10,7", "police" }, { "11,3", "tower" }, { "10,4", "plaza" }, { "5,6", "park" }, { "6,6", "park" },
	{ "14,5", "vipers" }, { "16,10", "warehouse" }, { "9,0", "mansion_boss" }, { "7,12", "pierfront" }, { "13,2", "garage" },
	{ "0,12", "projects" }, { "4,12", "liquor" },
};
static bool RbSet(int i, int j) { for (auto& p : CITY_ROUNDABOUTS) if (p.first == i && p.second == j) return true; return false; }
static std::string Key2(int i, int j) { return std::to_string(i) + "," + std::to_string(j); }

CityMap::CityMap(uint32_t s) : seed(s) { Build(); }

int CityMap::Idx(const std::vector<double>& arr, double v) {
	if (v < arr.front() || v > arr.back()) return -1;
	int lo = 0, hi = (int)arr.size() - 1;
	while (hi - lo > 1) { const int m = (lo + hi) >> 1; if (arr[m] <= v) lo = m; else hi = m; }
	return lo;
}
const Block* CityMap::BlockAt(double x, double z) const {
	const int i = Idx(XS, x), j = Idx(ZS, z);
	if (i < 0 || j < 0) return nullptr;
	const int c = cellBlocks[(size_t)j * (XS.size() - 1) + i];
	return c < 0 ? nullptr : &blockStore[c];
}
const Block* CityMap::GetBlock(int i, int j) const {
	if (i < 0 || j < 0 || i >= (int)XS.size() - 1 || j >= (int)ZS.size() - 1) return nullptr;
	const int c = cellBlocks[(size_t)j * (XS.size() - 1) + i];
	return c < 0 ? nullptr : &blockStore[c];
}
Block* CityMap::BlockMut(int i, int j) { return const_cast<Block*>(GetBlock(i, j)); }
int CityMap::NearestX(double x) const { double best = 1e9; int bi = 0; for (int i = 0; i < (int)XS.size(); i++) { const double d = std::fabs(x - XS[i]); if (d < best) { best = d; bi = i; } } return bi; }
int CityMap::NearestZ(double z) const { double best = 1e9; int bi = 0; for (int j = 0; j < (int)ZS.size(); j++) { const double d = std::fabs(z - ZS[j]); if (d < best) { best = d; bi = j; } } return bi; }

bool CityMap::IsOnRoad(double x, double z) const {
	if (IsOnCityStreet(x, z)) return true;
	return roads.OnRoad(x, z).valid();
}
bool CityMap::IsOnCityStreet(double x, double z, double margin) const {
	if (x < CITY_MINX - 1 || x > CITY_MAXX + 1 || z < CITY_MINZ - 1 || z > CITY_MAXZ + 1) return false;
	const int i = NearestX(x), j = NearestZ(z);
	const double dx = std::fabs(x - XS[i]), dz = std::fabs(z - ZS[j]);
	if (dx > HALF_ROAD + margin && dz > HALF_ROAD + margin) return false;
	const Block* b = BlockAt(x, z);
	if (b && b->merged && x > b->x0 && x < b->x1 && z > b->z0 && z < b->z1) return false;
	return true;
}

std::string CityMap::DistrictAt(double x, double z) const {
	const Block* b = BlockAt(x, z);
	if (b) return b->district;
	if (CityDist(x, z) > 700) {
		const NcDistrict nd = NcDistrictAt(x, z);
		if (nd.key) return nd.key;
		if (x > BASE.minX - 100 && x < BASE.maxX + 100 && z > BASE.minZ - 100 && z < BASE.maxZ + 100) return "base";
		const Regions w = RegionWeights(x, z);
		return w.desert > 0.5 ? "desert" : w.mountain > 0.5 ? "forest" : "country";
	}
	if (z > CITY_MAXZ && x < 480) return "beach";
	if (x > CITY_MAXX && z > -250) return "docks";
	if (z < CITY_MINZ) return "hills";
	const int i = (int)Clamp(Idx(XS, Clamp(x, XS.front() + 1, XS.back() - 1)), 0, (double)XS.size() - 2);
	const int j = (int)Clamp(Idx(ZS, Clamp(z, ZS.front() + 1, ZS.back() - 1)), 0, (double)ZS.size() - 2);
	const Block* bb = GetBlock(i, j);
	return bb ? bb->district : "midtown";
}

std::string CityMap::ZoneName(double x, double z) const {
	for (const Town& t : TOWNS) if (Hypot(x - t.x, z - t.z) < t.r + 60) return t.name;
	if (x > BASE.minX && x < BASE.maxX && z > BASE.minZ && z < BASE.maxZ) return "Fort Carver";
	if (Hypot(x - AIRFIELD.x, z - AIRFIELD.z) < AIRFIELD.len / 2 + 80) return AIRFIELD.name;
	if (Hypot(x - LAKE.x, z - LAKE.z) < LAKE.r + 120) return "Lake Mirador";
	if (hf.Sample(x, z) < -0.5 && CityDist(x, z) > 0) {
		if (RiverDist(x, z) < 60) return "Rio Verde";
		return "Pacific Ocean";
	}
	if (z > CITY_MAXZ + 70 && x > 60 && x < 150 && z < 960) return "Santa Luz Pier";
	const double dC = CityDist(x, z);
	if (dC > 0 && dC < 700) {
		if (z < CITY_MINZ - 60 && x > -700 && x < 700) return dC < 360 ? "Vistawood Hills" : "Mount Vista";
		if (x < CITY_MINX - 60) return "Red Canyon";
		if (x > CITY_MAXX + 60 && z < -250) return "Bayshore";
	}
	if (dC >= 700) {
		const NcDistrict nd = NcDistrictAt(x, z);
		if (nd.key) return std::string(nd.key) == "aurcentro" ? "San Aurelio" : nd.name;
		const double ne = NcEdgeDist(x, z);
		if (ne < 700) return x > NCITY.x + 380 && TerrainHeight(x, z) < 5.5 ? "Aurelio Beach" : "Aurelio Hills";
		if (RiverDist(x, z) < 90) return "Rio Verde";
		if (Hypot(x + 900, z + 3250) < 700) return "Mount Cedro";
		const Regions w = RegionWeights(x, z);
		if (w.desert > 0.5) return x < -4600 ? "Bone Flats" : "Tierra Seca Desert";
		if (w.mountain > 0.5) return x > 400 ? "Bayshore" : "Pinewood Forest";
		return "Verde County";
	}
	return DistrictName(DistrictAt(x, z));
}

double CityMap::GroundHeight(double x, double z) const {
	auto it = landmarks.find("pier");
	if (it != landmarks.end()) {
		const Landmark& P = it->second;
		const double x0 = P.vals.at("x0"), x1 = P.vals.at("x1"), z0 = P.vals.at("z0"), z1 = P.vals.at("z1");
		if (x > x0 && x < x1 && z > z0 && z < z1) {
			const double t = Clamp((z - z0) / 24, 0, 1);
			return Lerp(Max(TerrainHeight(x, z), 0.1), P.y, t);
		}
	}
	if (x >= CITY_MINX && x <= CITY_MAXX && z >= CITY_MINZ && z <= CITY_MAXZ) {
		const Block* b = BlockAt(x, z);
		if (b && x > b->x0 && x < b->x1 && z > b->z0 && z < b->z1) return CURB_H;
		return 0;
	}
	return hf.Sample(x, z);
}
double CityMap::WaterLevel(double x, double z) const { return Hypot(x - LAKE.x, z - LAKE.z) < LAKE.r + 40 ? LAKE.y : WATER_Y; }
double CityMap::CityGround(double x, double z) const {
	const Block* b = BlockAt(x, z);
	if (b && x > b->x0 && x < b->x1 && z > b->z0 && z < b->z1) return CURB_H;
	return 0;
}

// a stand-in for the JavaScript's Math.random() seed on the few buildings that don't pass one (it only
// varies the facade pattern)
static double PosSeed(double x, double z) { return Hash2(ToInt32(std::floor(x * 10)), ToInt32(std::floor(z * 10))); }

int CityMap::AddBuilding(const Block* b, double x0, double z0, double x1, double z1, double height, int style, const BldOpts& o) {
	if (x1 - x0 < 2 || z1 - z0 < 2) return -1;
	Building B;
	B.x0 = x0; B.z0 = z0; B.x1 = x1; B.z1 = z1;
	B.y0 = IsSet(o.y0) ? o.y0 : CURB_H; B.y1 = B.y0 + height;
	B.style = style; B.rot = o.rot; B.base = o.base;
	if (o.hasTint) { B.tint[0] = o.tint[0]; B.tint[1] = o.tint[1]; B.tint[2] = o.tint[2]; }
	B.seed = IsSet(o.seed) ? o.seed : PosSeed(x0, z0);
	B.roof = o.roof.empty() ? "flat" : o.roof;
	B.district = b ? b->district : "midtown";
	B.kind = o.kind.empty() ? "building" : o.kind;
	B.floorH = o.floorH != 0 ? o.floorH : (style == 4 ? 6 : 3.4);
	B.cell = o.cell != 0 ? o.cell : 3.2;
	B.name = o.name; B.sign = o.sign;
	B.noCollide = o.noCollide || o.shop.valid();
	B.shop = o.shop;
	buildings.push_back(B);
	return (int)buildings.size() - 1;
}

template <typename T, typename F>
static void EraseIf(std::vector<T>& v, F pred) { v.erase(std::remove_if(v.begin(), v.end(), pred), v.end()); }

// ---------------------------------------------------------------- build
void CityMap::Build() {
	const int nI = (int)XS.size() - 1, nJ = (int)ZS.size() - 1;
	std::vector<Pad> pads;
	for (const Town& t : TOWNS) {
		Pad p; p.key = t.key; p.x = t.x; p.z = t.z; p.r = IsSet(t.padR) ? t.padR : t.r; p.y = t.padY; p.blend = 140; p.keepSea = true;
		pads.push_back(p);
	}
	{ Pad p; p.key = "base"; p.minX = BASE.minX; p.maxX = BASE.maxX; p.minZ = BASE.minZ; p.maxZ = BASE.maxZ; p.blend = 160; pads.push_back(p); }
	{ Pad p; p.key = "air"; p.minX = AIRFIELD.x - 60; p.maxX = AIRFIELD.x + 60; p.minZ = AIRFIELD.z - AIRFIELD.len / 2 - 20; p.maxZ = AIRFIELD.z + AIRFIELD.len / 2 + 20; p.blend = 80; pads.push_back(p); }
	for (const Pad& p : SkateparkPads()) pads.push_back(p);
	hf.Generate(LandHeight, 3, pads);
	hf.CarveRoutes(ROUTES(), [&](const std::string& key) { for (const Pad& p : pads) if (p.key == key) return p.y; return NaN(); });
	for (Pad& p : pads) hf.PadIt(p);
	BuildRoadNetwork(hf, roads, roadInfo);

	for (int j = 0; j < nJ; j++) {
		for (int i = 0; i < nI; i++) {
			Block b;
			b.i = i; b.j = j;
			b.x0 = XS[i] + HALF_ROAD; b.x1 = XS[i + 1] - HALF_ROAD; b.z0 = ZS[j] + HALF_ROAD; b.z1 = ZS[j + 1] - HALF_ROAD;
			b.cx = (b.x0 + b.x1) / 2; b.cz = (b.z0 + b.z1) / 2;
			b.district = DistrictFor(b.cx, b.cz);
			auto sp = SPECIAL_BLOCKS.find(Key2(i, j));
			b.special = sp == SPECIAL_BLOCKS.end() ? "" : sp->second;
			b.ix0 = b.x0 + SIDEWALK_W; b.iz0 = b.z0 + SIDEWALK_W; b.ix1 = b.x1 - SIDEWALK_W; b.iz1 = b.z1 - SIDEWALK_W;
			b.seed = (uint32_t)(i * 7919 + j * 104729 + (int64_t)seed);
			b.corners[0] = { b.x0 + 2, b.z0 + 2 }; b.corners[1] = { b.x1 - 2, b.z0 + 2 }; b.corners[2] = { b.x1 - 2, b.z1 - 2 }; b.corners[3] = { b.x0 + 2, b.z1 - 2 };
			blockStore.push_back(b);
			blocks.push_back((int)blockStore.size() - 1);
			cellBlocks.push_back((int)blockStore.size() - 1);
		}
	}
	for (const SuperBlockDef& sb : SUPERBLOCKS) {
		const int c0 = cellBlocks[(size_t)sb.cells[0][1] * nI + sb.cells[0][0]], c1 = cellBlocks[(size_t)sb.cells[1][1] * nI + sb.cells[1][0]];
		Block& m = blockStore[c0];
		const Block& o = blockStore[c1];
		const double x0 = Min(m.x0, o.x0), x1 = Max(m.x1, o.x1), z0 = Min(m.z0, o.z0), z1 = Max(m.z1, o.z1);
		m.x0 = x0; m.x1 = x1; m.z0 = z0; m.z1 = z1; m.cx = (x0 + x1) / 2; m.cz = (z0 + z1) / 2;
		m.ix0 = x0 + SIDEWALK_W; m.iz0 = z0 + SIDEWALK_W; m.ix1 = x1 - SIDEWALK_W; m.iz1 = z1 - SIDEWALK_W;
		m.merged = true; m.special = "super"; m.superKind = sb.kind; m.superName = sb.name;
		m.corners[0] = { x0 + 2, z0 + 2 }; m.corners[1] = { x1 - 2, z0 + 2 }; m.corners[2] = { x1 - 2, z1 - 2 }; m.corners[3] = { x0 + 2, z1 - 2 };
		blocks.erase(std::find(blocks.begin(), blocks.end(), c1));
		cellBlocks[(size_t)o.j * nI + o.i] = c0;
	}
	{ Landmark L; L.x = 106; L.z = CITY_MAXZ + 8; L.y = 2.6; L.vals = { { "x0", 92 }, { "x1", 120 }, { "z0", CITY_MAXZ + 8 }, { "z1", 960 } }; landmarks["pier"] = L; }
	for (int bi : blocks) FillBlock(blockStore[bi]);
	ClearUnderFreeway();
	StreetProps();
	BuildSidewalkGraph();
	ShapeTerrain(roads, hf, [this](double x, double z) { return CityDist(x, z) < 0.5 ? CityGround(x, z) : NaN(); });
	PopulateCountryside(*this);
	PlanSkateparks();
	BuildLandmarks();
}

void CityMap::PlanSkateparks() {
	for (const SkateparkDef& p : SKATEPARKS) {
		PadSurface s; s.cx = p.x; s.cz = p.z; s.hx = p.hx; s.hz = p.hz; s.yaw = p.yaw; s.y = p.y + 0.02; s.type = "asphalt"; s.skate = true;
		padSurfaces.push_back(s);
		Landmark L; L.x = p.x; L.z = p.z; L.name = p.name;
		landmarks[std::string("skate_") + p.key] = L;
	}
}

// Buildings, fences, lots, props and parking under the city freeway and its ramps are cleared away
void CityMap::ClearUnderFreeway() {
	auto hit = [&](double x0, double z0, double x1, double z1, double pad) {
		for (int eid : roads.EdgesIn(x0 - 30, z0 - 30, x1 + 30, z1 + 30)) {
			const REdge& e = roads.edges[eid];
			if (e.hasGrid || (e.type != ERoad::Freeway && e.type != ERoad::Ramp)) continue;
			for (int i = 0; i < e.n - 1; i++) {
				const double ax = e.p[i * 3], az = e.p[i * 3 + 2], bx = e.p[i * 3 + 3], bz = e.p[i * 3 + 5];
				const double w = Max(e.wL, e.wR) + pad;
				if (Max(ax, bx) + w < x0 || Min(ax, bx) - w > x1 || Max(az, bz) + w < z0 || Min(az, bz) - w > z1) continue;
				for (int k = 0; k <= 4; k++) {
					const double t = k / 4.0, px = ax + (bx - ax) * t, pz = az + (bz - az) * t;
					const double dx = Max(Max(x0 - px, 0), px - x1), dz = Max(Max(z0 - pz, 0), pz - z1);
					if (Hypot(dx, dz) < w) return true;
				}
			}
		}
		return false;
	};
	auto inCity = [](double x, double z) { return x > CITY_MINX && x < CITY_MAXX && z > CITY_MINZ && z < CITY_MAXZ; };
	EraseIf(buildings, [&](const Building& b) { return inCity(b.x0, b.z0) && hit(b.x0, b.z0, b.x1, b.z1, 1.5); });
	EraseIf(fences, [&](const Fence& f) { return inCity(f.x0, f.z0) && hit(Min(f.x0, f.x1), Min(f.z0, f.z1), Max(f.x0, f.x1), Max(f.z0, f.z1), 1.5); });
	EraseIf(props, [&](const Prop& p) { return inCity(p.x, p.z) && p.type != "trafficlight" && hit(p.x - 0.5, p.z - 0.5, p.x + 0.5, p.z + 0.5, 1); });
	EraseIf(parkingSpots, [&](const ParkingSpot& p) { return inCity(p.x, p.z) && !p.curb && hit(p.x - 2.5, p.z - 2.5, p.x + 2.5, p.z + 2.5, 0.5); });
	EraseIf(containers, [&](const Container& c) { return hit(c.x - 3, c.z - 3, c.x + 3, c.z + 3, 1.5); });
	EraseIf(pools, [&](const Pool& p) { return hit(p.x0, p.z0, p.x1, p.z1, 1.5); });
	for (LotSurface& l : lotSurfaces) if (inCity(l.x0, l.z0) && hit(l.x0, l.z0, l.x1, l.z1, 0)) l.type = l.type == "grass" ? "dirt" : l.type;
}

void CityMap::Tower(Block& b, RNG& rng, double x0, double z0, double x1, double z1, double H, int style, const C3& tint) {
	const double sd = rng.Next();
	const double podiumH = rng.Range(10, 18);
	const double w = x1 - x0, d = z1 - z0;
	auto opts = [&](const char* roof, double y0 = NaN()) { BldOpts o; o.Tint(tint); o.seed = sd; o.roof = roof; o.y0 = y0; return o; };
	if (H < 35 || rng.Chance(0.25)) {
		AddBuilding(&b, x0, z0, x1, z1, H, style, opts(rng.Chance(0.3) ? "antenna" : "flat"));
		return;
	}
	const int podStyle = rng.Chance(0.5) ? 5 : style;
	AddBuilding(&b, x0, z0, x1, z1, podiumH, podStyle, opts("flat"));
	const double inset = Min(w, d) * rng.Range(0.1, 0.2);
	const double tx0 = x0 + inset, tz0 = z0 + inset, tx1 = x1 - inset, tz1 = z1 - inset;
	const double mainH = H - podiumH;
	if (rng.Chance(0.5)) {
		const double h1 = mainH * rng.Range(0.55, 0.8);
		AddBuilding(&b, tx0, tz0, tx1, tz1, h1, style, opts("flat", CURB_H + podiumH));
		const double in2 = Min(tx1 - tx0, tz1 - tz0) * rng.Range(0.12, 0.22);
		AddBuilding(&b, tx0 + in2, tz0 + in2, tx1 - in2, tz1 - in2, mainH - h1, style, opts(rng.Chance(0.5) ? "spire" : "helipad", CURB_H + podiumH + h1));
	} else {
		const char* roof = rng.Chance(0.4) ? "antenna" : rng.Chance(0.5) ? "helipad" : "flat";
		AddBuilding(&b, tx0, tz0, tx1, tz1, mainH, style, opts(roof, CURB_H + podiumH));
	}
}

void CityMap::Perimeter(Block& b, RNG& rng, const PerimOpts& o) {
	const double ix0 = b.ix0, iz0 = b.iz0, ix1 = b.ix1, iz1 = b.iz1;
	const double depth = rng.Range(o.depth[0], o.depth[1]);
	struct Row { double x0, z0, x1, z1; bool alongX; };
	const Row rows[4] = {
		{ ix0, iz0, ix1, iz0 + depth, true },
		{ ix0, iz1 - depth, ix1, iz1, true },
		{ ix0, iz0 + depth, ix0 + depth, iz1 - depth, false },
		{ ix1 - depth, iz0 + depth, ix1, iz1 - depth, false },
	};
	struct Seg { double x0, z0, x1, z1, h; int style; C3 tint; };
	std::vector<Seg> segs;
	for (const Row& r : rows) {
		const double len = r.alongX ? r.x1 - r.x0 : r.z1 - r.z0;
		double pos = 0;
		while (pos < len - 4) {
			double w = rng.Range(o.width[0], o.width[1]);
			if (len - pos - w < o.width[0]) w = len - pos;
			const double gap = rng.Chance(o.gapChance) ? rng.Range(3, 6) : 0;
			const double a = pos, c = Min(len, pos + w);
			const int style = rng.Pick(o.styles);
			const int floors = rng.Int(o.floors[0], o.floors[1]);
			const double fh = style == 5 ? 4.2 : 3.4;
			const double h = floors * fh + (style == 5 ? 0.6 : 0);
			const C3 tint = o.tint(rng);
			if (r.alongX) segs.push_back({ r.x0 + a, r.z0, r.x0 + c - gap, r.z1, h, style, tint });
			else segs.push_back({ r.x0, r.z0 + a, r.x1, r.z0 + c - gap, h, style, tint });
			pos = c;
		}
	}
	for (const Seg& s : segs) {
		BldOpts bo; bo.Tint(s.tint); bo.seed = rng.Next(); bo.roof = rng.Chance(0.35) ? "ac" : "flat"; bo.floorH = s.style == 5 ? 4.2 : 3.4;
		AddBuilding(&b, s.x0, s.z0, s.x1, s.z1, s.h, s.style, bo);
	}
	lotSurfaces.push_back({ ix0 + depth, iz0 + depth, ix1 - depth, iz1 - depth, "asphalt" });
	const double yx = (ix0 + ix1) / 2, yz = (iz0 + iz1) / 2;
	static const std::vector<double> rots = { 0, kPi / 2, kPi, -kPi / 2 };
	for (int k = 0; k < 3; k++) {
		if (rng.Chance(0.6)) {
			ParkingSpot p; p.x = yx + rng.Range(-12, 12); p.z = yz + rng.Range(-12, 12); p.rot = rng.Pick(rots); p.district = b.district; p.lot = true;
			parkingSpots.push_back(p);
		}
	}
	if (rng.Chance(0.7)) { Prop p; p.type = "dumpster"; p.x = ix0 + depth + 2.5; p.z = yz + rng.Range(-8, 8); p.rot = kPi / 2; props.push_back(p); }
}

static Prop MkProp(const char* type, double x, double z, double rot = 0, double scale = 1) { Prop p; p.type = type; p.x = x; p.z = z; p.rot = rot; p.scale = scale; return p; }
static Fence MkFence(double x0, double z0, double x1, double z1, double h, const char* type) { Fence f; f.x0 = x0; f.z0 = z0; f.x1 = x1; f.z1 = z1; f.h = h; f.type = type; return f; }

void CityMap::Houses(Block& b, RNG& rng) {
	const double ix0 = b.ix0, iz0 = b.iz0, ix1 = b.ix1, iz1 = b.iz1;
	const double mid = (iz0 + iz1) / 2;
	static const Palette pastels = { { 1, 0.93, 0.8 }, { 0.95, 0.85, 0.75 }, { 0.8, 0.88, 0.95 }, { 0.95, 0.8, 0.8 }, { 0.85, 0.95, 0.82 }, { 1, 1, 0.95 }, { 0.9, 0.82, 0.95 }, { 0.98, 0.9, 0.7 } };
	for (int side : { 0, 1 }) {
		double x = ix0;
		while (x < ix1 - 10) {
			double w = rng.Range(15, 19);
			if (ix1 - x - w < 12) w = ix1 - x;
			const double lz0 = side == 0 ? iz0 : mid, lz1 = side == 0 ? mid : iz1;
			const double front = side == 0 ? lz0 : lz1;
			const double hw = Min(w - 5, rng.Range(9, 12));
			const double hd = rng.Range(8, 11);
			const double hx0 = x + 1.5, hx1 = hx0 + hw;
			const double setback = rng.Range(5, 8);
			const double hz0 = side == 0 ? front + setback : front - setback - hd;
			const double hz1 = hz0 + hd;
			const int floors = rng.Chance(0.3) ? 2 : 1;
			const double h = floors * 3.1 + 0.4;
			{
				BldOpts o; o.Tint(rng.Pick(pastels)); o.seed = rng.Next(); o.roof = "gable"; o.floorH = 3.1; o.cell = 3.0; o.kind = "house";
				AddBuilding(&b, hx0, hz0, hx1, hz1, h, 3, o);
			}
			lotSurfaces.push_back({ x, lz0, x + w, lz1, rng.Chance(0.25) ? "dirt" : "grass" });
			const double dx0 = hx1 + 0.5, dx1 = Min(x + w - 0.5, dx0 + 3.6);
			if (dx1 - dx0 > 2.6) {
				lotSurfaces.push_back({ dx0, side == 0 ? lz0 : lz1 - setback - hd, dx1, side == 0 ? lz0 + setback + hd : lz1, "concrete" });
				if (rng.Chance(0.55)) {
					ParkingSpot p; p.x = (dx0 + dx1) / 2; p.z = side == 0 ? lz0 + setback * 0.6 + 2 : lz1 - setback * 0.6 - 2; p.rot = side == 0 ? kPi : 0; p.district = b.district; p.driveway = true;
					parkingSpots.push_back(p);
				}
			}
			const double fz0 = lz0, fz1 = lz1;
			if (rng.Chance(0.8)) fences.push_back(MkFence(x + w - 0.05, side == 0 ? fz0 + 2 : fz0, x + w + 0.05, side == 0 ? fz1 : fz1 - 2, 1.5, rng.Chance(0.5) ? "chain" : "wood"));
			if (rng.Chance(0.5)) fences.push_back(MkFence(x + 0.5, side == 0 ? front + 0.3 : front - 0.4, hx0 + hw * 0.5, side == 0 ? front + 0.4 : front - 0.3, 1.0, "wood"));
			if (rng.Chance(0.4)) {
				const char* type = rng.Chance(0.6) ? "palm" : "tree";
				const double px = x + rng.Range(2, w - 2);
				const double rot = rng.Range(0, 6.28);
				const double sc = rng.Range(0.8, 1.2);
				props.push_back(MkProp(type, px, side == 0 ? front + 2.5 : front - 2.5, rot, sc));
			}
			if (rng.Chance(0.3)) props.push_back(MkProp("trashcan", dx0 + 1, side == 0 ? front + 1 : front - 1));
			x += w;
		}
	}
	fences.push_back(MkFence(ix0, mid - 0.05, ix1, mid + 0.05, 1.8, "wood"));
}

void CityMap::FillBlock(Block& b) {
	RNG rng(b.seed);
	const double ix0 = b.ix0, iz0 = b.iz0, ix1 = b.ix1, iz1 = b.iz1;
	const double D = iz1 - iz0;
	const std::string sp = b.special;
	static const Palette warmP = { { 1, 0.95, 0.88 }, { 0.93, 0.9, 0.86 }, { 0.85, 0.8, 0.75 }, { 1, 0.88, 0.75 }, { 0.8, 0.82, 0.86 }, { 0.95, 0.95, 0.95 }, { 0.95, 0.75, 0.6 }, { 0.75, 0.85, 0.95 }, { 0.85, 0.95, 0.85 }, { 1, 0.82, 0.82 }, { 0.9, 0.8, 0.6 }, { 0.7, 0.72, 0.78 } };
	auto warm = [&rng]() { return rng.Pick(warmP); };
	auto warmFn = [](RNG& r) { return r.Pick(warmP); };

	if (sp == "super") { SuperBlock(b, rng); return; }
	if (sp == "park" || sp == "court" || sp == "plaza") {
		b.ground = sp == "plaza" ? "plaza" : "grass";
		b.park = true;
		Park(b, rng, sp);
		return;
	}
	if (sp == "home") {
		b.ground = "grass";
		Houses(b, rng);
		int home = -1;
		for (int k = 0; k < (int)buildings.size(); k++) { const Building& x = buildings[k]; if (x.kind == "house" && x.z0 > (iz0 + iz1) / 2 && x.x0 < ix0 + 20 && x.district == "hood") home = k; }
		if (home >= 0) {
			Building& h = buildings[home];
			h.name = "Castillo House"; h.tint[0] = 0.85; h.tint[1] = 0.95; h.tint[2] = 0.8;
			Landmark L; L.x = (h.x0 + h.x1) / 2; L.z = h.z1 + 3.5; L.pts["door"] = { (h.x0 + h.x1) / 2, h.z1 + 0.6, NaN() };
			landmarks["home"] = L;
		}
		return;
	}
	static const std::set<std::string> specials = { "hospital", "police", "gunshop", "spray", "spray2", "burger", "tower", "vipers", "warehouse", "mansion_boss", "garage", "liquor", "projects", "pierfront" };
	if (specials.count(sp)) { SpecialBlock(b, rng, sp); return; }

	const std::string& district = b.district;
	if (district == "downtown") {
		const double r = rng.Next();
		const double distC = Hypot(b.cx - 150, b.cz + 350);
		const double hmax = 60 + 180 * Clamp(1 - distC / 320, 0, 1);
		if (r < 0.12) { b.ground = "plaza"; Park(b, rng, "plaza"); return; }
		auto style = [&]() { return rng.Weighted<int>({ { 1, 5 }, { 0, 3 }, { 6, 2 } }); };
		static const Palette glassP = { { 0.7, 0.85, 1.0 }, { 0.6, 0.9, 0.85 }, { 1.0, 0.85, 0.65 }, { 0.8, 0.8, 0.85 }, { 0.55, 0.7, 0.95 }, { 0.9, 0.95, 1.0 } };
		if (r < 0.35) {
			const int st = style();
			const double H = rng.Range(hmax * 0.5, hmax);
			const C3 tint = st == 1 ? rng.Pick(glassP) : warm();
			Tower(b, rng, ix0 + 2, iz0 + 2, ix1 - 2, iz1 - 2, H, st, tint);
		} else {
			const double mx = (ix0 + ix1) / 2 + rng.Range(-8, 8);
			const double mz = (iz0 + iz1) / 2 + rng.Range(-8, 8);
			const double lots[4][4] = { { ix0, iz0, mx - 2, mz - 2 }, { mx + 2, iz0, ix1, mz - 2 }, { ix0, mz + 2, mx - 2, iz1 }, { mx + 2, mz + 2, ix1, iz1 } };
			for (const auto& L : lots) {
				if (rng.Chance(0.12)) { lotSurfaces.push_back({ L[0], L[1], L[2], L[3], "plaza" }); props.push_back(MkProp("tree", (L[0] + L[2]) / 2, (L[1] + L[3]) / 2, 0, 1.2)); continue; }
				const int st = style();
				const double H = rng.Range(hmax * 0.3, hmax);
				const C3 tint = st == 1 ? rng.Pick(glassP) : warm();
				Tower(b, rng, L[0] + 1, L[1] + 1, L[2] - 1, L[3] - 1, H, st, tint);
			}
			lotSurfaces.push_back({ mx - 2, iz0, mx + 2, iz1, "asphalt" });
			lotSurfaces.push_back({ ix0, mz - 2, ix1, mz + 2, "asphalt" });
		}
	} else if (district == "midtown") {
		Perimeter(b, rng, { { 16, 24 }, { 12, 26 }, { 2, 9 }, { 0, 2, 5, 5, 6, 2 }, warmFn, 0.2 });
		if (rng.Chance(0.25)) {
			const double bx = (ix0 + ix1) / 2, bz = (iz0 + iz1) / 2;
			const double H = rng.Range(30, 60);
			const int st = rng.Pick(std::vector<int>{ 0, 1, 2 });
			BldOpts o; o.Tint(warm()); o.seed = rng.Next(); o.roof = "ac";
			AddBuilding(&b, bx - 10, bz - 8, bx + 10, bz + 8, H, st, o);
		}
	} else if (district == "westside") {
		if (rng.Chance(0.45)) Houses(b, rng);
		else Perimeter(b, rng, { { 14, 20 }, { 14, 24 }, { 2, 5 }, { 2, 3, 3, 5 }, warmFn, 0.3 });
	} else if (district == "hood") {
		b.ground = "grass";
		if (rng.Chance(0.82)) Houses(b, rng);
		else Perimeter(b, rng, { { 14, 18 }, { 18, 30 }, { 2, 4 }, { 2, 3, 5 }, warmFn, 0.35 });
	} else if (district == "corona") {
		static const Palette cor = { { 1, 0.85, 0.7 }, { 0.95, 0.75, 0.65 }, { 0.9, 0.95, 0.75 }, { 0.8, 0.9, 1 }, { 1, 0.95, 0.6 }, { 0.95, 0.8, 0.9 } };
		if (rng.Chance(0.4)) { b.ground = "dirt"; Houses(b, rng); }
		else Perimeter(b, rng, { { 14, 20 }, { 10, 20 }, { 1, 3 }, { 3, 5, 5, 3 }, [](RNG& r) { return r.Pick(cor); }, 0.3 });
	} else if (district == "beach") {
		const bool nearSand = b.j == (int)ZS.size() - 2;
		if (nearSand && rng.Chance(0.6)) {
			const double H = rng.Range(25, 70);
			const int st = rng.Pick(std::vector<int>{ 7, 7, 3, 1 });
			Tower(b, rng, ix0 + 4, iz0 + 6, ix1 - 4, iz1 - 10, H, st, { 1, 0.97, 0.9 });
			lotSurfaces.push_back({ ix0, iz1 - 10, ix1, iz1, "plaza" });
			for (double x = ix0 + 4; x < ix1; x += 9) props.push_back(MkProp("palm", x, iz1 - 4, 0, rng.Range(1.1, 1.4)));
		} else {
			static const Palette bp = { { 1, 0.95, 0.85 }, { 0.9, 0.97, 1 }, { 1, 0.9, 0.9 }, { 0.95, 1, 0.9 } };
			Perimeter(b, rng, { { 14, 20 }, { 12, 22 }, { 1, 4 }, { 5, 3, 7, 5 }, [](RNG& r) { return r.Pick(bp); }, 0.25 });
		}
	} else if (district == "docks") {
		b.ground = "asphalt";
		const int n = rng.Chance(0.5) ? 1 : 2;
		if (n == 1) {
			static const Palette dp = { { 0.75, 0.8, 0.85 }, { 0.85, 0.7, 0.6 }, { 0.7, 0.75, 0.7 }, { 0.9, 0.88, 0.8 } };
			const double H = rng.Range(9, 15);
			BldOpts o; o.Tint(rng.Pick(dp)); o.seed = rng.Next(); o.roof = "flat"; o.floorH = 6; o.cell = 5; o.kind = "warehouse";
			AddBuilding(&b, ix0 + 4, iz0 + 4, ix1 - 4, iz0 + D * 0.55, H, 4, o);
		} else {
			const double mx = (ix0 + ix1) / 2;
			{ const double H = rng.Range(8, 13); BldOpts o; o.Tint(0.8, 0.8, 0.82); o.seed = rng.Next(); o.floorH = 6; o.cell = 5; o.kind = "warehouse"; AddBuilding(&b, ix0 + 3, iz0 + 4, mx - 3, iz0 + D * 0.6, H, 4, o); }
			{ const double H = rng.Range(8, 13); BldOpts o; o.Tint(0.72, 0.62, 0.55); o.seed = rng.Next(); o.floorH = 6; o.cell = 5; o.kind = "warehouse"; AddBuilding(&b, mx + 3, iz0 + 4, ix1 - 3, iz0 + D * 0.6, H, 4, o); }
		}
		const double cz = iz0 + D * 0.72;
		for (double x = ix0 + 4; x < ix1 - 8; x += 7) {
			if (rng.Chance(0.3)) continue;
			const int stack = rng.Int(1, 3);
			for (int s = 0; s < stack; s++) {
				Container c; c.x = x + 1.2; c.z = cz + rng.Range(-1, 6); c.rot = kPi / 2 + rng.Range(-0.04, 0.04); c.level = s; c.color = rng.Int(0, 5);
				containers.push_back(c);
			}
		}
		fences.push_back(MkFence(ix0, iz1 - 0.05, ix1 - 10, iz1 + 0.05, 2.4, "chain"));
		for (int k = 0; k < 2; k++) { ParkingSpot p; p.x = ix0 + 10 + k * 8; p.z = iz1 - 6; p.rot = 0; p.district = "docks"; p.lot = true; parkingSpots.push_back(p); }
	} else if (district == "hills") {
		b.ground = "grass";
		Mansion(b, rng);
	} else {
		Perimeter(b, rng, { { 14, 20 }, { 12, 24 }, { 2, 6 }, { 0, 2, 5 }, warmFn, 0.15 });
	}
}

void CityMap::SuperBlock(Block& b, RNG& rng) {
	const double ix0 = b.ix0, iz0 = b.iz0, ix1 = b.ix1, iz1 = b.iz1;
	const double cx = (ix0 + ix1) / 2, cz = (iz0 + iz1) / 2;
	const double W = ix1 - ix0, D = iz1 - iz0;
	{ Landmark L; L.x = cx; L.z = cz; L.name = b.superName; landmarks[b.superKind] = L; }
	if (b.superKind == "stadium") {
		b.ground = "asphalt";
		lotSurfaces.push_back({ ix0, iz0, ix1, iz1, "asphalt" });
		const double ra = Min(W, D) * 0.42, rb = Max(W, D) * 0.4;
		const bool alongZ = D > W;
		const int n = 22;
		for (int k = 0; k < n; k++) {
			const double a0 = (double)k / n * kPi * 2, a1 = (double)(k + 1) / n * kPi * 2, am = (a0 + a1) / 2;
			const double ex = alongZ ? ra : rb, ez = alongZ ? rb : ra;
			const double p0x = cx + std::cos(a0) * ex, p0z = cz + std::sin(a0) * ez, p1x = cx + std::cos(a1) * ex, p1z = cz + std::sin(a1) * ez;
			const double len = Hypot(p1x - p0x, p1z - p0z) + 0.6;
			const double mx = (p0x + p1x) / 2, mz = (p0z + p1z) / 2;
			const double yaw = std::atan2(p1x - p0x, p1z - p0z);
			const double dep = 9;
			const double nx = std::cos(am), nz = std::sin(am);
			const double ccx = mx + nx * dep * 0.1, ccz = mz + nz * dep * 0.1;
			BldOpts o; o.rot = yaw; o.Tint(0.92, 0.9, 0.86); o.seed = 0.66 + k * 0.001; o.roof = "flat"; o.kind = "stand"; if (k == 0) o.name = b.superName;
			AddBuilding(&b, ccx - dep / 2, ccz - len / 2, ccx + dep / 2, ccz + len / 2, 16 + (k % 2) * 0.01, 6, o);
		}
		const double px = alongZ ? ra * 0.72 : rb * 0.7, pz = alongZ ? rb * 0.7 : ra * 0.72;
		lotSurfaces.push_back({ cx - px, cz - pz, cx + px, cz + pz, "grass" });
		for (int k = 0; k < 6; k++) { ParkingSpot p; p.x = ix0 + 8 + k * 7; p.z = iz0 + 6; p.district = b.district; p.lot = true; parkingSpots.push_back(p); }
		const double lamps[4][2] = { { ix0 + 4, iz0 + 4 }, { ix1 - 4, iz0 + 4 }, { ix0 + 4, iz1 - 4 }, { ix1 - 4, iz1 - 4 } };
		for (const auto& q : lamps) props.push_back(MkProp("streetlight", q[0], q[1]));
	} else if (b.superKind == "mall") {
		b.ground = "asphalt";
		lotSurfaces.push_back({ ix0, iz0, ix1, iz1, "asphalt" });
		{ BldOpts o; o.Tint(0.95, 0.9, 0.82); o.seed = 0.71; o.roof = "ac"; o.floorH = 6.5; o.name = b.superName; o.sign = "MALL"; AddBuilding(&b, ix0 + 12, iz0 + 8, ix1 - 12, iz0 + D * 0.55, 13, 5, o); }
		{ BldOpts o; o.Tint(0.7, 0.85, 1.0); o.seed = 0.72; o.roof = "flat"; AddBuilding(&b, cx - 14, iz0 + D * 0.55 - 1, cx + 14, iz0 + D * 0.55 + 7, 8, 1, o); }
		for (double x = ix0 + 8; x < ix1 - 8; x += 6) for (double z : { iz1 - 8, iz1 - 22 }) if (rng.Chance(0.45)) {
			ParkingSpot p; p.x = x; p.z = z; p.rot = kPi / 2 * (rng.Chance(0.5) ? 1 : -1); p.district = b.district; p.lot = true; parkingSpots.push_back(p);
		}
		for (double x = ix0 + 10; x < ix1; x += 26) props.push_back(MkProp("streetlight", x, iz1 - 15));
	} else if (b.superKind == "golf") {
		b.ground = "grass";
		{ BldOpts o; o.Tint(1, 0.97, 0.9); o.seed = 0.73; o.roof = "gable"; o.floorH = 3.5; o.kind = "house"; o.name = b.superName; AddBuilding(&b, cx - 14, iz0 + 6, cx + 14, iz0 + 20, 7, 7, o); }
		lotSurfaces.push_back({ cx - 18, iz0 + 20, cx + 18, iz0 + 34, "asphalt" });
		for (int k = 0; k < 4; k++) { ParkingSpot p; p.x = cx - 12 + k * 7; p.z = iz0 + 27; p.rot = kPi / 2; p.district = "hills"; p.lot = true; p.fancy = true; parkingSpots.push_back(p); }
		for (int k = 0; k < 40; k++) {
			const double x = rng.Range(ix0 + 4, ix1 - 4), z = rng.Range(iz0 + 40, iz1 - 4);
			if (rng.Chance(0.4)) {
				const char* type = rng.Chance(0.5) ? "palm" : "tree";
				const double rot = rng.Range(0, 6.28);
				props.push_back(MkProp(type, x, z, rot, rng.Range(1, 1.4)));
			}
		}
		lotSurfaces.push_back({ cx - 30, iz1 - 40, cx + 10, iz1 - 25, "sand" });
	} else {
		b.ground = "grass"; b.park = true;
		Park(b, rng, "park");
	}
}

CityMap::MansionInfo CityMap::Mansion(Block& b, RNG& rng, bool boss) {
	const double ix0 = b.ix0, iz0 = b.iz0, ix1 = b.ix1, iz1 = b.iz1;
	const double cx = (ix0 + ix1) / 2 + rng.Range(-6, 6);
	const double cz = (iz0 + iz1) / 2 + rng.Range(-4, 4);
	const double w = boss ? 34 : rng.Range(22, 30);
	const double d = boss ? 22 : rng.Range(14, 18);
	static const Palette mp = { { 1, 1, 0.97 }, { 0.97, 0.93, 0.85 }, { 0.92, 0.92, 0.9 } };
	const C3 tint = rng.Pick(mp);
	{ BldOpts o; o.Tint(tint); o.seed = rng.Next(); o.roof = "flat"; o.floorH = 3.7; o.cell = 3.6; o.kind = "mansion"; if (boss) o.name = "Salazar Estate"; AddBuilding(&b, cx - w / 2, cz - d / 2, cx + w / 2, cz + d / 2, boss ? 10 : 7.5, 7, o); }
	{ BldOpts o; o.Tint(tint); o.seed = rng.Next(); o.roof = "flat"; o.floorH = 3.7; o.cell = 3.6; o.kind = "mansion"; AddBuilding(&b, cx + w / 2, cz - d / 2 + 3, cx + w / 2 + 10, cz + d / 2 - 3, 4, 7, o); }
	const double pz = cz + d / 2 + 5;
	const Pool pool{ cx - 8, pz, cx + 6, pz + 7 };
	pools.push_back(pool);
	lotSurfaces.push_back({ pool.x0 - 2, pool.z0 - 2, pool.x1 + 2, pool.z1 + 2, "plaza" });
	lotSurfaces.push_back({ cx - w / 2 - 12, iz0, cx - w / 2 - 7, cz, "concrete" });
	{ ParkingSpot p; p.x = cx - w / 2 - 9.5; p.z = cz - 6; p.rot = kPi; p.district = "hills"; p.driveway = true; p.fancy = true; parkingSpots.push_back(p); }
	for (int k = 0; k < 8; k++) {
		const double x = rng.Range(ix0 + 3, ix1 - 3);
		const double z = rng.Chance(0.5) ? iz0 + rng.Range(2, 8) : iz1 - rng.Range(2, 8);
		props.push_back(MkProp("palm", x, z, 0, rng.Range(1.1, 1.5)));
	}
	const double hH = 2.2;
	fences.push_back(MkFence(ix0, iz1 - 0.6, ix1, iz1, hH, "hedge"));
	fences.push_back(MkFence(ix0, iz0, cx - w / 2 - 13, iz0 + 0.6, hH, "hedge"));
	fences.push_back(MkFence(cx - w / 2 - 6, iz0, ix1, iz0 + 0.6, hH, "hedge"));
	fences.push_back(MkFence(ix0, iz0, ix0 + 0.6, iz1, hH, "hedge"));
	fences.push_back(MkFence(ix1 - 0.6, iz0, ix1, iz1, hH, "hedge"));
	return { cx, cz, w, d };
}

void CityMap::Park(Block& b, RNG& rng, const std::string& kind) {
	const double ix0 = b.ix0, iz0 = b.iz0, ix1 = b.ix1, iz1 = b.iz1;
	const double cx = (ix0 + ix1) / 2, cz = (iz0 + iz1) / 2;
	if (kind == "plaza") {
		lotSurfaces.push_back({ ix0, iz0, ix1, iz1, "plaza" });
		props.push_back(MkProp("fountain", cx, cz));
		for (int a = 0; a < 8; a++) { const double ang = a / 8.0 * kPi * 2; props.push_back(MkProp("tree", cx + std::cos(ang) * 22, cz + std::sin(ang) * 22, 0, 1.1)); }
		for (int a = 0; a < 6; a++) { const double ang = a / 6.0 * kPi * 2 + 0.3; props.push_back(MkProp("bench", cx + std::cos(ang) * 12, cz + std::sin(ang) * 12, -ang + kPi / 2)); }
		if (b.special == "plaza") { Landmark L; L.x = cx; L.z = cz; landmarks["plaza"] = L; }
		return;
	}
	if (kind == "court") {
		lotSurfaces.push_back({ cx - 15, cz - 9, cx + 15, cz + 9, "court" });
		props.push_back(MkProp("hoop", cx - 13, cz, kPi / 2));
		props.push_back(MkProp("hoop", cx + 13, cz, -kPi / 2));
		fences.push_back(MkFence(cx - 16, cz - 10, cx + 16, cz - 9.9, 3, "chain"));
		fences.push_back(MkFence(cx - 16, cz + 9.9, cx - 3, cz + 10, 3, "chain"));
		fences.push_back(MkFence(cx + 3, cz + 9.9, cx + 16, cz + 10, 3, "chain"));
		Landmark L; L.x = cx; L.z = cz; landmarks["court"] = L;
	}
	lotSurfaces.push_back({ cx - 2, iz0, cx + 2, iz1, "path" });
	lotSurfaces.push_back({ ix0, cz - 2, ix1, cz + 2, "path" });
	const int n = kind == "park" ? 26 : 10;
	for (int k = 0; k < n; k++) {
		const double x = rng.Range(ix0 + 3, ix1 - 3), z = rng.Range(iz0 + 3, iz1 - 3);
		if (std::fabs(x - cx) < 4 || std::fabs(z - cz) < 4) continue;
		if (kind == "court" && std::fabs(x - cx) < 18 && std::fabs(z - cz) < 12) continue;
		const char* type = rng.Chance(0.3) ? "palm" : "tree";
		const double rot = rng.Range(0, 6.28);
		props.push_back(MkProp(type, x, z, rot, rng.Range(0.9, 1.5)));
	}
	for (int k = 0; k < 4; k++) props.push_back(MkProp("bench", cx + 3.2, iz0 + 10 + k * 15, -kPi / 2));
}

void CityMap::SpecialBlock(Block& b, RNG& rng, const std::string& sp) {
	const double ix0 = b.ix0, iz0 = b.iz0, ix1 = b.ix1, iz1 = b.iz1;
	const double cx = (ix0 + ix1) / 2, cz = (iz0 + iz1) / 2;
	const C3 warm = { 0.95, 0.92, 0.86 };
	auto warmC = [warm](RNG&) { return warm; };
	auto bo = [](double r, double g, double bl, double sd, const char* roof = "flat") { BldOpts o; o.Tint(r, g, bl); o.seed = sd; o.roof = roof; return o; };
	if (sp == "hospital") {
		{ BldOpts o = bo(0.95, 0.96, 1, 0.3, "helipad"); o.name = "All Saints General"; o.sign = "HOSPITAL"; AddBuilding(&b, ix0 + 4, iz0 + 4, ix1 - 4, iz0 + 36, 34, 0, o); }
		AddBuilding(&b, ix0 + 4, iz0 + 36, ix0 + 30, iz1 - 16, 14, 0, bo(0.95, 0.96, 1, 0.31, "ac"));
		lotSurfaces.push_back({ ix0 + 30, iz0 + 36, ix1, iz1, "asphalt" });
		Landmark L; L.x = cx + 10; L.z = iz1 + 1; L.pts["respawn"] = { cx + 10, iz1 + 1.5, NaN() }; L.vals["respawnRot"] = 0;
		landmarks["hospital"] = L;
	} else if (sp == "police") {
		{ BldOpts o = bo(0.85, 0.83, 0.78, 0.4, "antenna"); o.name = "LSPD Central"; o.sign = "POLICE"; AddBuilding(&b, ix0 + 6, iz0 + 4, ix1 - 6, iz0 + 30, 16, 6, o); }
		lotSurfaces.push_back({ ix0, iz0 + 30, ix1, iz1, "asphalt" });
		fences.push_back(MkFence(ix0, iz1 - 0.1, cx - 6, iz1, 2.4, "chain"));
		fences.push_back(MkFence(cx + 6, iz1 - 0.1, ix1, iz1, 2.4, "chain"));
		for (int k = 0; k < 5; k++) { ParkingSpot p; p.x = ix0 + 10 + k * 8; p.z = iz0 + 40; p.district = b.district; p.police = true; p.lot = true; parkingSpots.push_back(p); }
		Landmark L; L.x = cx; L.z = iz0 - 1; L.pts["respawn"] = { cx, iz0 - 2, NaN() }; L.vals["respawnRot"] = kPi;
		landmarks["police"] = L;
	} else if (sp == "gunshop") {
		Perimeter(b, rng, { { 16, 20 }, { 14, 22 }, { 2, 6 }, { 2, 5, 0 }, warmC, 0.15 });
		BldOpts o = bo(0.6, 0.6, 0.55, 0.77); o.name = "Gun Barn"; o.sign = "GUN BARN"; o.shop = { "gunshop", "z1" };
		const int shop = AddBuilding(&b, ix0 + 2, iz1 - 14, ix0 + 20, iz1 - 2, 6, 5, o);
		std::vector<Building> keep;
		for (int k = 0; k < (int)buildings.size(); k++) {
			const Building& x = buildings[k];
			if (k == shop || !(x.x0 < ix0 + 21 && x.x1 > ix0 + 1 && x.z1 > iz1 - 15 && x.z0 < iz1 - 1 && x.district == b.district && x.y0 < 1)) keep.push_back(x);
		}
		buildings.swap(keep);
		Landmark L; L.x = ix0 + 11; L.z = iz1 + 1.5; landmarks["gunshop"] = L;
	} else if (sp == "spray" || sp == "spray2") {
		Perimeter(b, rng, { { 14, 18 }, { 14, 22 }, { 1, 3 }, { 3, 5 }, warmC, 0.15 });
		const double x0 = ix0 + 22, x1 = ix0 + 36;
		EraseIf(buildings, [&](const Building& x) { return x.x0 < x1 + 1 && x.x1 > x0 - 1 && x.z0 < iz0 + 20 && x.z1 > iz0 && x.y0 < 1; });
		AddBuilding(&b, x0, iz0 + 6, x0 + 1, iz0 + 18, 6, 4, bo(0.9, 0.6, 0.3, 0.5));
		AddBuilding(&b, x1 - 1, iz0 + 6, x1, iz0 + 18, 6, 4, bo(0.9, 0.6, 0.3, 0.5));
		AddBuilding(&b, x0, iz0 + 17, x1, iz0 + 18, 6, 4, bo(0.9, 0.6, 0.3, 0.5));
		{ BldOpts o = bo(0.9, 0.6, 0.3, 0.5); o.y0 = CURB_H + 5; o.name = "Spray Shack"; o.sign = "SPRAY SHACK"; o.noCollide = true; AddBuilding(&b, x0 - 0.3, iz0 + 5.5, x1 + 0.3, iz0 + 18.3, 1.2, 4, o); }
		lotSurfaces.push_back({ x0, iz0, x1, iz0 + 18, "concrete" });
		Landmark L; L.x = (x0 + x1) / 2; L.z = iz0 + 12; L.pts["entry"] = { (x0 + x1) / 2, iz0 - 3, NaN() };
		landmarks[sp] = L;
	} else if (sp == "burger") {
		Houses(b, rng);
		const double bx0 = ix1 - 30;
		EraseIf(buildings, [&](const Building& x) { return x.x1 > bx0 - 2 && x.z0 < iz0 + 32 && x.district == b.district && x.x0 < ix1 && x.z1 > iz0 && x.z1 < (iz0 + iz1) / 2 + 1; });
		EraseIf(fences, [&](const Fence& f) { return f.x1 > bx0 - 2 && f.z0 < iz0 + 32 && f.x0 < ix1 && f.z1 > iz0 - 1; });
		lotSurfaces.push_back({ bx0 - 2, iz0, ix1, iz0 + 32, "asphalt" });
		BldOpts o = bo(1, 0.85, 0.5, 0.9); o.floorH = 5; o.name = "Big Bun Burgers"; o.sign = "BIG BUN"; o.shop = { "burger", "z0" };
		AddBuilding(&b, bx0 + 4, iz0 + 8, ix1 - 4, iz0 + 22, 5, 5, o);
		Landmark L; L.x = (bx0 + 4 + ix1 - 4) / 2; L.z = iz0 + 5; landmarks["burger"] = L;
	} else if (sp == "tower") {
		{ BldOpts o = bo(0.9, 0.9, 0.95, 0.12); o.floorH = 4.5; AddBuilding(&b, ix0 + 2, iz0 + 2, ix1 - 2, iz1 - 2, 18, 5, o); }
		{ BldOpts o = bo(0.75, 0.85, 0.9, 0.13); o.y0 = CURB_H + 18; o.name = "Deacon Tower"; AddBuilding(&b, ix0 + 12, iz0 + 12, ix1 - 12, iz1 - 12, 150, 1, o); }
		{ BldOpts o = bo(0.75, 0.85, 0.9, 0.14, "helipad"); o.y0 = CURB_H + 168; AddBuilding(&b, ix0 + 20, iz0 + 20, ix1 - 20, iz1 - 20, 40, 1, o); }
		Landmark L; L.x = cx; L.z = iz1 + 1; L.pts["top"] = { cx, cz, CURB_H + 208 }; landmarks["tower"] = L;
	} else if (sp == "vipers") {
		Perimeter(b, rng, { { 16, 20 }, { 14, 22 }, { 1, 3 }, { 3, 5 }, [](RNG&) { return C3{ 0.95, 0.75, 0.7 }; }, 0.15 });
		Landmark L; L.x = cx; L.z = cz; landmarks["vipers"] = L;
	} else if (sp == "warehouse") {
		b.ground = "asphalt";
		{ BldOpts o = bo(0.6, 0.65, 0.7, 0.55); o.floorH = 6; o.cell = 5; o.kind = "warehouse"; o.name = "Pier 9 Warehouse"; AddBuilding(&b, ix0 + 6, iz0 + 6, ix1 - 6, iz0 + 36, 12, 4, o); }
		for (double x = ix0 + 6; x < ix1 - 8; x += 6.5) { Container c; c.x = x; c.z = iz1 - 12; c.rot = kPi / 2; c.level = 0; c.color = ToInt32(x) % 6; containers.push_back(c); }
		Landmark L; L.x = cx; L.z = iz0 + 42; landmarks["warehouse"] = L;
	} else if (sp == "mansion_boss") {
		b.ground = "grass";
		const MansionInfo m = Mansion(b, rng, true);
		Landmark L; L.x = m.cx; L.z = m.cz + m.d / 2 + 2; L.pts["gate"] = { m.cx - m.w / 2 - 9.5, iz0 - 2, NaN() };
		landmarks["mansion"] = L;
	} else if (sp == "garage") {
		Perimeter(b, rng, { { 14, 18 }, { 14, 22 }, { 2, 5 }, { 2, 3 }, warmC, 0.15 });
		EraseIf(buildings, [&](const Building& x) { return x.x0 < ix0 + 30 && x.x1 > ix0 && x.z1 > iz1 - 20 && x.y0 < 1; });
		AddBuilding(&b, ix0 + 4, iz1 - 16, ix0 + 26, iz1 - 15, 6, 4, bo(0.4, 0.45, 0.5, 0.61));
		AddBuilding(&b, ix0 + 4, iz1 - 16, ix0 + 5, iz1 - 2, 6, 4, bo(0.4, 0.45, 0.5, 0.61));
		AddBuilding(&b, ix0 + 25, iz1 - 16, ix0 + 26, iz1 - 2, 6, 4, bo(0.4, 0.45, 0.5, 0.61));
		{ BldOpts o = bo(0.4, 0.45, 0.5, 0.61); o.y0 = CURB_H + 5.9; o.name = "Lock-Up Garage"; o.sign = "GARAGE"; o.noCollide = true; AddBuilding(&b, ix0 + 3.7, iz1 - 16.3, ix0 + 26.3, iz1 - 2, 1, 4, o); }
		lotSurfaces.push_back({ ix0 + 4, iz1 - 16, ix0 + 26, iz1, "concrete" });
		Landmark L; L.x = ix0 + 15; L.z = iz1 - 8; L.pts["entry"] = { ix0 + 15, iz1 + 3, NaN() }; landmarks["garage"] = L;
	} else if (sp == "liquor") {
		Houses(b, rng);
		const double sx0 = cx - 13, sx1 = cx + 13, mid = (iz0 + iz1) / 2;
		auto clear = [&](double ox0, double oz0, double ox1, double oz1) { return ox1 > sx0 - 1 && ox0 < sx1 + 1 && oz0 < mid - 0.2 && oz1 > iz0 - 1; };
		EraseIf(buildings, [&](const Building& x) { return clear(x.x0, x.z0, x.x1, x.z1) && x.district == b.district && x.y0 < 1; });
		EraseIf(fences, [&](const Fence& f) { return Max(f.x0, f.x1) > sx0 - 1 && Min(f.x0, f.x1) < sx1 + 1 && Min(f.z0, f.z1) < mid - 0.2 && Max(f.z0, f.z1) > iz0 - 1; });
		EraseIf(lotSurfaces, [&](const LotSurface& l) { return clear(l.x0, l.z0, l.x1, l.z1); });
		EraseIf(props, [&](const Prop& pr) { return pr.x > sx0 - 1 && pr.x < sx1 + 1 && pr.z > iz0 - 1 && pr.z < mid - 0.2; });
		EraseIf(parkingSpots, [&](const ParkingSpot& pr) { return pr.x > sx0 - 1 && pr.x < sx1 + 1 && pr.z > iz0 - 1 && pr.z < mid - 0.2; });
		lotSurfaces.push_back({ sx0, iz0, sx1, mid - 0.2, "asphalt" });
		{ BldOpts o = bo(0.95, 0.88, 0.7, 0.83, "ac"); o.floorH = 5; o.name = "Ray's Liquor"; o.sign = "LIQUOR"; o.shop = { "liquor", "z0" }; AddBuilding(&b, cx - 11, iz0 + 9, cx + 11, iz0 + 21, 5, 5, o); }
		props.push_back(MkProp("trashcan", cx + 12, iz0 + 8));
		props.push_back(MkProp("phonebooth", cx - 12.2, iz0 + 7.5));
		Landmark L; L.x = cx; L.z = iz0 - 1; landmarks["liquor"] = L;
	} else if (sp == "projects") {
		b.ground = "grass";
		{ BldOpts o = bo(0.8, 0.6, 0.5, 0.2, "ac"); o.name = "Cedar Row Projects"; AddBuilding(&b, ix0 + 4, iz0 + 4, ix1 - 4, iz0 + 20, 13.6, 2, o); }
		AddBuilding(&b, ix0 + 4, iz1 - 20, ix1 - 4, iz1 - 4, 13.6, 2, bo(0.8, 0.6, 0.5, 0.21, "ac"));
		lotSurfaces.push_back({ ix0 + 4, iz0 + 26, ix1 - 4, iz1 - 26, "asphalt" });
		Landmark L; L.x = cx; L.z = cz; landmarks["projects"] = L;
		for (int k = 0; k < 3; k++) { ParkingSpot p; p.x = ix0 + 14 + k * 9; p.z = cz; p.rot = kPi / 2; p.district = "hood"; p.lot = true; parkingSpots.push_back(p); }
	} else if (sp == "pierfront") {
		Perimeter(b, rng, { { 14, 18 }, { 12, 20 }, { 1, 3 }, { 5, 7, 3 }, [](RNG&) { return C3{ 1, 0.95, 0.88 }; }, 0.15 });
		Landmark L; L.x = cx; L.z = iz1 + 1; landmarks["pierfront"] = L;
	}
}

// Street furniture: street lights, traffic lights, palms along sidewalks, hydrants, etc.
void CityMap::StreetProps() {
	RNG rng(seed + 99);
	for (int bi : blocks) {
		const Block& b = blockStore[bi];
		const double x0 = b.x0, z0 = b.z0, x1 = b.x1, z1 = b.z1;
		const std::string& district = b.district;
		struct Ed { double ax, az, bx, bz, nx, nz; };
		const Ed edges[4] = { { x0, z0, x1, z0, 0, -1 }, { x1, z0, x1, z1, 1, 0 }, { x1, z1, x0, z1, 0, 1 }, { x0, z1, x0, z0, -1, 0 } };
		const bool palmy = district == "beach" || district == "hills" || district == "hood" || district == "westside";
		for (int e = 0; e < 4; e++) {
			const Ed& E = edges[e];
			const double len = Hypot(E.bx - E.ax, E.bz - E.az);
			const double dx = (E.bx - E.ax) / len, dz = (E.bz - E.az) / len;
			const double lightRot = std::atan2(E.nx, E.nz);
			const double offs = 0.6;
			if (e == 0 || e == 2 || b.district != "hood")
				for (double t = 12; t < len - 8; t += 34) props.push_back(MkProp("streetlight", E.ax + dx * t - E.nx * offs, E.az + dz * t - E.nz * offs, lightRot));
			const double treeStep = palmy ? 13 : 17;
			if (district != "docks" && (palmy || district == "downtown" || district == "midtown" || rng.Chance(0.5))) {
				for (double t = 6 + (e * 3) % 7; t < len - 6; t += treeStep) {
					if (rng.Chance(0.25)) continue;
					const double rot = rng.Range(0, 6.28);
					Prop p = MkProp(palmy ? "palm" : "tree", E.ax + dx * t - E.nx * 1.3, E.az + dz * t - E.nz * 1.3, rot, rng.Range(0.9, 1.3));
					p.street = true;
					props.push_back(p);
				}
			}
			if (rng.Chance(0.55)) { const double px = E.ax + dx * rng.Range(10, len - 10) - E.nx * 0.8; const double pz = E.az + dz * rng.Range(10, len - 10) - E.nz * 0.8; props.push_back(MkProp("hydrant", px, pz)); }
			if (district != "hills" && rng.Chance(0.4)) { const double px = E.ax + dx * rng.Range(8, len - 8) - E.nx * 0.9; const double pz = E.az + dz * rng.Range(8, len - 8) - E.nz * 0.9; props.push_back(MkProp("trashcan", px, pz)); }
			if ((district == "midtown" || district == "downtown" || district == "beach") && rng.Chance(0.3)) {
				const double t = rng.Range(15, len - 15);
				props.push_back(MkProp("busstop", E.ax + dx * t - E.nx * 3.0, E.az + dz * t - E.nz * 3.0, lightRot + kPi));
			}
			if (rng.Chance(0.25)) { const double px = E.ax + dx * rng.Range(10, len - 10) - E.nx * 1.0; const double pz = E.az + dz * rng.Range(10, len - 10) - E.nz * 1.0; props.push_back(MkProp("phonebooth", px, pz, lightRot)); }
			if (district != "hills" && district != "docks" && rng.Chance(0.55)) {
				for (double t = 16; t < len - 16; t += 7) {
					if (rng.Chance(0.55)) continue;
					ParkingSpot p; p.x = E.ax + dx * t; p.z = E.az + dz * t; p.edge = e; p.district = district; p.curb = true; p.rot = 0;
					parkingSpots.push_back(p);
				}
			}
		}
		const double cornersInfo[4][4] = { { x0, z0, -1, -1 }, { x1, z0, 1, -1 }, { x1, z1, 1, 1 }, { x0, z1, -1, 1 } };
		for (const auto& c : cornersInfo) {
			const int ni = NearestX(c[0] + c[2] * 10), nj = NearestZ(c[1] + c[3] * 10);
			if (RbSet(ni, nj)) { props.push_back(MkProp("palm", c[0] - c[2] * 1.4, c[1] - c[3] * 1.4, 0, 1.1)); continue; }
			Prop p = MkProp("trafficlight", c[0] - c[2] * 0.8, c[1] - c[3] * 0.8); p.hasCorner = true; p.corner[0] = (int)c[2]; p.corner[1] = (int)c[3];
			props.push_back(p);
		}
	}
	const double k = HALF_ROAD - PARK_OFF;
	for (ParkingSpot& sp : parkingSpots) {
		if (!sp.curb) continue;
		if (sp.edge == 0) { sp.z -= k; sp.rot = kPi / 2; }
		else if (sp.edge == 1) { sp.x += k; sp.rot = 0; }
		else if (sp.edge == 2) { sp.z += k; sp.rot = -kPi / 2; }
		else { sp.x -= k; sp.rot = kPi; }
	}
	EraseIf(props, [&](const Prop& p) { return IsOnRoad(p.x, p.z) && p.type != "trafficlight"; });
}

void CityMap::BuildSidewalkGraph() {
	std::vector<WalkNode>& nodes = walkNodes;
	const int nI = (int)XS.size() - 1;
	for (int bi : blocks) {
		Block& b = blockStore[bi];
		b.nodeIds.clear();
		for (const V2& c : b.corners) { WalkNode n; n.id = (int)nodes.size(); n.x = c.x; n.z = c.z; n.block = bi; nodes.push_back(n); b.nodeIds.push_back(n.id); }
		for (int k = 0; k < 4; k++) { const int a = b.nodeIds[k], c = b.nodeIds[(k + 1) % 4]; nodes[a].links.push_back(c); nodes[c].links.push_back(a); }
	}
	for (int bi : blocks) {
		const Block& b = blockStore[bi];
		const Block* east = b.i + 1 < nI ? GetBlock(b.i + 1, b.j) : nullptr;
		const Block* south = GetBlock(b.i, b.j + 1);
		auto link = [&](int a, int c) { nodes[a].links.push_back(c); nodes[c].links.push_back(a); nodes[a].cross.push_back(c); nodes[c].cross.push_back(a); };
		if (east) { link(b.nodeIds[1], east->nodeIds[0]); link(b.nodeIds[2], east->nodeIds[3]); }
		if (south && south->j == b.j + 1) { link(b.nodeIds[3], south->nodeIds[0]); link(b.nodeIds[2], south->nodeIds[1]); }
	}
}

void CityMap::RetrofitShops() {
	struct Want { const char* key; const char* name; const char* sign; std::vector<std::string> districts; const char* lm; };
	const std::vector<Want> WANT = {
		{ "petshop", "Pet Palace", "PET PALACE", { "midtown", "westside" }, "petshop" },
		{ "store", "24/7 Downtown", "24/7", { "downtown" }, "store1" },
		{ "store", "24/7 Rosewood", "24/7", { "westside", "beach" }, "store2" },
		{ "store", "24/7 El Corona", "24/7", { "corona", "hood" }, "store3" },
		{ "bar", "The Rusty Anchor", "RUSTY ANCHOR", { "docks", "hood", "corona" }, "bar" },
		{ "cafe", "Bean Scene", "BEAN SCENE", { "downtown", "midtown" }, "cafe" },
	};
	std::vector<V2> used;
	auto hash = [](double x, double z) {
		const int32_t a = ToInt32(JsRound(x) * 73856093.0), b = ToInt32(JsRound(z) * 19349663.0);
		uint32_t h = (uint32_t)(a ^ b) * 0x5bd1e995u;
		h ^= h >> 13;
		return h / 4294967296.0;
	};
	for (const Want& W : WANT) {
		struct Cand { int b; std::string front; double cx, cz, fz, h; };
		std::vector<Cand> cands;
		for (int bi = 0; bi < (int)buildings.size(); bi++) {
			const Building& b = buildings[bi];
			if (b.shop.valid() || b.rot != 0 || !b.name.empty() || !b.sign.empty() || b.noCollide || (IsSet(b.base) && b.base != 0) || b.kind == "house" || b.kind == "warehouse" || b.kind == "mansion" || b.kind == "stand") continue;
			if (std::find(W.districts.begin(), W.districts.end(), b.district) == W.districts.end() || b.y0 > CURB_H + 0.6) continue;
			const double w = b.x1 - b.x0, d = b.z1 - b.z0;
			if (w < 14 || w > 30 || d < 11 || d > 24 || b.y1 - b.y0 < 4.4) continue;
			const double cx = (b.x0 + b.x1) / 2, cz = (b.z0 + b.z1) / 2;
			const Block* blk = BlockAt(cx, cz);
			if (!blk || !blk->special.empty() || blk->park) continue;
			const std::string front = std::fabs(b.z0 - blk->iz0) < 3.5 ? "z0" : std::fabs(b.z1 - blk->iz1) < 3.5 ? "z1" : "";
			if (front.empty()) continue;
			const double fz = front == "z0" ? b.z0 : b.z1, out = front == "z0" ? -1 : 1;
			auto inFront = [&](double x0, double z0, double x1, double z1) { return x1 > cx - 2 && x0 < cx + 2 && (out < 0 ? z1 > fz - 3 && z0 < fz + 0.1 : z0 < fz + 3 && z1 > fz - 0.1); };
			bool blocked = false;
			for (int oi = 0; oi < (int)buildings.size() && !blocked; oi++) {
				if (oi == bi) continue;
				const Building& o = buildings[oi];
				if (inFront(o.x0, o.z0, o.x1, o.z1) || (o.x1 > b.x0 + 0.3 && o.x0 < b.x1 - 0.3 && o.z1 > b.z0 + 0.3 && o.z0 < b.z1 - 0.3 && o.y0 < b.y0 + 3)) blocked = true;
			}
			if (blocked) continue;
			for (const Fence& f : fences) if (inFront(Min(f.x0, f.x1), Min(f.z0, f.z1), Max(f.x0, f.x1), Max(f.z0, f.z1))) { blocked = true; break; }
			if (blocked) continue;
			for (const V2& u : used) if (Hypot(u.x - cx, u.z - cz) < 220) { blocked = true; break; }
			if (blocked) continue;
			cands.push_back({ bi, front, cx, cz, fz, hash(cx, cz) });
		}
		std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& c) { return a.h - c.h < 0; });
		if (cands.empty()) continue;
		const Cand c = cands[0];
		Building& b = buildings[c.b];
		b.shop = { W.key, c.front };
		b.name = W.name; b.sign = W.sign; b.noCollide = true;
		EraseIf(props, [&](const Prop& p) { return std::fabs(p.x - c.cx) < 2.2 && std::fabs(p.z - c.fz) < 3.5; });
		used.push_back({ c.cx, c.cz });
		Landmark L; L.x = c.cx; L.z = c.fz + (c.front == "z0" ? -2 : 2); L.name = W.name;
		landmarks[W.lm] = L;
	}
}

void CityMap::BuildLandmarks() {
	const Landmark pier = landmarks["pier"];
	RetrofitShops();
	{ Landmark L; L.x = (pier.vals.at("x0") + pier.vals.at("x1")) / 2; L.z = pier.vals.at("z1") - 40; landmarks["ferris"] = L; }
	{ Landmark L; L.x = -60; L.z = -1010; landmarks["sign"] = L; }
	{ Landmark L; L.x = -40; L.z = CITY_MAXZ + 30; landmarks["beach"] = L; }
	{ Landmark L; L.x = 900; L.z = 300; landmarks["docksQuay"] = L; }
	colliders.clear();
	for (const Building& b : buildings) {
		if (b.noCollide) continue;
		const double maxY = b.y1 + (b.roof == "gable" ? 2.5 : 0);
		Collider c; c.type = "building"; c.maxY = maxY;
		if (b.rot != 0) { c.oriented = true; c.cx = (b.x0 + b.x1) / 2; c.cz = (b.z0 + b.z1) / 2; c.hx = (b.x1 - b.x0) / 2; c.hz = (b.z1 - b.z0) / 2; c.yaw = b.rot; c.minY = b.y0 - 1.2; }
		else { c.minX = b.x0; c.minZ = b.z0; c.maxX = b.x1; c.maxZ = b.z1; c.minY = b.kind == "pier" ? -8 : b.y0 - 0.2; }
		colliders.push_back(c);
	}
	for (const Fence& f : fences) {
		const double y0 = IsSet(f.y) ? f.y : 0;
		Collider c; c.type = "fence"; c.minY = y0 - 0.5; c.maxY = y0 + CURB_H + f.h; c.soft = f.type != "hedge" && f.type != "wall";
		if (f.rot != 0) { c.oriented = true; c.cx = f.cx; c.cz = f.cz; c.hx = f.hx; c.hz = f.hz; c.yaw = f.rot; }
		else { c.minX = Min(f.x0, f.x1); c.minZ = Min(f.z0, f.z1); c.maxX = Max(f.x0, f.x1); c.maxZ = Max(f.z0, f.z1); }
		colliders.push_back(c);
	}
	for (const Container& ct : containers) {
		const double hx = 1.25, hz = 3.05;
		const bool ch = std::fabs(std::sin(ct.rot)) > 0.5;
		const double cy = IsSet(ct.y) ? ct.y : 0;
		Collider c; c.type = "container";
		c.minX = ct.x - (ch ? hz : hx); c.maxX = ct.x + (ch ? hz : hx); c.minZ = ct.z - (ch ? hx : hz); c.maxZ = ct.z + (ch ? hx : hz);
		c.minY = cy + ct.level * 2.6; c.maxY = cy + (ct.level + 1) * 2.6 + CURB_H;
		colliders.push_back(c);
	}
	PlanInteriors();
}

// ------------------------------------------------------------------ walk-in shops (interiors.js)
void InteriorShell::Rect(double u0, double w0, double u1, double w1, double out[4]) const {
	const double xa = X(u0, w0), xb = X(u1, w1), za = Z(u0, w0), zb = Z(u1, w1);
	out[0] = Min(xa, xb); out[1] = Min(za, zb); out[2] = Max(xa, xb); out[3] = Max(za, zb);
}
double InteriorShell::YawIn() const { return std::atan2(f[0], f[1]); }
double InteriorShell::YawR() const { return std::atan2(r[0], r[1]); }
bool InteriorShell::Inside(double x, double z, double pad) const {
	double q[4]; Rect(-W / 2, 0, W / 2, D, q);
	return x > q[0] - pad && x < q[2] + pad && z > q[1] - pad && z < q[3] + pad;
}

namespace {
const double InteriorDoorW = 2.4, InteriorDoorH = 2.7;
void InteriorSet3(double* d, double a, double b, double c) { d[0] = a; d[1] = b; d[2] = c; }

// LAYOUTS: the room's colours, counter, the clerk's, service and till spots, the ceiling lights
InteriorShell::Layout InteriorLayout(const std::string& key, const InteriorShell& F) {
	InteriorShell::Layout L;
	const double D = F.D, H = F.W / 2;
	L.H = H;
	auto counter = [&](double u0, double w0, double u1, double w1, double t0, double t1, double t2, double b0, double b1, double b2) {
		L.counter.u0 = u0; L.counter.w0 = w0; L.counter.u1 = u1; L.counter.w1 = w1;
		InteriorSet3(L.counter.top, t0, t1, t2); InteriorSet3(L.counter.body, b0, b1, b2);
	};
	auto spots = [&](double cu, double cw, double su, double sw, double tu, double tw) { L.clerk = { cu, cw }; L.service = { su, sw }; L.till = { tu, tw }; };
	auto colours = [&](std::array<double, 3> f0, std::array<double, 3> f1, std::array<double, 3> wall, std::array<double, 3> dado) {
		for (int k = 0; k < 3; k++) { L.floor[0][k] = f0[k]; L.floor[1][k] = f1[k]; L.wall[k] = wall[k]; L.dado[k] = dado[k]; }
	};
	if (key == "gunshop") {
		L.name = "Gun Barn"; L.ceil = 3.6;
		colours({ 0.25, 0.27, 0.25 }, { 0.3, 0.32, 0.3 }, { 0.62, 0.58, 0.5 }, { 0.36, 0.25, 0.16 });
		counter(-6, D - 3.6, 6, D - 2.8, 0.3, 0.2, 0.12, 0.2, 0.2, 0.22);
		spots(0, D - 1.9, 0, D - 4.5, 1.5, D - 3.2);
		for (const auto& l : std::vector<std::array<double, 2>>{ { -5, 4 }, { 5, 4 }, { -5, 8.5 }, { 5, 8.5 } }) if (l[1] < D - 1) L.lights.push_back(l);
		L.hostile = true; L.extra = "gunshop";
	} else if (key == "burger") {
		L.name = "Big Bun Burgers"; L.ceil = 3.6;
		colours({ 0.78, 0.1, 0.08 }, { 0.93, 0.92, 0.88 }, { 0.95, 0.88, 0.7 }, { 0.7, 0.12, 0.08 });
		counter(-7, D - 5.2, 7, D - 4.4, 0.72, 0.72, 0.74, 0.78, 0.14, 0.1);
		spots(0, D - 3.5, 0, D - 6.1, -2.5, D - 4.8);
		L.lights = { { -6, 3.5 }, { 6, 3.5 }, { -6, 8 }, { 6, 8 }, { 0, D - 2 } };
		L.extra = "burger";
	} else if (key == "liquor" || key == "store") {
		L.name = key == "store" ? "24/7" : "Ray's Liquor"; L.ceil = 3.4;
		if (key == "store") colours({ 0.92, 0.92, 0.9 }, { 0.8, 0.84, 0.82 }, { 0.94, 0.96, 0.92 }, { 0.08, 0.5, 0.25 });
		else colours({ 0.82, 0.82, 0.8 }, { 0.66, 0.68, 0.68 }, { 0.72, 0.84, 0.76 }, { 0.18, 0.36, 0.26 });
		counter(-H + 2.4, 1.3, -H + 3.2, 5.2, 0.35, 0.22, 0.14, 0.28, 0.18, 0.12);
		spots(-H + 1.3, 3.2, -H + 4.5, 3.2, -H + 2.8, 2.4); L.clerkFacesR = true;
		L.lights = { { -5, 3 }, { 2, 3 }, { -5, 8 }, { 2, 8 }, { 8, 6 } };
		L.extra = "liquor";
	} else if (key == "petshop") {
		L.name = "Pet Palace"; L.ceil = 3.5;
		colours({ 0.82, 0.78, 0.66 }, { 0.74, 0.7, 0.58 }, { 0.86, 0.94, 0.84 }, { 0.2, 0.46, 0.3 });
		counter(1.2, D - 3.4, H - 1.2, D - 2.7, 0.9, 0.9, 0.86, 0.2, 0.46, 0.3);
		spots((H + 1.2) / 2, D - 1.7, (H + 1.2) / 2, D - 4.3, H - 2.2, D - 3.05);
		L.lights = { { -H / 2, 3 }, { H / 2, 3 }, { -H / 2, D - 3 }, { H / 2, D - 3 } };
		L.extra = "petshop";
	} else if (key == "bar") {
		L.name = "The Rusty Anchor"; L.ceil = 3.4;
		colours({ 0.28, 0.18, 0.11 }, { 0.24, 0.15, 0.09 }, { 0.42, 0.28, 0.2 }, { 0.2, 0.12, 0.07 });
		counter(-H + 2.6, 2.2, -H + 3.4, D - 2.2, 0.18, 0.1, 0.05, 0.3, 0.18, 0.1);
		spots(-H + 1.4, D / 2, -H + 4.4, D / 2, -H + 3.0, D - 3.2); L.clerkFacesR = true;
		L.lights = { { -H + 3, D / 3 }, { -H + 3, D * 2 / 3 }, { 2, D / 2 }, { H - 2.5, 3 }, { H - 2.5, D - 3 } };
		L.extra = "bar";
	} else if (key == "cafe") {
		L.name = "Bean Scene"; L.ceil = 3.4;
		colours({ 0.55, 0.38, 0.24 }, { 0.6, 0.42, 0.27 }, { 0.93, 0.88, 0.8 }, { 0.36, 0.24, 0.16 });
		counter(-H + 1.5, D - 3.4, 2.5, D - 2.6, 0.85, 0.85, 0.82, 0.36, 0.24, 0.16);
		spots(-2, D - 1.6, -2, D - 4.3, 1.2, D - 3.0);
		L.lights = { { -H / 2, 3 }, { H / 2, 3 }, { -H / 2, D - 3 }, { H / 2, D - 3 } };
		L.extra = "cafe";
	}
	return L;
}

// the per-shop furniture: its colliders, and what the renderer dresses
template <typename Box> void InteriorFurniture(InteriorShell& it, const Box& box) {
	const auto& L = it.L;
	const double D = it.D, H = it.W / 2;
	auto add = [&](const std::string& kind, InteriorShell::Furniture fu) { fu.kind = kind; it.furniture.push_back(fu); };
	auto ext = [](double u0, double w0, double u1, double w1) { InteriorShell::Furniture f; f.u0 = u0; f.w0 = w0; f.u1 = u1; f.w1 = w1; return f; };
	auto at = [](double u, double w) { InteriorShell::Furniture f; f.u = u; f.w = w; return f; };
	if (L.extra == "gunshop") {
		// ammo shelving down both sides, an armour case, pegboard racks on the back wall
		box(-H + 0.25, 2, -H + 0.85, D - 4.2, 0, 2.2, "shelf"); { auto f = ext(-H + 0.25, 2, -H + 0.85, D - 4.2); f.h = 2.2; f.face = 1; add("shelf", f); }
		box(H - 0.85, 2, H - 0.25, D - 4.2, 0, 2.2, "shelf"); { auto f = ext(H - 0.85, 2, H - 0.25, D - 4.2); f.h = 2.2; f.face = -1; add("shelf", f); }
		box(3.4, 3.2, 7.4, 4.2, 0, 1.0, "case"); add("case", ext(3.4, 3.2, 7.4, 4.2)); // (clear of the way to the counter)
	} else if (L.extra == "burger") {
		// the kitchen line along the back wall
		box(-9, D - 1.1, 9, D - 0.25, 0, 1.0, "kitchen"); add("kitchen", {});
		// tables (not in the aisle to the counter)
		for (double u : { -7.5, -4.0, 4.0, 7.5 }) for (double w : { 2.8, 6.0 }) {
			if (w > D - 6.4) continue;
			box(u - 0.45, w - 0.45, u + 0.45, w + 0.45, 0, 0.8, "table");
			add("table", at(u, w));
		}
	} else if (L.extra == "petshop") {
		// kennels down the left wall (glass fronts), the aquarium wall on the right, a food aisle in the middle
		const int nK = (int)Max(2, Min(4, std::floor((D - 2.5) / 2.6)));
		for (int k = 0; k < nK; k++) {
			const double w0 = 1.8 + k * 2.6;
			box(-H + 0.25, w0 - 0.05, -H + 2.4, w0 + 0.05, 0, 1.2, "pen"); // (a divider)
			auto f = ext(-H + 0.25, w0, -H + 2.4, w0 + 2.5); f.i = k; add("pen", f);
		}
		box(-H + 2.35, 1.8, -H + 2.45, 1.8 + nK * 2.6, 0, 1.2, "glass");
		box(H - 0.9, 1.5, H - 0.25, D - 4.2, 0, 2.0, "tanks"); add("tanks", ext(H - 0.9, 1.5, H - 0.25, D - 4.2));
		box(-0.5, 2.6, 0.5, Max(4, D - 5.6), 0, 1.6, "shelf"); { auto f = at(0, 0); f.w0 = 2.6; f.w1 = Max(4, D - 5.6); add("aisle", f); }
	} else if (L.extra == "bar") {
		// the bottle wall behind the bar, a pool table, booths down the right wall
		box(-H + 0.25, 1.6, -H + 0.7, D - 1.2, 0, 2.3, "shelf"); add("bottles", {});
		const double pu = H * 0.25, pw = D * 0.5;
		box(pu - 1.3, pw - 0.75, pu + 1.3, pw + 0.75, 0, 0.85, "table"); add("pool", at(pu, pw));
		for (double w = 2.2; w < D - 2; w += 3.2) { box(H - 1.9, w - 0.45, H - 0.25, w + 0.45, 0, 0.75, "table"); add("booth", at(0, w)); }
		for (double w = 2.8; w < D - 2.2; w += 1.3) add("stool", at(-H + 3.9, w));
	} else if (L.extra == "cafe") {
		// a pastry case on the counter, tables by the window
		for (const auto& q : std::vector<std::array<double, 2>>{ { -H + 2.5, 2.8 }, { H - 2.5, 2.8 }, { H - 2.5, 5.8 }, { 3, 5.8 } }) {
			const double u = q[0], w = q[1];
			if (w > D - 5.2 || u > H - 1.5) continue;
			box(u - 0.4, w - 0.4, u + 0.4, w + 0.4, 0, 0.78, "table"); add("table", at(u, w));
		}
	} else if (L.extra == "liquor") {
		for (double u : { -2.5, 1.5, 5.5 }) { box(u - 0.5, 3.4, u + 0.5, Min(8.6, D - 2.2), 0, 1.75, "shelf"); auto f = at(u, 0); f.w0 = 3.4; f.w1 = Min(8.6, D - 2.2); add("aisle", f); }
		box(-H + 2, D - 0.95, H - 0.3, D - 0.25, 0, 2.2, "fridge"); { InteriorShell::Furniture f; f.u0 = -H + 2; f.u1 = H - 0.3; add("fridges", f); }
		box(-H + 0.25, 1.4, -H + 0.7, 5.0, 0.9, 2.4, "shelf"); add("smokes", {});
	}
}
}

// planInteriors: the shell walls round the room with a doorway in the shop front, the ceiling slab, the
// counter and the furniture (all colliders), and the spots
void CityMap::PlanInteriors() {
	interiors.clear();
	for (int bi = 0; bi < (int)buildings.size(); bi++) {
		const Building& b = buildings[bi];
		if (!b.shop.valid()) continue;
		InteriorShell it;
		const bool z1 = b.shop.front == "z1";
		it.fy = b.y0;
		it.f[0] = 0; it.f[1] = z1 ? -1 : 1;
		it.r[0] = -it.f[1]; it.r[1] = it.f[0];
		it.ox = (b.x0 + b.x1) / 2; it.oz = z1 ? b.z1 : b.z0;
		it.W = b.x1 - b.x0; it.D = b.z1 - b.z0;
		it.L = InteriorLayout(b.shop.key, it);
		it.key = b.shop.key; it.name = !b.name.empty() ? b.name : it.L.name; it.building = bi;
		const InteriorShell::Layout& L = it.L;
		const double y0 = it.fy, y1 = Max(b.y1, it.fy + L.ceil + 0.6);
		auto box = [&](double u0, double w0, double u1, double w1, double h0, double h1, const char* type) {
			double q[4]; it.Rect(u0, w0, u1, w1, q);
			Collider c; c.minX = q[0]; c.minZ = q[1]; c.maxX = q[2]; c.maxZ = q[3]; c.minY = y0 + h0; c.maxY = y0 + h1; c.type = type;
			it.colliders.push_back(c); colliders.push_back(c);
		};
		const double T = 0.25, H = it.W / 2, D = it.D;
		// the shell: back, sides, and the front either side of the doorway (plus the lintel over it)
		box(-H, D - T, H, D, -0.3, y1 - y0, "building");
		box(-H, 0, -H + T, D, -0.3, y1 - y0, "building");
		box(H - T, 0, H, D, -0.3, y1 - y0, "building");
		box(-H, 0, -InteriorDoorW / 2, T, -0.3, y1 - y0, "building");
		box(InteriorDoorW / 2, 0, H, T, -0.3, y1 - y0, "building");
		box(-InteriorDoorW / 2, 0, InteriorDoorW / 2, T, InteriorDoorH, y1 - y0, "building");
		// the roof slab over the room (bullets and the camera stop at it), and the building's own roof on top
		box(-H, 0, H, D, L.ceil, L.ceil + 0.3, "building");
		if (y1 - y0 > L.ceil + 1.5) box(-H, 0, H, D, y1 - y0 - 0.3, y1 - y0, "building");
		// the counter
		box(L.counter.u0, L.counter.w0, L.counter.u1, L.counter.w1, 0, 1.05, "counter");
		InteriorFurniture(it, box);
		// the spots in world space
		auto sp = [&](double u, double w) { P3 p; p.x = it.X(u, w); p.z = it.Z(u, w); p.y = it.fy; return p; };
		it.clerk = sp(L.clerk.u, L.clerk.w);
		it.clerkYaw = L.clerkFacesR ? it.YawR() : it.YawIn() + kPi;
		it.service = sp(L.service.u, L.service.w);
		it.till = sp(L.till.u, L.till.w); it.till.y = it.fy + 1.1;
		it.door = sp(0, -1.2);
		it.center = sp(0, D / 2);
		it.light = sp(0, D / 2); it.light.y = it.fy + L.ceil - 0.4;
		interiors.push_back(it);
	}
}

} // namespace atg
