// City geometry (port of src/world/city.js): Los Soles' street plane, the raised blocks (sidewalk band +
// lot), lot and pad surfaces, buildings (facade walls, flat roofs with parapets and rooftop clutter, gable
// roofs, shop doorways), fences / hedges and pools.
#include "WorldMeshes.h"

namespace atg {

namespace {
int GroundType(const std::string& t) {
	static const std::map<std::string, int> T = { { "concrete", 0 }, { "grass", 1 }, { "dirt", 2 }, { "asphalt", 3 }, { "plaza", 4 }, { "driveway", 5 }, { "court", 6 }, { "path", 7 },
		{ "sand", 8 }, { "runway", 9 }, { "taxiway", 10 }, { "apron", 11 }, { "helipad", 12 }, { "dirtpad", 13 } };
	auto it = T.find(t);
	return it == T.end() ? 0 : it->second;
}
constexpr double CITY_CHUNK = 200;
} // namespace

MeshBuf BuildStreetPlane() {
	MeshBuf g;
	const double x0 = CityMap::CITY_MINX, x1 = CityMap::CITY_MAXX, z0 = CityMap::CITY_MINZ, z1 = CityMap::CITY_MAXZ;
	const int n = 32;
	for (int j = 0; j <= n; j++) for (int i = 0; i <= n; i++) g.V(x0 + (x1 - x0) * i / n, 0, z0 + (z1 - z0) * j / n, 0, 1, 0, 0, 0);
	for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
		const uint32_t a = j * (n + 1) + i, b = a + 1, c = a + (n + 1), d = c + 1;
		g.Tri(a, c, d); g.Tri(a, d, b);
	}
	return g;
}

// A block: kerbed sides, a 4 m sidewalk band (edge distance in ch1.y) round the lot surface.
MeshBuf BuildBlocks(const CityMap& map) {
	MeshBuf g;
	const double SW = SIDEWALK_W, H = CURB_H;
	for (int bi : map.blocks) {
		const Block& b = map.blockStore[bi];
		const int type = GroundType(b.ground);
		g.Set(2, 0, 0); g.Set(3, 0, 0);
		// kerb faces (the material paints faces that aren't level as kerb)
		g.Set(1, type, 0);
		g.Box(b.x0, 0, b.z0, b.x1, H, b.z1, false, true, false);
		// sidewalk band: four mitred trapezoids, edge distance 0 at the kerb, SW at the inner edge
		const double o[4][2] = { { b.x0, b.z0 }, { b.x1, b.z0 }, { b.x1, b.z1 }, { b.x0, b.z1 } };
		const double in[4][2] = { { b.x0 + SW, b.z0 + SW }, { b.x1 - SW, b.z0 + SW }, { b.x1 - SW, b.z1 - SW }, { b.x0 + SW, b.z1 - SW } };
		for (int k = 0; k < 4; k++) {
			const int k2 = (k + 1) % 4;
			g.Set(1, type, 0);
			const uint32_t a = g.V(o[k][0], H, o[k][1], 0, 1, 0), bb = g.V(o[k2][0], H, o[k2][1], 0, 1, 0);
			g.Set(1, type, SW);
			const uint32_t c = g.V(in[k2][0], H, in[k2][1], 0, 1, 0), d = g.V(in[k][0], H, in[k][1], 0, 1, 0);
			g.QuadAuto(a, bb, c, d);
		}
		g.Set(1, type, 99);
		const uint32_t a = g.V(in[0][0], H, in[0][1], 0, 1, 0), bb = g.V(in[1][0], H, in[1][1], 0, 1, 0), c = g.V(in[2][0], H, in[2][1], 0, 1, 0), d = g.V(in[3][0], H, in[3][1], 0, 1, 0);
		g.QuadAuto(a, bb, c, d);
	}
	return g;
}

