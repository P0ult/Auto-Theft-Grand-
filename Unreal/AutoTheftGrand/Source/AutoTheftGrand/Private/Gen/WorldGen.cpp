// Port of src/world/worldgen.js. See WorldGen.h.
#include "WorldGen.h"
#include "RoadNet.h"
#include <cstring>

namespace atg {

const std::vector<Town> TOWNS = {
	{ "fern", "Fern Creek", -2520, -240, 330, NaN(), NaN() },
	{ "pine", "Pine Hollow", -300, -2330, 240, NaN(), NaN() },
	{ "dry", "Dry Wells", -3860, -2480, 300, NaN(), NaN() },
	{ "mirador", "Mirador", -1240, -1790, 175, 80, NaN() },
	{ "hale", "Port Hale", 1000, -960, 170, 95, NaN() },
	{ "seco", "Puerto Seco", -4000, 470, 215, 170, NaN() },
	{ "gull", "Gull Bay", 972, -2760, 190, 115, 8 },
	{ "ridge", "Cedar Ridge", -40, -3180, 185, 115, 44 },
	{ "timber", "Timberline", -640, -4390, 190, 120, 46 },
};
const Town& TownByKey(const char* key) {
	for (const Town& t : TOWNS) if (std::strcmp(key, t.key) == 0) return t;
	return TOWNS[0];
}

const NCityDef NCITY = { "aurelio", "San Aurelio", 420, -4060, 6, { 135, 292, 468 } };
const BaseDef BASE = { "Fort Carver", -5320, -3820, -4760, -3860, -4060 };
const AirfieldDef AIRFIELD = { "Fern Creek Airfield", -2960, 330, 560, kPi / 2 };
const LakeDef LAKE = { -1500, -1900, 210, 64 };
const std::vector<V2> RIVER = { {-1640, -1720}, {-1760, -1400}, {-1830, -1100}, {-1960, -800}, {-2010, -560}, {-2140, -330}, {-2130, -80}, {-2050, 150}, {-2110, 400}, {-2190, 640}, {-2230, 900}, {-2260, 1400} };
namespace wg_detail {
struct Mesa { double x, z, r, h; };
const std::vector<Mesa> MESAS = {
	{-4400, -1900, 150, 70}, {-4900, -2600, 210, 95}, {-4300, -3150, 120, 60}, {-3350, -3500, 170, 80}, {-5100, -1500, 180, 85},
	{-3000, -2900, 110, 55}, {-4700, -3400, 90, 70}, {-3450, -1750, 90, 45}, {-5200, -3500, 140, 90}, {-2800, -4300, 200, 100},
	{-3500, -4600, 150, 70}, {-2600, -3700, 120, 60},
};

// gradient table (Float32Array in the original)
struct GradTable {
	float gx[256], gy[256];
	GradTable() { for (int i = 0; i < 256; i++) { const double a = Hash2(i, 911) * kPi * 2; gx[i] = (float)std::cos(a); gy[i] = (float)std::sin(a); } }
};
const GradTable& Grads() { static GradTable t; return t; }
inline int Gh(int32_t ix, int32_t iy) {
	uint32_t h = (uint32_t)ix * 374761393u + (uint32_t)iy * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return (int)((h ^ (h >> 16)) & 255u);
}
} // namespace wg_detail

double PNoise(double x, double y) {
	using namespace wg_detail;
	const GradTable& G = Grads();
	const double fxi = std::floor(x), fyi = std::floor(y), xf = x - fxi, yf = y - fyi;
	const int32_t xi = ToInt32(fxi), yi = ToInt32(fyi);
	const double u = xf * xf * xf * (xf * (xf * 6 - 15) + 10), v = yf * yf * yf * (yf * (yf * 6 - 15) + 10);
	int g = Gh(xi, yi); const double a = (double)G.gx[g] * xf + (double)G.gy[g] * yf;
	g = Gh(xi + 1, yi); const double b = (double)G.gx[g] * (xf - 1) + (double)G.gy[g] * yf;
	g = Gh(xi, yi + 1); const double c = (double)G.gx[g] * xf + (double)G.gy[g] * (yf - 1);
	g = Gh(xi + 1, yi + 1); const double d = (double)G.gx[g] * (xf - 1) + (double)G.gy[g] * (yf - 1);
	const double ab = a + (b - a) * u, cd = c + (d - c) * u;
	return ab + (cd - ab) * v;
}

double FbmN(double x, double y, int oct) {
	double s = 0, a = 0.5, n = 0;
	for (int i = 0; i < oct; i++) {
		s += a * PNoise(x, y); n += a; a *= 0.5;
		const double nx = (x * 1.6 - y * 1.2) * 1.02 + 5.2, ny = (x * 1.2 + y * 1.6) * 1.02 - 1.7; x = nx; y = ny;
	}
	return Clamp(s / n * 0.9 + 0.5, 0, 1);
}

double Ridged(double x, double y, int oct) {
	double s = 0, a = 0.5, n = 0, w = 1;
	for (int i = 0; i < oct; i++) {
		double r = 1 - std::fabs(PNoise(x, y) * 1.5);
		r = r * r * w; w = Clamp(r * 1.8, 0, 1);
		s += a * r; n += a; a *= 0.5;
		const double nx = (x * 1.6 - y * 1.2) * 1.05 + 3.1, ny = (x * 1.2 + y * 1.6) * 1.05 + 7.3; x = nx; y = ny;
	}
	return s / n;
}

double CityDist(double x, double z) {
	const double dx = Max(Max(CITY_RECT.minX - x, 0), x - CITY_RECT.maxX);
	const double dz = Max(Max(CITY_RECT.minZ - z, 0), z - CITY_RECT.maxZ);
	return Hypot(dx, dz);
}
namespace wg_detail {
double SegDist(double px, double pz, double ax, double az, double bx, double bz) {
	const double dx = bx - ax, dz = bz - az;
	double L2 = dx * dx + dz * dz; if (L2 == 0) L2 = 1;
	const double t = Clamp(((px - ax) * dx + (pz - az) * dz) / L2, 0, 1);
	return Hypot(px - ax - dx * t, pz - az - dz * t);
}
} // namespace wg_detail
double RiverDist(double x, double z) {
	double d = kInf;
	for (size_t i = 0; i + 1 < RIVER.size(); i++) d = Min(d, wg_detail::SegDist(x, z, RIVER[i].x, RIVER[i].z, RIVER[i + 1].x, RIVER[i + 1].z));
	return d;
}

double CoastZ(double x) { return 700 + 120 * std::sin(x * 0.0011 + 0.6) + 80 * (FbmN(x * 0.0021, 3.3, 3) - 0.5) * 2; }
double CoastX(double z) { return 1180 + 110 * std::sin(z * 0.0013) + 60 * (FbmN(7.1, z * 0.002, 3) - 0.5) * 2; }

// ------------------------------------------------------------------ San Aurelio
double NcRingR(int k, double th) {
	const double R = NCITY.rings[k];
	return R * (1 + 0.085 * std::sin(2 * th + 0.7 + k * 0.45) + 0.055 * std::sin(3 * th + 1.9 * k) + 0.035 * std::sin(5 * th + k * 2.3));
}
double NcEdgeDist(double x, double z) {
	const double dx = x - NCITY.x, dz = z - NCITY.z;
	return Hypot(dx, dz) - (NcRingR(2, std::atan2(dz, dx)) + 75);
}
double NcUrban(double x, double z) {
	if (std::fabs(x - NCITY.x) > 1400 || std::fabs(z - NCITY.z) > 1400) return 0;
	return 1 - Smooth(0, 260, NcEdgeDist(x, z));
}

namespace wg_detail {
struct Valley {
	std::vector<V2> pts; std::vector<double> cum; double L, y0, y1, w, slope; bool shelf; double box[4];
};
const std::vector<Valley>& Valleys() {
	static std::vector<Valley> V;
	if (!V.empty()) return V;
	auto mk = [](std::vector<V2> pts, double y0, double y1, double w, double slope, bool shelf) {
		Valley v; v.pts = pts; v.cum.push_back(0);
		for (size_t i = 1; i < pts.size(); i++) v.cum.push_back(v.cum[i - 1] + Hypot(pts[i].x - pts[i - 1].x, pts[i].z - pts[i - 1].z));
		double x0 = kInf, x1 = -kInf, z0 = kInf, z1 = -kInf;
		for (auto& p : pts) { x0 = Min(x0, p.x); x1 = Max(x1, p.x); z0 = Min(z0, p.z); z1 = Max(z1, p.z); }
		const double pad = w + 400;
		v.L = v.cum.back(); v.y0 = y0; v.y1 = y1; v.w = w; v.slope = slope; v.shelf = shelf;
		v.box[0] = x0 - pad; v.box[1] = z0 - pad; v.box[2] = x1 + pad; v.box[3] = z1 + pad;
		return v;
	};
	std::vector<V2> shelf;
	for (int z = -2050; z >= -3760; z -= 110) shelf.push_back({ CoastX(z) - 265 + (z > -2300 ? (z + 2300) * 0.55 : 0), (double)z });
	V.push_back(mk(shelf, 8, 8, 175, 0.32, true));
	V.push_back(mk({ {300, -2322}, {262, -2560}, {160, -2830}, {-40, -3180}, {-10, -3420}, {60, -3620}, {100, -3700} }, 108, 6, 85, 0.42, false));
	V.push_back(mk({ {-150, -4070}, {-330, -4190}, {-490, -4300}, {-640, -4390}, {-800, -4420} }, 6, 52, 80, 0.45, false));
	return V;
}
double ValleyFloor(const Valley& V, double x, double z) {
	if (x < V.box[0] || x > V.box[2] || z < V.box[1] || z > V.box[3]) return kInf;
	double best = kInf, bs = 0;
	for (size_t i = 0; i + 1 < V.pts.size(); i++) {
		const double ax = V.pts[i].x, az = V.pts[i].z, dx = V.pts[i + 1].x - ax, dz = V.pts[i + 1].z - az;
		double L2 = dx * dx + dz * dz; if (L2 == 0) L2 = 1;
		const double t = Clamp(((x - ax) * dx + (z - az) * dz) / L2, 0, 1);
		const double d = Hypot(x - ax - dx * t, z - az - dz * t);
		if (d < best) { best = d; bs = V.cum[i] + std::sqrt(L2) * t; }
	}
	const double floor = V.shelf ? Lerp(66, 8, Smooth(0, 420, bs)) : Lerp(V.y0, V.y1, bs / V.L);
	const double dd = Max(0, best - V.w);
	return floor + dd * V.slope + dd * dd * 0.0006;
}
} // namespace wg_detail
double NeValleyFloor(double x, double z) {
	if (x < -1300 || z > -1900) return kInf;
	double f = kInf;
	for (const auto& V : wg_detail::Valleys()) f = Min(f, wg_detail::ValleyFloor(V, x, z));
	return f;
}

// ------------------------------------------------------------------ regions
Regions RegionWeights(double x, double z) {
	const double wx = x + (FbmN(x * 0.0009, z * 0.0009, 2) - 0.5) * 700, wz = z + (FbmN(x * 0.0009 + 9, z * 0.0009 - 4, 2) - 0.5) * 700;
	const double d = Clamp(Smooth(-2750, -3350, wx) + Smooth(-3200, -3800, wz) * Smooth(-1900, -2500, wx), 0, 1);
	const double m = Clamp(Smooth(-1650, -2300, wz) * (1 - Smooth(-2300, -2900, wx)), 0, 1) * (1 - d) * (1 - NcUrban(x, z));
	return { d, m, Max(0, 1 - d - m) };
}

namespace wg_detail {
double CityCoast(double x, double z, double h) {
	const double eW = CITY_RECT.minX - 6, eE = CITY_RECT.maxX + 6, eS = CITY_RECT.maxZ + 6;
	(void)eW;
	if (z > eS && x > -1100 && x < 1600) {
		const double d = z - eS;
		if (x < 470) {
			const double beach = -d * 0.012 - Max(0, d - 70) * 0.05;
			const double k = Smooth(-1100, -850, x);
			h = z > eS + 2 ? Lerp(h, Min(h, beach), k) : h;
		} else {
			h = d < 34 ? 0 : -9;
		}
	}
	if (x > eE && z > -250) { const double d = x - eE; if (d > 36) h = Min(h, -9); else if (z > -250) h = Min(h, 0.0); }
	return h;
}

double CityRing(double x, double z, double dC) {
	double h = 5 * Smooth(0, 50, dC);
	const double n = FbmN(x * 0.004, z * 0.004, 5);
	if (z < CITY_RECT.minZ - 6) {
		const double d = CITY_RECT.minZ - 6 - z;
		h = Max(h, 5 * Smooth(0, 50, d) + Min(170, Max(0, d - 30) * 0.22) + n * 75 * Smooth(40, 260, d));
	}
	if (x < CITY_RECT.minX - 6) {
		const double d = CITY_RECT.minX - 6 - x;
		const double fade = 1 - Smooth(420, 620, z);
		const double pass = 1 - 0.78 * std::exp(-((z + 40) * (z + 40)) / (170 * 170));
		h = Max(h, (5 * Smooth(0, 50, d) + Min(130, Max(0, d - 30) * 0.2) + n * 65 * Smooth(40, 260, d)) * fade * pass);
	}
	if (x > CITY_RECT.maxX + 6 && z < -250) {
		const double d = x - CITY_RECT.maxX - 6;
		const double fade = Smooth(-250, -420, z);
		h = Max(h, (5 * Smooth(0, 50, d) + Min(140, Max(0, d - 30) * 0.22) + n * 70 * Smooth(40, 260, d)) * fade);
	}
	return h;
}
} // namespace wg_detail

double LandHeight(double x, double z) {
	using namespace wg_detail;
	const double dC = CityDist(x, z);
	if (dC <= 6) return CityCoast(x, z, 0);
	const Regions W = RegionWeights(x, z);
	double h = 0;
	if (W.country > 0.001) {
		const double c = 18 + (FbmN(x * 0.0009 + 3, z * 0.0009 - 7, 4) - 0.5) * 60 + (FbmN(x * 0.004, z * 0.004, 2) - 0.5) * 8;
		h += c * W.country;
	}
	if (W.mountain > 0.001) {
		const double r = Ridged(x * 0.0012, z * 0.0012, 5);
		double m = 55 + r * 300 * (0.5 + 0.7 * FbmN(x * 0.0005, z * 0.0005, 2)) + (FbmN(x * 0.003, z * 0.003, 3) - 0.5) * 30;
		const double pk = Hypot(x + 900, z + 3250);
		m += 320 * std::exp(-(pk * pk) / (800.0 * 800.0));
		h += m * W.mountain;
	}
	if (W.desert > 0.001) {
		double d = 44 + (FbmN(x * 0.0008 - 11, z * 0.0008 + 4, 3) - 0.5) * 30;
		d += std::sin(x * 0.018 + std::sin(z * 0.011) * 2.4 + PNoise(x * 0.004, z * 0.004) * 3) * 2.2 * Smooth(-3800, -4800, x);
		for (const Mesa& ms : MESAS) {
			const double dd0 = Hypot(x - ms.x, z - ms.z);
			if (dd0 > ms.r * 1.5 + 80) continue;
			const double wob = ms.r * (1 + 0.35 * PNoise(x * 0.008 + ms.x * 0.01, z * 0.008));
			const double t = Smooth(wob + 45, wob - 8, dd0);
			const double terr = std::floor(t * 3 + 0.2) / 3;
			d += ms.h * Lerp(t * t, terr, 0.6) * (0.92 + 0.08 * FbmN(x * 0.03, z * 0.03, 2));
		}
		h += d * W.desert;
	}

	if (dC < 1250) h = Lerp(CityRing(x, z, dC), h, Smooth(420, 1150, dC));

	// San Aurelio's plain
	{
		const double ne = NcEdgeDist(x, z);
		if (ne < 1300) {
			const double th = std::atan2(z - NCITY.z, x - NCITY.x);
			const double east = Smooth(0.1, 0.75, std::cos(th));
			const double t = Smooth(0, 380 + east * 900, ne);
			const double hill = east > 0 ? Lerp(h, Min(h, NCITY.y + 14 * t), east) : h;
			h = ne <= 0 ? NCITY.y : Lerp(NCITY.y, hill, t * t * (3 - 2 * t));
		}
	}
	// the valleys out of it
	{
		const double vf = NeValleyFloor(x, z);
		if (vf < h) h = vf;
	}

	const double dl = Hypot(x - LAKE.x, z - LAKE.z);
	if (dl < LAKE.r + 300) {
		const double shore = Lerp(LAKE.y + 2.5, h, Smooth(LAKE.r + 20, LAKE.r + 300, dl));
		h = dl < LAKE.r ? LAKE.y - 1.5 - 9 * Smooth(LAKE.r, LAKE.r * 0.3, dl) : shore;
	}

	const double dr = RiverDist(x, z);
	if (dr < 150) {
		const double bed = -2.2 - 1.6 * Smooth(16, 0, dr);
		const double k = Smooth(12, 150, dr);
		h = Lerp(bed, h, k * k * (3 - 2 * k));
	}

	if (x < -1000) {
		const double cz = CoastZ(x);
		if (z > cz - 220) {
			const double t = Smooth(cz - 220, cz, z);
			const double cliff = Smooth(0.53, 0.66, FbmN(x * 0.004, 5.5, 2));
			h = Lerp(h, Lerp(1.2, Max(h * 0.8, 8), cliff), t);
			if (z > cz) h = Min(h, 1.0 - (z - cz) * Lerp(0.045, 0.4, cliff));
		}
	}
	if (z < -250) {
		const double cx = CoastX(z);
		if (x > cx - 220) {
			const double t = Smooth(cx - 220, cx, x);
			h = Lerp(h, 1.0, t);
			if (x > cx) h = Min(h, 1.0 - (x - cx) * 0.06);
		}
	}
	h = CityCoast(x, z, h);
	const double eWd = x - WORLD.minX + (FbmN(4.2, z * 0.003, 2) - 0.5) * 60, eNd = z - WORLD.minZ + (FbmN(x * 0.003, 8.1, 2) - 0.5) * 60;
	if (eWd < 450) h += std::pow(1 - eWd / 450, 2) * 360;
	if (eNd < 450) h += std::pow(1 - eNd / 450, 2) * 360;
	const double sea = Min(Smooth(10, 170, eWd), Smooth(10, 170, eNd));
	if (sea < 1) h = Lerp(-28, h, sea * sea * (3 - 2 * sea));
	return Max(h, -40);
}

// ------------------------------------------------------------------ heightfield
Heightfield::Heightfield(const Rect& b, double stepM) {
	minX = b.minX; minZ = b.minZ; step = stepM;
	nx = (int)JsRound((b.maxX - b.minX) / step) + 1;
	nz = (int)JsRound((b.maxZ - b.minZ) / step) + 1;
	h.assign((size_t)nx * nz, 0.0f);
}

void Heightfield::Generate(double (*fn)(double, double), int coarse, std::vector<Pad>& pads) {
	const double cs = step * coarse;
	const int cnx = (int)std::ceil((nx - 1) / (double)coarse) + 1, cnz = (int)std::ceil((nz - 1) / (double)coarse) + 1;
	std::vector<float> c((size_t)cnx * cnz);
	for (int j = 0; j < cnz; j++) {
		const double z = minZ + j * cs;
		for (int i = 0; i < cnx; i++) c[(size_t)j * cnx + i] = (float)fn(minX + i * cs, z);
	}
	for (int j = 0; j < nz; j++) {
		const double fj = j / (double)coarse; const int j0 = (int)Min(cnz - 2, std::floor(fj)); const double tj = fj - j0;
		for (int i = 0; i < nx; i++) {
			const double fi = i / (double)coarse; const int i0 = (int)Min(cnx - 2, std::floor(fi)); const double ti = fi - i0;
			const double a = c[(size_t)j0 * cnx + i0], b = c[(size_t)j0 * cnx + i0 + 1], cc = c[(size_t)(j0 + 1) * cnx + i0], d = c[(size_t)(j0 + 1) * cnx + i0 + 1];
			h[(size_t)j * nx + i] = (float)(a + (b - a) * ti + (cc - a) * tj + (a - b - cc + d) * ti * tj);
		}
	}
	for (size_t i = 0; i + 1 < RIVER.size(); i++) {
		const double ax = RIVER[i].x, az = RIVER[i].z, bx = RIVER[i + 1].x, bz = RIVER[i + 1].z;
		ForRect(Min(ax, bx) - 60, Min(az, bz) - 60, Max(ax, bx) + 60, Max(az, bz) + 60, [&](int k, double x, double z) {
			const double d = RiverDist(x, z);
			if (d > 55) return;
			const double bed = -2.4 - 1.4 * Smooth(18, 0, d);
			const double t = Smooth(16, 55, d);
			h[k] = (float)Min((double)h[k], Lerp(bed, Max((double)h[k], 1.5), t));
		});
	}
	ForRect(CITY_RECT.minX - 2, CITY_RECT.minZ - 2, CITY_RECT.maxX + 2, CITY_RECT.maxZ + 2, [&](int k, double x, double z) {
		if (CityDist(x, z) < 1) h[k] = (float)wg_detail::CityCoast(x, z, 0);
	});
	for (Pad& p : pads) PadIt(p);
}

void Heightfield::CarveRoutes(const std::vector<RouteDef>& routes, const std::function<double(const std::string&)>& endY) {
	for (const RouteDef& R : routes) {
		if (R.type == "dirt") continue;
		std::vector<V2> ctrl = R.ctrl;
		Line pts = Catmull(ctrl, 24);
		auto endVal = [&](const RouteEnd& e) -> double { if (e.kind == RouteEnd::Num) return e.y; if (e.kind == RouteEnd::Key) return endY(e.key); return NaN(); };
		const double y0 = endVal(R.ends[0]), y1 = endVal(R.ends[1]);
		const double grade = IsSet(R.grade) ? R.grade : (R.type == "road" ? 0.08 : 0.06);
		ProfileOpts o; o.window = 420; o.maxGrade = grade; o.maxCut = 400; o.y0 = y0; o.y1 = y1; o.minY = 1.5;
		Profile prof = SolveProfile(pts, [this](double x, double z) { return Sample(x, z); }, o);
		const double core = (IsSet(R.width) ? R.width : 18) + 10, fall = 190;
		for (size_t i = 0; i + 1 < pts.size(); i++) {
			const double ax = pts[i].x, az = pts[i].z, bx = pts[i + 1].x, bz = pts[i + 1].z;
			const double ya = prof.y[i], yb = prof.y[i + 1];
			const double dx = bx - ax, dz = bz - az; double L2 = dx * dx + dz * dz; if (L2 == 0) L2 = 1;
			const double Rr = core + fall;
			ForRect(Min(ax, bx) - Rr, Min(az, bz) - Rr, Max(ax, bx) + Rr, Max(az, bz) + Rr, [&](int k, double x, double z) {
				if (CityDist(x, z) < 2) return;
				const double t = Clamp(((x - ax) * dx + (z - az) * dz) / L2, 0, 1);
				const double d = Hypot(x - ax - dx * t, z - az - dz * t);
				if (d > Rr) return;
				const double ty = ya + (yb - ya) * t + 0.5;
				if (h[k] <= ty) return;
				const double w = d <= core ? 1 : 1 - Smooth(core, Rr, d);
				h[k] = (float)(h[k] + (ty - h[k]) * (w * w * (3 - 2 * w)));
			});
		}
	}
}

void Heightfield::ForRect(double x0, double z0, double x1, double z1, const std::function<void(int, double, double)>& cb) {
	const int i0 = (int)Clamp(std::floor((x0 - minX) / step), 0, nx - 1), i1 = (int)Clamp(std::ceil((x1 - minX) / step), 0, nx - 1);
	const int j0 = (int)Clamp(std::floor((z0 - minZ) / step), 0, nz - 1), j1 = (int)Clamp(std::ceil((z1 - minZ) / step), 0, nz - 1);
	for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) cb(j * nx + i, minX + i * step, minZ + j * step);
}

