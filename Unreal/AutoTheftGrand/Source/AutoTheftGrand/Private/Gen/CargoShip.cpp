#include "CargoShip.h"
#include "CityMap.h"
#include "GenMath.h"
#include "RoadLayout.h"

namespace atg {

namespace {
constexpr double ShipDY = WATER_Y + 10; // main deck
}

const ShipSpec SHIP = {
	"MV Pacific Star", CityMap::CITY_MAXX + 56, 270, 170, 26, ShipDY,
	85, -93, -68,
	{ -50, -29, -8, 13, 34 }, 9.45,
	{ ShipDY, ShipDY + 3, ShipDY + 6, ShipDY + 9 },
	{ -21.8, -17.4, 17, 46.3, { 46, 49.5 } },
};

double ShipHalfBeam(double lz) {
	if (lz > 80) return 13 - (lz - 80) * 0.08;
	if (lz > -48) return 13;
	const double u = (-48 - lz) / (-SHIP.bow - 48 + 1e-6);
	return Max(0.3, 13 * std::sqrt(Max(0.0, 1 - u * u)));
}

bool CargoShip::Aboard(double x, double y, double z) {
	const double lz = z - SHIP.z, lx = x - SHIP.x;
	return lz > SHIP.bow && lz < SHIP.stern && std::fabs(lx) < ShipHalfBeam(lz) && y > ShipDY - 1.5;
}

CargoShip BuildCargoShip() {
	const ShipSpec& S = SHIP;
	const double DY = ShipDY;
	CargoShip out;
	auto X = [&](double lx) { return S.x + lx; };
	auto Z = [&](double lz) { return S.z + lz; };
	Part hull, deck, steel, white, glass, lamp;
	RNG rng(19);
	auto box = [&](double minX, double maxX, double minZ, double maxZ, double minY, double maxY, const std::string& type) {
		ShipCollider c; c.kind = "box"; c.type = type; c.minX = minX; c.maxX = maxX; c.minZ = minZ; c.maxZ = maxZ; c.minY = minY; c.maxY = maxY; out.colliders.push_back(c);
	};
	auto obox = [&](double cx, double cz, double hx, double hz, double yaw, double minY, double maxY, const std::string& type) {
		ShipCollider c; c.kind = "obox"; c.type = type; c.cx = cx; c.cz = cz; c.hx = hx; c.hz = hz; c.yaw = yaw; c.minY = minY; c.maxY = maxY; out.colliders.push_back(c);
	};
	auto deckCol = [&](double ax, double az, double ay, double bx, double bz, double by, double hl, double hr) {
		ShipCollider c; c.kind = "deck"; c.ax = ax; c.az = az; c.ay = ay; c.bx = bx; c.bz = bz; c.by = by; c.hl = hl; c.hr = hr; out.colliders.push_back(c);
	};

	// ---------------- hull (lofted: flat bottom, round bilge, flared bow, raised forecastle)
	const double keel = WATER_Y - 6.5;
	auto topAt = [&](double lz) { return (lz < S.fc - 1 ? DY + 2.4 : lz < S.fc + 1 ? DY + 2.4 * (S.fc + 1 - lz) / 2 : DY) + 1.1; };
	const std::vector<double> zs = Stations(S.bow, S.stern, 3, { S.fc - 1, S.fc + 1, -48, 80 });
	auto ring = [&](double lz) {
		const double b = ShipHalfBeam(lz), top = topAt(lz);
		const double fore = lz < -60 ? (-60 - lz) / 33 : 0;
		const double y0 = keel + fore * fore * 5.5; // the forefoot rises
		const double br = Min(1.6, b * 0.5);
		std::vector<Pt2> pts = { { 0, y0 } };
		pts.push_back({ Max(0.0, b - br), y0 });
		for (int k = 1; k <= 4; k++) { const double a = -kPi / 2 + k / 4.0 * kPi / 2; pts.push_back({ b - br + std::cos(a) * br, y0 + br + std::sin(a) * br }); }
		for (int k = 1; k <= 4; k++) { const double y = y0 + br + (top - y0 - br) * k / 4; pts.push_back({ b * (1 + fore * 0.08 * k / 4), y }); }
		return pts;
	};
	auto hullCol = [](double, double y) -> Pt3 {
		return y < WATER_Y + 0.4 ? Pt3{ 0.45, 0.12, 0.08 } : y < WATER_Y + 0.9 ? Pt3{ 0.9, 0.9, 0.88 } : y > ShipDY + 0.6 ? Pt3{ 0.85, 0.85, 0.82 } : Pt3{ 0.1, 0.18, 0.3 };
	};
	for (int sd : { 1, -1 }) {
		std::vector<std::vector<Pt3>> rows;
		for (double lz : zs) { std::vector<Pt3> r; for (const Pt2& p : ring(lz)) r.push_back({ X(p[0] * sd), p[1], Z(lz) }); rows.push_back(r); }
		GridOpts o; o.ref = { S.x, 0, S.z }; o.colorAt = [&](double x, double y, double, Part*) { return hullCol(x, y); };
		EmitGrid(rows, [&](int, int) { return &hull; }, o);
	}
	{
		const std::vector<Pt2> r0 = ring(S.stern);
		hull.color(0.1, 0.18, 0.3);
		std::vector<Pt3> pts;
		for (const Pt2& p : r0) pts.push_back({ X(p[0]), p[1], Z(S.stern) });
		for (auto it = r0.rbegin(); it != r0.rend(); ++it) pts.push_back({ X(-(*it)[0]), (*it)[1], Z(S.stern) });
		hull.poly(pts, { 0, 0, 1 });
	}
	// inner face of the bulwark (so the rail reads from the deck)
	for (int sd : { 1, -1 }) {
		std::vector<std::vector<Pt3>> rows;
		for (double lz : zs) if (lz > S.bow + 4) { const double b = ShipHalfBeam(lz) - 0.25, t = topAt(lz); rows.push_back({ { X(sd * b), t - 1.1, Z(lz) }, { X(sd * b), t, Z(lz) } }); }
		GridOpts o; o.ref = { S.x, DY + 0.5, S.z }; o.inward = true; o.colorAt = [](double, double, double, Part*) { return Pt3{ 0.85, 0.85, 0.82 }; };
		EmitGrid(rows, [&](int, int) { return &hull; }, o);
	}

	// ---------------- decks
	deck.color(0.42, 0.18, 0.14);
	{
		std::vector<std::vector<Pt3>> rows;
		for (double lz : Stations(S.fc + 1, S.stern - 0.6, 4)) { const double b = ShipHalfBeam(lz) - 0.26; rows.push_back({ { X(-b), DY, Z(lz) }, { X(b), DY, Z(lz) } }); }
		GridOpts o; o.ref = { S.x, 0, S.z };
		EmitGrid(rows, [&](int, int) { return &deck; }, o);
	}
	{
		std::vector<std::vector<Pt3>> rows;
		for (double lz : Stations(S.bow + 2, S.fc + 1, 2)) { const double b = Max(0.2, ShipHalfBeam(lz) - 0.26); rows.push_back({ { X(-b), DY + 2.4, Z(lz) }, { X(b), DY + 2.4, Z(lz) } }); }
		GridOpts o; o.ref = { S.x, 0, S.z };
		EmitGrid(rows, [&](int, int) { return &deck; }, o);
	}
	// floors (walkable) and the hull's sides (solid to boats, swimmers and anyone on deck)
	box(X(-12.8), X(12.8), Z(S.fc), Z(S.stern - 0.4), DY - 1.2, DY, "deck");
	for (int k = 0; k < 4; k++) {
		const double z0 = S.bow + 3 + k * (S.fc - S.bow - 3) / 4, z1 = z0 + (S.fc - S.bow - 3) / 4;
		const double b = ShipHalfBeam(z0 + (z1 - z0) * 0.3) - 0.3;
		box(X(-b), X(b), Z(z0), Z(z1 + 0.5), DY + 1.2, DY + 2.4, "deck");
	}
	const ShipSpec::Gangway& g = S.gangway;
	auto wall = [&](double x0, double z0, double x1, double z1, double y0, double y1, const std::string& type = "hull") {
		box(Min(x0, x1), Max(x0, x1), Min(z0, z1), Max(z0, z1), y0, y1, type);
	};
	for (int sd : { 1, -1 }) {
		// parallel body: straight walls (a gap in the port rail for the gangway)
		wall(X(sd * 12.7), Z(-48), X(sd * 13.4), Z(S.stern), -9, DY, "hull");
		if (sd < 0) { wall(X(-12.7), Z(-48), X(-13.4), Z(g.land[0]), DY - 0.1, DY + 1.1, "rail"); wall(X(-12.7), Z(g.land[1]), X(-13.4), Z(S.stern), DY - 0.1, DY + 1.1, "rail"); }
		else wall(X(12.7), Z(-48), X(13.4), Z(S.stern), DY - 0.1, DY + 1.1, "rail");
		// the bow: angled segments
		const int segs = 5;
		for (int k = 0; k < segs; k++) {
			const double za = -48 + (S.bow + 2 + 48) * k / segs, zb = -48 + (S.bow + 2 + 48) * (k + 1) / segs;
			const double xa = ShipHalfBeam(za), xb = ShipHalfBeam(zb);
			const double cx = (xa + xb) / 2, cz = (za + zb) / 2, len = Hypot(xb - xa, zb - za);
			const double yaw = std::atan2((xb - xa) * sd, zb - za);
			obox(X(sd * cx), Z(cz), 0.35, len / 2 + 0.3, yaw, -9, topAt(cz), "hull");
		}
	}
	wall(X(-13.4), Z(S.stern - 0.4), X(13.4), Z(S.stern + 0.4), -9, DY + 1.1, "hull");
	// the forecastle break: a wall with a ramp up the middle
	wall(X(-12), Z(S.fc - 0.4), X(-2.2), Z(S.fc + 0.4), DY - 0.2, DY + 2.4 + 1.1, "rail"); wall(X(2.2), Z(S.fc - 0.4), X(12), Z(S.fc + 0.4), DY - 0.2, DY + 2.4 + 1.1, "rail");
	deckCol(X(0), Z(S.fc + 8), DY, X(0), Z(S.fc - 0.2), DY + 2.4, 2, 2);
	steel.color(0.35, 0.36, 0.38);
	{
		auto P = [&](double lx, double lz, double y) { return Pt3{ X(lx), y, Z(lz) }; };
		steel.poly({ P(-2, S.fc + 8, DY + 0.02), P(2, S.fc + 8, DY + 0.02), P(2, S.fc - 0.2, DY + 2.42), P(-2, S.fc - 0.2, DY + 2.42) }, { 0, 0.95, 0.3 });
	}
	// the bulwark rails, drawn: posts and two bars
	steel.color(0.8, 0.8, 0.78);
	for (int sd : { 1, -1 }) for (double lz = S.bow + 6; lz < S.stern - 1; lz += 2.5) {
		if (sd < 0 && lz > g.land[0] - 0.5 && lz < g.land[1] + 0.5) continue;
		const double b = ShipHalfBeam(lz) - 0.2, t = topAt(lz);
		steel.box(X(sd * b) - 0.04, t, Z(lz) - 0.04, X(sd * b) + 0.04, t + 0.5, Z(lz) + 0.04);
	}

	// ---------------- cargo: hatch covers and container stacks, five bays
	const std::vector<std::array<double, 3>> COLORS = { { 0.69, 0.23, 0.18 }, { 0.12, 0.38, 0.55 }, { 0.07, 0.48, 0.4 }, { 0.83, 0.67, 0.05 }, { 0.42, 0.2, 0.51 }, { 0.73, 0.29, 0 }, { 0.55, 0.55, 0.55 } };
	Part cont;
	for (double bz : S.bays) {
		steel.color(0.25, 0.3, 0.33);
		steel.box(X(-10.3), DY, Z(bz - S.bayHalf), X(10.3), DY + 0.8, Z(bz + S.bayHalf));
		wall(X(-10.3), Z(bz - S.bayHalf), X(10.3), Z(bz + S.bayHalf), DY - 0.5, DY + 0.8, "hatch");
		for (int row = -1; row <= 1; row++) for (int c = -3; c <= 3; c++) {
			const double cx = c * 2.6, cz = bz + row * 6.3;
			const int n = rng.Int(1, 4);
			const double base = DY + 0.8;
			for (int k = 0; k < n; k++) {
				const auto& cc = rng.Pick(COLORS);
				cont.color(cc[0], cc[1], cc[2]);
				const double y0 = base + k * 2.6;
				cont.box(X(cx - 1.21), y0, Z(cz - 3.02), X(cx + 1.21), y0 + 2.58, Z(cz + 3.02));
				// corrugation ribs and the door end
				cont.color(cc[0] * 0.8, cc[1] * 0.8, cc[2] * 0.8);
				for (int r = -2; r <= 2; r++) cont.box(X(cx - 1.23), y0 + 0.15, Z(cz + r * 1.1 - 0.05), X(cx + 1.23), y0 + 2.43, Z(cz + r * 1.1 + 0.05));
			}
			out.stacks.push_back({ cx, cz, n, bz, row });
			wall(X(cx - 1.22), Z(cz - 3.03), X(cx + 1.22), Z(cz + 3.03), DY + 0.7, base + n * 2.6, "container");
		}
	}
	// lashing bridges between the bays (walkways across the ship between the stacks)
	steel.color(0.75, 0.55, 0.12);
	for (int k = 0; k < 4; k++) { const double gz = (S.bays[k] + S.bays[k + 1]) / 2; for (int sd : { 1, -1 }) steel.box(X(sd * 10.4) - 0.06, DY, Z(gz) - 0.06, X(sd * 10.4) + 0.06, DY + 3.2, Z(gz) + 0.06); }

	// ---------------- forecastle kit: windlasses, bollards, a foremast
	steel.color(0.2, 0.22, 0.24);
	for (int sd : { 1, -1 }) {
		steel.geo(Geo::Cylinder(0.6, 0.6, 1.4, 12), Mat4::Compose(X(sd * 3.5), DY + 3.1, Z(-80), 0, 0, kPi / 2));
		for (double lz : { -74.0, -86.0 }) steel.geo(Geo::Cylinder(0.25, 0.3, 0.7, 10), Mat4::Compose(X(sd * ShipHalfBeam(lz) - sd * 1.6), DY + 2.75, Z(lz)));
	}
	steel.color(0.85, 0.85, 0.82);
	steel.geo(Geo::Cylinder(0.25, 0.35, 12, 10), Mat4::Compose(X(0), DY + 8.4, Z(-86)));
	lamp.color(1, 1, 0.9); lamp.box(X(-0.2), DY + 14.4, Z(-86.2), X(0.2), DY + 14.8, Z(-85.8));
	{ ShipCollider c; c.kind = "circle"; c.x = X(0); c.z = Z(-86); c.r = 0.4; c.h = DY + 14; c.y0 = DY + 2; out.colliders.push_back(c); }

	// ---------------- superstructure: four stepped levels, balconies, ramps, the bridge
	const double* L = S.levels;
	const double front[4] = { 56, 58, 60, 62 }, back = 74;
	auto windows = [&](double y0, double y1, double zFront, double xw) {
		glass.color(1, 1, 1);
		for (double x = -xw + 1.2; x <= xw - 1.2; x += 2.2) glass.box(X(x - 0.6), y0 + 1.0, Z(zFront) - 0.03, X(x + 0.6), y1 - 0.8, Z(zFront) + 0.01);
		for (int sd : { 1, -1 }) for (double lz = zFront + 1.5; lz < back - 1; lz += 2.4) glass.box(X(sd * xw) - 0.01, y0 + 1.0, Z(lz - 0.4), X(sd * xw) + 0.03 * sd + 0.01, y1 - 0.8, Z(lz + 0.4));
	};
	for (int k = 0; k < 3; k++) {
		white.color(0.93, 0.93, 0.9);
		white.box(X(-10), L[k], Z(front[k]), X(10), L[k] + 3, Z(back));
		wall(X(-10), Z(front[k]), X(10), Z(back), L[k] - 0.2, L[k] + 3, "superstructure");
		windows(L[k], L[k] + 3, front[k], 10);
	}
	// balconies (the stepped roofs): rails down the sides and along the front, with gaps where the ramps arrive
	steel.color(0.85, 0.85, 0.82);
	auto rail = [&](double x0, double x1, double lz0, double lz1, double y) {
		wall(X(x0), Z(lz0), X(x1), Z(lz1), y - 0.1, y + 1.1, "rail");
		const bool alongX = std::fabs(x1 - x0) > std::fabs(lz1 - lz0);
		const int n = (int)Max(1.0, std::floor((alongX ? std::fabs(x1 - x0) : std::fabs(lz1 - lz0)) / 1.2 + 0.5));
		for (int k = 0; k <= n; k++) { const double t = (double)k / n, x = X(x0 + (x1 - x0) * t), z = Z(lz0 + (lz1 - lz0) * t); steel.box(x - 0.03, y, z - 0.03, x + 0.03, y + 1.05, z + 0.03); }
		steel.box(X(Min(x0, x1)) - 0.03, y + 1.0, Z(Min(lz0, lz1)) - 0.03, X(Max(x0, x1)) + 0.03, y + 1.06, Z(Max(lz0, lz1)) + 0.03);
	};
	const double gapsAt[4][2] = { { 0, 0 }, { 0, 2 }, { 8.4, 10 }, { -10, -8.4 } };
	for (int k = 1; k <= 3; k++) {
		const double y = L[k], z0 = front[k - 1], z1 = front[k];
		for (int sd : { 1, -1 }) rail(sd * 10, sd * 10.05, z0, z1 + (k == 3 ? 3 : 0), y);
		const double ga = gapsAt[k][0], gb = gapsAt[k][1];
		if (ga > -10) rail(-10, ga, z0, z0 + 0.05, y);
		if (gb < 10) rail(gb, 10, z0, z0 + 0.05, y);
	}
	// the bridge: a room (walls with a door, big windows), wings, a roof with the radar mast
	{
		const double y = L[3], z0 = front[3], H = 3;
		white.color(0.93, 0.93, 0.9);
		// front wall: below the windows, the window mullions, above; a door in the middle
		white.box(X(-10), y, Z(z0), X(-1.1), y + 1.1, Z(z0 + 0.25)); white.box(X(1.1), y, Z(z0), X(10), y + 1.1, Z(z0 + 0.25));
		white.box(X(-10), y + 2.5, Z(z0), X(10), y + H, Z(z0 + 0.25));
		for (double x : { -10.0, -6.0, -1.3, 1.1, 6.0, 9.8 }) white.box(X(x), y + 1.1, Z(z0), X(x + 0.2), y + 2.5, Z(z0 + 0.25));
		glass.color(1, 1, 1);
		const double panes[4][2] = { { -9.8, -6 }, { -5.8, -1.3 }, { 1.3, 6 }, { 6.2, 9.8 } };
		for (const auto& p : panes) glass.box(X(p[0]), y + 1.1, Z(z0 + 0.08), X(p[1]), y + 2.5, Z(z0 + 0.14));
		white.box(X(-10), y, Z(back - 0.25), X(10), y + H, Z(back));
		for (int sd : { 1, -1 }) { white.box(X(sd > 0 ? 9.75 : -10), y, Z(z0), X(sd > 0 ? 10 : -9.75), y + H, Z(back)); glass.box(X(sd > 0 ? 10 : -10.05), y + 1.1, Z(z0 + 1), X(sd > 0 ? 10.05 : -10), y + 2.5, Z(back - 2)); }
		white.box(X(-10.5), y + H, Z(z0 - 0.5), X(10.5), y + H + 0.3, Z(back + 0.3));
		// wings
		for (int sd : { 1, -1 }) white.box(X(sd > 0 ? 10 : -13), y - 0.3, Z(z0), X(sd > 0 ? 13 : -10), y, Z(z0 + 3));
		// walls for the collision (the door gap is x -1.1..1.1)
		wall(X(-10), Z(z0), X(-1.1), Z(z0 + 0.25), y - 0.1, y + H, "wall"); wall(X(1.1), Z(z0), X(10), Z(z0 + 0.25), y - 0.1, y + H, "wall");
		wall(X(-10), Z(back - 0.25), X(10), Z(back), y - 0.1, y + H, "wall");
		wall(X(-10), Z(z0), X(-9.75), Z(back), y - 0.1, y + H, "wall"); wall(X(9.75), Z(z0), X(10), Z(back), y - 0.1, y + H, "wall");
		wall(X(-10.5), Z(z0 - 0.5), X(10.5), Z(back + 0.3), y + H, y + H + 0.3, "roof");
		// inside: the helm console, a chart table, the wheel, and the safe against the back wall
		steel.color(0.18, 0.2, 0.22);
		steel.box(X(-6), y, Z(z0 + 0.6), X(6), y + 1.05, Z(z0 + 1.5));
		steel.color(0.12, 0.35, 0.2); steel.box(X(-5.5), y + 1.05, Z(z0 + 0.7), X(5.5), y + 1.1, Z(z0 + 1.3));
		steel.color(0.5, 0.35, 0.2); steel.box(X(-7), y, Z(back - 5), X(-4), y + 1.0, Z(back - 3));
		steel.color(0.3, 0.3, 0.32); steel.geo(Geo::Torus(0.35, 0.04, 6, 16), Mat4::Compose(X(0), y + 1.3, Z(z0 + 1.9), -0.3, 0, 0));
		steel.color(0.24, 0.26, 0.28); steel.box(X(3.8), y, Z(back - 1.2), X(5.2), y + 1.4, Z(back - 0.25));
		steel.color(0.6, 0.6, 0.62); steel.geo(Geo::Cylinder(0.12, 0.12, 0.05, 12), Mat4::Compose(X(4.5), y + 0.8, Z(back - 1.23), kPi / 2, 0, 0));
		wall(X(-6), Z(z0 + 0.6), X(6), Z(z0 + 1.5), y - 0.1, y + 1.05, "console");
		wall(X(3.8), Z(back - 1.2), X(5.2), Z(back - 0.25), y - 0.1, y + 1.4, "safe");
		// radar mast and a horn on the roof
		steel.color(0.85, 0.85, 0.82); steel.geo(Geo::Cylinder(0.15, 0.2, 5, 8), Mat4::Compose(X(0), y + H + 2.8, Z(back - 4)));
		steel.color(0.2, 0.2, 0.22); steel.box(X(-1.8), y + H + 4.8, Z(back - 4.15), X(1.8), y + H + 5.05, Z(back - 3.85));
		lamp.color(1, 0.2, 0.1); lamp.box(X(-0.12), y + H + 5.3, Z(back - 4.12), X(0.12), y + H + 5.55, Z(back - 3.88));
		out.safe = { X(4.5), y, Z(back - 2.2) };
		out.bridgeDoor = { X(0), y, Z(z0 - 0.8) };
	}
	// funnel with the company colours
	white.color(0.72, 0.1, 0.08); white.geo(Geo::Cylinder(2.6, 3, 9, 16), Mat4::Compose(X(0), L[3] + 7.5, Z(79), 0, 0, 0, 1, 1, 1.5));
	white.color(0.08, 0.08, 0.08); white.geo(Geo::Cylinder(2.62, 2.62, 1.2, 16), Mat4::Compose(X(0), L[3] + 12.4, Z(79), 0, 0, 0, 1, 1, 1.5));
	wall(X(-3), Z(74), X(3), Z(84), DY, L[3] + 13, "funnel");
	// lifeboats on davits at the stern quarters
	for (int sd : { 1, -1 }) {
		white.color(0.95, 0.45, 0.1);
		RoundBox(white, X(sd * 11.2) - 1.1, X(sd * 11.2) + 1.1, L[1] + 0.4, L[1] + 2.2, Z(76), Z(83), 0.6, 3);
		steel.color(0.8, 0.8, 0.8);
		for (double lz : { 76.5, 82.5 }) steel.box(X(sd * 10.4) - 0.1, L[0] + 3, Z(lz) - 0.1, X(sd * 10.4) + 0.1, L[1] + 3.2, Z(lz) + 0.1);
	}

	// ramps: main deck -> A (in front of the block), A -> B and B -> bridge along the balconies
	auto ramp = [&](double x0, double x1, double lz, double y0, double y1, double w = 1) {
		deckCol(X(x0), Z(lz), y0, X(x1), Z(lz), y1, w, w);
		steel.color(0.4, 0.42, 0.45);
		steel.poly({ { X(x0), y0 + 0.03, Z(lz - w) }, { X(x1), y1 + 0.03, Z(lz - w) }, { X(x1), y1 + 0.03, Z(lz + w) }, { X(x0), y0 + 0.03, Z(lz + w) } }, { 0, 1, 0 });
		steel.color(0.8, 0.8, 0.78);
		const int n = (int)std::ceil(std::fabs(x1 - x0) / 1.5);
		for (int k = 0; k <= n; k++) { const double t = (double)k / n, x = X(x0 + (x1 - x0) * t), y = y0 + (y1 - y0) * t; steel.box(x - 0.03, y, Z(lz - w) - 0.03, x + 0.03, y + 1.0, Z(lz - w) + 0.03); }
		auto P = [&](double t) { return Pt3{ X(x0 + (x1 - x0) * t), y0 + (y1 - y0) * t + 1.0, Z(lz - w) }; };
		const Pt3 p0 = P(0), p1 = P(1);
		steel.poly({ p0, p1, { p1[0], p1[1] + 0.06, p1[2] }, { p0[0], p0[1] + 0.06, p0[2] } }, { 0, 0, -1 });
	};
	ramp(-9, 1, 55, L[0], L[1]);
	ramp(2, 10, 57, L[1], L[2]);
	ramp(-2, -10, 59, L[2], L[3]);
	out.route = { { -9, 55, L[0] }, { 1, 55, L[1] }, { 1.5, 57, L[1] }, { 2, 57, L[1] }, { 10, 57, L[2] }, { 9, 59, L[2] }, { -2, 59, L[2] }, { -10, 59, L[3] }, { -9, 61, L[3] }, { 0, 61, L[3] }, { 0, 64, L[3] } };

	// ---------------- the gangway: from the quay edge, slanting out over the water to a landing at the rail
	{
		const double ax = X(g.xFoot), az = Z(g.z0), bx = X(g.x), bz = Z(g.z1);
		deckCol(ax, az, 0.2, bx, bz, DY, 1.2, 1.2);
		const double dx = bx - ax, dz = bz - az, len = Hypot(dx, dz), ux = dx / len, uz = dz / len, nx = -uz, nz = ux;
		auto P = [&](double t, double s, double y) { return Pt3{ ax + dx * t + nx * s, y, az + dz * t + nz * s }; };
		steel.color(0.55, 0.56, 0.58);
		steel.poly({ P(0, -1.2, 0.23), P(0, 1.2, 0.23), P(1, 1.2, DY + 0.03), P(1, -1.2, DY + 0.03) }, { 0, 1, 0 });
		steel.box(ax - 1.3, -3, az - 0.3, ax + 1.3, 0.2, az + 0.3); // (the foot, on the quay)
		steel.color(0.85, 0.85, 0.82);
		for (double sd : { -1.2, 1.2 }) {
			for (int k = 0; k <= 14; k++) { const double t = k / 14.0; const Pt3 q = P(t, sd, 0.2 + (DY - 0.2) * t); steel.box(q[0] - 0.03, q[1], q[2] - 0.03, q[0] + 0.03, q[1] + 1.0, q[2] + 0.03); }
			steel.poly({ P(0, sd, 1.2), P(1, sd, DY + 1.0), P(1, sd, DY + 1.06), P(0, sd, 1.26) }, { nx, 0, nz });
		}
		// the landing across to the deck
		const double lx0 = bx - 1.3, lx1 = X(-11.8);
		steel.color(0.55, 0.56, 0.58); steel.box(lx0, DY - 0.15, Z(g.land[0]), lx1, DY, Z(g.land[1]));
		box(lx0, lx1, Z(g.land[0]), Z(g.land[1]), DY - 1, DY, "deck");
		// supports down into the water
		steel.color(0.3, 0.3, 0.32);
		for (double t : { 0.35, 0.7 }) { const Pt3 q = P(t, 0, 0.2 + (DY - 0.2) * t); steel.box(q[0] - 0.1, -3, q[2] - 0.1, q[0] + 0.1, q[1], q[2] + 0.1); }
		out.gangwayFoot = { ax, 0, az - 2.5 };
		out.gangwayTop = { X(-10.5), DY, Z((g.land[0] + g.land[1]) / 2) };
	}

	// ---------------- deck lights (they glow at night)
	for (double lz : { -60.0, -18.0, 25.0, 50.0 }) for (int sd : { 1, -1 }) {
		steel.color(0.3, 0.3, 0.32); steel.box(X(sd * 12.2) - 0.08, DY, Z(lz) - 0.08, X(sd * 12.2) + 0.08, DY + 4, Z(lz) + 0.08);
		lamp.color(1, 0.95, 0.8); lamp.box(X(sd * 11.9) - 0.25, DY + 3.8, Z(lz) - 0.18, X(sd * 11.9) + 0.25, DY + 4.0, Z(lz) + 0.18);
	}

	out.hull = std::move(hull.m); out.deck = std::move(deck.m); out.steel = std::move(steel.m); out.white = std::move(white.m);
	out.cont = std::move(cont.m); out.glass = std::move(glass.m); out.lamp = std::move(lamp.m);
	return out;
}

} // namespace atg