// Overlapping lot surfaces are stacked in 1 cm layers so they never share a plane.
static std::vector<int> LotLayers(const std::vector<LotSurface>& list) {
	const double cell = 40;
	std::unordered_map<int64_t, std::vector<int>> grid;
	std::vector<int> layers(list.size(), 0);
	auto key = [](int64_t gx, int64_t gz) { return gx * 7919 + gz; };
	for (size_t i = 0; i < list.size(); i++) {
		const LotSurface& s = list[i];
		int layer = 0;
		std::set<int> seen;
		for (int64_t gx = (int64_t)std::floor(s.x0 / cell); gx <= (int64_t)std::floor(s.x1 / cell); gx++) for (int64_t gz = (int64_t)std::floor(s.z0 / cell); gz <= (int64_t)std::floor(s.z1 / cell); gz++) {
			auto it = grid.find(key(gx, gz));
			if (it == grid.end()) continue;
			for (int j : it->second) {
				if (!seen.insert(j).second) continue;
				const LotSurface& o = list[j];
				if (o.x0 < s.x1 - 0.01 && o.x1 > s.x0 + 0.01 && o.z0 < s.z1 - 0.01 && o.z1 > s.z0 + 0.01) layer = (std::max)(layer, layers[j] + 1);
			}
		}
		layers[i] = (std::min)(layer, 4);
		for (int64_t gx = (int64_t)std::floor(s.x0 / cell); gx <= (int64_t)std::floor(s.x1 / cell); gx++) for (int64_t gz = (int64_t)std::floor(s.z0 / cell); gz <= (int64_t)std::floor(s.z1 / cell); gz++) grid[key(gx, gz)].push_back((int)i);
	}
	return layers;
}

MeshBuf BuildLots(const CityMap& map) {
	MeshBuf g;
	const std::vector<int> layers = LotLayers(map.lotSurfaces);
	for (size_t i = 0; i < map.lotSurfaces.size(); i++) {
		const LotSurface& s = map.lotSurfaces[i];
		const double y = CURB_H + 0.01 + layers[i] * 0.01;
		g.Set(1, GroundType(s.type), 99); g.Set(2, 0, 0); g.Set(3, 0, 0);
		const double a[3] = { s.x0, y, s.z1 }, b[3] = { s.x1, y, s.z1 }, c[3] = { s.x1, y, s.z0 }, d[3] = { s.x0, y, s.z0 }, n[3] = { 0, 1, 0 };
		g.Quad(a, b, c, d, n);
	}
	return g;
}

MeshBuf BuildPads(const CityMap& map) {
	MeshBuf g;
	for (const PadSurface& p : map.padSurfaces) {
		const double s = std::sin(p.yaw), c = std::cos(p.yaw);
		auto L = [&](double lx, double lz, double out[3]) { out[0] = p.cx + lx * c + lz * s; out[1] = p.y; out[2] = p.cz - lx * s + lz * c; };
		g.Set(1, GroundType(p.type), 99); g.Set(2, 1, p.hx); g.Set(3, p.hz, 0);
		const int nz = (int)Max(1, std::ceil(p.hz * 2 / 60));
		for (int k = 0; k < nz; k++) {
			const double z0 = -p.hz + ((double)k / nz) * p.hz * 2, z1 = -p.hz + ((double)(k + 1) / nz) * p.hz * 2;
			double A[3], B[3], C[3], D[3];
			L(-p.hx, z1, A); L(p.hx, z1, B); L(p.hx, z0, C); L(-p.hx, z0, D);
			const double n[3] = { 0, 1, 0 };
			const double ua[2] = { -p.hx, z1 }, ub[2] = { p.hx, z1 }, uc[2] = { p.hx, z0 }, ud[2] = { -p.hx, z0 };
			g.Quad(A, B, C, D, n, ua, ub, uc, ud);
		}
	}
	return g;
}

MeshBuf BuildPools(const CityMap& map) {
	MeshBuf g;
	for (const Pool& pl : map.pools) {
		const double y = CURB_H + 0.08;
		const double a[3] = { pl.x0, y, pl.z1 }, b[3] = { pl.x1, y, pl.z1 }, c[3] = { pl.x1, y, pl.z0 }, d[3] = { pl.x0, y, pl.z0 }, n[3] = { 0, 1, 0 };
		g.Quad(a, b, c, d, n);
	}
	return g;
}

