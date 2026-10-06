// See VehicleModels.h. Line for line with vehiclemodels.js: carDesign (the per-body numbers), sectionAt /
// sideX / topY / lowerRing / cabRing (the cross-sections), carTemplate (the lofted parts), wheelGeometry and
// buildVehicleModel (light bar, taxi sign, armoured-van parts, door, wheels, seats and the crash hull).
#include "VehicleModels.h"
#include <map>
#include <memory>
#include <mutex>

namespace atg {

namespace {
constexpr int NS = 7, NA = 5, NT = 6;    // lower body: side, shoulder and top segments (per half section)
constexpr int NS2 = 5, NA2 = 4, NT2 = 6; // greenhouse

struct Ends { double z, x, vz, top, chin; };
struct Cab { double bf = 0, tf = 0, tr = 0, br = 0, top = 0, belt = 0.1, inset = 0.2, rr = 0.08, crown = 0.035; };
struct Span { double z0 = 0, z1 = 0; };
struct Bed { double z0, z1, floor; };
struct Cargo { double z0, z1, y0, y1; bool canvas; };
struct Extras {
	bool lip = false, sideskirt = false, twinExhaust = false, scoop = false, stripes = false, rails = false, steps = false, spare = false;
	bool slider = false, mudflaps = false, stacks = false, wing = false, intakes = false, louvres = false, trimLine = false, whitewall = false, pushbar = false;
};
struct Section { double z, w, y0, y1, rt, crown, yB; };
struct CabRing { std::vector<Pt2> pts; double yb, yc, xb, xr; };

struct Design {
	double f, r, L, W, H, c, R, wb, track, tireW;
	double rt = 0.1, tumble = 0.05, under = 0.06, belly = 0.45, crown = 0.03, clad = 0;
	Ends nose{ 0.42, 0.16, 0.14, 0.05, 0.1 }, tail{ 0.32, 0.1, 0.12, 0.04, 0.12 };
	Cab cab;
	int doors = 4; double doorLen = 1.12, pillarC = 0.34; bool rearGlass = true; bool hasSideGlass = false; double sideGlass[2] = { 0, 0 };
	std::string bumper = "body", grille = "wide", lamps = "wide", tails = "wide";
	double bumperH = 0.22; bool trunk = true;
	bool hasBed = false; Bed bed{}; bool hasCargo = false; Cargo cargo{};
	double fender = 0; Extras extras;
	std::function<double(double)> line;
	double zf = 0, zr = 0, aR = 0, xi = 0;
	Span door; bool hasDoor2 = false; Span door2;
	std::function<double(double)> roof;
	std::vector<Section> cabS; std::vector<CabRing> cabR;
	double tubXw = 0, tubYt = 0;
};

Design CarDesign(const VehicleDef& def) {
	Design D;
	const double L = def.L, W = def.W, H = def.H, c = def.clearance, f = L / 2, r = -L / 2;
	D.f = f; D.r = r; D.L = L; D.W = W; D.H = H; D.c = c; D.R = def.wheelR; D.wb = def.wheelbase; D.track = def.track;
	D.tireW = def.body == "truck" ? 0.32 : 0.24;
	std::vector<Pt2> line;
	Cab& C = D.cab;
	const std::string& b = def.body;
	if (b == "coupe") {
		line = { { f, 0.6 }, { f - 0.4, 0.69 }, { f - 1.45, 0.81 }, { r + 0.9, 0.86 }, { r + 0.25, 0.87 }, { r, 0.82 } };
		C.bf = f - 1.45; C.tf = f - 2.15; C.tr = r + 1.55; C.br = r + 0.8; C.top = H; C.inset = 0.24; C.rr = 0.1;
		D.rt = 0.13; D.tumble = 0.07; D.doors = 2; D.doorLen = 1.3; D.pillarC = 0.3; D.nose = { 0.5, 0.22, 0.18, 0.05, 0.08 };
		D.grille = "slim"; D.lamps = "swept"; D.tails = "slim"; D.fender = 0.02; D.extras.lip = D.extras.sideskirt = D.extras.twinExhaust = true;
	} else if (b == "muscle") {
		line = { { f, 0.79 }, { f - 0.3, 0.85 }, { f - 1.9, 0.9 }, { r + 1.25, 0.92 }, { r + 0.2, 0.93 }, { r, 0.9 } };
		C.bf = f - 1.9; C.tf = f - 2.45; C.tr = r + 1.65; C.br = r + 1.05; C.top = H; C.inset = 0.17; C.rr = 0.05; C.crown = 0.02;
		D.rt = 0.05; D.tumble = 0.025; D.under = 0.035; D.belly = 0.55; D.doors = 2; D.doorLen = 1.3; D.pillarC = 0.38;
		D.nose = { 0.2, 0.07, 0.07, 0.02, 0.05 }; D.tail = { 0.16, 0.05, 0.06, 0.02, 0.06 };
		D.bumper = "chrome"; D.bumperH = 0.16; D.grille = "muscle"; D.lamps = "round2"; D.tails = "bar"; D.extras.scoop = D.extras.stripes = D.extras.twinExhaust = true;
	} else if (b == "suv") {
		line = { { f, 0.98 }, { f - 0.35, 1.05 }, { f - 1.3, 1.11 }, { r + 0.2, 1.13 }, { r, 1.1 } };
		C.bf = f - 1.3; C.tf = f - 1.95; C.tr = r + 0.28; C.br = r + 0.12; C.top = H; C.belt = 0.09; C.inset = 0.16; C.rr = 0.09;
		D.rt = 0.09; D.tumble = 0.04; D.clad = 0.2; D.doors = 4; D.doorLen = 1.05; D.pillarC = 0.26; D.trunk = false;
		D.nose = { 0.36, 0.13, 0.12, 0.05, 0.1 }; D.tail = { 0.22, 0.08, 0.1, 0.03, 0.1 };
		D.bumper = "dark"; D.bumperH = 0.26; D.grille = "tall"; D.lamps = "wide"; D.tails = "tall"; D.fender = 0.02;
		D.extras.rails = D.extras.steps = true; D.extras.spare = def.military;
	} else if (b == "pickup") {
		line = { { f, 0.98 }, { f - 0.35, 1.05 }, { f - 1.45, 1.1 }, { f - 3.0, 1.12 }, { r + 0.05, 1.14 }, { r, 1.12 } };
		C.bf = f - 1.45; C.tf = f - 2.05; C.tr = f - 2.92; C.br = f - 3.0; C.top = H; C.belt = 0.09; C.inset = 0.14; C.rr = 0.08;
		D.rt = 0.08; D.tumble = 0.035; D.clad = 0.12; D.doors = 2; D.doorLen = 1.2; D.pillarC = 0.12; D.trunk = false;
		D.hasBed = true; D.bed = { r + 0.12, f - 3.1, 0.74 };
		D.nose = { 0.3, 0.1, 0.1, 0.04, 0.1 }; D.tail = { 0.1, 0.04, 0.06, 0.02, 0.1 };
		D.bumper = "chrome"; D.bumperH = 0.24; D.grille = "truck"; D.lamps = "tall"; D.tails = "tall"; D.extras.steps = true;
	} else if (b == "van") {
		line = { { f, 0.92 }, { f - 0.4, 1.0 }, { f - 0.95, 1.06 }, { r + 0.05, 1.08 }, { r, 1.05 } };
		C.bf = f - 0.95; C.tf = f - 1.6; C.tr = r + 0.07; C.br = r; C.top = H; C.belt = 0.08; C.inset = 0.12; C.rr = 0.1;
		D.rt = 0.08; D.tumble = 0.03; D.doors = 2; D.doorLen = 0.92; D.pillarC = 0.0; D.trunk = false; D.rearGlass = false;
		D.hasSideGlass = true; D.sideGlass[0] = f - 2.05; D.sideGlass[1] = f;
		D.nose = { 0.36, 0.16, 0.16, 0.06, 0.1 }; D.tail = { 0.12, 0.05, 0.05, 0.02, 0.08 };
		D.bumper = "dark"; D.bumperH = 0.24; D.grille = "tall"; D.lamps = "tall"; D.tails = "tall"; D.extras.slider = !def.armored;
	} else if (b == "truck") {
		line = { { f, 1.2 }, { f - 0.3, 1.3 }, { f - 0.6, 1.35 }, { f - 2.3, 1.36 }, { r, 1.36 } };
		C.bf = f - 0.6; C.tf = f - 1.0; C.tr = f - 2.18; C.br = f - 2.25; C.top = 2.6; C.belt = 0.07; C.inset = 0.12; C.rr = 0.12;
		D.rt = 0.08; D.tumble = 0.03; D.under = 0.04; D.doors = 2; D.doorLen = 0.95; D.pillarC = 0.0; D.trunk = false; D.rearGlass = true;
		D.nose = { 0.22, 0.12, 0.12, 0.04, 0.06 }; D.tail = { 0.06, 0.03, 0.04, 0.01, 0.04 };
		D.bumper = "dark"; D.bumperH = 0.3; D.grille = "truck"; D.lamps = "tall"; D.tails = "tall";
		D.hasCargo = true; D.cargo = { r + 0.02, f - 2.4, 1.2, H, def.military };
		D.extras.mudflaps = true; D.extras.stacks = !def.military;
	} else if (b == "super") {
		line = { { f, 0.48 }, { f - 0.6, 0.6 }, { f - 1.45, 0.75 }, { r + 0.6, 0.86 }, { r + 0.1, 0.86 }, { r, 0.8 } };
		C.bf = f - 1.45; C.tf = f - 2.15; C.tr = r + 1.45; C.br = r + 0.65; C.top = H; C.belt = 0.14; C.inset = 0.3; C.rr = 0.12; C.crown = 0.05;
		D.rt = 0.16; D.tumble = 0.09; D.under = 0.05; D.belly = 0.35; D.doors = 2; D.doorLen = 1.2; D.pillarC = 0.45; D.trunk = false;
		D.nose = { 0.55, 0.26, 0.22, 0.04, 0.04 }; D.tail = { 0.3, 0.12, 0.12, 0.03, 0.1 };
		D.grille = "intake"; D.lamps = "slit"; D.tails = "slit"; D.bumperH = 0.16; D.fender = 0.05;
		D.extras.wing = D.extras.intakes = D.extras.louvres = D.extras.twinExhaust = D.extras.sideskirt = true;
	} else if (b == "lowrider") {
		line = { { f, 0.74 }, { f - 0.3, 0.8 }, { f - 1.7, 0.84 }, { r + 1.35, 0.86 }, { r + 0.2, 0.87 }, { r, 0.82 } };
		C.bf = f - 1.7; C.tf = f - 2.35; C.tr = r + 1.85; C.br = r + 1.25; C.top = H; C.inset = 0.15; C.rr = 0.07; C.crown = 0.025;
		D.rt = 0.06; D.tumble = 0.03; D.under = 0.04; D.belly = 0.55; D.doors = 2; D.doorLen = 1.4; D.pillarC = 0.42;
		D.nose = { 0.24, 0.08, 0.08, 0.02, 0.05 }; D.tail = { 0.2, 0.06, 0.07, 0.02, 0.07 };
		D.bumper = "chrome"; D.bumperH = 0.15; D.grille = "chrome"; D.lamps = "quad"; D.tails = "round"; D.extras.trimLine = D.extras.whitewall = true;
	} else if (b == "hatch") {
		line = { { f, 0.7 }, { f - 0.3, 0.79 }, { f - 1.15, 0.9 }, { r + 0.25, 0.98 }, { r, 0.94 } };
		C.bf = f - 1.15; C.tf = f - 1.88; C.tr = r + 0.42; C.br = r + 0.12; C.top = H; C.belt = 0.1; C.inset = 0.19; C.rr = 0.1;
		D.rt = 0.12; D.tumble = 0.05; D.doors = 4; D.doorLen = 1.0; D.pillarC = 0.34; D.trunk = false;
		D.nose = { 0.36, 0.16, 0.14, 0.05, 0.08 }; D.tail = { 0.18, 0.08, 0.08, 0.03, 0.1 };
		D.grille = "slim"; D.lamps = "swept"; D.tails = "tall";
	} else { // sedan (Meridian, cab, police cruiser)
		line = { { f, 0.76 }, { f - 0.35, 0.83 }, { f - 1.0, 0.88 }, { f - 1.45, 0.93 }, { r + 0.85, 0.97 }, { r + 0.35, 0.98 }, { r, 0.93 } };
		C.bf = f - 1.45; C.tf = f - 2.2; C.tr = r + 1.4; C.br = r + 0.85; C.top = H;
		D.doors = 4; D.extras.pushbar = def.police;
	}
	D.line = SmoothLine(line);
	D.zf = D.wb / 2; D.zr = -D.wb / 2;
	D.aR = D.R + 0.07;
	D.xi = D.track / 2 - D.tireW / 2 - 0.05;
	D.door = { C.bf - 0.02, Max(C.bf - 0.02 - D.doorLen, C.br + 0.12) };
	if (D.doors == 4) { D.hasDoor2 = true; D.door2 = { D.door.z1, Max(Max(D.door.z1 - 0.95, D.zr + D.aR + 0.03), C.br + 0.05) }; }
	const Cab Cc = C;
	const auto lineF = D.line;
	auto pl = [Cc, lineF](double z) {
		const double top = Cc.top;
		if (z >= Cc.tf) return top + (lineF(Cc.bf) - top) * Clamp((z - Cc.tf) / Max(0.01, Cc.bf - Cc.tf), 0, 1);
		if (z <= Cc.tr) return top + (lineF(Cc.br) - top) * Clamp((Cc.tr - z) / Max(0.01, Cc.tr - Cc.br), 0, 1);
		return top;
	};
	D.roof = [Cc, pl](double z) {
		const double rad = Min(0.14, Min((Cc.bf - z) * 0.7, (z - Cc.br) * 0.7));
		if (rad < 0.01) return pl(z);
		double s = 0, n = 0;
		for (int k = -4; k <= 4; k++) { const double w = 5 - std::abs(k); s += pl(z + k / 4.0 * rad) * w; n += w; }
		return s / n;
	};
	return D;
}

Section SectionAt(const Design& D, double z) {
	auto ne = [](double d, double e) { return d >= e ? 0.0 : 1 - std::sqrt(Max(0, 1 - std::pow((e - d) / e, 2))); };
	const double dF = D.f - z, dR = z - D.r;
	const double w = D.W / 2 - Max(ne(dF, D.nose.z) * D.nose.x, ne(dR, D.tail.z) * D.tail.x);
	const double vF = ne(dF, D.nose.vz), vR = ne(dR, D.tail.vz);
	double y1 = D.line(z) - vF * D.nose.top - vR * D.tail.top;
	const double y0 = D.c + vF * D.nose.chin + vR * D.tail.chin;
	double rt = Min(D.rt, (y1 - y0) * 0.35), crown = D.crown;
	for (double zc : { D.zf, D.zr }) {
		const double dz = std::fabs(z - zc), rr = D.aR + 0.12;
		if (dz >= rr) continue;
		const double need = D.R + std::sqrt(rr * rr - dz * dz) - 0.06 + D.fender;
		rt = Min(rt, Max(0.035, y1 - need));
		if (need + 0.035 > y1) { const double d = need + 0.035 - y1; y1 += d; crown -= d; }
	}
	return { z, w, y0, y1, rt, crown, y0 + (y1 - rt - y0) * D.belly };
}
double SideX(const Design& D, const Section& s, double y) {
	const double top = s.y1 - s.rt;
	if (y <= s.yB) { const double t = Clamp((y - s.y0) / Max(1e-4, s.yB - s.y0), 0, 1); return s.w - D.under * (1 - t) * (1 - t); }
	const double t = Clamp((y - s.yB) / Max(1e-4, top - s.yB), 0, 1);
	return s.w - D.tumble * t * t;
}
double TopY(const Design& D, const Section& s, double x) {
	const double xs = SideX(D, s, s.y1 - s.rt), xt = xs - s.rt;
	x = std::fabs(x);
	if (x <= xt) return s.y1 + s.crown * (1 - std::pow(x / Max(0.01, xt), 2));
	const double dx = Min(s.rt, x - xt);
	return s.y1 - s.rt + std::sqrt(Max(0, s.rt * s.rt - dx * dx));
}
std::vector<Pt2> LowerRing(const Design& D, const Section& s, double ys) {
	std::vector<Pt2> pts;
	const double top = s.y1 - s.rt;
	ys = Min(ys, top - 0.005);
	for (int j = 0; j <= NS; j++) { const double y = ys + (top - ys) * j / NS; pts.push_back({ SideX(D, s, y), y }); }
	const double cx = SideX(D, s, top) - s.rt;
	for (int k = 1; k <= NA; k++) { const double a = ((double)k / NA) * kPi / 2; pts.push_back({ cx + std::cos(a) * s.rt, top + std::sin(a) * s.rt }); }
	for (int k = 1; k <= NT; k++) { const double x = cx * (1 - (double)k / NT); pts.push_back({ x, s.y1 + s.crown * (1 - std::pow(x / Max(0.01, cx), 2)) }); }
	return pts;
}
CabRing MakeCabRing(const Design& D, const Section& s) {
	const Cab& C = D.cab;
	const double xb = s.w - C.belt, yb = TopY(D, s, xb) - 0.004;
	const double yc = Max(yb, D.roof(s.z));
	const double xr = Min(xb - 0.01, s.w - C.inset);
	const double rr = Max(0, Min(C.rr, Min((yc - yb) * 0.6, xr * 0.4)));
	CabRing out; out.yb = yb; out.yc = yc; out.xb = xb; out.xr = xr;
	for (int j = 0; j <= NS2; j++) { const double t = (double)j / NS2; out.pts.push_back({ xb + (xr - xb) * (0.75 * t + 0.25 * t * t), yb + (yc - rr - yb) * t }); }
	for (int k = 1; k <= NA2; k++) { const double a = ((double)k / NA2) * kPi / 2; out.pts.push_back({ xr - rr + std::cos(a) * rr, yc - rr + std::sin(a) * rr }); }
	const double x0 = xr - rr;
	for (int k = 1; k <= NT2; k++) { const double x = x0 * (1 - (double)k / NT2); out.pts.push_back({ x, yc + C.crown * (1 - std::pow(x / Max(0.01, x0), 2)) * Clamp((yc - yb) / 0.2, 0, 1) }); }
	return out;
}

Mat4 M4(double x = 0, double y = 0, double z = 0, double rx = 0, double ry = 0, double rz = 0, double sx = 1, double sy = 1, double sz = 1) { return Mat4::Compose(x, y, z, rx, ry, rz, sx, sy, sz); }
Mat4 RotX(double a) { return Mat4::Compose(0, 0, 0, a, 0, 0); }

struct Template {
	Design D;
	std::map<std::string, Part> P;
	double seatY = 0, seatZ = 0, cabMid = 0;
	Pt3 hinge{ 0, 0, 0 };
};

Template CarTemplate(const VehicleDef& def) {
	Template T;
	T.D = CarDesign(def);
	Design& D = T.D;
	const double f = D.f, r = D.r, W = D.W, c = D.c, R = D.R, zf = D.zf, zr = D.zr, aR = D.aR, xi = D.xi;
	const Cab& C = D.cab;
	auto& P = T.P;
	for (const char* k : { "body", "hood", "trunk", "door", "door2", "glass", "doorGlass", "trim", "chrome", "dark", "head", "tail", "bumperF", "bumperR" }) P[k];
	Part &body = P["body"], &hood = P["hood"], &trunk = P["trunk"], &door = P["door"], &door2 = P["door2"], &glass = P["glass"], &doorGlass = P["doorGlass"];
	Part &trim = P["trim"], &chrome = P["chrome"], &head = P["head"], &tail = P["tail"];
	Part* dark = &P["dark"];
	const double DARK[3] = { 0.035, 0.035, 0.037 };
	auto darkReset = [&]() { dark->color(DARK[0], DARK[1], DARK[2]); };
	darkReset();
	auto inDoor = [&](double z) { return (z < D.door.z0 && z > D.door.z1) || (D.hasDoor2 && z < D.door2.z0 && z > D.door2.z1); };
	auto inFront = [&](double z) { return z < D.door.z0 && z > D.door.z1; };
	const bool police = def.police;
	auto bodyCol = [&](double, double y, double z) -> Pt3 {
		if (police) {
			const bool white = (y > D.line(z) - 0.06 && z < C.bf + 0.05 && z > C.br - 0.05) || (z < D.door.z0 + 0.02 && z > (D.hasDoor2 ? D.door2.z1 : D.door.z1 - 0.8));
			return white && y > c + 0.1 ? Pt3{ 1, 1, 1 } : Pt3{ 0.045, 0.045, 0.05 };
		}
		if (D.clad > 0 && y < c + D.clad) return { 0.09, 0.09, 0.09 };
		return { 1, 1, 1 };
	};

	// ---------------- lower body, split at the wheel arches
	const double breaks[6] = { r, zr - aR, zr + aR, zf - aR, zf + aR, f };
	std::vector<double> feat = { C.bf, C.br, C.tf, C.tr, D.door.z0, D.door.z1 };
	if (D.hasDoor2) { feat.push_back(D.door2.z0); feat.push_back(D.door2.z1); }
	feat.push_back(f - D.nose.z); feat.push_back(r + D.tail.z); feat.push_back(f - D.nose.vz); feat.push_back(r + D.tail.vz);
	if (D.hasBed) { feat.push_back(D.bed.z0); feat.push_back(D.bed.z1); }
	if (D.hasCargo) feat.push_back(D.cargo.z1);
	for (int k = 0; k < 5; k++) {
		const double z0 = breaks[k], z1 = breaks[k + 1];
		const bool arch = k == 1 || k == 3;
		const double zc = k == 1 ? zr : zf;
		const std::vector<double> zsL = Stations(z0, z1, 0.075, feat);
		std::vector<Section> S; for (double z : zsL) S.push_back(SectionAt(D, z));
		auto ysOf = [&](const Section& s) { return arch ? Max(s.y0, R + std::sqrt(Max(0, aR * aR - std::pow(s.z - zc, 2)))) : s.y0; };
		std::vector<std::vector<Pt2>> rings; for (const Section& s : S) rings.push_back(LowerRing(D, s, ysOf(s)));
		for (int side : { 1, -1 }) {
			std::vector<std::vector<Pt3>> rows;
			for (size_t i = 0; i < rings.size(); i++) { std::vector<Pt3> row; for (const Pt2& p : rings[i]) row.push_back({ p[0] * side, p[1], S[i].z }); rows.push_back(row); }
			GridOpts o; o.ref = { 0, (c + 1) / 2, rows[0][0][2] };
			o.colorAt = [&](double x, double y, double z, Part*) { return bodyCol(x, y, z); };
			EmitGrid(rows, [&](int i, int j) -> Part* {
				const double z = (S[i].z + S[i + 1].z) / 2;
				const int kind = j < NS ? 0 : j < NS + NA ? 1 : 2;
				if (kind < 2 || (kind == 2 && j < NS + NA + 2 && z < C.bf && z > C.br)) {
					if (inFront(z)) return side > 0 ? &door : &door2;
					if (inDoor(z) && kind < 2) return &body;
				}
				if (kind == 2) {
					if (z < C.bf - 0.01 && z > C.br + 0.01) return nullptr;
					if (D.hasBed && z > D.bed.z0 && z < D.bed.z1) return nullptr;
					if (D.hasCargo && z < D.cargo.z1 + 0.02) return nullptr;
					if (z > C.bf + 0.02 && z < f - D.nose.vz * 0.5 && def.body != "truck") return &hood;
					if (D.trunk && z < C.br - 0.02 && z > r + D.tail.vz * 0.5) return &trunk;
				}
				return &body;
			}, o);
			if (arch) {
				std::vector<std::vector<Pt3>> wall, lin;
				for (size_t i = 0; i < S.size(); i++) {
					wall.push_back({ { xi * side, S[i].y0, S[i].z }, { xi * side, ysOf(S[i]), S[i].z } });
					lin.push_back({ { xi * side, ysOf(S[i]), S[i].z }, { rings[i][0][0] * side, ysOf(S[i]), S[i].z } });
				}
				GridOpts ow; ow.ref = { 0, 0.8, zc };
				EmitGrid(wall, [&](int, int) { return dark; }, ow);
				GridOpts ol; ol.ref = { 0, 2, zc };
				EmitGrid(lin, [&](int, int) { return dark; }, ol);
				for (size_t e : { (size_t)0, S.size() - 1 }) {
					const Section& s = S[e];
					const double ys = ysOf(s);
					if (ys - s.y0 < 0.01) continue;
					std::vector<Pt3> pts = { { xi * side, s.y0, s.z } };
					for (int q = 0; q <= 4; q++) { const double y = s.y0 + (ys - s.y0) * q / 4; pts.push_back({ SideX(D, s, y) * side, y, s.z }); }
					pts.push_back({ xi * side, ys, s.z });
					dark->poly(pts, { 0, 0, s.z > zc ? -1.0 : 1.0 });
				}
			}
			std::vector<std::vector<Pt3>> fl;
			for (const Section& s : S) fl.push_back({ { 0, s.y0, s.z }, { (arch ? xi : SideX(D, s, s.y0)) * side, s.y0, s.z } });
			GridOpts of; of.ref = { 0, 3, 0 };
			EmitGrid(fl, [&](int, int) { return dark; }, of);
		}
		std::vector<size_t> caps;
		if (k == 0) caps.push_back(0);
		if (k == 4) caps.push_back(S.size() - 1);
		for (size_t e : caps) {
			const Section& s = S[e];
			const std::vector<Pt2>& ring = rings[e];
			std::vector<Pt3> pts;
			for (const Pt2& p : ring) pts.push_back({ p[0], p[1], s.z });
			for (int q = (int)ring.size() - 2; q >= 0; q--) pts.push_back({ -ring[q][0], ring[q][1], s.z });
			pts.push_back({ -ring[0][0], s.y0, s.z }); pts.push_back({ ring[0][0], s.y0, s.z });
			body.poly(pts, { 0, 0, e == 0 ? -1.0 : 1.0 });
		}
	}

	// ---------------- greenhouse
	{
		std::vector<std::array<double, 2>> pillars;
		if (D.doors == 4 && D.hasDoor2) pillars.push_back({ D.door.z1 - 0.05, D.door.z1 + 0.07 });
		else if (D.door.z1 > C.br + 0.25 && D.pillarC > 0) pillars.push_back({ D.door.z1 - 0.04, D.door.z1 + 0.05 });
		if (D.pillarC > 0) pillars.push_back({ C.br - 0.01, C.br + D.pillarC });
		if (def.body == "suv") pillars.push_back({ D.door2.z1 - 0.05, D.door2.z1 + 0.05 });
		std::vector<double> fe = { C.tf, C.tr, D.door.z0, D.door.z1 };
		if (D.hasDoor2) { fe.push_back(D.door2.z0); fe.push_back(D.door2.z1); }
		for (auto& p : pillars) { fe.push_back(p[0]); fe.push_back(p[1]); }
		if (D.hasSideGlass) { fe.push_back(D.sideGlass[0]); fe.push_back(D.sideGlass[1]); }
		const std::vector<double> zsC = Stations(C.br, C.bf, 0.06, fe);
		std::vector<Section> S; for (double z : zsC) S.push_back(SectionAt(D, z));
		std::vector<CabRing> rings; for (const Section& s : S) rings.push_back(MakeCabRing(D, s));
		auto inPillar = [&](double z) {
			for (auto& p : pillars) if (z > p[0] && z < p[1]) return true;
			return D.hasSideGlass && (z < D.sideGlass[0] || z > D.sideGlass[1]);
		};
		for (int side : { 1, -1 }) {
			std::vector<std::vector<Pt3>> rows;
			for (size_t i = 0; i < rings.size(); i++) { std::vector<Pt3> row; for (const Pt2& p : rings[i].pts) row.push_back({ p[0] * side, p[1], S[i].z }); rows.push_back(row); }
			GridOpts o; o.ref = { 0, 0.8, (C.bf + C.br) / 2 };
			o.colorAt = [&](double x, double y, double z, Part*) { return bodyCol(x, y, z); };
			o.offset = [&](Part* p) { return p == &glass || p == &doorGlass ? -0.008 : 0.0; };
			EmitGrid(rows, [&](int i, int j) -> Part* {
				const double z = (S[i].z + S[i + 1].z) / 2;
				const double h = rings[i].yc - rings[i].yb;
				if (j < NS2) {
					if (h < 0.03) return &body;
					if (inPillar(z)) return &body;
					return inFront(z) && side > 0 ? &doorGlass : &glass;
				}
				if (j < NS2 + NA2) return &body;
				if (z > C.tr + 0.005 && z < C.tf - 0.005) return &body;
				if (z >= C.tf) return &glass;
				return D.rearGlass ? &glass : &body;
			}, o);
		}
		for (int side : { 1, -1 }) {
			std::vector<std::vector<Pt3>> rows;
			for (size_t i = 0; i < rings.size(); i++) { std::vector<Pt3> row; for (const Pt2& p : rings[i].pts) row.push_back({ p[0] * side, p[1], S[i].z }); rows.push_back(row); }
			GridOpts o; o.ref = { 0, 0.8, (C.bf + C.br) / 2 }; o.inward = true;
			o.offset = [](Part*) { return 0.02; };
			o.colorAt = [](double, double, double, Part*) { return Pt3{ 0.55, 0.53, 0.5 }; };
			EmitGrid(rows, [&](int i, int j) -> Part* {
				const double z = (S[i].z + S[i + 1].z) / 2;
				if (j < NS2) return inPillar(z) ? &trim : nullptr;
				if (j < NS2 + NA2) return &trim;
				return z > C.tr + 0.005 && z < C.tf - 0.005 ? &trim : nullptr;
			}, o);
		}
		for (int side : { 1, -1 }) {
			std::vector<Pt3> path;
			for (size_t i = 0; i < S.size(); i++) { const CabRing& rg = rings[i]; if (rg.yc - rg.yb > 0.05) path.push_back({ rg.xb * side, rg.yb + 0.004, S[i].z }); }
			if (path.size() > 2) Sweep(*dark, path, { { -0.012, -0.004 }, { 0.012, -0.004 }, { 0.012, 0.018 }, { -0.012, 0.018 } });
		}
		D.cabS = S; D.cabR = rings;
	}

	// ---------------- pickup bed / cargo box / interior tub
	if (D.hasBed) {
		const Bed& b = D.bed;
		const Section s0 = SectionAt(D, (b.z0 + b.z1) / 2);
		const double xin = SideX(D, s0, s0.y1 - s0.rt) - 0.07;
		dark->hex(0x1d1d1f).box(-xin, b.floor - 0.03, b.z0, xin, b.floor, b.z1);
		body.color(1, 1, 1);
		for (int sd : { 1, -1 }) body.box(sd > 0 ? xin - 0.012 : -xin, b.floor, b.z0, sd > 0 ? xin : -xin + 0.012, s0.y1 + 0.005, b.z1);
		body.box(-xin, b.floor, b.z1 - 0.012, xin, s0.y1 + 0.005, b.z1);
		body.box(-xin, b.floor, b.z0, xin, s0.y1 + 0.005, b.z0 + 0.012);
		dark->hex(0x151515);
		for (int sd : { 1, -1 }) RoundBox(*dark, sd > 0 ? xi - 0.02 : -xin, sd > 0 ? xin : -xi + 0.02, b.floor, R + aR - 0.06, zr - aR + 0.02, zr + aR - 0.02, 0.1, 3);
		darkReset();
	}
	if (D.hasCargo) {
		const Cargo& g = D.cargo;
		const double cw = W / 2 + 0.02;
		if (g.canvas) {
			trim.hex(0x4a5230);
			std::vector<Pt2> sec;
			for (int k = 0; k <= 8; k++) { const double a = kPi * k / 8; sec.push_back({ std::cos(a) * cw, g.y1 - 0.45 + std::sin(a) * 0.45 }); }
			sec.push_back({ -cw, g.y0 + 0.35 }); sec.push_back({ cw, g.y0 + 0.35 });
			std::vector<std::vector<Pt3>> rows;
			for (double z : { g.z0, g.z1 }) { std::vector<Pt3> row; for (const Pt2& p : sec) row.push_back({ p[0], p[1], z }); row.push_back(row[0]); rows.push_back(row); }
			GridOpts o; o.ref = { 0, (g.y0 + g.y1) / 2, (g.z0 + g.z1) / 2 };
			EmitGrid(rows, [&](int, int) { return &trim; }, o);
			std::vector<Pt3> e1, e0;
			for (const Pt2& p : sec) e1.push_back({ p[0], p[1], g.z1 });
			for (auto it = sec.rbegin(); it != sec.rend(); ++it) e0.push_back({ (*it)[0], (*it)[1], g.z0 });
			trim.poly(e1, { 0, 0, 1 });
			trim.poly(e0, { 0, 0, -1 });
			body.color(1, 1, 1);
			body.box(-cw, g.y0, g.z0, cw, g.y0 + 0.36, g.z1, true);
			trim.color(1, 1, 1);
		} else {
			body.color(0.96, 0.96, 0.96);
			RoundBox(body, -cw, cw, g.y0, g.y1, g.z0, g.z1, 0.06, 2);
			body.color(1, 1, 1);
			trim.hex(0x9a9a9a);
			for (int k = 1; k < 10; k++) { const double y = g.y0 + 0.15 + (g.y1 - g.y0 - 0.3) * k / 10; trim.box(-cw + 0.12, y - 0.006, g.z0 - 0.012, cw - 0.12, y + 0.006, g.z0 - 0.002); }
			trim.hex(0x2a2a2a);
			trim.box(-cw + 0.1, g.y0 + 0.1, g.z0 - 0.02, -cw + 0.16, g.y1 - 0.08, g.z0);
			trim.box(cw - 0.16, g.y0 + 0.1, g.z0 - 0.02, cw - 0.1, g.y1 - 0.08, g.z0);
		}
		dark->hex(0x151515).box(-0.5, c + 0.1, g.z0 + 0.3, 0.5, g.y0, g.z1 - 0.2);
		darkReset();
	}
	{
		const double zb = C.br + 0.02, zf2 = C.bf - 0.04;
		const Section sm = SectionAt(D, (zb + zf2) / 2);
		const double xw = SideX(D, sm, sm.y1 - sm.rt) - 0.06, yt = sm.y1 - 0.02;
		trim.hex(0x2b2724);
		for (int sd : { 1, -1 }) trim.poly({ { sd * xw, c + 0.06, zb }, { sd * xw, c + 0.06, zf2 }, { sd * xw, yt, zf2 }, { sd * xw, yt, zb } }, { (double)-sd, 0, 0 });
		trim.hex(0x1c1a19);
		trim.poly({ { -xw, c + 0.06, zb }, { xw, c + 0.06, zb }, { xw, c + 0.06, zf2 }, { -xw, c + 0.06, zf2 } }, { 0, 1, 0 });
		trim.poly({ { -xw, c + 0.06, zf2 }, { xw, c + 0.06, zf2 }, { xw, yt, zf2 }, { -xw, yt, zf2 } }, { 0, 0, -1 });
		if (!D.hasCargo && def.body != "van") trim.poly({ { -xw, c + 0.06, zb }, { xw, c + 0.06, zb }, { xw, yt, zb }, { -xw, yt, zb } }, { 0, 0, 1 });
		D.tubXw = xw; D.tubYt = yt;
	}

	// ---------------- bumpers (swept around the nose and tail)
	struct BumperInfo { double yb0, yb1, xe, zFace; };
	auto mkBumper = [&](bool front) -> BumperInfo {
		Part& part = front ? P["bumperF"] : P["bumperR"];
		const double yb0 = c + 0.03 + (front ? 0 : 0.03), yb1 = yb0 + D.bumperH, ym = (yb0 + yb1) / 2;
		const double zEnd = front ? f : r;
		const double ez = front ? D.nose.z : D.tail.z;
		std::vector<Pt3> path;
		const double zSide = front ? f - ez - 0.12 : r + ez + 0.12;
		const int n = 10;
		for (int k = 0; k <= n; k++) {
			const double z = zSide + (zEnd - zSide) * std::pow((double)k / n, 0.7);
			const Section s = SectionAt(D, Max(r, Min(f, z)));
			path.push_back({ SideX(D, s, Max(s.y0 + 0.01, ym)), 0, z });
		}
		const double xe = path.back()[0];
		const int nx = 6;
		for (int k = 1; k < nx; k++) path.push_back({ xe * (1 - 2.0 * k / nx), 0, zEnd });
		for (int k = n; k >= 0; k--) path.push_back({ -path[k][0], 0, path[k][2] });
		const double t = D.bumper == "chrome" ? 0.075 : 0.05, rr = D.bumper == "chrome" ? 0.05 : 0.035;
		std::vector<Pt2> sec;
		for (int k = 0; k <= 4; k++) { const double a = -kPi / 2 + (k / 4.0) * kPi / 2; sec.push_back({ t - rr + std::cos(a) * rr, yb0 + rr + std::sin(a) * rr }); }
		for (int k = 0; k <= 4; k++) { const double a = (k / 4.0) * kPi / 2; sec.push_back({ t - rr + std::cos(a) * rr, yb1 - rr + std::sin(a) * rr }); }
		sec.push_back({ -0.04, yb1 }); sec.push_back({ -0.04, yb0 });
		if (D.bumper == "chrome") part.color(0.9, 0.9, 0.92); else if (D.bumper == "dark") part.color(0.1, 0.1, 0.1); else part.color(1, 1, 1);
		Sweep(part, path, sec, true, !front);
		if (D.bumper != "dark") {
			dark->color(0.05, 0.05, 0.05);
			const double zz = zEnd + (front ? 0.035 : -0.035);
			dark->box(-xe * 0.72, yb0 - 0.01, front ? zz - 0.03 : zz, xe * 0.72, yb0 + 0.05, front ? zz : zz + 0.03);
			darkReset();
		}
		return { yb0, yb1, xe, zEnd + (front ? t : -t) };
	};
	const BumperInfo BF = mkBumper(true), BR = mkBumper(false);
	(void)BR;

	// ---------------- lamps: lenses laid onto the curved nose / tail
	const Section sF = SectionAt(D, f), sR = SectionAt(D, r);
	auto endZ = [&](bool front, double x, double y) {
		const Ends& e = front ? D.nose : D.tail;
		const double zE = front ? f : r;
		const Section& s = front ? sF : sR;
		const double k = Clamp((D.W / 2 - std::fabs(x)) / e.x, 0, 1);
		const double dp = e.z * (1 - std::sqrt(Max(0, 1 - std::pow(1 - k, 2))));
		const double yTopFull = D.line(zE);
		const double kv = Clamp((yTopFull - y) / Max(0.005, e.top), 0, 1);
		const double dv = y > s.y1 - 0.02 ? e.vz * (1 - std::sqrt(Max(0, 1 - std::pow(1 - kv, 2)))) : 0;
		return zE + (front ? -1 : 1) * Max(dp, dv);
	};
	auto lens = [&](Part& part, bool front, double cx, double cy, double hx, double hy, double shape = 6, double skew = 0, double wrap = 0) {
		const int N = 8;
		std::vector<std::vector<Pt3>> rows;
		for (int i = 0; i <= N; i++) {
			std::vector<Pt3> row;
			for (int j = 0; j <= N; j++) {
				double a = (double)i / N * 2 - 1, b = (double)j / N * 2 - 1;
				const double m = Max(std::fabs(a), std::fabs(b));
				if (m > 0) { const double sa = std::fabs(a) / m, sb = std::fabs(b) / m; const double q = std::pow(std::pow(sa, shape) + std::pow(sb, shape), 1 / shape); a /= q; b /= q; }
				const double x = cx + a * hx, y = cy + b * hy + skew * a * hx;
				const double sx = x > 0 ? 1 : x < 0 ? -1 : 0;
				row.push_back({ x, y, endZ(front, x + wrap * sx, y) + (front ? 0.006 : -0.006) });
			}
			rows.push_back(row);
		}
		GridOpts o; o.ref = { cx * 0.5, cy, 0 };
		EmitGrid(rows, [&](int, int) { return &part; }, o);
	};
	const double yF = D.line(f) - D.nose.top, yR = D.line(r) - D.tail.top;
	const double hw = W / 2;
	for (int sd : { 1, -1 }) {
		const std::string& Lm = D.lamps;
		if (Lm == "round2" || Lm == "quad") { for (int k : { 0, 1 }) lens(head, true, sd * (hw - 0.2 - k * 0.22), yF - 0.12, 0.085, 0.085, 2); }
		else if (Lm == "slit") lens(head, true, sd * (hw - 0.3), yF - 0.05, 0.22, 0.035, 5, 0.12 * sd, 0.05);
		else if (Lm == "swept") lens(head, true, sd * (hw - 0.27), yF - 0.08, 0.2, 0.055, 5, 0.1 * sd, 0.04);
		else if (Lm == "tall") lens(head, true, sd * (hw - 0.22), yF - 0.16, 0.14, 0.11, 6);
		else lens(head, true, sd * (hw - 0.26), yF - 0.1, 0.2, 0.07, 6, 0, 0.03);
		const std::string& Tl = D.tails;
		if (Tl == "round") { for (int k : { 0, 1 }) lens(tail, false, sd * (hw - 0.18 - k * 0.2), yR - 0.14, 0.07, 0.07, 2); }
		else if (Tl == "bar") lens(tail, false, sd * (hw * 0.5), yR - 0.12, hw * 0.44, 0.05, 8);
		else if (Tl == "slit") lens(tail, false, sd * (hw - 0.34), yR - 0.06, 0.26, 0.03, 6, 0.05 * sd, 0.04);
		else if (Tl == "tall") lens(tail, false, sd * (hw - 0.12), yR - 0.25, 0.07, 0.18, 6);
		else lens(tail, false, sd * (hw - 0.3), yR - 0.12, 0.24, 0.065, 6, 0, 0.03);
		trim.hex(0xff8a00);
		const double ix = sd * (hw - 0.12), iy = BF.yb1 + 0.04;
		trim.box(ix - 0.05, iy - 0.02, endZ(true, ix, iy) - 0.02, ix + 0.05, iy + 0.02, endZ(true, ix, iy) + 0.008);
		trim.hex(0xe0e0e0);
		const double rx = sd * (hw * 0.32), ry = yR - 0.13;
		if (Tl != "bar") trim.box(rx - 0.06, ry - 0.025, endZ(false, rx, ry) - 0.008, rx + 0.06, ry + 0.025, endZ(false, rx, ry) + 0.02);
	}
	chrome.hex(0xffffff);
	if (D.lamps == "wide" || D.lamps == "tall" || D.lamps == "swept") for (int sd : { 1, -1 }) for (double k : { 0.35, 0.72 }) {
		const double x = sd * (hw - 0.26 - 0.2 + 0.4 * k), y = yF - (D.lamps == "tall" ? 0.16 : D.lamps == "swept" ? 0.08 : 0.1);
		chrome.geo(Geo::Cylinder(0.035, 0.035, 0.01, 12), M4(x, y, endZ(true, x, y) + 0.01, kPi / 2));
	}

	// ---------------- grille
	{
		const double zg = f + 0.003, gy1 = Min(yF - 0.06, BF.yb1 + 0.3), gy0 = BF.yb1 + 0.01;
		const std::string& G = D.grille;
		auto frame = [&](double gx, double y0, double y1, bool isChrome) {
			Part& pp = isChrome ? chrome : trim;
			pp.hex(isChrome ? 0xdddddd : 0x151515);
			pp.box(-gx - 0.025, y0 - 0.025, zg - 0.01, gx + 0.025, y1 + 0.025, zg + 0.02);
			dark->color(0.02, 0.02, 0.02).box(-gx, y0, zg, gx, y1, zg + 0.025);
			darkReset();
		};
		auto slats = [&](double gx, double y0, double y1, int n, bool isChrome, bool vertical = false) {
			Part& pp = isChrome ? chrome : trim;
			pp.hex(isChrome ? 0xe8e8e8 : 0x303030);
			if (vertical) for (int i = 1; i < n; i++) { const double x = -gx + 2 * gx * i / n; pp.box(x - 0.008, y0, zg + 0.02, x + 0.008, y1, zg + 0.035); }
			else for (int i = 1; i < n; i++) { const double y = y0 + (y1 - y0) * i / n; pp.box(-gx, y - 0.007, zg + 0.02, gx, y + 0.007, zg + 0.035); }
		};
		if (G == "intake") { frame(hw * 0.62, c + 0.06, c + 0.2, false); slats(hw * 0.62, c + 0.06, c + 0.2, 3, false); }
		else if (G == "muscle") { frame(hw - 0.08, gy0, yF - 0.04, false); slats(hw - 0.08, gy0, yF - 0.04, 5, true); }
		else if (G == "chrome") { frame(hw * 0.55, gy0, gy1, true); slats(hw * 0.55, gy0, gy1, 14, true, true); }
		else if (G == "truck") { frame(hw * 0.55, gy0, Min(yF - 0.04, gy0 + 0.55), true); slats(hw * 0.55, gy0, Min(yF - 0.04, gy0 + 0.55), 6, true); }
		else if (G == "tall") { frame(hw * 0.42, gy0, gy1, false); slats(hw * 0.42, gy0, gy1, 5, false); }
		else if (G == "slim") { frame(hw * 0.4, yF - 0.14, yF - 0.08, false); frame(hw * 0.55, c + 0.07, BF.yb0 + 0.1, false); }
		else { frame(hw * 0.45, gy0 + 0.02, gy1, false); slats(hw * 0.45, gy0 + 0.02, gy1, 4, false); }
		chrome.hex(0xdcdcdc).geo(Geo::Cylinder(0.035, 0.035, 0.012, 14), M4(0, (gy0 + gy1) / 2 + 0.02, zg + 0.04, kPi / 2));
	}

	// ---------------- number plates
	auto plate = [&](bool front) {
		const double z = front ? BF.zFace + 0.004 : endZ(false, 0, yR - 0.3) - 0.01, y = front ? (BF.yb0 + BF.yb1) / 2 - 0.065 : yR - 0.34, sg = front ? 1 : -1;
		trim.hex(0xeeeeee).box(-0.26, y, front ? z : z - 0.012, 0.26, y + 0.13, front ? z + 0.012 : z);
		trim.hex(0x1b2a55);
		for (int k = 0; k < 6; k++) { const double x = -0.2 + k * 0.08 + (k > 2 ? 0.02 : 0); trim.box(x - 0.022, y + 0.03, z + sg * 0.012 - 0.003, x + 0.022, y + 0.1, z + sg * 0.012 + 0.003); }
	};
	plate(true); plate(false);

	// ---------------- panel seams, handles, mirrors, wipers
	auto seamSide = [&](double z, double y0, double y1) {
		const Section s = SectionAt(D, z);
		for (int sd : { 1, -1 }) {
			std::vector<Pt3> path;
			for (int q = 0; q <= 6; q++) { const double y = y0 + (y1 - y0) * q / 6; path.push_back({ (SideX(D, s, Min(y, s.y1 - s.rt)) + 0.002) * sd, y, z }); }
			std::vector<std::vector<Pt3>> rows(2);
			for (const Pt3& p : path) { rows[0].push_back({ p[0], p[1], p[2] - 0.004 }); rows[1].push_back({ p[0], p[1], p[2] + 0.004 }); }
			GridOpts o; o.ref = { 0, (y0 + y1) / 2, z };
			EmitGrid(rows, [&](int, int) { return dark; }, o);
		}
	};
	const double sill = c + 0.08;
	auto beltAt = [&](double z) { const Section s = SectionAt(D, z); return s.y1 - s.rt * 0.5; };
	seamSide(D.door.z0, sill, beltAt(D.door.z0));
	seamSide(D.door.z1, sill, beltAt(D.door.z1));
	if (D.hasDoor2) seamSide(D.door2.z1, sill, beltAt(D.door2.z1));
	if (D.extras.slider) seamSide(D.door.z1 - 1.2, sill, D.line(D.door.z1) + 0.9);
	auto topSeam = [&](double z0, double z1, const std::function<double(const Section&)>& xOf) {
		for (int sd : { 1, -1 }) {
			std::vector<Pt3> path;
			for (int q = 0; q <= 10; q++) { const double z = z0 + (z1 - z0) * q / 10; const Section s = SectionAt(D, z); const double x = xOf(s); path.push_back({ x * sd, TopY(D, s, x) + 0.002, z }); }
			std::vector<std::vector<Pt3>> rows(2);
			for (const Pt3& p : path) { rows[0].push_back({ p[0] - 0.004, p[1], p[2] }); rows[1].push_back({ p[0] + 0.004, p[1], p[2] }); }
			GridOpts o; o.ref = { 0, 0, 0 };
			EmitGrid(rows, [&](int, int) { return dark; }, o);
		}
	};
	auto edgeX = [&](const Section& s) { return SideX(D, s, s.y1 - s.rt) - s.rt; };
	if (def.body != "truck") topSeam(C.bf + 0.03, f - D.nose.vz * 0.5, edgeX);
	if (D.trunk) topSeam(r + D.tail.vz * 0.5, C.br - 0.03, edgeX);
	auto handle = [&](double z) {
		const double y = beltAt(z) - 0.14;
		const Section s = SectionAt(D, z);
		for (int sd : { 1, -1 }) {
			const double x = SideX(D, s, y) * sd;
			Part& pp = D.bumper == "chrome" ? chrome : trim;
			pp.hex(D.bumper == "chrome" ? 0xdddddd : 0x151515);
			pp.box(Min(x, x + sd * 0.025), y - 0.018, z - 0.13, Max(x, x + sd * 0.025), y + 0.018, z - 0.01);
		}
	};
	handle(D.door.z1 + 0.02);
	if (D.hasDoor2) handle(D.door2.z1 + 0.02);
	{
		const double z = C.bf - 0.2;
		const Section s = SectionAt(D, z);
		const double y = beltAt(z) + 0.1, x = SideX(D, s, s.y1 - s.rt) + 0.1;
		for (int sd : { 1, -1 }) {
			Part& part = sd > 0 ? door : door2;
			part.color(1, 1, 1);
			part.geo(Geo::Sphere(0.1, 12, 8), M4(sd * (x + 0.04), y, z - 0.02, 0, 0, 0, 0.75, 0.7, 0.5));
			part.box(sd > 0 ? x - 0.1 : -x, y - 0.03, z - 0.04, sd > 0 ? x : -x + 0.1, y + 0.01, z + 0.02);
			chrome.hex(0x9aa4ae).box(sd * (x + 0.04) - 0.065, y - 0.045, z - 0.075, sd * (x + 0.04) + 0.065, y + 0.045, z - 0.065);
		}
	}
	{
		const Section s = SectionAt(D, C.bf);
		const double y = TopY(D, s, 0) + 0.02;
		dark->color(0.03, 0.03, 0.03).box(-s.w + C.belt + 0.05, y - 0.03, C.bf - 0.02, s.w - C.belt - 0.05, y + 0.005, C.bf + 0.1);
		darkReset();
		for (double x0 : { -0.55, 0.05 }) dark->geo(Geo::Box(0.5, 0.015, 0.02), M4(x0 + 0.24, y + 0.02, C.bf - 0.05, 0, -0.12, 0.04));
	}

	// ---------------- extras
	const Extras& X = D.extras;
	if (X.scoop) {
		hood.color(1, 1, 1);
		const Section s = SectionAt(D, f - 1.0); const double y = TopY(D, s, 0);
		RoundBox(hood, -0.28, 0.28, y - 0.02, y + 0.07, f - 1.35, f - 0.75, 0.05, 2);
		dark->color(0.02, 0.02, 0.02).box(-0.24, y + 0.005, f - 0.752, 0.24, y + 0.05, f - 0.745);
		darkReset();
	}
	if (X.stripes) {
		trim.hex(0xf2f2f2);
		for (double sx : { -0.2, 0.2 }) {
			std::vector<Pt3> path;
			for (int q = 0; q <= 24; q++) { const double z = r + 0.05 + (f - 0.05 - r - 0.05) * q / 24; if (z < C.bf + 0.01 && z > C.br - 0.01) continue; const Section s = SectionAt(D, z); path.push_back({ sx, TopY(D, s, sx) + 0.003, z }); }
			std::vector<Pt3> pieces[2];
			for (const Pt3& p : path) { if (p[2] > C.bf) pieces[0].push_back(p); if (p[2] < C.br) pieces[1].push_back(p); }
			for (auto& pc : pieces) if (pc.size() > 1) {
				std::vector<std::vector<Pt3>> rows(2);
				for (const Pt3& p : pc) { rows[0].push_back({ p[0] - 0.07, p[1], p[2] }); rows[1].push_back({ p[0] + 0.07, p[1], p[2] }); }
				GridOpts o; o.ref = { 0, 0, 0 };
				EmitGrid(rows, [&](int, int) { return &trim; }, o);
			}
			std::vector<Pt3> rp;
			for (size_t i = 0; i < D.cabS.size(); i++) { const double z = D.cabS[i].z; if (z > C.tr && z < C.tf) rp.push_back({ sx, D.cabR[i].yc + C.crown * (1 - std::pow(sx / D.cabR[i].xr, 2)) + 0.004, z }); }
			if (rp.size() > 1) {
				std::vector<std::vector<Pt3>> rows(2);
				for (const Pt3& p : rp) { rows[0].push_back({ p[0] - 0.07, p[1], p[2] }); rows[1].push_back({ p[0] + 0.07, p[1], p[2] }); }
				GridOpts o; o.ref = { 0, 0, 0 };
				EmitGrid(rows, [&](int, int) { return &trim; }, o);
			}
		}
	}
	if (X.wing) {
		const double z = r + 0.2, y = D.line(z) + 0.28, span = W / 2 - 0.08;
		trim.hex(0x151515);
		const Pt2 foil[6] = { { -0.18, 0 }, { -0.1, 0.03 }, { 0.1, 0.02 }, { 0.2, 0 }, { 0.1, -0.01 }, { -0.1, -0.012 } };
		std::vector<std::vector<Pt3>> rows;
		for (int q = 0; q <= 8; q++) {
			const double x = -span + 2 * span * q / 8;
			std::vector<Pt3> row; for (const Pt2& fo : foil) row.push_back({ x, y + fo[1], z + fo[0] }); row.push_back(row[0]); rows.push_back(row);
		}
		GridOpts o; o.ref = { 0, y, z };
		EmitGrid(rows, [&](int, int) { return &trim; }, o);
		for (int sd : { 1, -1 }) trim.box(sd * 0.5 - 0.02, D.line(z), z - 0.08, sd * 0.5 + 0.02, y, z + 0.05);
	}
	if (X.lip) { trim.hex(0x151515); const double z = r + 0.06, y = D.line(z) + 0.01; trim.box(-W / 2 + 0.2, y, z - 0.02, W / 2 - 0.2, y + 0.04, z + 0.08); }
	if (X.intakes) {
		for (int sd : { 1, -1 }) {
			const double zc = zr + aR + 0.3;
			std::vector<std::vector<Pt3>> rows;
			for (int i = 0; i <= 4; i++) {
				const double z = zc - 0.25 + 0.5 * i / 4; const Section ss = SectionAt(D, z);
				std::vector<Pt3> row;
				for (int j = 0; j <= 3; j++) { const double y = c + 0.25 + 0.2 * j / 3 + (z - zc) * 0.3; row.push_back({ (SideX(D, ss, y) + 0.004) * sd, y, z }); }
				rows.push_back(row);
			}
			GridOpts o; o.ref = { 0, 0.5, zc };
			EmitGrid(rows, [&](int, int) { return dark; }, o);
		}
	}
	if (X.louvres) {
		dark->color(0.02, 0.02, 0.02);
		for (int k = 0; k < 6; k++) { const double z = r + 0.35 + k * 0.1; const Section s = SectionAt(D, z); const double y = TopY(D, s, 0); dark->box(-0.4, y - 0.01, z - 0.02, 0.4, y + 0.012, z + 0.02); }
		darkReset();
	}
	if (X.sideskirt) {
		dark->color(0.03, 0.03, 0.03);
		for (int sd : { 1, -1 }) { const Section s = SectionAt(D, 0); const double x = SideX(D, s, c + 0.02) * sd; dark->box(Min(x, x + sd * 0.05), c - 0.03, zr + aR + 0.05, Max(x, x + sd * 0.05), c + 0.06, zf - aR - 0.05); }
		darkReset();
	}
	if (X.rails) {
		chrome.hex(0x8a8a8a);
		for (int sd : { 1, -1 }) {
			std::vector<Pt3> path;
			for (size_t i = 0; i < D.cabS.size(); i++) { const double z = D.cabS[i].z; if (z < C.tr + 0.05 || z > C.tf - 0.1) continue; path.push_back({ sd * (D.cabR[i].xr - 0.1), D.cabR[i].yc + 0.07, z }); }
			if (path.size() > 2) Sweep(chrome, path, { { -0.02, -0.02 }, { 0.02, -0.02 }, { 0.02, 0.02 }, { -0.02, 0.02 } });
			for (double z : { C.tr + 0.1, C.tf - 0.15 }) {
				const double wz = SectionAt(D, z).w - C.inset - 0.1;
				chrome.box(sd * wz - 0.02, C.top, z - 0.03, sd * wz + 0.02, C.top + 0.07, z + 0.03);
			}
		}
	}
	if (X.steps) {
		dark->color(0.06, 0.06, 0.06);
		for (int sd : { 1, -1 }) { const Section s = SectionAt(D, 0); const double x = SideX(D, s, c) * sd; dark->box(Min(x - sd * 0.02, x + sd * 0.12), c - 0.06, zr + aR + 0.08, Max(x - sd * 0.02, x + sd * 0.12), c - 0.02, zf - aR - 0.08); }
		darkReset();
	}
	if (X.pushbar) {
		trim.hex(0x252525);
		trim.box(-0.62, c + 0.08, BF.zFace, -0.52, c + 0.62, BF.zFace + 0.14); trim.box(0.52, c + 0.08, BF.zFace, 0.62, c + 0.62, BF.zFace + 0.14);
		trim.box(-0.62, c + 0.5, BF.zFace + 0.1, 0.62, c + 0.58, BF.zFace + 0.16); trim.box(-0.62, c + 0.22, BF.zFace + 0.1, 0.62, c + 0.3, BF.zFace + 0.16);
	}
	if (X.mudflaps) { dark->color(0.03, 0.03, 0.03); for (int sd : { 1, -1 }) dark->box(sd * D.track / 2 - 0.18, c - 0.1, zr - aR - 0.03, sd * D.track / 2 + 0.18, c + 0.3, zr - aR - 0.01); darkReset(); }
	if (X.stacks) { chrome.hex(0xcccccc); for (int sd : { 1, -1 }) chrome.geo(Geo::Cylinder(0.06, 0.06, 1.3, 10), M4(sd * (W / 2 - 0.05), C.top - 0.35, C.br - 0.1)); }
	if (X.spare) { dark->color(0.05, 0.05, 0.05).geo(Geo::Cylinder(D.R, D.R, 0.24, 18), M4(0, D.line(r) - 0.1, r - 0.14, kPi / 2)); darkReset(); }
	if (X.trimLine) {
		chrome.hex(0xe0e0e0);
		for (int sd : { 1, -1 }) {
			std::vector<Pt3> path;
			for (int q = 0; q <= 20; q++) { const double z = r + 0.2 + (f - 0.4 - r) * q / 20; const Section s = SectionAt(D, z); const double y = s.yB + 0.05; path.push_back({ (SideX(D, s, y) + 0.004) * sd, y, z }); }
			Sweep(chrome, path, { { 0, -0.012 }, { 0.012, 0 }, { 0, 0.012 }, { -0.004, 0 } });
		}
	}
	chrome.hex(0xb0b0b0);
	{
		std::vector<double> xs = X.twinExhaust ? std::vector<double>{ -W * 0.28, W * 0.28 } : std::vector<double>{ -W * 0.3 };
		for (double x : xs) chrome.geo(Geo::Cylinder(0.045, 0.05, 0.22, 10, 1, true), M4(x, c + 0.06, r - 0.02, kPi / 2));
	}

	// ---------------- interior: seats, dash, wheel, console, rear bench
	const double seatY = Clamp(C.top - 1.02, c + 0.08, Max(c + 0.25, D.line(C.bf) - 0.01 - 0.45));
	const double seatZ = (C.bf + C.br) / 2 + 0.25;
	{
		trim.hex(0x2a2622);
		for (double sx : { 0.38, -0.38 }) {
			RoundBox(trim, sx - 0.27, sx + 0.27, seatY, seatY + 0.16, seatZ - 0.36, seatZ + 0.16, 0.05, 2);
			trim.geo(Geo::Box(0.52, 0.64, 0.14), M4(sx, seatY + 0.48, seatZ - 0.4, -0.18));
			trim.geo(Geo::Box(0.28, 0.2, 0.1), M4(sx, seatY + 0.9, seatZ - 0.48, -0.18));
		}
		if (D.doors == 4 || def.body == "suv" || def.police || def.taxi) {
			const double rz = seatZ - 0.95;
			if (rz > C.br + 0.2) { RoundBox(trim, -0.66, 0.66, seatY - 0.02, seatY + 0.14, rz - 0.3, rz + 0.18, 0.05, 2); trim.geo(Geo::Box(1.3, 0.58, 0.14), M4(0, seatY + 0.42, rz - 0.36, -0.16)); }
		}
		const double dy = D.line(C.bf) - 0.04, tw = D.tubXw;
		trim.hex(0x1a1a1a);
		RoundBox(trim, -tw + 0.02, tw - 0.02, dy - 0.26, dy, C.bf - 0.5, C.bf - 0.05, 0.08, 2);
		trim.geo(Geo::Cylinder(0.12, 0.12, 0.2, 12, 1, false, 0, kPi), M4(0.38, dy, C.bf - 0.45, kPi / 2, 0, kPi / 2));
		trim.box(-0.1, c + 0.1, seatZ - 0.3, 0.1, seatY + 0.2, C.bf - 0.45);
		trim.hex(0x111111).geo(Geo::Torus(0.17, 0.022, 6, 18), M4(0.38, dy - 0.05, C.bf - 0.58, -0.35, 0, 0));
		trim.geo(Geo::Cylinder(0.02, 0.02, 0.3, 6), M4(0.38, dy - 0.08, C.bf - 0.46, kPi / 2 - 0.35));
	}
	T.cabMid = (C.tf + C.tr) / 2;
	// the dark plastics merge into the trim mesh
	trim.m.Append(dark->m);
	dark->m = MeshBuf();
	T.seatY = seatY; T.seatZ = seatZ;
	const double hingeZ = D.door.z0;
	const Section hs = SectionAt(D, hingeZ);
	const double hingeY = (c + D.line(hingeZ)) / 2, hingeX = SideX(D, hs, hs.yB);
	T.hinge = { hingeX, hingeY, hingeZ };
	for (const char* k : { "door", "doorGlass" }) P[k].m.Translate(-hingeX, -hingeY, -hingeZ);
	return T;
}

MeshBuf WheelGeometry(const VehicleDef& def) {
	const double R = def.wheelR;
	Part w;
	const double tireW = def.body == "truck" ? 0.32 : def.body == "super" ? 0.27 : 0.24;
	{
		std::vector<std::array<double, 2>> pts;
		const double hwT = tireW / 2, rIn = R * 0.64;
		pts.push_back({ rIn, -hwT + 0.01 });
		for (int i = 0; i <= 6; i++) { const double a = -kPi / 2 + (i / 6.0) * kPi / 2; pts.push_back({ R - 0.05 + std::cos(a) * 0.05, -hwT + 0.05 + std::sin(a) * 0.05 }); }
		for (int i = 0; i <= 6; i++) { const double a = (i / 6.0) * kPi / 2; pts.push_back({ R - 0.05 + std::cos(a) * 0.05, hwT - 0.05 + std::sin(a) * 0.05 }); }
		pts.push_back({ rIn, hwT - 0.01 });
		w.color(0.055, 0.055, 0.058);
		w.geo(Geo::Lathe(pts, 28), M4(0, 0, 0, 0, 0, kPi / 2));
		w.color(0.02, 0.02, 0.02);
		for (double gg : { -0.3, 0.0, 0.3 }) w.geo(Geo::Torus(R + 0.001, 0.008, 3, 28), M4(gg * tireW, 0, 0, 0, kPi / 2, 0));
		if (def.body == "lowrider") { w.color(0.92, 0.92, 0.9); w.geo(Geo::Ring(R * 0.7, R * 0.86, 28), M4(tireW / 2 + 0.002, 0, 0, 0, kPi / 2, 0)); }
	}
	const std::string style = def.body == "lowrider" ? "wire" : def.body == "super" ? "mesh" : def.body == "coupe" ? "split" : (def.body == "truck" || def.body == "van") ? "steel" : def.military ? "steel" : "alloy";
	const double rimCol[3] = { style == "wire" ? 0.9 : (style == "mesh" || style == "split") ? 0.22 : style == "steel" ? 0.35 : 0.62,
		style == "wire" ? 0.9 : (style == "mesh" || style == "split") ? 0.22 : style == "steel" ? 0.36 : 0.62,
		style == "wire" ? 0.9 : (style == "mesh" || style == "split") ? 0.24 : style == "steel" ? 0.38 : 0.64 };
	auto rim = [&](double k) { w.color(rimCol[0] * k, rimCol[1] * k, rimCol[2] * k); };
	w.color(0.32, 0.32, 0.33);
	w.geo(Geo::Cylinder(R * 0.55, R * 0.55, 0.02, 18), M4(tireW * 0.1, 0, 0, 0, 0, kPi / 2));
	if (style == "mesh" || style == "split") { w.color(0.8, 0.1, 0.08); w.geo(Geo::Box(0.06, 0.12, 0.08), M4(tireW * 0.18, R * 0.38, R * 0.12)); }
	rim(1);
	w.geo(Geo::Torus(R * 0.645, 0.018, 6, 24), M4(tireW / 2 - 0.005, 0, 0, 0, kPi / 2, 0));
	w.geo(Geo::Cylinder(R * 0.64, R * 0.64, 0.02, 20, 1, true), M4(tireW / 2 - 0.02, 0, 0, 0, 0, kPi / 2));
	rim(0.35);
	w.geo(Geo::Cylinder(R * 0.62, R * 0.62, 0.01, 20), M4(tireW / 2 - (style == "steel" ? 0.02 : 0.05), 0, 0, 0, 0, kPi / 2));
	rim(1);
	auto spoke = [&](double a, double sw, double len, double depth, double r0) { w.geo(Geo::Box(depth, len, sw), RotX(a) * M4(tireW / 2 - 0.02, r0 + len / 2, 0, 0, 0, 0.2)); };
	if (style == "wire") { for (int k = 0; k < 36; k++) { const double a = k / 36.0 * kTau; w.geo(Geo::Box(0.006, R * 0.5, 0.006), RotX(a) * M4(tireW / 2 - 0.025 + (k % 2) * 0.02, R * 0.36, 0, 0.35 * (k % 2 ? 1 : -1), 0, 0)); } }
	else if (style == "mesh") { for (int k = 0; k < 10; k++) spoke(k / 10.0 * kTau, 0.028, R * 0.5, 0.03, R * 0.1); }
	else if (style == "split") { for (int k = 0; k < 5; k++) { const double a = k / 5.0 * kTau; spoke(a - 0.1, 0.025, R * 0.5, 0.04, R * 0.1); spoke(a + 0.1, 0.025, R * 0.5, 0.04, R * 0.1); } }
	else if (style == "steel") {
		w.geo(Geo::Cylinder(R * 0.5, R * 0.56, 0.04, 20), M4(tireW / 2 - 0.03, 0, 0, 0, 0, kPi / 2));
		w.color(0.12, 0.12, 0.13);
		for (int k = 0; k < 8; k++) { const double a = k / 8.0 * kTau; w.geo(Geo::Cylinder(0.035, 0.035, 0.05, 8), M4(tireW / 2 - 0.01, std::cos(a) * R * 0.38, std::sin(a) * R * 0.38, 0, 0, kPi / 2)); }
		rim(1);
	} else { const int n = def.body == "suv" || def.body == "pickup" ? 6 : 5; for (int k = 0; k < n; k++) spoke((double)k / n * kTau, 0.06, R * 0.5, 0.035, R * 0.1); }
	w.geo(Geo::Cylinder(R * 0.17, R * 0.2, 0.05, 14), M4(tireW / 2 - 0.01, 0, 0, 0, 0, kPi / 2));
	rim(0.55);
	for (int k = 0; k < 5; k++) { const double a = k / 5.0 * kTau + 0.3; w.geo(Geo::Cylinder(0.011, 0.011, 0.02, 6), M4(tireW / 2 + 0.016, std::cos(a) * R * 0.1, std::sin(a) * R * 0.1, 0, 0, kPi / 2)); }
	return std::move(w.m);
}

VehicleModel Build(const VehicleDef& def) {
	VehicleModel M;
	Template T = CarTemplate(def);
	const Design& D = T.D;
	const Cab& C = D.cab;
	const double L = def.L, W = def.W, c = def.clearance, R = def.wheelR, wb = def.wheelbase;
	auto add = [&](const char* name, MeshBuf&& m, EVMat mat, const char* pivot = "", uint32_t color = 0) {
		if (m.Empty()) return;
		VPart p; p.name = name; p.mesh = std::move(m); p.mat = mat; p.pivot = pivot; p.color = color;
		M.parts.push_back(std::move(p));
	};
	M.bumperBody = D.bumper == "body";
	const EVMat bumperMat = D.bumper == "body" ? EVMat::Paint : D.bumper == "chrome" ? EVMat::Chrome : EVMat::Trim;
	add("body", std::move(T.P["body"].m), EVMat::Paint);
	// bonnet and boot lid hang on hinges at the windscreen / rear window (they spring open in a crash)
	if (!T.P["hood"].empty()) { const double hz = C.bf + 0.03, hy = D.line(hz); M.hasHood = true; M.hoodHinge = { 0, hy, hz }; T.P["hood"].m.Translate(0, -hy, -hz); add("hood", std::move(T.P["hood"].m), EVMat::Paint, "hood"); }
	if (!T.P["trunk"].empty()) { const double hz = C.br - 0.03, hy = D.line(hz); M.hasTrunk = true; M.trunkHinge = { 0, hy, hz }; T.P["trunk"].m.Translate(0, -hy, -hz); add("trunk", std::move(T.P["trunk"].m), EVMat::Paint, "trunk"); }
	add("door2", std::move(T.P["door2"].m), EVMat::Paint);
	add("bumperF", std::move(T.P["bumperF"].m), bumperMat);
	add("bumperR", std::move(T.P["bumperR"].m), bumperMat);
	add("glass", std::move(T.P["glass"].m), EVMat::Glass);
	add("trim", std::move(T.P["trim"].m), EVMat::Trim);
	add("chrome", std::move(T.P["chrome"].m), EVMat::Chrome);
	add("head", std::move(T.P["head"].m), EVMat::Head);
	add("tail", std::move(T.P["tail"].m), EVMat::Tail);

	// ---------------- police light bar / taxi sign
	const double roofY = C.top + C.crown;
	if (def.police) {
		const double z = T.cabMid + 0.1;
		Part base; base.color(1, 1, 1); base.geo(Geo::Box(1.36, 0.06, 0.34), M4(0, roofY + 0.02, z));
		add("lightbarBase", std::move(base.m), EVMat::Trim);
		auto mk = [&](const char* name, uint32_t col, double x0, double x1) {
			Part p; p.color(1, 1, 1);
			// (cylinder along x, flattened)
			p.geo(Geo::Cylinder(0.1, 0.1, x1 - x0, 12), M4((x0 + x1) / 2, roofY + 0.1, z, 0, 0, kPi / 2, 1, 1, 1) * Mat4::Compose(0, 0, 0, 0, 0, 0, 1, 1, 1));
			// three.js: g.rotateZ(PI/2) then g.scale(1, 0.75, 1.3): scale the rotated geometry
			MeshBuf& g = p.m;
			const double cx = (x0 + x1) / 2, cy = roofY + 0.1;
			for (size_t i = 0; i < g.Count(); i++) {
				g.P[i * 3 + 1] = (float)(cy + (g.P[i * 3 + 1] - cy) * 0.75);
				g.P[i * 3 + 2] = (float)(z + (g.P[i * 3 + 2] - z) * 1.3);
			}
			(void)cx;
			add(name, std::move(p.m), EVMat::Lightbar, "", col);
		};
		mk("lightRed", 0xff1a1a, 0.03, 0.64);
		mk("lightBlue", 0x1a4dff, -0.64, -0.03);
		M.lightbar = true;
	}
	if (def.taxi) {
		Part sign; sign.color(1, 1, 1); sign.geo(Geo::Box(0.7, 0.2, 0.24), M4(0, roofY + 0.11, T.cabMid));
		add("taxiSign", std::move(sign.m), EVMat::TaxiSign, "", 0xffe066);
		Part cg;
		for (int sd : { 1, -1 }) for (int k = 0; k < 22; k++) {
			const double z = D.door.z0 - 0.02 - k * 0.1, y0 = c + 0.36;
			if (z < C.br - 0.2) break;
			for (int row : { 0, 1 }) {
				if ((k + row) % 2) continue;
				const Section s = SectionAt(D, z);
				const double y = y0 + row * 0.08, x = (SideX(D, s, y + 0.04) + 0.004) * sd;
				cg.color(0.05, 0.05, 0.05);
				cg.m.Color(0.05, 0.05, 0.05);
				cg.m.Box(Min(x, x + sd * 0.004), y, z - 0.1, Max(x, x + sd * 0.004), y + 0.08, z, false);
			}
		}
		add("taxiChequer", std::move(cg.m), EVMat::Trim);
	}

	// ---------------- armoured vans: a livery band, a roof beacon, push bar and twin rear doors
	if (def.armored) {
		MeshBuf band;
		const double bandCol[3] = { def.police ? 0.85 : 0.08, def.police ? 0.85 : 0.22, def.police ? 0.8 : 0.5 };
		const double y0 = c + 0.42, y1 = c + 0.62;
		for (int sd : { 1, -1 }) for (double z = D.door.z0 - 0.05; z > D.r + 0.25; z -= 0.25) {
			const Section s = SectionAt(D, z);
			const double x = (SideX(D, s, (y0 + y1) / 2) + 0.006) * sd;
			band.Color(bandCol[0], bandCol[1], bandCol[2]);
			band.Box(Min(x, x + sd * 0.006), y0, z - 0.25, Max(x, x + sd * 0.006), y1, z, false);
		}
		band.Color(0.06, 0.06, 0.06);
		band.Box(-W * 0.36, c + 0.05, L / 2 - 0.02, W * 0.36, c + 0.12, L / 2 + 0.12);
		for (double x : { -0.42, 0.42 }) band.Box(x - 0.04, c + 0.05, L / 2 + 0.02, x + 0.04, c + 0.62, L / 2 + 0.12);
		add("armorBand", std::move(band), EVMat::Trim);
		if (!def.police) {
			Part bm; bm.color(1, 1, 1); bm.geo(Geo::Cylinder(0.11, 0.11, 0.14, 12), M4(0, roofY + 0.07, C.tf - 0.25));
			add("beacon", std::move(bm.m), EVMat::Beacon, "", 0xffa31a);
		}
		const double zb = D.r - 0.004, dh = def.H - 0.42 - c, dw = W / 2 - 0.14;
		MeshBuf hold;
		hold.Color(0.03, 0.03, 0.035);
		hold.Box(-W / 2 + 0.12, c + 0.3, zb - 0.0035, W / 2 - 0.12, c + 0.3 + dh, zb - 0.002, false);
		if (!def.police) {
			hold.Color(0.2, 0.45, 0.16);
			const double bags[5][2] = { { -0.45, 0.25 }, { 0.05, 0.22 }, { 0.5, 0.27 }, { -0.25, 0.95 }, { 0.35, 0.92 } };
			for (const auto& b : bags) hold.Box(b[0] - 0.17, c + 0.3 + b[1] - 0.12, zb - 0.0075, b[0] + 0.17, c + 0.3 + b[1] + 0.12, zb - 0.004);
			hold.Color(0.12, 0.12, 0.13);
			hold.Box(-W / 2 + 0.12, c + 0.3 + 0.66, zb - 0.0045, W / 2 - 0.12, c + 0.3 + 0.7, zb - 0.0036);
		}
		add("hold", std::move(hold), EVMat::Trim);
		int idx = 0;
		for (int sd : { 1, -1 }) {
			const std::string pv = idx == 0 ? "rear0" : "rear1";
			MeshBuf g; g.Color(1, 1, 1);
			g.Box(sd > 0 ? -dw : 0, 0, -0.03, sd > 0 ? 0 : dw, dh, 0.0, true, true, true);
			add(idx == 0 ? "rearDoor0" : "rearDoor1", std::move(g), EVMat::Paint, pv.c_str());
			MeshBuf hg; hg.Color(0.05, 0.05, 0.05);
			const double hx = sd > 0 ? -dw + 0.08 : dw - 0.08;
			hg.Box(hx - 0.03, dh * 0.45, -0.06, hx + 0.03, dh * 0.6, -0.03);
			hg.Box(sd > 0 ? -dw : dw - 0.012, 0, -0.035, sd > 0 ? -dw + 0.012 : dw, dh, -0.03);
			add(idx == 0 ? "rearHandle0" : "rearHandle1", std::move(hg), EVMat::Trim, pv.c_str());
			M.rearDoors.push_back({ { sd * (W / 2 - 0.1), c + 0.3, zb }, sd });
			idx++;
		}
	}

	// ---------------- driver door (left side, +x), hinged at the front
	M.doorHinge = T.hinge;
	add("door", std::move(T.P["door"].m), EVMat::Paint, "door");
	add("doorGlass", std::move(T.P["doorGlass"].m), EVMat::Glass, "door");

	// ---------------- wheels
	M.wheel = WheelGeometry(def);
	const double zf = wb / 2, zr = -wb / 2;
	M.wheels = { { def.track / 2, R, zf, true }, { -def.track / 2, R, zf, true }, { def.track / 2, R, zr, false }, { -def.track / 2, R, zr, false } };
	M.beamZ = L / 2 + 5.5;
	const Pt3 seatBase = { 0.38, T.seatY + 0.05 - 0.02, T.seatZ - 0.1 };
	const double rearZ = Clamp(seatBase[2] - 0.9, C.tr + 0.14, seatBase[2] - 0.55);
	M.seats = { seatBase, { -0.38, seatBase[1], seatBase[2] }, { 0.38, seatBase[1] - 0.04, rearZ }, { -0.38, seatBase[1] - 0.04, rearZ } };
	M.doorPos = { W / 2 + 0.55, 0, (D.door.z0 + D.door.z1) / 2 - 0.15 };
	M.hull.roofX = SectionAt(D, (C.tf + C.tr) / 2).w - C.inset;
	M.hull.roofY = D.hasCargo ? Max(C.top, D.cargo.y1) : C.top;
	M.hull.roofZ0 = D.hasCargo ? D.cargo.z0 : C.tr;
	M.hull.roofZ1 = C.tf;
	M.hull.beltY = D.line(0);
	M.hull.cgH = Max(0.45, (c + C.top) * 0.42);
	return M;
}
} // namespace

const VehicleModel& BuildVehicleModel(const VehicleDef& def) {
	static std::mutex Lock;
	static std::map<std::string, std::unique_ptr<VehicleModel>> Cache;
	std::lock_guard<std::mutex> G(Lock);
	auto it = Cache.find(def.id);
	if (it != Cache.end()) return *it->second;
	auto m = std::make_unique<VehicleModel>(Build(def));
	const VehicleModel& ref = *m;
	Cache[def.id] = std::move(m);
	return ref;
}

} // namespace atg