double Heightfield::PadIt(Pad& p) {
	const double blend = p.blend;
	if (p.r > 0) {
		const double y = IsSet(p.y) ? p.y : Sample(p.x, p.z);
		ForRect(p.x - p.r - blend, p.z - p.r - blend, p.x + p.r + blend, p.z + p.r + blend, [&](int k, double x, double z) {
			const double d = Hypot(x - p.x, z - p.z);
			double t = Smooth(p.r + blend, p.r, d);
			if (p.keepSea) t *= Smooth(-1, 14, h[k]);
			h[k] = (float)Lerp(h[k], y, t);
		});
		p.y = y;
	} else {
		const double y = IsSet(p.y) ? p.y : Sample((p.minX + p.maxX) / 2, (p.minZ + p.maxZ) / 2);
		ForRect(p.minX - blend, p.minZ - blend, p.maxX + blend, p.maxZ + blend, [&](int k, double x, double z) {
			const double dx = Max(Max(p.minX - x, 0), x - p.maxX), dz = Max(Max(p.minZ - z, 0), z - p.maxZ);
			const double t = Smooth(blend, 0, Hypot(dx, dz));
			h[k] = (float)Lerp(h[k], y, t);
		});
		p.y = y;
	}
	return p.y;
}

double Heightfield::Sample(double x, double z) const {
	double fx = (x - minX) / step, fz = (z - minZ) / step;
	if (fx < 0) fx = 0; else if (fx > nx - 1.001) fx = nx - 1.001;
	if (fz < 0) fz = 0; else if (fz > nz - 1.001) fz = nz - 1.001;
	const int i = (int)fx, j = (int)fz;
	const double tx = fx - i, tz = fz - j;
	const size_t k = (size_t)j * nx + i;
	const double a = h[k], b = h[k + 1], c = h[k + nx], d = h[k + nx + 1];
	return a + (b - a) * tx + (c - a) * tz + (a - b - c + d) * tx * tz;
}

void Heightfield::Normal(double x, double z, double out[3]) const {
	const double e = step;
	const double dx = Sample(x + e, z) - Sample(x - e, z), dz = Sample(x, z + e) - Sample(x, z - e);
	const double l = Hypot3(dx, 2 * e, dz);
	out[0] = -dx / l; out[1] = 2 * e / l; out[2] = -dz / l;
}

} // namespace atg