// ------------------------------------------------------------------ buildings
namespace {
struct BuildingBuilder {
	const CityMap& map;
	std::map<std::pair<int, int>, CityChunk> chunks;
	RNG rng{ 4242 };
	explicit BuildingBuilder(const CityMap& m) : map(m) {}

	CityChunk& chunk(double x, double z) {
		const std::pair<int, int> k{ (int)std::floor(x / CITY_CHUNK), (int)std::floor(z / CITY_CHUNK) };
		auto it = chunks.find(k);
		if (it == chunks.end()) {
			CityChunk c; c.cx = (k.first + 0.5) * CITY_CHUNK; c.cz = (k.second + 0.5) * CITY_CHUNK;
			c.det.Rough(0.8);
			it = chunks.emplace(k, std::move(c)).first;
		}
		return it->second;
	}

	static void setB(MeshBuf& g, const Building& b, int roof, int ground) {
		g.Set(1, b.style, b.seed);
		g.Set(2, roof * 4 + ground, b.tint[0]);
		g.Set(3, b.tint[1], b.tint[2]);
	}
	static void quad(MeshBuf& g, double ax, double ay, double az, double bx, double by, double bz, double cx, double cy, double cz, double dx, double dy, double dz,
		double nx, double ny, double nz, double u0, double v0, double u1, double v1, double u2, double v2, double u3, double v3) {
		const double A[3] = { ax, ay, az }, B[3] = { bx, by, bz }, C[3] = { cx, cy, cz }, D[3] = { dx, dy, dz }, N[3] = { nx, ny, nz };
		const double t0[2] = { u0, v0 }, t1[2] = { u1, v1 }, t2[2] = { u2, v2 }, t3[2] = { u3, v3 };
		g.Quad(A, B, C, D, N, t0, t1, t2, t3);
	}

	void building(const Building& b) {
		const double bcx = (b.x0 + b.x1) / 2, bcz = (b.z0 + b.z1) / 2;
		CityChunk& c = chunk(bcx, bcz);
		MeshBuf& gb = c.bld;
		const size_t start = gb.Count(), startD = c.det.Count();
		const bool onGround = b.y0 < CURB_H + 0.5 || (IsSet(b.base) && std::fabs(b.y0 - b.base) < 0.01);
		const int ground = onGround ? (b.kind == "house" || b.kind == "mansion" || b.kind == "barn" ? 2 : 1) : 0;
		const double h = b.y1 - b.y0;
		const double nx = Max(1, JsRound((b.x1 - b.x0) / b.cell));
		const double nz = Max(1, JsRound((b.z1 - b.z0) / b.cell));
		const double floors = Max(1, JsRound(h / b.floorH));
		const double fH = h / floors;
		const double v0 = 0, v1 = h / fH;
		setB(gb, b, 0, ground);
		const double x0 = b.x0, z0 = b.z0, x1 = b.x1, z1 = b.z1, y0 = b.y0, y1 = b.y1;
		auto V = [&](double y) { return (y - y0) / fH; };
		auto U0 = [&](double x) { return (x1 - x) / (x1 - x0) * nx; };
		auto U1 = [&](double x) { return (x - x0) / (x1 - x0) * nx; };
		const std::string front = b.shop.valid() ? b.shop.front : "";
		const double DOOR_W = 2.4, DOOR_H = 2.7;
		const double dcx = (x0 + x1) / 2, dl = dcx - DOOR_W / 2, dr = dcx + DOOR_W / 2;
		const double pieces[3][4] = { { x0, dl, y0, y1 }, { dr, x1, y0, y1 }, { dl, dr, y0 + DOOR_H, y1 } };
		if (front == "z0") for (const auto& p : pieces) quad(gb, p[1], p[2], z0, p[0], p[2], z0, p[0], p[3], z0, p[1], p[3], z0, 0, 0, -1, U0(p[1]), V(p[2]), U0(p[0]), V(p[2]), U0(p[0]), V(p[3]), U0(p[1]), V(p[3]));
		else quad(gb, x1, y0, z0, x0, y0, z0, x0, y1, z0, x1, y1, z0, 0, 0, -1, 0, v0, nx, v0, nx, v1, 0, v1);
		if (front == "z1") for (const auto& p : pieces) quad(gb, p[0], p[2], z1, p[1], p[2], z1, p[1], p[3], z1, p[0], p[3], z1, 0, 0, 1, U1(p[0]), V(p[2]), U1(p[1]), V(p[2]), U1(p[1]), V(p[3]), U1(p[0]), V(p[3]));
		else quad(gb, x0, y0, z1, x1, y0, z1, x1, y1, z1, x0, y1, z1, 0, 0, 1, 0, v0, nx, v0, nx, v1, 0, v1);
		quad(gb, x0, y0, z0, x0, y0, z1, x0, y1, z1, x0, y1, z0, -1, 0, 0, 0, v0, nz, v0, nz, v1, 0, v1);
		quad(gb, x1, y0, z1, x1, y0, z0, x1, y1, z0, x1, y1, z1, 1, 0, 0, 0, v0, nz, v0, nz, v1, 0, v1);
		if (b.roof == "gable") gable(gb, b);
		else {
			setB(gb, b, 1, 0);
			quad(gb, x0, y1, z1, x1, y1, z1, x1, y1, z0, x0, y1, z0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0);
			if (h > 7 && b.kind != "house") {
				const double p = 0.35, ph = 0.9;
				gb.Box(x0, y1, z0, x1, y1 + ph, z0 + p, true);
				gb.Box(x0, y1, z1 - p, x1, y1 + ph, z1, true);
				gb.Box(x0, y1, z0 + p, x0 + p, y1 + ph, z1 - p, true);
				gb.Box(x1 - p, y1, z0 + p, x1, y1 + ph, z1 - p, true);
			}
			roofDetails(c, b);
		}
		if (b.rot != 0) { gb.RotateFrom(start, bcx, bcz, b.rot); c.det.RotateFrom(startD, bcx, bcz, b.rot); }
	}

