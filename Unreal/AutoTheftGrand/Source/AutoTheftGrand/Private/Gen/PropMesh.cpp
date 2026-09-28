// Street furniture, countryside props and vegetation models (port of src/world/props.js and the templates
// in vegetation.js), their placement (city.js _props) and collision stand-ins.
#include "WorldMeshes.h"

namespace atg {

namespace {

// three.js Color.setHSL (in the linear working space)
void Hsl(MeshBuf& g, double h, double s, double l) {
	auto hue2rgb = [](double p, double q, double t) {
		if (t < 0) t += 1;
		if (t > 1) t -= 1;
		if (t < 1.0 / 6) return p + (q - p) * 6 * t;
		if (t < 1.0 / 2) return q;
		if (t < 2.0 / 3) return p + (q - p) * 6 * (2.0 / 3 - t);
		return p;
	};
	h = std::fmod(std::fmod(h, 1) + 1, 1); s = Clamp(s, 0, 1); l = Clamp(l, 0, 1);
	if (s == 0) { g.Color(l, l, l); return; }
	const double p = l <= 0.5 ? l * (1 + s) : l + s - l * s, q = 2 * l - p;
	g.Color(hue2rgb(q, p, h + 1.0 / 3), hue2rgb(q, p, h), hue2rgb(q, p, h - 1.0 / 3));
}
double PosHash(double x, double y, double z) {
	const double h = std::sin(JsRound(x * 1000) * 12.9898 + JsRound(y * 1000) * 78.233 + JsRound(z * 1000) * 37.719) * 43758.5453;
	return h - std::floor(h);
}
Mat4 M(double x = 0, double y = 0, double z = 0, double rx = 0, double ry = 0, double rz = 0, double sx = 1, double sy = 1, double sz = 1) { return Mat4::Compose(x, y, z, rx, ry, rz, sx, sy, sz); }
void Put(MeshBuf& gb, const MeshBuf& geo, uint32_t color, const Mat4& m, double glow = 0, int sig = 0) {
	gb.ColorHex(color); gb.Glow(glow); gb.Signal(sig);
	gb.Add(geo, m);
	gb.Glow(0); gb.Signal(0);
}
MeshBuf Cyl(double rt, double rb, double h, int s = 8) { return Geo::Cylinder(rt, rb, h, s); }
MeshBuf Bx(double w, double h, double d) { return Geo::Box(w, h, d); }
MeshBuf Sph(double r, int w = 8, int h = 6) { return Geo::Sphere(r, w, h); }

void ComputeSmoothNormals(MeshBuf& g) {
	std::vector<double> acc(g.P.size(), 0.0);
	for (size_t t = 0; t + 2 < g.I.size(); t += 3) {
		const uint32_t a = g.I[t], b = g.I[t + 1], c = g.I[t + 2];
		const double ux = g.P[b * 3] - g.P[a * 3], uy = g.P[b * 3 + 1] - g.P[a * 3 + 1], uz = g.P[b * 3 + 2] - g.P[a * 3 + 2];
		const double vx = g.P[c * 3] - g.P[a * 3], vy = g.P[c * 3 + 1] - g.P[a * 3 + 1], vz = g.P[c * 3 + 2] - g.P[a * 3 + 2];
		const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
		for (uint32_t v : { a, b, c }) { acc[v * 3] += nx; acc[v * 3 + 1] += ny; acc[v * 3 + 2] += nz; }
	}
	for (size_t v = 0; v < g.Count(); v++) {
		const double l = Hypot3(acc[v * 3], acc[v * 3 + 1], acc[v * 3 + 2]);
		if (l > 0) { g.N[v * 3] = (float)(acc[v * 3] / l); g.N[v * 3 + 1] = (float)(acc[v * 3 + 1] / l); g.N[v * 3 + 2] = (float)(acc[v * 3 + 2] / l); }
	}
}

// extrude a closed outline (shape x, shape y) along y from y0 to y0 + h: shape y becomes z (the
// ExtrudeGeometry + rotateX(-PI/2) + scale(1, 1, -1) the boat hull uses)
MeshBuf ExtrudeY(const std::vector<V2>& outline, double y0, double h) {
	MeshBuf g;
	double cx = 0, cz = 0; for (const V2& p : outline) { cx += p.x; cz += p.z; } cx /= outline.size(); cz /= outline.size();
	const size_t n = outline.size();
	for (int top = 0; top < 2; top++) {
		const double y = top ? y0 + h : y0, ny = top ? 1 : -1;
		const uint32_t c = g.V(cx, y, cz, 0, ny, 0);
		std::vector<uint32_t> rim;
		for (const V2& p : outline) rim.push_back(g.V(p.x, y, p.z, 0, ny, 0));
		for (size_t i = 0; i < n; i++) {
			const uint32_t a = rim[i], b = rim[(i + 1) % n];
			// outline is counter-clockwise seen from +y when (x, z) turn left
			if (top) g.Tri(c, b, a); else g.Tri(c, a, b);
		}
	}
	for (size_t i = 0; i < n; i++) {
		const V2 a = outline[i], b = outline[(i + 1) % n];
		double nx = b.z - a.z, nz = -(b.x - a.x); const double l = Hypot(nx, nz); if (l > 0) { nx /= l; nz /= l; }
		const uint32_t i0 = g.V(a.x, y0, a.z, nx, 0, nz), i1 = g.V(b.x, y0, b.z, nx, 0, nz), i2 = g.V(b.x, y0 + h, b.z, nx, 0, nz), i3 = g.V(a.x, y0 + h, a.z, nx, 0, nz);
		g.QuadAuto(i0, i1, i2, i3);
	}
	return g;
}

MeshBuf StreetlightGeo() {
	MeshBuf gb; const uint32_t metal = 0x4a4f55;
	Put(gb, Cyl(0.09, 0.14, 8, 8), metal, M(0, 4, 0));
	Put(gb, Cyl(0.2, 0.24, 0.5, 8), metal, M(0, 0.25, 0));
	Put(gb, Cyl(0.06, 0.06, 3.2, 6), metal, M(0, 7.9, 1.5, kPi / 2 - 0.12, 0, 0));
	Put(gb, Bx(0.5, 0.18, 0.9), 0x33373b, M(0, 8.05, 3.1));
	Put(gb, Bx(0.38, 0.05, 0.7), 0xfff0d0, M(0, 7.94, 3.1), 5);
	return gb;
}
MeshBuf TrafficLightGeo() {
	MeshBuf gb; const uint32_t metal = 0x2e3236;
	Put(gb, Cyl(0.1, 0.12, 5.2, 8), metal, M(0, 2.6, 0));
	for (int axis = 0; axis < 2; axis++) {
		const double fx = axis ? 1 : 0, fz = axis ? 0 : 1;
		const double ox = fx * 0.22, oz = fz * 0.22;
		Put(gb, Bx(axis ? 0.3 : 0.4, 1.1, axis ? 0.4 : 0.3), 0x1c1e20, M(ox * 1.2, 4.6, oz * 1.2));
		const uint32_t cols[3] = { 0xff2a1a, 0xffb000, 0x2aff6a };
		for (int k = 0; k < 3; k++) {
			const double y = 4.95 - k * 0.34;
			const int sig = 1 + axis * 3 + k;
			const MeshBuf g = Geo::Circle(0.12, 10);
			const Mat4 rot = axis ? M(ox * 1.2 + fx * 0.21, y, 0, 0, kPi / 2, 0) : M(0, y, oz * 1.2 + fz * 0.21);
			Put(gb, g, cols[k], rot, 0, sig);
			const Mat4 rot2 = axis ? M(ox * 1.2 - fx * 0.51, y, 0, 0, -kPi / 2, 0) : M(0, y, oz * 1.2 - fz * 0.51, 0, kPi, 0);
			Put(gb, g, cols[k], rot2, 0, sig);
		}
	}
	return gb;
}
MeshBuf HydrantGeo() {
	MeshBuf gb; const uint32_t c = 0xc8281e;
	Put(gb, Cyl(0.16, 0.18, 0.6, 10), c, M(0, 0.3, 0));
	Put(gb, Sph(0.17, 10, 6), c, M(0, 0.62, 0));
	Put(gb, Cyl(0.06, 0.06, 0.46, 6), c, M(0, 0.42, 0, 0, 0, kPi / 2));
	Put(gb, Cyl(0.2, 0.2, 0.06, 10), 0x9a1d15, M(0, 0.04, 0));
	return gb;
}
MeshBuf TrashcanGeo() {
	MeshBuf gb;
	Put(gb, Cyl(0.3, 0.26, 0.95, 12), 0x2f4f3a, M(0, 0.475, 0));
	Put(gb, Cyl(0.33, 0.33, 0.08, 12), 0x223a2b, M(0, 0.98, 0));
	return gb;
}
MeshBuf DumpsterGeo() {
	MeshBuf gb;
	Put(gb, Bx(1.9, 1.2, 1.1), 0x2d5e34, M(0, 0.7, 0));
	Put(gb, Bx(2.0, 0.08, 1.2), 0x244a2a, M(0, 1.34, -0.05, 0.08, 0, 0));
	for (double x : { -0.8, 0.8 }) for (double z : { -0.4, 0.4 }) Put(gb, Cyl(0.08, 0.08, 0.12, 8), 0x111111, M(x, 0.06, z, kPi / 2, 0, 0));
	return gb;
}
MeshBuf BenchGeo() {
	MeshBuf gb; const uint32_t wood = 0x7a5132, metal = 0x333333;
	for (int k = 0; k < 3; k++) Put(gb, Bx(1.8, 0.05, 0.12), wood, M(0, 0.45, -0.14 + k * 0.14));
	for (int k = 0; k < 2; k++) Put(gb, Bx(1.8, 0.12, 0.04), wood, M(0, 0.65 + k * 0.16, -0.26, -0.15, 0, 0));
	for (double x : { -0.8, 0.8 }) { Put(gb, Bx(0.06, 0.45, 0.4), metal, M(x, 0.22, -0.05)); Put(gb, Bx(0.06, 0.5, 0.05), metal, M(x, 0.7, -0.25, -0.15, 0, 0)); }
	return gb;
}
MeshBuf BusstopGeo() {
	MeshBuf gb; const uint32_t metal = 0x5a6068;
	for (double x : { -1.6, 1.6 }) Put(gb, Bx(0.08, 2.5, 0.08), metal, M(x, 1.25, -0.6));
	Put(gb, Bx(3.4, 0.08, 1.6), 0x3a4048, M(0, 2.52, 0));
	Put(gb, Bx(3.3, 2.0, 0.04), 0x8fb3c7, M(0, 1.35, -0.62));
	Put(gb, Bx(1.2, 1.8, 0.1), 0xffffff, M(1.9, 1.2, -0.6), 1.1);
	for (int k = 0; k < 2; k++) Put(gb, Bx(2.4, 0.05, 0.3), 0x6b4a2e, M(0, 0.5, -0.35 + k * 0.001));
	return gb;
}
MeshBuf PhoneboothGeo() {
	MeshBuf gb;
	Put(gb, Bx(0.9, 2.3, 0.9), 0x7e8a95, M(0, 1.15, 0));
	Put(gb, Bx(0.95, 0.25, 0.95), 0x2255aa, M(0, 2.2, 0), 3);
	Put(gb, Bx(0.7, 1.4, 0.02), 0x9ec3d8, M(0, 1.2, 0.46));
	return gb;
}
MeshBuf HoopGeo() {
	MeshBuf gb;
	Put(gb, Cyl(0.08, 0.08, 3.1, 8), 0x444444, M(0, 1.55, -0.6));
	Put(gb, Bx(1.8, 1.05, 0.05), 0xf2f2f2, M(0, 3.3, -0.35));
	Put(gb, Geo::Torus(0.23, 0.02, 6, 16), 0xe05a1c, M(0, 3.05, -0.05, kPi / 2, 0, 0));
	Put(gb, Bx(0.05, 0.6, 0.7), 0x444444, M(0, 3.1, -0.55, 0.5, 0, 0));
	return gb;
}
MeshBuf FountainGeo() {
	MeshBuf gb; const uint32_t stone = 0xb7ada0;
	Put(gb, Cyl(5, 5.2, 0.7, 28), stone, M(0, 0.35, 0));
	Put(gb, Cyl(4.6, 4.6, 0.1, 28), 0x2a6f86, M(0, 0.62, 0));
	Put(gb, Cyl(0.6, 0.9, 2.2, 12), stone, M(0, 1.3, 0));
	Put(gb, Cyl(1.8, 1.4, 0.35, 16), stone, M(0, 2.4, 0));
	Put(gb, Cyl(0.3, 0.4, 1.2, 10), stone, M(0, 3.1, 0));
	return gb;
}

// Palm: curved tapered trunk + frond cards
PropTemplate PalmGeos(int variant) {
	RNG r(100 + variant);
	const double height = 9 + variant * 1.3;
	const int segs = 14, radial = 7;
	const double bend = r.Range(0.4, 1.2);
	auto center = [&](double t) { return V2{ std::sin(t * 1.4) * bend * t, 0 }; };
	MeshBuf trunk;
	trunk.ColorHex(0x8a7458);
	std::vector<std::vector<uint32_t>> ring(segs + 1);
	for (int i = 0; i <= segs; i++) {
		const double t = (double)i / segs, y = t * height;
		const double cx = center(t).x;
		const double k = 1.25 - t * 0.5 + (std::sin(y * 9) > 0.6 ? 0.06 : 0);
		for (int j = 0; j <= radial; j++) {
			const double a = (double)j / radial * kTau, s = std::sin(a), c = std::cos(a);
			ring[i].push_back(trunk.V(cx + c * 0.22 * k, y, s * 0.22 * k, c, 0, s, (double)j / radial, t));
		}
	}
	for (int i = 0; i < segs; i++) for (int j = 0; j < radial; j++) trunk.QuadAuto(ring[i][j], ring[i][j + 1], ring[i + 1][j + 1], ring[i + 1][j]);
	const double topX = center(1).x, topY = height, topZ = 0;
	Put(trunk, Sph(0.45, 8, 6), 0x6b5a3c, M(topX, topY - 0.1, topZ));
	MeshBuf fr;
	fr.Rough(0.8);
	const int n = 11;
	for (int k = 0; k < n; k++) {
		const double ang = ((double)k / n) * kTau + r.Range(-0.15, 0.15);
		const double len = r.Range(3.6, 4.6);
		const double droop = r.Range(0.5, 1.1);
		const double w = 1.5;
		const int seg = 5;
		const double dirx = std::cos(ang), dirz = std::sin(ang), sx = -dirz, sz = dirx;
		const double shade = r.Range(0.85, 1.1);
		fr.Color(0.16 * shade, 0.3 * shade, 0.08 * shade);
		double pL[3] = { 0, 0, 0 }, pR[3] = { 0, 0, 0 };
		for (int s = 0; s <= seg; s++) {
			const double t = (double)s / seg, d = t * len;
			const double y = topY + 0.3 + std::sin(t * kPi * 0.6) * 0.8 - t * t * droop * 2.2;
			const double cx = topX + dirx * d, cz = topZ + dirz * d, hw = w * 0.5;
			const double L[3] = { cx - sx * hw, y + 0.05, cz - sz * hw }, R[3] = { cx + sx * hw, y + 0.05, cz + sz * hw };
			if (s > 0) {
				const uint32_t a = fr.V(pL[0], pL[1], pL[2], 0, 1, 0, 0, (double)(s - 1) / seg), b = fr.V(pR[0], pR[1], pR[2], 0, 1, 0, 1, (double)(s - 1) / seg);
				const uint32_t c = fr.V(R[0], R[1], R[2], 0, 1, 0, 1, t), d2 = fr.V(L[0], L[1], L[2], 0, 1, 0, 0, t);
				fr.Tri(a, b, c); fr.Tri(a, c, d2);
			}
			for (int q = 0; q < 3; q++) { pL[q] = L[q]; pR[q] = R[q]; }
		}
	}
	PropTemplate p; p.mesh = trunk; p.leaves = fr; p.frondMat = true; p.r = 0.32; p.h = height;
	return p;
}

PropTemplate TreeGeos(int variant) {
	RNG r(300 + variant);
	MeshBuf trunk;
	const double h = 3 + variant * 0.6;
	Put(trunk, Cyl(0.16, 0.26, h, 7), 0x5a4330, M(0, h / 2, 0));
	Put(trunk, Cyl(0.07, 0.1, 1.8, 5), 0x5a4330, M(0.4, h - 0.2, 0, 0, 0, -0.6));
	Put(trunk, Cyl(0.07, 0.1, 1.6, 5), 0x5a4330, M(-0.35, h - 0.3, 0.2, 0.3, 0, 0.6));
	MeshBuf leaves;
	leaves.Rough(0.85);
	const int blobs = 5 + variant;
	for (int k = 0; k < blobs; k++) {
		MeshBuf g = Geo::Icosahedron(r.Range(1.3, 2.0), 1);
		const double salt = k * 13.7;
		for (size_t i = 0; i < g.Count(); i++) {
			const double x = g.P[i * 3], y = g.P[i * 3 + 1], z = g.P[i * 3 + 2];
			const double s = 1 + (PosHash(x + salt, y, z) - 0.5) * 0.25;
			g.P[i * 3] = (float)(x * s); g.P[i * 3 + 1] = (float)(y * s * 0.85); g.P[i * 3 + 2] = (float)(z * s);
		}
		Geo::ComputeFlatNormals(g);
		const double hh = 0.26 + r.Range(-0.04, 0.03);
		const double ll = 0.22 + r.Range(-0.04, 0.06);
		Hsl(leaves, hh, 0.45, ll);
		const double px = r.Range(-1.3, 1.3);
		const double py = h + 0.8 + r.Range(-0.3, 1.4);
		const double pz = r.Range(-1.3, 1.3);
		leaves.Add(g, M(px, py, pz));
	}
	PropTemplate p; p.mesh = trunk; p.leaves = leaves; p.r = 0.3; p.h = h + 3;
	return p;
}

MeshBuf SiloGeo() {
	MeshBuf gb;
	Put(gb, Cyl(3.2, 3.2, 16, 16), 0xb9bcc0, M(0, 8, 0));
	for (int k = 1; k < 8; k++) Put(gb, Cyl(3.26, 3.26, 0.12, 16), 0x8e9296, M(0, k * 2, 0));
	Put(gb, Geo::Sphere(3.2, 16, 8, 0, kTau, 0, kPi / 2), 0xa9acb0, M(0, 16, 0));
	Put(gb, Bx(0.5, 16, 0.2), 0x6d6f72, M(0, 8, 3.3));
	return gb;
}
MeshBuf PumpGeo() {
	MeshBuf gb;
	Put(gb, Bx(0.8, 0.25, 1.4), 0x9a9a9a, M(0, 0.12, 0));
	Put(gb, Bx(0.55, 1.7, 0.8), 0xd8d8d8, M(0, 1.05, 0));
	Put(gb, Bx(0.57, 0.35, 0.82), 0xc0281c, M(0, 1.55, 0), 0.8);
	Put(gb, Bx(0.1, 0.4, 0.25), 0x222222, M(0.33, 1.0, 0.2));
	return gb;
}
MeshBuf PostGeo(double h) { MeshBuf gb; Put(gb, Bx(0.35, h, 0.35), 0xd9d9d9, M(0, h / 2, 0)); return gb; }
MeshBuf BoatGeo() {
	MeshBuf gb;
	auto hull = [&](double w, double len, double y0, double h, uint32_t col) {
		std::vector<V2> o;
		o.push_back({ -w, -len / 2 }); o.push_back({ w, -len / 2 }); o.push_back({ w * 1.02, len * 0.18 });
		for (int k = 1; k <= 6; k++) { const double t = k / 6.0; const double ax = w * 1.02, az = len * 0.18, cx = w * 0.9, cz = len * 0.42, bx = 0, bz = len / 2; o.push_back({ (1 - t) * (1 - t) * ax + 2 * (1 - t) * t * cx + t * t * bx, (1 - t) * (1 - t) * az + 2 * (1 - t) * t * cz + t * t * bz }); }
		for (int k = 1; k <= 6; k++) { const double t = k / 6.0; const double ax = 0, az = len / 2, cx = -w * 0.9, cz = len * 0.42, bx = -w * 1.02, bz = len * 0.18; o.push_back({ (1 - t) * (1 - t) * ax + 2 * (1 - t) * t * cx + t * t * bx, (1 - t) * (1 - t) * az + 2 * (1 - t) * t * cz + t * t * bz }); }
		Put(gb, ExtrudeY(o, y0, h), col, Mat4::Identity());
	};
	hull(1.15, 8.5, 0, 0.65, 0xa8322a);
	hull(1.3, 9.0, 0.65, 0.75, 0xf2f0ea);
	Put(gb, Bx(2.5, 0.08, 7.6), 0x8a6a48, M(0, 1.36, -0.3));
	Put(gb, Bx(1.7, 1.7, 2.0), 0xf5f5f0, M(0, 2.25, 0.6));
	Put(gb, Bx(1.75, 0.1, 2.2), 0x2d4e6e, M(0, 3.12, 0.6));
	Put(gb, Bx(1.72, 0.5, 0.05), 0x1a2630, M(0, 2.6, 1.61));
	Put(gb, Cyl(0.06, 0.07, 5.5, 6), 0xcccccc, M(0, 3.8, -1.2));
	Put(gb, Cyl(0.04, 0.04, 3.2, 5), 0xcccccc, M(0, 4.2, -2.6, kPi / 2 - 0.3, 0, 0));
	Put(gb, Bx(0.4, 0.4, 0.4), 0xff7a1a, M(0.9, 1.6, -3.2));
	Put(gb, Bx(0.4, 0.4, 0.4), 0xff7a1a, M(-0.9, 1.6, -2.6));
	return gb;
}
MeshBuf CrossbuckGeo() {
	MeshBuf gb;
	Put(gb, Cyl(0.07, 0.08, 4.2, 8), 0xdedede, M(0, 2.1, 0));
	for (double a : { 0.75, -0.75 }) Put(gb, Bx(1.9, 0.26, 0.04), 0xf2f2f2, M(0, 3.7, 0.06, 0, 0, a));
	for (double a : { 0.75, -0.75 }) Put(gb, Bx(1.95, 0.05, 0.03), 0xb01b1b, M(0, 3.7, 0.075, 0, 0, a));
	Put(gb, Bx(1.3, 0.1, 0.1), 0x222222, M(0, 2.65, 0.05));
	for (double x : { -0.55, 0.55 }) {
		MeshBuf lamp; lamp.Add(Cyl(0.2, 0.2, 0.16, 12), M(0, 0, 0, kPi / 2, 0, 0));
		Put(gb, lamp, 0x151515, M(x, 2.65, 0.12));
		MeshBuf lens; lens.Add(Cyl(0.14, 0.14, 0.05, 12), M(0, 0, 0, kPi / 2, 0, 0));
		Put(gb, lens, 0x8a1010, M(x, 2.65, 0.21), 4.8);
	}
	Put(gb, Sph(0.14, 8, 6), 0x303030, M(0, 4.35, 0));
	return gb;
}
MeshBuf HaybaleGeo() { MeshBuf gb; Put(gb, Cyl(0.75, 0.75, 1.2, 14), 0xc9a54a, M(0, 0.75, 0, 0, 0, kPi / 2)); return gb; }
MeshBuf WindsockGeo() {
	MeshBuf gb;
	Put(gb, Cyl(0.06, 0.08, 6, 6), 0xdddddd, M(0, 3, 0));
	Put(gb, Geo::Cylinder(0.35, 0.18, 2.2, 8, 1, true), 0xff6a00, M(1.1, 5.8, 0, 0, 0, kPi / 2 + 0.25));
	return gb;
}
MeshBuf FueltankGeo() {
	MeshBuf gb;
	Put(gb, Cyl(8, 8, 9, 20), 0xe6e6e0, M(0, 4.5, 0));
	Put(gb, Cyl(8.1, 8.1, 0.3, 20), 0x9a9a92, M(0, 9.1, 0));
	Put(gb, Bx(0.4, 9.5, 0.4), 0x777777, M(0, 4.75, 8.1));
	return gb;
}
MeshBuf RadarGeo() {
	MeshBuf gb;
	Put(gb, Bx(5, 10, 5), 0x9a9f94, M(0, 5, 0));
	Put(gb, Geo::Sphere(5.5, 18, 12), 0xf2f2ee, M(0, 12.5, 0));
	Put(gb, Bx(0.4, 3, 0.4), 0x666666, M(3.5, 11.5, 3.5));
	Put(gb, Sph(0.3), 0xff2a1a, M(0, 18.2, 0), 4);
	return gb;
}
MeshBuf BoothbarGeo() {
	MeshBuf gb;
	Put(gb, Bx(3, 2.8, 3), 0xe9e4d6, M(0, 1.4, 0));
	Put(gb, Bx(3.3, 0.25, 3.3), 0x4a5a3a, M(0, 2.9, 0));
	Put(gb, Bx(2.6, 1.0, 0.05), 0x9fd4ff, M(0, 1.8, 1.52));
	Put(gb, Bx(0.3, 1.1, 0.3), 0x333333, M(0, 0.55, -2));
	Put(gb, Bx(0.15, 0.15, 9), 0xd82020, M(0, 1.05, -6.5));
	for (int k = 0; k < 4; k++) Put(gb, Bx(0.16, 0.16, 1.0), 0xffffff, M(0, 1.05, -3.5 - k * 2.2));
	return gb;
}
MeshBuf SandbagsGeo() {
	MeshBuf gb;
	RNG r(5);
	for (int row = 0; row < 3; row++) for (int k = 0; k < 9; k++) {
		const MeshBuf g = Geo::Capsule(0.22, 0.55, 3, 6);
		Hsl(gb, 0.12, 0.25, 0.48 + r.Range(-0.04, 0.04));
		gb.Add(g, M(-4 + k * 1.0 + (row % 2) * 0.5, 0.22 + row * 0.38, 0, 0, 0, kPi / 2));
	}
	return gb;
}
MeshBuf PowerpoleGeo() {
	MeshBuf gb; const uint32_t wood = 0x5b4633;
	Put(gb, Cyl(0.14, 0.2, 11, 7), wood, M(0, 5.5, 0));
	Put(gb, Bx(3.2, 0.2, 0.2), wood, M(0, 10.3, 0));
	for (double x : { -1.4, 0.0, 1.4 }) Put(gb, Cyl(0.08, 0.08, 0.3, 6), 0x7a8a8a, M(x, 10.55, 0));
	Put(gb, Cyl(0.35, 0.35, 0.9, 8), 0x6f7478, M(0.35, 8.6, 0));
	return gb;
}

PropTemplate T(MeshBuf m, double r, double h, bool breakable) { PropTemplate p; p.mesh = std::move(m); p.r = r; p.h = h; p.breakable = breakable; return p; }

} // namespace

MeshBuf ContainerGeo() {
	MeshBuf gb;
	Put(gb, Bx(2.44, 2.59, 6.06), 0xffffff, M(0, 1.295, 0));
	for (int k = -9; k <= 9; k++) Put(gb, Bx(2.48, 2.4, 0.08), 0xe8e8e8, M(0, 1.3, k * 0.32));
	Put(gb, Bx(2.3, 2.4, 0.05), 0xcccccc, M(0, 1.3, 3.05));
	return gb;
}

std::map<std::string, PropTemplate> BuildPropTemplates() {
	std::map<std::string, PropTemplate> d;
	d["streetlight"] = T(StreetlightGeo(), 0.18, 8, true);
	d["trafficlight"] = T(TrafficLightGeo(), 0.18, 5, true);
	d["hydrant"] = T(HydrantGeo(), 0.25, 0.8, true);
	d["trashcan"] = T(TrashcanGeo(), 0.35, 1, true);
	d["dumpster"] = T(DumpsterGeo(), 1.0, 1.4, false);
	d["bench"] = T(BenchGeo(), 0.6, 0.9, true);
	d["busstop"] = T(BusstopGeo(), 0.5, 2.6, true);
	d["phonebooth"] = T(PhoneboothGeo(), 0.55, 2.4, true);
	d["hoop"] = T(HoopGeo(), 0.2, 4, false);
	d["fountain"] = T(FountainGeo(), 5.2, 0.7, false);
	d["silo"] = T(SiloGeo(), 3.3, 19, false);
	d["pump"] = T(PumpGeo(), 0.5, 1.9, true);
	d["post"] = T(PostGeo(4.6), 0.25, 4.6, false);
	d["stilt"] = T(PostGeo(7), 0.25, 7, false);
	d["haybale"] = T(HaybaleGeo(), 0.75, 1.5, false);
	d["windsock"] = T(WindsockGeo(), 0.15, 6, true);
	d["fueltank"] = T(FueltankGeo(), 8.1, 9.3, false);
	d["radar"] = T(RadarGeo(), 3.6, 18, false);
	d["boothbar"] = T(BoothbarGeo(), 1.6, 3, false);
	d["sandbags"] = T(SandbagsGeo(), 0.9, 1.1, false);
	d["powerpole"] = T(PowerpoleGeo(), 0.25, 11, true);
	d["boat"] = T(BoatGeo(), 1.4, 3.2, false);
	d["crossbuck"] = T(CrossbuckGeo(), 0.15, 4.3, true);
	for (int v = 0; v < 3; v++) d["palm" + std::to_string(v)] = PalmGeos(v);
	for (int v = 0; v < 2; v++) d["tree" + std::to_string(v)] = TreeGeos(v);
	return d;
}

std::vector<PropInstance> PlaceProps(const CityMap& map) {
	std::vector<PropInstance> out;
	RNG rng(777);
	static const std::set<std::string> known = { "streetlight", "trafficlight", "hydrant", "trashcan", "dumpster", "bench", "busstop", "phonebooth", "hoop", "fountain", "silo", "pump", "post", "stilt",
		"haybale", "windsock", "fueltank", "radar", "boothbar", "sandbags", "powerpole", "boat", "crossbuck" };
	for (const Prop& p : map.props) {
		std::string type = p.type;
		if (type == "palm") type = "palm" + std::to_string(rng.Int(0, 2));
		else if (type == "tree") type = "tree" + std::to_string(rng.Int(0, 1));
		else if (!known.count(type)) continue;
		PropInstance inst;
		inst.type = type; inst.x = p.x; inst.z = p.z;
		inst.y = IsSet(p.y) ? p.y : map.GroundHeight(p.x, p.z);
		inst.rot = p.rot; inst.scale = p.scale != 0 ? p.scale : 1;
		if (type == "trafficlight") inst.phase = CellPhase(map.NearestX(p.x), map.NearestZ(p.z));
		out.push_back(inst);
	}
	return out;
}

std::map<std::string, VegTemplate> BuildVegTemplates() {
	std::map<std::string, VegTemplate> V;
	auto vput = [](MeshBuf& g, const MeshBuf& geo, uint32_t color, const Mat4& m) { g.ColorHex(color); g.Add(geo, m); };
	{ // pine
		VegTemplate t; t.nearMesh.Rough(0.9); t.farMesh.Rough(0.9);
		vput(t.nearMesh, Cyl(0.2, 0.36, 4, 6), 0x4a3526, M(0, 2, 0));
		const double tiers[4][3] = { { 3.4, 5.2, 3.2 }, { 2.8, 4.6, 5.8 }, { 2.1, 4.0, 8.2 }, { 1.3, 3.2, 10.4 } };
		for (int k = 0; k < 4; k++) {
			MeshBuf g = Geo::Cone(tiers[k][0], tiers[k][1], 8, 1);
			for (size_t i = 0; i < g.Count(); i++) {
				if (g.P[i * 3 + 1] >= 0) continue;
				const double a = std::atan2(g.P[i * 3 + 2], g.P[i * 3]);
				const double s = 1 + 0.12 * std::sin(a * 5 + k);
				g.P[i * 3] = (float)(g.P[i * 3] * s); g.P[i * 3 + 2] = (float)(g.P[i * 3 + 2] * s);
			}
			ComputeSmoothNormals(g);
			Hsl(t.nearMesh, 0.33, 0.42, 0.16 + k * 0.025);
			t.nearMesh.Add(g, M(0, tiers[k][2], 0));
		}
		vput(t.farMesh, Cyl(0.25, 0.3, 3, 3), 0x4a3526, M(0, 1.5, 0));
		vput(t.farMesh, Geo::Cone(3.2, 10.5, 5, 1), 0x264a1f, M(0, 7.2, 0));
		t.r = 0.45; t.h = 12; t.nearD = 260; t.farD = 2200;
		V["pine"] = t;
	}
	{ // oak
		const PropTemplate g = TreeGeos(1);
		VegTemplate t; t.nearMesh = g.mesh; t.nearLeaves = g.leaves; t.farMesh.Rough(0.9);
		vput(t.farMesh, Cyl(0.2, 0.25, 3.4, 3), 0x5a4330, M(0, 1.7, 0));
		vput(t.farMesh, Geo::Icosahedron(2.6, 0), 0x3d5a23, M(0, 4.8, 0));
		t.r = 0.4; t.h = 6; t.nearD = 220; t.farD = 1700;
		V["oak"] = t;
	}
	{ // cactus
		VegTemplate t; t.nearMesh.Rough(0.8); t.farMesh.Rough(0.8);
		const uint32_t green = 0x3f6b35;
		vput(t.nearMesh, Geo::Capsule(0.32, 4.6, 4, 8), green, M(0, 2.6, 0));
		vput(t.nearMesh, Geo::Capsule(0.22, 1.3, 3, 7), green, M(0.75, 2.2, 0, 0, 0, kPi / 2));
		vput(t.nearMesh, Geo::Capsule(0.22, 1.6, 3, 7), green, M(1.1, 3.1, 0));
		vput(t.nearMesh, Geo::Capsule(0.2, 1.0, 3, 7), green, M(-0.6, 3.0, 0, 0, 0, kPi / 2));
		vput(t.nearMesh, Geo::Capsule(0.2, 1.2, 3, 7), green, M(-0.95, 3.7, 0));
		vput(t.farMesh, Cyl(0.35, 0.35, 5, 4), green, M(0, 2.5, 0));
		t.r = 0.4; t.h = 5; t.nearD = 180; t.farD = 1200;
		V["cactus"] = t;
	}
	{ // rock
		VegTemplate t; t.nearMesh.Rough(0.95); t.farMesh.Rough(0.95);
		MeshBuf g = Geo::Icosahedron(1.4, 1);
		for (size_t i = 0; i < g.Count(); i++) {
			const double x = g.P[i * 3], y = g.P[i * 3 + 1], z = g.P[i * 3 + 2];
			const double s = 1 + (PosHash(x, y, z) - 0.5) * 0.45;
			g.P[i * 3] = (float)(x * s * 1.2); g.P[i * 3 + 1] = (float)(y * s * 0.75); g.P[i * 3 + 2] = (float)(z * s);
		}
		Geo::ComputeFlatNormals(g);
		vput(t.nearMesh, g, 0x8a7560, M(0, 0.6, 0));
		vput(t.farMesh, Geo::Icosahedron(1.4, 0), 0x8a7560, M(0, 0.6, 0, 0, 0, 0, 1.2, 0.75, 1));
		t.r = 1.3; t.h = 1.4; t.nearD = 200; t.farD = 1500;
		V["rock"] = t;
	}
	{ // dead tree
		VegTemplate t; t.nearMesh.Rough(0.95); t.farMesh.Rough(0.95);
		const uint32_t wood = 0x6e5a48;
		vput(t.nearMesh, Cyl(0.14, 0.26, 4.2, 6), wood, M(0, 2.1, 0));
		vput(t.nearMesh, Cyl(0.05, 0.1, 2.2, 5), wood, M(0.6, 3.7, 0, 0, 0, -0.8));
		vput(t.nearMesh, Cyl(0.05, 0.09, 1.8, 5), wood, M(-0.5, 3.4, 0.2, 0.3, 0, 0.9));
		vput(t.nearMesh, Cyl(0.04, 0.08, 1.6, 5), wood, M(0.1, 4.5, -0.5, -0.7, 0, 0.2));
		vput(t.farMesh, Cyl(0.15, 0.2, 4.2, 3), wood, M(0, 2.1, 0));
		t.r = 0.3; t.h = 4; t.nearD = 180; t.farD = 1000;
		V["deadtree"] = t;
	}
	{ // bush
		VegTemplate t; t.nearMesh.Rough(0.9); t.farMesh.Rough(0.9);
		RNG r(88);
		for (int k = 0; k < 4; k++) {
			const MeshBuf g = Geo::Icosahedron(r.Range(0.6, 0.95), 0);
			const double hh = 0.22 + r.Range(-0.03, 0.04);
			const double ll = 0.2 + r.Range(0, 0.06);
			Hsl(t.nearMesh, hh, 0.35, ll);
			const double px = r.Range(-0.6, 0.6);
			const double py = 0.55 + r.Range(0, 0.3);
			const double pz = r.Range(-0.6, 0.6);
			t.nearMesh.Add(g, M(px, py, pz));
		}
		vput(t.farMesh, Geo::Icosahedron(1.0, 0), 0x3f5226, M(0, 0.6, 0));
		t.r = 0; t.h = 1.2; t.nearD = 140; t.farD = 600;
		V["bush"] = t;
	}
	{ // palm
		const PropTemplate g = PalmGeos(0);
		VegTemplate t; t.nearMesh = g.mesh; t.nearLeaves = g.leaves; t.frond = true; t.farMesh = g.mesh;
		t.r = 0.35; t.h = 9; t.nearD = 400; t.farD = 1200;
		V["palm"] = t;
	}
	return V;
}

// hexagonal prisms standing in for props, trunks, cacti and boulders in the collision world
std::map<std::pair<int, int>, MeshBuf> BuildPropColliders(const CityMap& map, const std::vector<PropInstance>& props, const std::map<std::string, PropTemplate>& defs) {
	std::map<std::pair<int, int>, MeshBuf> out;
	const double CH = 400;
	auto prism = [&](double x, double z, double r, double y0, double y1) {
		if (r <= 0) return;
		MeshBuf& g = out[{ (int)std::floor(x / CH), (int)std::floor(z / CH) }];
		const int n = 6;
		std::vector<uint32_t> bot, top;
		for (int k = 0; k < n; k++) {
			const double a = (double)k / n * kTau, c = std::cos(a), s = std::sin(a);
			bot.push_back(g.V(x + c * r, y0, z + s * r, c, 0, s));
			top.push_back(g.V(x + c * r, y1, z + s * r, c, 0, s));
		}
		for (int k = 0; k < n; k++) g.QuadAuto(bot[k], bot[(k + 1) % n], top[(k + 1) % n], top[k]);
		const uint32_t c = g.V(x, y1, z, 0, 1, 0);
		for (int k = 0; k < n; k++) g.Tri(c, top[(k + 1) % n], top[k]);
	};
	for (const PropInstance& p : props) {
		auto it = defs.find(p.type);
		if (it == defs.end()) continue;
		const bool multi = p.type == "boothbar" || p.type == "sandbags";
		prism(p.x, p.z, multi ? 1.4 : it->second.r, p.y - 0.5, p.y + it->second.h);
	}
	const std::map<std::string, std::pair<double, double>> veg = { { "pine", { 0.45, 12 } }, { "oak", { 0.4, 6 } }, { "cactus", { 0.4, 5 } }, { "rock", { 1.3, 1.4 } }, { "deadtree", { 0.3, 4 } }, { "palm", { 0.35, 9 } } };
	const Vegetation& V = map.vegetation;
	const std::pair<const char*, const std::vector<float>*> lists[] = { { "pine", &V.pine }, { "oak", &V.oak }, { "cactus", &V.cactus }, { "rock", &V.rock }, { "deadtree", &V.deadtree }, { "palm", &V.palm } };
	for (const auto& L : lists) {
		const auto K = veg.at(L.first);
		const std::vector<float>& a = *L.second;
		const bool rock = std::string(L.first) == "rock";
		for (size_t i = 0; i + 4 < a.size(); i += 5) {
			const double sc = a[i + 4];
			prism(a[i], a[i + 2], K.first * (rock ? sc : Min(1.3, sc)), a[i + 1] - 1, a[i + 1] + K.second * sc);
		}
	}
	return out;
}

} // namespace atg
