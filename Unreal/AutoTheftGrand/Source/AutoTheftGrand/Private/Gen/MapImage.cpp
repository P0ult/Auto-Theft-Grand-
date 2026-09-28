#include "MapImage.h"
#include <algorithm>
#include <array>
#include <cstdio>

namespace atg {

namespace {
struct Rgba { double r, g, b, a; };
Rgba Hex(const char* s, double a = 1) {
	unsigned v = 0;
	std::sscanf(s + 1, "%x", &v);
	return { (double)((v >> 16) & 255), (double)((v >> 8) & 255), (double)(v & 255), a };
}

// just enough of a 2D canvas: filled polygons and thick polylines (round joins and caps), with alpha
// blending that doesn't double up where one path overlaps itself
struct Canvas {
	MapLayer& L;
	double sx, sz;
	std::vector<uint32_t> stamp;
	uint32_t path = 0;
	Canvas(MapLayer& layer, int W, int H) : L(layer) {
		L.w = W; L.h = H;
		L.rgba.assign((size_t)W * H * 4, 0);
		stamp.assign((size_t)W * H, 0);
		sx = W / (L.maxX - L.minX); sz = H / (L.maxZ - L.minZ);
	}
	double X(double x) const { return (x - L.minX) * sx; }
	double Z(double z) const { return (z - L.minZ) * sz; }
	void Put(int x, int y, const Rgba& c) {
		if (x < 0 || y < 0 || x >= L.w || y >= L.h) return;
		const size_t k = (size_t)y * L.w + x;
		if (stamp[k] == path) return;
		stamp[k] = path;
		uint8_t* p = &L.rgba[k * 4];
		const double a = c.a, ia = 1 - a, da = p[3] / 255.0;
		const double oa = a + da * ia;
		auto mix = [&](int i, double v) { p[i] = (uint8_t)std::lround(Clamp(oa > 0 ? (v * a + p[i] * da * ia) / oa : 0, 0, 255)); };
		mix(0, c.r); mix(1, c.g); mix(2, c.b);
		p[3] = (uint8_t)std::lround(Clamp(oa * 255, 0, 255));
	}
	// scanline fill of a polygon in pixel coordinates (even-odd, pixel centres)
	void Fill(const std::vector<V2>& pts, const Rgba& c) {
		path++;
		double y0 = 1e30, y1 = -1e30;
		for (const V2& p : pts) { y0 = Min(y0, p.z); y1 = Max(y1, p.z); }
		const int ya = Max(0, (int)std::floor(y0)), yb = Min(L.h - 1, (int)std::ceil(y1));
		std::vector<double> xs;
		for (int y = ya; y <= yb; y++) {
			const double cy = y + 0.5;
			xs.clear();
			for (size_t i = 0, n = pts.size(); i < n; i++) {
				const V2& a = pts[i]; const V2& b = pts[(i + 1) % n];
				if ((a.z <= cy && b.z > cy) || (b.z <= cy && a.z > cy)) xs.push_back(a.x + (cy - a.z) / (b.z - a.z) * (b.x - a.x));
			}
			std::sort(xs.begin(), xs.end());
			for (size_t k = 0; k + 1 < xs.size(); k += 2) {
				const int xa = (int)std::ceil(xs[k] - 0.5), xb = (int)std::floor(xs[k + 1] - 0.5);
				for (int x = xa; x <= xb; x++) Put(x, y, c);
			}
		}
	}
	void Rect(double x0, double z0, double x1, double z1, const Rgba& c) {
		Fill({ { X(x0), Z(z0) }, { X(x1), Z(z0) }, { X(x1), Z(z1) }, { X(x0), Z(z1) } }, c);
	}
	// a rectangle turned by yaw about its centre (the buildings' and pads' convention)
	void Box(double cx, double cz, double hx, double hz, double yaw, const Rgba& c) {
		const double s = std::sin(yaw), co = std::cos(yaw);
		std::vector<V2> q;
		const double L2[4][2] = { { -hx, -hz }, { hx, -hz }, { hx, hz }, { -hx, hz } };
		for (const auto& l : L2) q.push_back({ X(cx + l[0] * co + l[1] * s), Z(cz - l[0] * s + l[1] * co) });
		Fill(q, c);
	}
	// thick polyline in pixel coordinates: every point within w/2 of a segment
	void Stroke(const std::vector<V2>& pts, double w, const Rgba& c, bool newPath = true) {
		if (newPath) path++;
		const double r = w / 2;
		for (size_t i = 0; i + 1 < pts.size(); i++) {
			const V2 a = pts[i], b = pts[i + 1];
			const int xa = Max(0, (int)std::floor(Min(a.x, b.x) - r - 1)), xb = Min(L.w - 1, (int)std::ceil(Max(a.x, b.x) + r + 1));
			const int ya = Max(0, (int)std::floor(Min(a.z, b.z) - r - 1)), yb = Min(L.h - 1, (int)std::ceil(Max(a.z, b.z) + r + 1));
			const double dx = b.x - a.x, dz = b.z - a.z, l2 = dx * dx + dz * dz;
			for (int y = ya; y <= yb; y++) for (int x = xa; x <= xb; x++) {
				const double px = x + 0.5 - a.x, pz = y + 0.5 - a.z;
				const double t = l2 > 1e-12 ? Clamp((px * dx + pz * dz) / l2, 0, 1) : 0;
				const double ex = px - dx * t, ez = pz - dz * t;
				if (ex * ex + ez * ez <= r * r) Put(x, y, c);
			}
		}
	}
	// dashed: on / off lengths in pixels
	void Dashed(const std::vector<V2>& pts, double w, double on, double off, const Rgba& c) {
		path++;
		double phase = 0;
		for (size_t i = 0; i + 1 < pts.size(); i++) {
			const V2 a = pts[i], b = pts[i + 1];
			const double len = std::hypot(b.x - a.x, b.z - a.z);
			double s = 0;
			while (s < len) {
				const double period = on + off, into = std::fmod(phase, period);
				const bool drawing = into < on;
				const double step = Min(len - s, drawing ? on - into : period - into);
				if (drawing && step > 0) {
					const double t0 = s / len, t1 = (s + step) / len;
					Stroke({ { a.x + (b.x - a.x) * t0, a.z + (b.z - a.z) * t0 }, { a.x + (b.x - a.x) * t1, a.z + (b.z - a.z) * t1 } }, w, c, false);
				}
				s += step; phase += step;
				if (step <= 1e-9) break;
			}
		}
	}
};

void DrawRoads(Canvas& g, const CityMap& map, bool city) {
	struct St { Rgba col; double w; };
	auto style = [](ERoad t, St& st) {
		switch (t) {
		case ERoad::Freeway: st = { Hex("#e0a340"), 9 }; return true;
		case ERoad::Ramp: st = { Hex("#e0b050"), 5 }; return true;
		case ERoad::Highway: st = { Hex("#e8e2d0"), 8 }; return true;
		case ERoad::Avenue: st = { Hex("#f2f0e8"), 9 }; return true;
		case ERoad::Road: st = { Hex("#e8e6de"), 6.5 }; return true;
		case ERoad::Dirt: st = { Hex("#a58a64"), 4 }; return true;
		default: return false;
		}
	};
	for (int pass = 0; pass < 2; pass++) {
		for (const REdge& e : map.roads.edges) {
			if (e.removed || e.hasGrid) continue;
			St st;
			if (!style(e.type, st)) continue;
			std::vector<V2> pts;
			for (int i = 0; i < e.n; i++) pts.push_back({ g.X(e.X(i)), g.Z(e.Z(i)) });
			const double w = Max(city ? 1.5 : 1.2, st.w * g.sx * (city ? 1 : 1.6));
			if (pass == 0) g.Stroke(pts, w + (city ? 2 : 1.5), { 30, 30, 32, 0.85 });
			else g.Stroke(pts, w, st.col);
		}
	}
	const RoadInfo& ri = map.roadInfo;
	if (ri.hasRail) {
		std::vector<V2> pts;
		for (const P3& p : ri.rail.pts) pts.push_back({ g.X(p.x), g.Z(p.z) });
		const double w = Max(1.6, 4 * g.sx * 1.6);
		g.Stroke(pts, w, { 25, 25, 28, 0.95 });
		g.Dashed(pts, w * 0.55, Max(1.0, w * 0.5), Max(2.0, w), { 235, 235, 235, 0.9 });
	}
}

void WorldLayer(MapLayer& L, const CityMap& map, int size) {
	L.minX = WORLD.minX; L.maxX = WORLD.maxX; L.minZ = WORLD.minZ; L.maxZ = WORLD.maxZ;
	const int W = size, H = (int)std::lround(size * (WORLD.maxZ - WORLD.minZ) / (WORLD.maxX - WORLD.minX));
	Canvas g(L, W, H);
	const Heightfield& hf = map.hf;
	const double px = 1 / g.sx;
	for (int py = 0; py < H; py++) {
		const double z = WORLD.minZ + (py + 0.5) / g.sz;
		for (int pxl = 0; pxl < W; pxl++) {
			const double x = WORLD.minX + (pxl + 0.5) / g.sx;
			const double h = hf.Sample(x, z);
			double r, gg, b;
			const bool lake = std::hypot(x - LAKE.x, z - LAKE.z) < LAKE.r + 10 && h < LAKE.y;
			if (h < WATER_Y - 0.2 || lake) { const double k = Min(1.0, (WATER_Y - h) / 12); r = 38 - k * 18; gg = 92 - k * 32; b = 132 - k * 20; }
			else {
				const Regions w = RegionWeights(x, z);
				const double fm = (pxl + py) % 2 == 0 ? FarmMask(x, z) : 0;
				r = 128 * w.country + 70 * w.mountain + 206 * w.desert;
				gg = 136 * w.country + 104 * w.mountain + 176 * w.desert;
				b = 76 * w.country + 58 * w.mountain + 120 * w.desert;
				if (fm > 0.3) { r = r * 0.6 + 170 * 0.4; gg = gg * 0.6 + 160 * 0.4; b = b * 0.6 + 70 * 0.4; }
				if (h < 2.2 && w.desert < 0.5) { r = 214; gg = 196; b = 146; }
				const double urb = NcUrban(x, z);
				if (urb > 0) { const double k = Min(1.0, urb * 1.6); r = r * (1 - k) + 150 * k; gg = gg * (1 - k) + 148 * k; b = b * (1 - k) + 142 * k; }
				const double hx = hf.Sample(x + px, z) - h, hz = hf.Sample(x, z + px) - h;
				const double shade = Max(0.55, Min(1.25, 1 - (hx + hz) / px * 0.9));
				const double alt = Min(1.0, Max(0.0, (h - 250) / 400));
				r = (r * (1 - alt) + 190 * alt) * shade; gg = (gg * (1 - alt) + 185 * alt) * shade; b = (b * (1 - alt) + 175 * alt) * shade;
			}
			uint8_t* p = &L.rgba[((size_t)py * W + pxl) * 4];
			p[0] = (uint8_t)Clamp(std::lround(r), 0, 255); p[1] = (uint8_t)Clamp(std::lround(gg), 0, 255); p[2] = (uint8_t)Clamp(std::lround(b), 0, 255); p[3] = 255;
		}
	}
	for (const PadSurface& p : map.padSurfaces) {
		const Rgba c = p.type == "runway" || p.type == "taxiway" ? Hex("#3c3d42") : p.type == "dirtpad" ? Hex("#8c7a5c") : Hex("#9a978f");
		g.Box(p.cx, p.cz, p.hx, p.hz, p.yaw, c);
	}
	DrawRoads(g, map, false);
	for (const Building& b : map.buildings) {
		if (b.rot == 0 && !IsSet(b.base)) continue;
		g.Box((b.x0 + b.x1) / 2, (b.z0 + b.z1) / 2, (b.x1 - b.x0) / 2, (b.z1 - b.z0) / 2, b.rot, { 88, 84, 80, 0.95 });
	}
	// the base's perimeter
	const double bx0 = g.X(BASE.minX), bz0 = g.Z(BASE.minZ), bx1 = g.X(BASE.maxX), bz1 = g.Z(BASE.maxZ);
	g.Stroke({ { bx0, bz0 }, { bx1, bz0 }, { bx1, bz1 }, { bx0, bz1 }, { bx0, bz0 } }, 1.5, { 40, 50, 30, 0.9 });
}

void CityLayer(MapLayer& L, const CityMap& map, int size) {
	L.minX = -900; L.maxX = 1000; L.minZ = -860; L.maxZ = 1000;
	const int W = size, H = (int)std::lround(size * (L.maxZ - L.minZ) / (L.maxX - L.minX));
	Canvas g(L, W, H);
	g.Rect(CITY_RECT.minX, CITY_RECT.minZ, CITY_RECT.maxX, CITY_RECT.maxZ, Hex("#3a3b3f"));
	for (int bi : map.blocks) {
		const Block& b = map.blockStore[bi];
		g.Rect(b.x0, b.z0, b.x1, b.z1, Hex("#b9b6ad"));
		const Rgba base = b.park ? Hex("#6f8f4e") : b.ground == "grass" ? Hex("#8e9a6d") : Hex(CityMap::DistrictColor(b.district));
		g.Rect(b.ix0, b.iz0, b.ix1, b.iz1, base);
	}
	for (const LotSurface& s : map.lotSurfaces) {
		const char* col = s.type == "grass" ? "#7e9a5a" : s.type == "dirt" ? "#9c8468" : s.type == "asphalt" ? "#6f6f72" : s.type == "plaza" ? "#b3a58f"
			: s.type == "concrete" ? "#a19d95" : s.type == "court" ? "#4a6f8f" : s.type == "path" ? "#b5a27f" : s.type == "sand" ? "#d8c38f" : nullptr;
		if (col) g.Rect(s.x0, s.z0, s.x1, s.z1, Hex(col));
	}
	for (const Pool& pl : map.pools) g.Rect(pl.x0, pl.z0, pl.x1, pl.z1, Hex("#4fb3c8"));
	for (const Building& b : map.buildings) {
		if (b.y0 > 1 && !IsSet(b.base)) continue;
		if (IsSet(b.base) && !(b.x0 > L.minX && b.x1 < L.maxX && b.z0 > L.minZ && b.z1 < L.maxZ)) continue;
		const double k = Min(1.0, (b.y1 - b.y0) / 120);
		const Rgba col{ (double)std::lround(70 - k * 30), (double)std::lround(72 - k * 30), (double)std::lround(80 - k * 25), 0.85 };
		if (b.rot != 0) g.Box((b.x0 + b.x1) / 2, (b.z0 + b.z1) / 2, (b.x1 - b.x0) / 2, (b.z1 - b.z0) / 2, b.rot, col);
		else g.Rect(b.x0, b.z0, b.x1, b.z1, col);
	}
	g.path++;
	for (const REdge& e : map.roads.edges) {
		if (e.removed || !e.hasGrid || e.n < 2) continue;
		g.Stroke({ { g.X(e.X(0)), g.Z(e.Z(0)) }, { g.X(e.X(e.n - 1)), g.Z(e.Z(e.n - 1)) } }, Max(1.0, g.sx * 0.6), { 230, 200, 80, 0.35 }, false);
	}
	DrawRoads(g, map, true);
	auto it = map.landmarks.find("pier");
	if (it != map.landmarks.end()) {
		const auto& v = it->second.vals;
		g.Rect(v.at("x0"), v.at("z0"), v.at("x1"), v.at("z1"), Hex("#8a6a4a"));
	}
}
} // namespace

MapImages BuildMapImages(const CityMap& map, int worldSize, int citySize) {
	MapImages out;
	WorldLayer(out.world, map, worldSize);
	CityLayer(out.city, map, citySize);
	std::vector<MapLabel>& labels = out.labels;
	// district names at the middle of their blocks (in the order the districts first appear)
	std::vector<std::string> order;
	std::map<std::string, std::array<double, 3>> bl;
	for (int bi : map.blocks) {
		const Block& b = map.blockStore[bi];
		if (!bl.count(b.district)) { order.push_back(b.district); bl[b.district] = { 0, 0, 0 }; }
		auto& v = bl[b.district]; v[0] += b.cx; v[1] += b.cz; v[2] += 1;
	}
	for (const std::string& d : order) { const auto& v = bl[d]; labels.push_back({ CityMap::DistrictName(d), v[0] / v[2], v[1] / v[2], false }); }
	auto sk = map.landmarks.find("skate_santaluz");
	if (sk != map.landmarks.end()) labels.push_back({ "Skatepark", sk->second.x, sk->second.z + 22, false });
	auto pier = map.landmarks.find("pier");
	if (pier != map.landmarks.end()) labels.push_back({ "Santa Luz Pier", (pier->second.vals.at("x0") + pier->second.vals.at("x1")) / 2, pier->second.vals.at("z1") - 80, false });
	labels.push_back({ "Mount Vista", 0, -1250, false });
	labels.push_back({ "Red Canyon", -1250, -250, false });
	labels.push_back({ "Pacific Ocean", -1500, 1150, false });
	for (const Town& t : TOWNS) labels.push_back({ t.name, t.x, t.z - t.r - 40, true });
	labels.push_back({ BASE.name, (BASE.minX + BASE.maxX) / 2, BASE.minZ - 50, true });
	labels.push_back({ "Tierra Seca Desert", -4600, -1500, false });
	labels.push_back({ "Pinewood Forest", -300, -3500, false });
	labels.push_back({ "Mount Cedro", -900, -3250, false });
	labels.push_back({ NCITY.name, NCITY.x, NCITY.z - 560, true });
	labels.push_back({ "Aurelio Beach", NCITY.x + 780, NCITY.z + 60, false });
	labels.push_back({ "Cedar Valley", 120, -2700, false });
	labels.push_back({ "Centro", NCITY.x, NCITY.z + 90, false });
	labels.push_back({ "Harborside", NCITY.x + 240, NCITY.z - 30, false });
	labels.push_back({ "Cathedral Hill", NCITY.x - 250, NCITY.z + 60, false });
	labels.push_back({ "Mission", NCITY.x + 20, NCITY.z + 250, false });
	labels.push_back({ "Northgate", NCITY.x + 20, NCITY.z - 250, false });
	labels.push_back({ "Verde County", -2400, 450, false });
	labels.push_back({ "Lake Mirador", LAKE.x, LAKE.z, false });
	labels.push_back({ "Bayshore", 1050, -1300, false });
	labels.push_back({ AIRFIELD.name, AIRFIELD.x - 120, AIRFIELD.z + 80, false });
	return out;
}

} // namespace atg