	void gable(MeshBuf& gb, const Building& b) {
		const double x0 = b.x0, z0 = b.z0, x1 = b.x1, z1 = b.z1, y1 = b.y1;
		const bool alongX = (x1 - x0) >= (z1 - z0);
		const double over = 0.5;
		const double rh = Min(x1 - x0, z1 - z0) * 0.32;
		setB(gb, b, 2, 0);
		const double X0 = x0 - over, X1 = x1 + over, Z0 = z0 - over, Z1 = z1 + over;
		if (alongX) {
			const double zm = (z0 + z1) / 2;
			const double ny = zm - Z0, l = Hypot(ny, rh);
			quad(gb, X0, y1 - 0.2, Z1, X1, y1 - 0.2, Z1, X1, y1 + rh, zm, X0, y1 + rh, zm, 0, ny / l, rh / l, 0, 0, (X1 - X0) / 3, 0, (X1 - X0) / 3, l / 3, 0, l / 3);
			quad(gb, X1, y1 - 0.2, Z0, X0, y1 - 0.2, Z0, X0, y1 + rh, zm, X1, y1 + rh, zm, 0, ny / l, -rh / l, 0, 0, (X1 - X0) / 3, 0, (X1 - X0) / 3, l / 3, 0, l / 3);
			setB(gb, b, 0, 0);
			uint32_t a = gb.V(x0, y1, z1, -1, 0, 0, 0, 0), bb = gb.V(x0, y1, z0, -1, 0, 0, 1, 0), cc = gb.V(x0, y1 + rh, zm, -1, 0, 0, 0.5, 0.3);
			gb.Tri(bb, a, cc);
			a = gb.V(x1, y1, z0, 1, 0, 0, 0, 0); bb = gb.V(x1, y1, z1, 1, 0, 0, 1, 0); cc = gb.V(x1, y1 + rh, zm, 1, 0, 0, 0.5, 0.3);
			gb.Tri(bb, a, cc);
		} else {
			const double xm = (x0 + x1) / 2;
			const double nx = xm - X0, l = Hypot(nx, rh);
			quad(gb, X1, y1 - 0.2, Z1, X1, y1 - 0.2, Z0, xm, y1 + rh, Z0, xm, y1 + rh, Z1, rh / l, nx / l, 0, 0, 0, (Z1 - Z0) / 3, 0, (Z1 - Z0) / 3, l / 3, 0, l / 3);
			quad(gb, X0, y1 - 0.2, Z0, X0, y1 - 0.2, Z1, xm, y1 + rh, Z1, xm, y1 + rh, Z0, -rh / l, nx / l, 0, 0, 0, (Z1 - Z0) / 3, 0, (Z1 - Z0) / 3, l / 3, 0, l / 3);
			setB(gb, b, 0, 0);
			uint32_t a = gb.V(x0, y1, z0, 0, 0, -1, 0, 0), bb = gb.V(x1, y1, z0, 0, 0, -1, 1, 0), cc = gb.V(xm, y1 + rh, z0, 0, 0, -1, 0.5, 0.3);
			gb.Tri(bb, a, cc);
			a = gb.V(x1, y1, z1, 0, 0, 1, 0, 0); bb = gb.V(x0, y1, z1, 0, 0, 1, 1, 0); cc = gb.V(xm, y1 + rh, z1, 0, 0, 1, 0.5, 0.3);
			gb.Tri(bb, a, cc);
		}
	}

