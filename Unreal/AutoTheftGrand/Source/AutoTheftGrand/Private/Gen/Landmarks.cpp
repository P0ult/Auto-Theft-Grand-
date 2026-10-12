#include "Landmarks.h"
#include "GenMath.h"
#include "RoadLayout.h"

namespace atg {

namespace {
// three.js Color.setHSL (in the linear working space)
void LmHsl(double h, double s, double l, double out[3]) {
	auto hue2rgb = [](double p, double q, double t) {
		if (t < 0) t += 1;
		if (t > 1) t -= 1;
		if (t < 1.0 / 6) return p + (q - p) * 6 * t;
		if (t < 1.0 / 2) return q;
		if (t < 2.0 / 3) return p + (q - p) * 6 * (2.0 / 3 - t);
		return p;
	};
	h = std::fmod(std::fmod(h, 1) + 1, 1); s = Clamp(s, 0, 1); l = Clamp(l, 0, 1);
	if (s == 0) { out[0] = out[1] = out[2] = l; return; }
	const double p = l <= 0.5 ? l * (1 + s) : l + s - l * s, q = 2 * l - p;
	out[0] = hue2rgb(q, p, h + 1.0 / 3); out[1] = hue2rgb(q, p, h); out[2] = hue2rgb(q, p, h - 1.0 / 3);
}
// GeoBuilder.addGeometry: the geometry made non-indexed, through the matrix, in the current colour
void LmAdd(MeshBuf& gb, const MeshBuf& g, const Mat4& m) {
	MeshBuf flat;
	const size_t n = g.I.empty() ? g.Count() : g.I.size();
	for (size_t k = 0; k < n; k++) {
		const uint32_t i = g.I.empty() ? (uint32_t)k : g.I[k];
		flat.V(g.P[i * 3], g.P[i * 3 + 1], g.P[i * 3 + 2], g.N[i * 3], g.N[i * 3 + 1], g.N[i * 3 + 2], g.C[0][i * 2], g.C[0][i * 2 + 1]);
		flat.I.push_back((uint32_t)k);
	}
	gb.Add(flat, m);
}
Mat4 LmM(double x, double y, double z, double rx = 0, double ry = 0, double rz = 0) { return Mat4::Compose(x, y, z, rx, ry, rz); }
} // namespace

Landmarks BuildLandmarks(const CityMap& map) {
	Landmarks out;
	auto box = [&](double minX, double maxX, double minZ, double maxZ, double minY, double maxY, const char* type, bool soft = false) {
		LandmarkCollider c; c.kind = "box"; c.type = type; c.soft = soft; c.minX = minX; c.maxX = maxX; c.minZ = minZ; c.maxZ = maxZ; c.minY = minY; c.maxY = maxY; out.colliders.push_back(c);
	};

	// ---------------------------------------------------------- Pier
	const Landmark& P = map.landmarks.at("pier");
	const double y = P.y, x0 = P.vals.at("x0"), x1 = P.vals.at("x1"), pz0 = P.vals.at("z0"), pz1 = P.vals.at("z1");
	{
		MeshBuf& gb = out.pierDeck;
		const double ramp = 24;
		// ramp (sloped quad) from ground to deck height
		const double zA = pz0, zB = pz0 + ramp;
		const double yA = Max(map.TerrainHeight((x0 + x1) / 2, zA), 0.1);
		const double nl = Hypot(ramp, y - yA);
		const double nrm[3] = { 0, ramp / nl, -(y - yA) / nl };
		{ const double a[3] = { x0, yA, zB - ramp }, b[3] = { x1, yA, zB - ramp }, c[3] = { x1, y, zB }, d[3] = { x0, y, zB }; gb.Quad(a, b, c, d, nrm); }
		{ const double a[3] = { x0, y, pz1 }, b[3] = { x1, y, pz1 }, c[3] = { x1, y, zB }, d[3] = { x0, y, zB }, up[3] = { 0, 1, 0 }; gb.Quad(a, b, c, d, up); }
		// deck thickness edges
		gb.Box(x0, y - 0.5, zB, x1, y, pz1, false, true);
		// posts & rails
		MeshBuf& posts = out.pierPosts;
		posts.Color(0.3, 0.24, 0.18);
		for (double z = zB; z < pz1; z += 8) for (double x : { x0 + 0.6, (x0 + x1) / 2, x1 - 0.6 }) LmAdd(posts, Geo::Cylinder(0.3, 0.3, y + 10, 8), LmM(x, (y - 10) / 2, z));
		posts.Color(0.85, 0.85, 0.82);
		for (double x : { x0 + 0.1, x1 - 0.1 }) {
			posts.Box(x - 0.06, y, zB, x + 0.06, y + 1.1, pz1, true);
			for (double z = zB; z < pz1; z += 2.5) posts.Box(x - 0.05, y, z - 0.05, x + 0.05, y + 1.1, z + 0.05, true);
			box(x - 0.2, x + 0.2, zB, pz1, y - 0.5, y + 1.1, "fence", true);
		}
		box(x0, x1, pz1 - 0.3, pz1 + 0.3, y - 0.5, y + 1.1, "fence", true);
		// the pilings: boats can't slip under the deck
		box(x0, x1, zB + 20, pz1, -8, 1.4, "pilings");
		// food booths along the pier
		RNG rng(31);
		for (double z = zB + 30; z < pz1 - 70; z += 36) {
			const double side = rng.Chance(0.5) ? x0 + 2 : x1 - 6;
			double c[3]; LmHsl(rng.Next(), 0.6, 0.55, c);
			posts.Color(c[0], c[1], c[2]);
			posts.Box(side, y, z, side + 4, y + 3, z + 5, true);
			posts.Color(0.95, 0.95, 0.95);
			posts.Box(side - 0.3, y + 3, z - 0.3, side + 4.3, y + 3.3, z + 5.3, true);
			box(side, side + 4, z, z + 5, y, y + 3.3, "building");
		}
		// pier lamps
		out.lampPoles.Color(0.2, 0.22, 0.25);
		int li = 0;
		for (double z = zB + 10; z < pz1 && li < 40; z += 20) {
			for (double x : { x0 + 0.4, x1 - 0.4 }) {
				if (li >= 40) break;
				LmAdd(out.lampPoles, Geo::Cylinder(0.07, 0.09, 4, 6), LmM(x, y + 2, z));
				out.lamps.push_back({ x, y + 4.1, z });
				li++;
			}
		}
	}

	// ------------------------------------------------------- Ferris wheel
	{
		const Landmark& F = map.landmarks.at("ferris");
		const double R = 20;
		const double hubY = y + R + 4;
		out.hubX = F.x; out.hubY = hubY; out.hubZ = F.z; out.wheelR = R;
		MeshBuf& wg = out.wheel;
		wg.Color(1, 1, 1);
		for (double zOff : { -1.6, 1.6 }) {
			LmAdd(wg, Geo::Torus(R, 0.25, 6, 64), LmM(0, 0, zOff));
			LmAdd(wg, Geo::Torus(R * 0.55, 0.15, 6, 48), LmM(0, 0, zOff));
			for (int k = 0; k < 16; k++) {
				const double a = k / 16.0 * kPi * 2;
				LmAdd(wg, Geo::Cylinder(0.1, 0.1, R, 4), LmM(std::cos(a) * R / 2, std::sin(a) * R / 2, zOff, 0, 0, a - kPi / 2));
			}
		}
		LmAdd(wg, Geo::Cylinder(1.0, 1.0, 4.4, 12), LmM(0, 0, 0, kPi / 2, 0, 0));
		// neon rim lights
		for (int k = 0; k < 96; k++) {
			const double a = k / 96.0 * kPi * 2;
			double c[3]; LmHsl(k / 96.0, 1, 0.5, c);
			out.neon.Color(c[0], c[1], c[2]);
			LmAdd(out.neon, Geo::Sphere(0.22, 6, 4), LmM(std::cos(a) * R, std::sin(a) * R, 1.9));
			LmAdd(out.neon, Geo::Sphere(0.22, 6, 4), LmM(std::cos(a) * R, std::sin(a) * R, -1.9));
		}
		// gondolas
		MeshBuf& gg = out.gondola;
		gg.Color(1, 1, 1);
		gg.Box(-1.1, -2.6, -1.1, 1.1, -0.6, 1.1, true, true, true);
		gg.Color(0.3, 0.3, 0.3);
		gg.Box(-1.3, -0.6, -1.3, 1.3, -0.4, 1.3, true, true, true);
		LmAdd(gg, Geo::Cylinder(0.05, 0.05, 0.6, 4), LmM(0, -0.3, 0));
		for (int k = 0; k < 16; k++) LmHsl(k / 16.0, 0.7, 0.5, out.gondolaColor[k]);
		// support A-frame
		MeshBuf& sup = out.support;
		sup.Color(0.95, 0.95, 0.95);
		for (double zOff : { -3.4, 3.4 }) for (int sx : { -1, 1 }) {
			const double bx = sx * 9;
			const double len = Hypot(bx, R + 4);
			const double ang = std::atan2(bx, R + 4);
			LmAdd(sup, Geo::Cylinder(0.35, 0.5, len, 8), LmM(F.x + bx / 2, hubY - (R + 4) / 2, F.z + zOff, 0, 0, ang));
		}
		LmAdd(sup, Geo::Cylinder(0.4, 0.4, 7.2, 8), LmM(F.x, hubY, F.z, kPi / 2, 0, 0));
		const double legs[4][2] = { { -9, -3.4 }, { 9, -3.4 }, { -9, 3.4 }, { 9, 3.4 } };
		for (const auto& d : legs) {
			LandmarkCollider c; c.kind = "circle"; c.type = "prop"; c.x = F.x + d[0]; c.z = F.z + d[1]; c.r = 0.8; c.h = 30; out.colliders.push_back(c);
		}
	}

	// ---------------------------------------------------------- Vistawood sign
	{
		const Landmark& S = map.landmarks.at("sign");
		const std::string word = "VISTAWOOD";
		out.scaffold.Color(0.35, 0.33, 0.3);
		for (size_t i = 0; i < word.size(); i++) {
			const double x = S.x + ((double)i - (word.size() - 1) / 2.0) * 15.5;
			const double z = S.z + std::sin(i * 0.7) * 3;
			const double h = map.TerrainHeight(x, z);
			out.letters.push_back({ word[i], x, h + 8.5, z });
			for (double dx : { -4.0, 0.0, 4.0 }) LmAdd(out.scaffold, Geo::Cylinder(0.15, 0.15, 12, 5), LmM(x + dx, h + 5, z - 1.5, 0.15, 0, 0));
		}
	}

	// ---------------------------------------------------------- Dock cranes
	{
		MeshBuf& cg = out.cranes;
		cg.Color(1, 1, 1);
		const double quayX = CityMap::CITY_MAXX + 34;
		for (double z : { 60.0, 200.0, 340.0, 480.0 }) {
			const double x = quayX - 8;
			const double feet[4][2] = { { -6, -5 }, { 6, -5 }, { -6, 5 }, { 6, 5 } };
			for (const auto& d : feet) {
				const double dx = d[0], dz = d[1];
				cg.Box(x + dx - 0.6, 0, z + dz - 0.6, x + dx + 0.6, 32, z + dz + 0.6, true);
				box(x + dx - 0.6, x + dx + 0.6, z + dz - 0.6, z + dz + 0.6, 0, 32, "building");
			}
			cg.Box(x - 7, 30, z - 6, x + 7, 34, z + 6, true, true, true);
			cg.Box(x - 30, 34, z - 2, x + 55, 37, z + 2, true, true, true);
			cg.Box(x + 2, 26, z - 3, x + 8, 30, z + 3, true, true, true);
			for (int k = 0; k < 6; k++) cg.Box(x - 2 + k * 0.1, 37, z - 0.2, x - 1.6 + k * 0.1, 48 - k * 2, z + 0.2, true);
			cg.Box(x - 3, 44, z - 1, x + 1, 50, z + 1, true);
			out.beacons.push_back({ x - 1, 50.6, z });
			out.beacons.push_back({ x + 55, 37.6, z });
		}
	}
	return out;
}

std::vector<LmPoint> BuildingBeacons(const CityMap& map) {
	std::vector<LmPoint> out;
	for (const Building& b : map.buildings) {
		if (b.roof == "gable") continue;
		const double w = b.x1 - b.x0, d = b.z1 - b.z0, cx = (b.x0 + b.x1) / 2, cz = (b.z0 + b.z1) / 2, y1 = b.y1;
		if (b.roof == "antenna") out.push_back({ cx, y1 + 14.2, cz });
		if (b.roof == "helipad") { out.push_back({ b.x0 + 0.5, y1 + 1.2, b.z0 + 0.5 }); out.push_back({ b.x1 - 0.5, y1 + 1.2, b.z1 - 0.5 }); }
		if (b.roof == "spire") out.push_back({ cx, y1 + Min(w, d) * 1.2 + 0.3, cz });
		if (b.y1 > 60 && b.roof == "flat") out.push_back({ cx, y1 + 1.0, cz });
	}
	return out;
}

} // namespace atg
