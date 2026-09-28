// See Models.h. The car bodies are lofted along their length: a lower body whose floor rises over the
// wheels (the wheel arches), a greenhouse of glass with a painted roof and pillars, bumpers, lights and
// per-body extras (pickup bed, van / truck box, police light bar, taxi sign).
#include "Models.h"

namespace atg {

const std::vector<CarDef>& CarDefs() {
	static const std::vector<CarDef> D = [] {
		std::vector<CarDef> v;
		auto add = [&](CarDef d) { v.push_back(d); };
		// id, name, body, L, W, H, wheelbase, track, wheelR, clearance, mass, force, top, grip, drive, steer, brake, rarity, camDist, camHeight, colours
		add({ "meridian", "Meridian", "sedan", 4.8, 1.86, 1.45, 2.8, 1.58, 0.34, 0.26, 1450, 8200, 47, 1.0, EDrive::RWD, 0.62, 15000, 10, 7.5, 1.6, { 0x8c1c13, 0x1d3557, 0xe8e8e8, 0x2b2b2b, 0x6c757d, 0x3a5a40, 0xbc6c25, 0x5e548e } });
		add({ "kestrel", "Kestrel GT", "coupe", 4.5, 1.88, 1.3, 2.62, 1.6, 0.34, 0.2, 1350, 12500, 60, 1.12, EDrive::RWD, 0.6, 18000, 4, 7.5, 1.6, { 0xd00000, 0xffba08, 0x0077b6, 0x111111, 0xf1faee, 0x2dc653 } });
		add({ "brawler", "Brawler", "muscle", 5.0, 1.95, 1.35, 2.95, 1.62, 0.36, 0.24, 1600, 13000, 55, 0.9, EDrive::RWD, 0.6, 15000, 5, 7.5, 1.6, { 0x111111, 0xf77f00, 0x9d0208, 0x3a86ff, 0xffffff, 0x606c38 } });
		add({ "summit", "Summit", "suv", 4.9, 2.0, 1.85, 2.9, 1.7, 0.4, 0.34, 2150, 11500, 44, 0.95, EDrive::AWD, 0.58, 17000, 7, 8.2, 1.9, { 0x222222, 0xe5e5e5, 0x283618, 0x14213d, 0x7f5539, 0x6d6875 } });
		add({ "hauler", "Hauler", "pickup", 5.3, 2.0, 1.8, 3.2, 1.7, 0.4, 0.34, 2100, 11000, 42, 0.92, EDrive::RWD, 0.56, 16000, 6, 8.5, 1.9, { 0x9b2226, 0x005f73, 0xe9d8a6, 0x3d405b, 0xffffff, 0x495057 } });
		add({ "parcel", "Parcel Van", "van", 5.1, 2.0, 2.2, 3.1, 1.72, 0.38, 0.3, 2400, 9500, 37, 0.88, EDrive::RWD, 0.55, 15000, 4, 9, 2.3, { 0xffffff, 0xd9d9d9, 0x8d99ae, 0x6a994e, 0xbc4749 } });
		{ CarDef d{ "taxi", "Cab", "sedan", 4.8, 1.86, 1.45, 2.8, 1.58, 0.34, 0.26, 1450, 8600, 47, 1.0, EDrive::RWD, 0.62, 15000, 3, 7.5, 1.6, { 0xf4c20d } }; d.taxi = true; add(d); }
		{ CarDef d{ "police", "Police Cruiser", "sedan", 4.95, 1.9, 1.5, 2.9, 1.6, 0.35, 0.26, 1650, 12500, 56, 1.08, EDrive::RWD, 0.6, 18000, 0, 7.5, 1.6, { 0x111111 } }; d.police = true; add(d); }
		add({ "zenith", "Zenith", "super", 4.6, 2.0, 1.15, 2.7, 1.7, 0.35, 0.14, 1400, 17000, 72, 1.25, EDrive::AWD, 0.58, 22000, 1, 7.5, 1.6, { 0xffd60a, 0xe63946, 0x00b4d8, 0xffffff, 0x111111, 0x80ed99 } });
		add({ "pico", "Pico", "hatch", 4.0, 1.76, 1.5, 2.5, 1.5, 0.31, 0.2, 1100, 6400, 43, 1.02, EDrive::FWD, 0.66, 12500, 7, 7.5, 1.6, { 0xe63946, 0x2a9d8f, 0xf4a261, 0xe9ecef, 0x457b9d, 0x6a4c93, 0x8ac926 } });
		{ CarDef d{ "bouncer", "Bouncer", "lowrider", 5.3, 1.96, 1.35, 3.05, 1.6, 0.33, 0.16, 1750, 9000, 46, 0.95, EDrive::RWD, 0.6, 14000, 3, 7.5, 1.6, { 0x5a189a, 0x2a9d8f, 0xe76f51, 0x9d0208, 0x264653, 0xffb703 } }; d.hydraulics = true; add(d); }
		add({ "boxer", "Boxer Truck", "truck", 7.4, 2.35, 3.2, 4.4, 1.95, 0.48, 0.38, 5200, 20000, 32, 0.85, EDrive::RWD, 0.5, 30000, 2, 12, 3.2, { 0xffffff, 0x1d3557, 0x9b2226 } });
		{ CarDef d{ "ranger", "Ranger", "suv", 4.7, 2.05, 1.95, 2.85, 1.75, 0.44, 0.42, 2300, 13000, 42, 1.05, EDrive::AWD, 0.6, 18000, 0, 8.4, 2.0, { 0x4f5a36 } }; d.military = true; add(d); }
		return v;
	}();
	return D;
}
const CarDef* FindCar(const std::string& id) { for (const CarDef& d : CarDefs()) if (id == d.id) return &d; return nullptr; }

namespace {
struct Shape {
	// fractions of the length (z = -L/2 at the back .. +L/2 at the front)
	double roofR, roofF, wsBase, rearBase;
	double beltK;   // belt line height as a fraction between clearance and H
	double roofInset, hoodDrop;
	bool pickup = false, van = false, truck = false;
};
Shape ShapeOf(const std::string& body) {
	if (body == "coupe") return { -0.2, 0.02, 0.26, -0.4, 0.55, 0.26, 0.12 };
	if (body == "muscle") return { -0.2, 0.03, 0.24, -0.36, 0.56, 0.24, 0.08 };
	if (body == "super") return { -0.16, 0.06, 0.3, -0.38, 0.52, 0.28, 0.14 };
	if (body == "hatch") return { -0.38, 0.1, 0.3, -0.47, 0.5, 0.2, 0.1 };
	if (body == "suv") return { -0.45, 0.12, 0.3, -0.48, 0.52, 0.16, 0.06 };
	if (body == "pickup") { Shape s{ -0.1, 0.14, 0.3, -0.1, 0.55, 0.16, 0.06 }; s.pickup = true; return s; }
	if (body == "van") { Shape s{ -0.48, 0.3, 0.42, -0.49, 0.5, 0.1, 0.04 }; s.van = true; return s; }
	if (body == "truck") { Shape s{ 0.25, 0.43, 0.49, 0.25, 0.4, 0.12, 0.02 }; s.truck = true; return s; }
	if (body == "lowrider") return { -0.22, 0.06, 0.28, -0.38, 0.56, 0.22, 0.08 };
	return { -0.22, 0.08, 0.28, -0.36, 0.55, 0.22, 0.1 }; // sedan
}

void Tri3(MeshBuf& g, const double a[3], const double b[3], const double c[3]) {
	const double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
	double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
	const double l = Hypot3(nx, ny, nz); if (l > 0) { nx /= l; ny /= l; nz /= l; }
	const uint32_t i0 = g.V(a[0], a[1], a[2], nx, ny, nz), i1 = g.V(b[0], b[1], b[2], nx, ny, nz), i2 = g.V(c[0], c[1], c[2], nx, ny, nz);
	g.Tri(i0, i1, i2);
}
// a flat quad whose front is where its corners run counter-clockwise; out = which way it should face
void Quad3(MeshBuf& g, const double a[3], const double b[3], const double c[3], const double d[3], const double out[3]) {
	const double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
	const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
	if (nx * out[0] + ny * out[1] + nz * out[2] >= 0) { Tri3(g, a, b, c); Tri3(g, a, c, d); } else { Tri3(g, a, c, b); Tri3(g, a, d, c); }
}
} // namespace

CarModel BuildCarModel(const CarDef& d) {
	CarModel m;
	const Shape S = ShapeOf(d.body);
	const double L = d.L, W = d.W, H = d.H, c = d.clearance;
	const double zF = L / 2, zR = -L / 2;
	const double belt = c + (H - c) * S.beltK;
	const double wHalf = W / 2, rc = Min(0.4, W * 0.22);
	const double archR = d.wheelR + 0.1, archTop = d.wheelR * 2 + 0.06;
	const double wz[2] = { d.wheelbase / 2, -d.wheelbase / 2 };
	// stations along the body (denser round the wheel arches and the ends)
	std::vector<double> zs;
	for (int i = 0; i <= 40; i++) zs.push_back(zR + L * i / 40.0);
	for (double w0 : wz) for (int k = -8; k <= 8; k++) zs.push_back(w0 + archR * k / 8.0);
	std::sort(zs.begin(), zs.end());
	zs.erase(std::unique(zs.begin(), zs.end(), [](double a, double b) { return std::fabs(a - b) < 1e-3; }), zs.end());
	auto halfW = [&](double z) {
		const double e = Min(z - zR, zF - z);
		if (e >= rc) return wHalf;
		const double dz = rc - e;
		return wHalf - rc + std::sqrt(Max(0, rc * rc - dz * dz));
	};
	auto floorY = [&](double z) {
		double y = c;
		for (double w0 : wz) { const double dz = std::fabs(z - w0); if (dz < archR) y = Max(y, c + (archTop - c) * std::sqrt(1 - (dz / archR) * (dz / archR))); }
		return y;
	};
	const double zWs = zR + L * (0.5 + S.wsBase), zRear = zR + L * (0.5 + S.rearBase);
	auto topY = [&](double z) {
		double y = belt;
		if (z > zWs) y = belt - S.hoodDrop * Smooth(zWs, zF, z);          // hood sloping down
		if (z < zRear) y = belt - 0.05 * Smooth(zRear, zR, z);             // boot deck
		const double e = Min(z - zR, zF - z);
		if (e < 0.12) y -= (0.12 - e) * 0.8;                               // bumper roundover
		if (S.truck) y = belt;
		return y;
	};
	// ---- lower body (painted)
	MeshBuf& P = m.paint;
	P.Color(1, 1, 1); P.Rough(0.3);
	for (size_t i = 0; i + 1 < zs.size(); i++) {
		const double za = zs[i], zb = zs[i + 1];
		const double wa = halfW(za), wb = halfW(zb), fa = floorY(za), fb = floorY(zb), ta = topY(za), tb = topY(zb);
		for (int side : { -1, 1 }) {
			const double A[3] = { side * wa, fa, za }, B[3] = { side * wb, fb, zb }, C[3] = { side * wb, tb, zb }, D[3] = { side * wa, ta, za };
			const double o[3] = { (double)side, 0, 0 };
			Quad3(P, A, B, C, D, o);
			// inner arch lining down to the floor under the arches
			const double A2[3] = { side * wa, fa, za }, B2[3] = { side * wb, fb, zb }, C2[3] = { side * (wb - 0.12), fb, zb }, D2[3] = { side * (wa - 0.12), fa, za };
			const double down[3] = { 0, -1, 0 };
			Quad3(P, A2, B2, C2, D2, down);
		}
		const double T1[3] = { -wa, ta, za }, T2[3] = { wa, ta, za }, T3[3] = { wb, tb, zb }, T4[3] = { -wb, tb, zb };
		const double up[3] = { 0, 1, 0 };
		Quad3(P, T1, T2, T3, T4, up);
	}
	// end caps
	for (int end : { 0, 1 }) {
		const double z = end ? zF : zR, w = halfW(z), f = floorY(z), t = topY(z);
		const double A[3] = { -w, f, z }, B[3] = { w, f, z }, C[3] = { w, t, z }, D[3] = { -w, t, z };
		const double o[3] = { 0, 0, end ? 1.0 : -1.0 };
		Quad3(P, A, B, C, D, o);
	}
	// underside (dark)
	MeshBuf& T = m.trim;
	T.Color(0.04, 0.04, 0.045); T.Rough(0.8);
	{
		const double A[3] = { -wHalf + 0.12, c + 0.02, zR + 0.1 }, B[3] = { wHalf - 0.12, c + 0.02, zR + 0.1 }, C[3] = { wHalf - 0.12, c + 0.02, zF - 0.1 }, D[3] = { -wHalf + 0.12, c + 0.02, zF - 0.1 };
		const double down[3] = { 0, -1, 0 };
		Quad3(T, A, B, C, D, down);
	}
	// ---- greenhouse
	const double roofZR = zR + L * (0.5 + S.roofR), roofZF = zR + L * (0.5 + S.roofF);
	const double wBelt = wHalf - 0.06, wRoof = wHalf - S.roofInset;
	const double roofY = S.truck ? belt + (H - belt) * 0.82 : H;
	// glass
	T.Color(0.035, 0.045, 0.055); T.Rough(0.04);
	const double wsA[3] = { -wBelt, belt, zWs }, wsB[3] = { wBelt, belt, zWs }, wsC[3] = { wRoof, roofY, roofZF }, wsD[3] = { -wRoof, roofY, roofZF };
	{ const double o[3] = { 0, 0.5, 1 }; Quad3(T, wsA, wsB, wsC, wsD, o); }
	const double rwA[3] = { -wBelt, belt, zRear }, rwB[3] = { wBelt, belt, zRear }, rwC[3] = { wRoof, roofY, roofZR }, rwD[3] = { -wRoof, roofY, roofZR };
	{ const double o[3] = { 0, 0.5, -1 }; Quad3(T, rwA, rwB, rwC, rwD, o); }
	for (int side : { -1, 1 }) {
		const double A[3] = { side * wBelt, belt, zRear }, B[3] = { side * wBelt, belt, zWs }, C[3] = { side * wRoof, roofY, roofZF }, D[3] = { side * wRoof, roofY, roofZR };
		const double o[3] = { (double)side, 0.3, 0 };
		Quad3(T, A, B, C, D, o);
	}
	// roof and pillars (painted)
	{
		const double A[3] = { -wRoof, roofY, roofZR }, B[3] = { wRoof, roofY, roofZR }, C[3] = { wRoof, roofY, roofZF }, D[3] = { -wRoof, roofY, roofZF };
		const double up[3] = { 0, 1, 0 };
		Quad3(P, A, B, C, D, up);
		// thin roof edge so the roof reads as a panel
		for (int side : { -1, 1 }) {
			const double E1[3] = { side * wRoof, roofY - 0.05, roofZR }, E2[3] = { side * wRoof, roofY - 0.05, roofZF }, E3[3] = { side * wRoof, roofY, roofZF }, E4[3] = { side * wRoof, roofY, roofZR };
			const double o[3] = { (double)side, 0, 0 };
			Quad3(P, E1, E2, E3, E4, o);
		}
	}
	auto pillar = [&](double zb, double zt, double t) {
		for (int side : { -1, 1 }) {
			const double xb = side * (wBelt + 0.012), xt = side * (wRoof + 0.012);
			const double A[3] = { xb, belt, zb - t }, B[3] = { xb, belt, zb + t }, C[3] = { xt, roofY, zt + t }, D[3] = { xt, roofY, zt - t };
			const double o[3] = { (double)side, 0.2, 0 };
			Quad3(P, A, B, C, D, o);
		}
	};
	pillar(zWs, roofZF, 0.06);
	pillar(zRear, roofZR, S.pickup || S.van ? 0.08 : 0.14);
	if (roofZF - roofZR > 1.2) pillar((zWs + zRear) / 2 - 0.1, (roofZF + roofZR) / 2 - 0.05, 0.05);
	// ---- body-specific extras
	if (S.pickup) {
		const double bedY = belt, wallH = 0.42, z0 = zR + 0.08, z1 = roofZR - 0.12;
		P.Box(-wHalf, bedY, z0, -wHalf + 0.08, bedY + wallH, z1);
		P.Box(wHalf - 0.08, bedY, z0, wHalf, bedY + wallH, z1);
		P.Box(-wHalf, bedY, z0, wHalf, bedY + wallH, z0 + 0.08);
		T.Color(0.06, 0.06, 0.065); T.Rough(0.7);
		T.Box(-wHalf + 0.08, bedY + 0.01, z0 + 0.08, wHalf - 0.08, bedY + 0.02, z1);
	}
	if (S.truck) {
		// cargo box behind the cab
		T.Color(0.92, 0.92, 0.9); T.Rough(0.6);
		T.Box(-wHalf, belt, zR + 0.05, wHalf, H, roofZR - 0.25, true, true, false);
	}
	if (S.van) {
		// the back of the van is a tall painted box behind the cabin glass
		P.Box(-wBelt, belt, zR + 0.06, wBelt, roofY, zRear + 0.02);
	}
	// ---- bumpers, grille, lights, mirrors, plates
	T.Color(0.07, 0.07, 0.075); T.Rough(0.55);
	T.Box(-wHalf + 0.05, c + 0.05, zF - 0.02, wHalf - 0.05, c + 0.28, zF + 0.1);
	T.Box(-wHalf + 0.05, c + 0.05, zR - 0.1, wHalf - 0.05, c + 0.28, zR + 0.02);
	T.Color(0.03, 0.03, 0.035);
	T.Box(-0.42, c + 0.32, zF, 0.42, topY(zF) - 0.08, zF + 0.02);
	T.Color(0.95, 0.93, 0.85); T.Glow(3.5); T.Rough(0.1);
	for (int side : { -1, 1 }) T.Box(side * (wHalf - 0.34) - 0.16, topY(zF) - 0.2, zF - 0.01, side * (wHalf - 0.34) + 0.16, topY(zF) - 0.07, zF + 0.03);
	T.Color(0.7, 0.02, 0.02); T.Glow(1.2);
	for (int side : { -1, 1 }) T.Box(side * (wHalf - 0.24) - 0.14, topY(zR) - 0.22, zR - 0.03, side * (wHalf - 0.24) + 0.14, topY(zR) - 0.08, zR + 0.01);
	T.Glow(0);
	T.Color(0.85, 0.85, 0.8); T.Rough(0.5);
	T.Box(-0.26, c + 0.3, zR - 0.04, 0.26, c + 0.44, zR - 0.02);
	T.Color(0.07, 0.07, 0.075);
	for (int side : { -1, 1 }) T.Box(side * (wBelt + 0.02) - 0.08 + side * 0.08, belt + 0.02, zWs - 0.12, side * (wBelt + 0.02) + 0.08 + side * 0.08, belt + 0.14, zWs - 0.02);
	if (d.police) {
		T.Color(0.9, 0.9, 0.9); T.Rough(0.4);
		T.Box(-0.55, roofY, (roofZR + roofZF) / 2 - 0.12, 0.55, roofY + 0.05, (roofZR + roofZF) / 2 + 0.12);
		T.Color(0.9, 0.05, 0.05); T.Glow(2.5);
		T.Box(0.05, roofY + 0.05, (roofZR + roofZF) / 2 - 0.1, 0.53, roofY + 0.14, (roofZR + roofZF) / 2 + 0.1);
		T.Color(0.05, 0.15, 0.95);
		T.Box(-0.53, roofY + 0.05, (roofZR + roofZF) / 2 - 0.1, -0.05, roofY + 0.14, (roofZR + roofZF) / 2 + 0.1);
		T.Glow(0);
		// white doors on the black cruiser
		T.Color(0.9, 0.9, 0.88); T.Rough(0.3);
		for (int side : { -1, 1 }) {
			const double x = side * (wHalf + 0.004);
			const double A[3] = { x, c + 0.35, zRear + 0.2 }, B[3] = { x, c + 0.35, zWs - 0.1 }, C[3] = { x, belt - 0.05, zWs - 0.1 }, D[3] = { x, belt - 0.05, zRear + 0.2 };
			const double o[3] = { (double)side, 0, 0 };
			Quad3(T, A, B, C, D, o);
		}
	}
	if (d.taxi) {
		T.Color(0.98, 0.85, 0.2); T.Glow(1.5); T.Rough(0.4);
		T.Box(-0.3, roofY, (roofZR + roofZF) / 2 - 0.12, 0.3, roofY + 0.22, (roofZR + roofZF) / 2 + 0.12);
		T.Glow(0);
	}
	// ---- interior hint: seats and dash seen through the glass
	T.Color(0.1, 0.09, 0.085); T.Rough(0.8);
	const double seatY = belt - 0.42, seatZ = (zWs + roofZR) / 2 + 0.05;
	for (int side : { -1, 1 }) {
		T.Box(side * 0.38 - 0.24, seatY, seatZ - 0.3, side * 0.38 + 0.24, seatY + 0.14, seatZ + 0.2);
		T.Box(side * 0.38 - 0.24, seatY, seatZ - 0.42, side * 0.38 + 0.24, seatY + 0.72, seatZ - 0.28);
	}
	T.Box(-wBelt + 0.05, belt - 0.2, zWs - 0.35, wBelt - 0.05, belt - 0.02, zWs - 0.05);
	// ---- wheel (axle along x)
	{
		MeshBuf& Wh = m.wheel;
		const double r = d.wheelR, wd = Clamp(d.W * 0.13, 0.18, 0.32);
		Wh.Color(0.03, 0.03, 0.032); Wh.Rough(0.9);
		Wh.Add(Geo::Cylinder(r, r, wd, 18), Mat4::Compose(0, 0, 0, 0, 0, kPi / 2));
		Wh.Color(0.62, 0.62, 0.64); Wh.Rough(0.25);
		for (int side : { -1, 1 }) Wh.Add(Geo::Cylinder(r * 0.62, r * 0.62, 0.02, 14), Mat4::Compose(side * (wd / 2 + 0.005), 0, 0, 0, 0, kPi / 2));
		Wh.Color(0.3, 0.3, 0.32);
		for (int k = 0; k < 5; k++) for (int side : { -1, 1 }) Wh.Add(Geo::Box(0.025, r * 0.9, 0.06), Mat4::Compose(side * (wd / 2 + 0.012), 0, 0, k * kTau / 5, 0, 0));
	}
	m.seat[0] = 0.38; m.seat[1] = seatY + 0.09; m.seat[2] = seatZ - 0.1;
	m.door[0] = wHalf + 0.55; m.door[1] = seatZ - 0.15;
	m.cgH = Max(0.35, H * 0.4);
	return m;
}

// ------------------------------------------------------------------ people
std::vector<HumanPart> BuildHuman(const HumanLook& k) {
	std::vector<HumanPart> parts;
	auto part = [&](const char* name, const char* parent, double jx, double jy, double jz) -> MeshBuf& {
		HumanPart p; p.name = name; p.parent = parent; p.joint[0] = jx; p.joint[1] = jy; p.joint[2] = jz;
		p.mesh.Rough(0.75);
		parts.push_back(p);
		return parts.back().mesh;
	};
	{ // hips: pelvis block in the trousers' colour
		MeshBuf& g = part("hips", "", 0, 0.98, 0);
		g.ColorHex(k.pants); g.Add(Geo::Box(0.34, 0.2, 0.21), Mat4::Compose(0, -0.02, 0));
		g.ColorHex(0x2a2a2a); g.Add(Geo::Box(0.35, 0.05, 0.22), Mat4::Compose(0, 0.06, 0)); // belt
	}
	{ // torso from the waist up
		MeshBuf& g = part("torso", "hips", 0, 0.06, 0);
		g.ColorHex(k.shirt);
		g.Add(Geo::Cylinder(0.2, 0.17, 0.48, 10), Mat4::Compose(0, 0.24, 0, 0, 0, 0, 1, 1, 0.62));
		g.Add(Geo::Sphere(0.2, 10, 6, 0, kTau, 0, kPi / 2), Mat4::Compose(0, 0.46, 0, 0, 0, 0, 1.05, 0.45, 0.62));
	}
	{ // neck + head
		MeshBuf& g = part("head", "torso", 0, 0.52, 0);
		g.ColorHex(k.skin);
		g.Add(Geo::Cylinder(0.05, 0.055, 0.1, 8), Mat4::Compose(0, 0.04, 0));
		g.Add(Geo::Sphere(0.105, 12, 10), Mat4::Compose(0, 0.19, 0.01, 0, 0, 0, 0.92, 1.1, 1.0));
		g.Add(Geo::Box(0.04, 0.05, 0.04), Mat4::Compose(0, 0.18, 0.105)); // nose
		g.ColorHex(k.hair);
		g.Add(Geo::Sphere(0.11, 12, 6, 0, kTau, 0, kPi * 0.55), Mat4::Compose(0, 0.215, -0.005, -0.25, 0, 0, 0.95, 1.0, 1.05));
		g.ColorHex(0x111111);
		for (int s : { -1, 1 }) g.Add(Geo::Sphere(0.012, 6, 4), Mat4::Compose(s * 0.036, 0.205, 0.093));
	}
	for (int s : { -1, 1 }) {
		const bool L = s > 0;
		{ // upper arm: short sleeve then skin
			MeshBuf& g = part(L ? "armL" : "armR", "torso", s * 0.235, 0.44, 0);
			g.ColorHex(k.shirt); g.Add(Geo::Capsule(0.062, 0.1, 3, 8), Mat4::Compose(0, -0.07, 0));
			g.ColorHex(k.skin); g.Add(Geo::Capsule(0.05, 0.16, 3, 8), Mat4::Compose(0, -0.18, 0));
		}
		{ // forearm + hand
			MeshBuf& g = part(L ? "foreL" : "foreR", L ? "armL" : "armR", 0, -0.29, 0);
			g.ColorHex(k.skin);
			g.Add(Geo::Capsule(0.045, 0.2, 3, 8), Mat4::Compose(0, -0.13, 0));
			g.Add(Geo::Sphere(0.052, 8, 6), Mat4::Compose(0, -0.29, 0.01, 0, 0, 0, 0.8, 1.15, 0.7));
		}
		{ // thigh
			MeshBuf& g = part(L ? "thighL" : "thighR", "hips", s * 0.095, -0.06, 0);
			g.ColorHex(k.pants); g.Add(Geo::Capsule(0.078, 0.3, 3, 8), Mat4::Compose(0, -0.21, 0));
		}
		{ // shin + shoe
			MeshBuf& g = part(L ? "shinL" : "shinR", L ? "thighL" : "thighR", 0, -0.43, 0);
			g.ColorHex(k.pants); g.Add(Geo::Capsule(0.062, 0.3, 3, 8), Mat4::Compose(0, -0.2, 0));
			g.ColorHex(k.shoes); g.Add(Geo::Box(0.11, 0.09, 0.27), Mat4::Compose(0, -0.42, 0.05));
		}
	}
	return parts;
}

} // namespace atg