	void roofDetails(CityChunk& c, const Building& b) {
		MeshBuf& det = c.det;
		const double x0 = b.x0, z0 = b.z0, x1 = b.x1, z1 = b.z1, y1 = b.y1;
		const double w = x1 - x0, d = z1 - z0;
		const double cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
		auto boxG = [&](double bx0, double by0, double bz0, double bx1, double by1, double bz1) { det.Box(bx0, by0, bz0, bx1, by1, bz1, true); };
		auto add = [&](const MeshBuf& g, double x, double y, double z, double rx = 0, double ry = 0, double rz = 0) { det.Add(g, Mat4::Compose(x, y, z, rx, ry, rz)); };
		bool ac = b.roof == "ac";
		if (!ac && b.roof == "flat") ac = rng.Chance(0.5) && w > 8 && d > 8;
		if (ac) {
			const int n = rng.Int(1, 4);
			for (int k = 0; k < n; k++) {
				const double ax = rng.Range(x0 + 2, x1 - 4), az = rng.Range(z0 + 2, z1 - 4);
				det.ColorHex(0x8c9196);
				const double bx = ax + rng.Range(1.2, 2.6);
				const double by = y1 + rng.Range(0.8, 1.6);
				const double bz = az + rng.Range(1.2, 2.2);
				boxG(ax, y1, az, bx, by, bz);
			}
			if (rng.Chance(0.3) && b.district != "downtown" && b.y1 < 40) {
				const double tx = rng.Range(x0 + 3, x1 - 3), tz = rng.Range(z0 + 3, z1 - 3);
				det.ColorHex(0x5a4a3a); add(Geo::Cylinder(1.4, 1.4, 2.6, 12), tx, y1 + 3.3, tz);
				det.ColorHex(0x3a3230); add(Geo::Cone(1.55, 1.0, 12), tx, y1 + 5.1, tz);
				det.ColorHex(0x333333);
				const double legs[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
				for (const auto& l : legs) add(Geo::Cylinder(0.07, 0.07, 2.2, 5), tx + l[0], y1 + 1.1, tz + l[1]);
			}
		}
		if (b.roof == "antenna") {
			det.ColorHex(0x777777); add(Geo::Cylinder(0.08, 0.2, 14, 6), cx, y1 + 7, cz);
			det.ColorHex(0x999999); boxG(cx - 1.5, y1, cz - 1.5, cx + 1.5, y1 + 2.5, cz + 1.5);
		}
		if (b.roof == "helipad") {
			det.ColorHex(0x3a3c40); boxG(cx - 7, y1, cz - 7, cx + 7, y1 + 0.25, cz + 7);
			det.ColorHex(0xe7c22a);
			{ MeshBuf ring = Geo::Ring(4.6, 5.2, 32); MeshBuf r2; r2.Add(ring, Mat4::Compose(0, 0, 0, -kPi / 2)); add(r2, cx, y1 + 0.27, cz); }
			det.ColorHex(0xf0f0f0);
			boxG(cx - 1.8, y1 + 0.25, cz - 2.2, cx - 1.1, y1 + 0.29, cz + 2.2);
			boxG(cx + 1.1, y1 + 0.25, cz - 2.2, cx + 1.8, y1 + 0.29, cz + 2.2);
			boxG(cx - 1.1, y1 + 0.25, cz - 0.35, cx + 1.1, y1 + 0.29, cz + 0.35);
		}
		if (b.roof == "spire") {
			det.ColorHex(0xb0b8c0);
			const double m = Min(w, d);
			add(Geo::Cone(m * 0.35, m * 1.2, 4), cx, y1 + m * 0.6, cz, 0, kPi / 4, 0);
		}
	}

	void fence(const Fence& f) {
		if (f.rot != 0) { rotFence(f); return; }
		CityChunk& c = chunk((f.x0 + f.x1) / 2, (f.z0 + f.z1) / 2);
		MeshBuf& det = c.det;
		const double y0 = (IsSet(f.y) ? f.y : 0) + CURB_H, y1 = y0 + f.h;
		const bool alongX = std::fabs(f.x1 - f.x0) > std::fabs(f.z1 - f.z0);
		if (f.type == "hedge") { det.Color(0.16, 0.3, 0.12); det.Box(f.x0, y0, f.z0, f.x1, y1, f.z1, true); }
		else if (f.type == "wood") {
			det.Color(0.45, 0.33, 0.22);
			if (alongX) det.Box(f.x0, y0, (f.z0 + f.z1) / 2 - 0.04, f.x1, y1, (f.z0 + f.z1) / 2 + 0.04, true);
			else det.Box((f.x0 + f.x1) / 2 - 0.04, y0, f.z0, (f.x0 + f.x1) / 2 + 0.04, y1, f.z1, true);
		} else {
			det.Color(0.45, 0.47, 0.5);
			const double len = alongX ? f.x1 - f.x0 : f.z1 - f.z0;
			const int n = (int)Max(1, JsRound(len / 3));
			for (int k = 0; k <= n; k++) {
				const double t = (double)k / n;
				const double px = alongX ? f.x0 + (f.x1 - f.x0) * t : (f.x0 + f.x1) / 2;
				const double pz = alongX ? (f.z0 + f.z1) / 2 : f.z0 + (f.z1 - f.z0) * t;
				det.Box(px - 0.04, y0, pz - 0.04, px + 0.04, y1, pz + 0.04, true);
			}
			const double mx = (f.x0 + f.x1) / 2, mz = (f.z0 + f.z1) / 2;
			if (alongX) det.Box(f.x0, y1 - 0.05, mz - 0.03, f.x1, y1, mz + 0.03, true);
			else det.Box(mx - 0.03, y1 - 0.05, f.z0, mx + 0.03, y1, f.z1, true);
			// (the see-through chain-link mesh itself comes with the transparent materials)
		}
	}
	void rotFence(const Fence& f) {
		CityChunk& c = chunk(f.cx, f.cz);
		MeshBuf& det = c.det;
		const size_t start = det.Count();
		const double y0 = f.y + CURB_H, y1 = y0 + f.h;
		det.Color(0.48, 0.36, 0.25);
		const int n = (int)Max(1, JsRound(f.hz * 2 / 3));
		for (int k = 0; k <= n; k++) { const double z = f.cz - f.hz + ((double)k / n) * f.hz * 2; det.Box(f.cx - 0.07, y0 - 0.3, z - 0.07, f.cx + 0.07, y1, z + 0.07, true); }
		det.Box(f.cx - 0.04, y1 - 0.25, f.cz - f.hz, f.cx + 0.04, y1 - 0.1, f.cz + f.hz, true);
		det.Box(f.cx - 0.04, y0 + 0.35, f.cz - f.hz, f.cx + 0.04, y0 + 0.5, f.cz + f.hz, true);
		det.RotateFrom(start, f.cx, f.cz, f.rot);
	}
};
} // namespace

std::map<std::pair<int, int>, CityChunk> BuildBuildings(const CityMap& map) {
	BuildingBuilder bb(map);
	for (const Building& b : map.buildings) bb.building(b);
	for (const Fence& f : map.fences) bb.fence(f);
	return std::move(bb.chunks);
}

} // namespace atg
