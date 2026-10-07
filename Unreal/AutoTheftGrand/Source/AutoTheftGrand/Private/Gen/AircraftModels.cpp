#include "AircraftModels.h"
#include "ShapeGeo.h"
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace atg {

namespace {
using C3 = std::array<double, 3>;
using Row = std::vector<double>;
using ColorFn = std::function<C3(double, double, double, double)>;

double AirSrgb(double c) { return c < 0.04045 ? c * 0.0773993808 : std::pow(c * 0.9478672986 + 0.0521327014, 2.4); }
C3 AirHex(uint32_t h) { return { AirSrgb(((h >> 16) & 255) / 255.0), AirSrgb(((h >> 8) & 255) / 255.0), AirSrgb((h & 255) / 255.0) }; }
C3 AirMix(const C3& a, const C3& b, double t) { return { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t }; }
void SetColor(MeshBuf& gb, const C3& c) { gb.Color(c[0], c[1], c[2]); }

// ---------- three.js matrices
Mat4 AirM(double x = 0, double y = 0, double z = 0, double rx = 0, double ry = 0, double rz = 0, double sx = 1, double sy = 1, double sz = 1) { return Mat4::Compose(x, y, z, rx, ry, rz, sx, sy, sz); }
Mat4 AirRotX(double a) { const double c = std::cos(a), s = std::sin(a); Mat4 r = Mat4::Identity(); r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r; }
Mat4 AirRotY(double a) { const double c = std::cos(a), s = std::sin(a); Mat4 r = Mat4::Identity(); r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r; }
Mat4 AirRotZ(double a) { const double c = std::cos(a), s = std::sin(a); Mat4 r = Mat4::Identity(); r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r; }
Mat4 AirTranslate(double x, double y, double z) { Mat4 r = Mat4::Identity(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
Mat4 AirScale(double x, double y, double z) { Mat4 r = Mat4::Identity(); r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
struct AirQuat { double x, y, z, w; };
AirQuat AirQuatFromUnitVectors(double fx, double fy, double fz, double tx, double ty, double tz) {
	double r = fx * tx + fy * ty + fz * tz + 1;
	AirQuat q;
	if (r < 1e-8) {
		r = 0;
		if (std::fabs(fx) > std::fabs(fz)) q = { -fy, fx, 0, r }; else q = { 0, -fz, fy, r };
	} else q = { fy * tz - fz * ty, fz * tx - fx * tz, fx * ty - fy * tx, r };
	double l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	if (l == 0) return { 0, 0, 0, 1 };
	l = 1 / l;
	return { q.x * l, q.y * l, q.z * l, q.w * l };
}
Mat4 AirCompose(double px, double py, double pz, const AirQuat& q) {
	const double x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
	const double xx = q.x * x2, xy = q.x * y2, xz = q.x * z2, yy = q.y * y2, yz = q.y * z2, zz = q.z * z2, wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
	Mat4 r;
	r.m[0] = 1 - (yy + zz); r.m[1] = xy + wz; r.m[2] = xz - wy; r.m[3] = 0;
	r.m[4] = xy - wz; r.m[5] = 1 - (xx + zz); r.m[6] = yz + wx; r.m[7] = 0;
	r.m[8] = xz + wy; r.m[9] = yz - wx; r.m[10] = 1 - (xx + yy); r.m[11] = 0;
	r.m[12] = px; r.m[13] = py; r.m[14] = pz; r.m[15] = 1;
	return r;
}

// ---------- a geometry with per-vertex colours (the lofts)
struct ColGeo { MeshBuf g; std::vector<float> col; };

// GeoBuilder.addGeometry: toNonIndexed, the matrix on positions and (normalised) normals, colours times the current one
void AddGeo(MeshBuf& gb, const MeshBuf& geo, const Mat4& m, const std::vector<float>* col = nullptr) {
	const float saved[3] = { gb.cur[1][0], gb.cur[1][1], gb.cur[2][0] };
	auto one = [&](uint32_t i) {
		double x = geo.P[i * 3], y = geo.P[i * 3 + 1], z = geo.P[i * 3 + 2], nx = geo.N[i * 3], ny = geo.N[i * 3 + 1], nz = geo.N[i * 3 + 2];
		m.Apply(x, y, z); m.ApplyNormal(nx, ny, nz);
		if (col) gb.Color(saved[0] * (*col)[i * 3], saved[1] * (*col)[i * 3 + 1], saved[2] * (*col)[i * 3 + 2]);
		const double u = geo.C[0].empty() ? 0 : geo.C[0][i * 2], v = geo.C[0].empty() ? 0 : geo.C[0][i * 2 + 1];
		gb.V(x, y, z, nx, ny, nz, u, v);
	};
	if (!geo.I.empty()) { const uint32_t base = (uint32_t)gb.Count(); for (uint32_t i : geo.I) one(i); for (size_t k = 0; k < geo.I.size(); k++) gb.I.push_back(base + (uint32_t)k); }
	else { const uint32_t base = (uint32_t)gb.Count(); for (uint32_t i = 0; i < geo.Count(); i++) one(i); for (size_t k = 0; k < geo.Count(); k++) gb.I.push_back(base + (uint32_t)k); }
	if (col) gb.Color(saved[0], saved[1], saved[2]);
}
void AddGeo(MeshBuf& gb, const ColGeo& g, const Mat4& m) { AddGeo(gb, g.g, m, g.col.empty() ? nullptr : &g.col); }

// Catmull-Rom resampling of station arrays [s, ...values] (s itself interpolated linearly)
std::vector<Row> Smooth(const std::vector<Row>& st, double step) {
	std::vector<Row> out;
	const int n0 = (int)st.size();
	for (int i = 0; i < n0 - 1; i++) {
		const Row& p0 = st[std::max(0, i - 1)]; const Row& p1 = st[i]; const Row& p2 = st[i + 1]; const Row& p3 = st[std::min(n0 - 1, i + 2)];
		const int n = std::max(1, (int)std::ceil(std::fabs(p2[0] - p1[0]) / step));
		for (int k = 0; k < n; k++) {
			const double t = (double)k / n, t2 = t * t, t3 = t2 * t;
			Row r(p1.size());
			for (size_t j = 0; j < p1.size(); j++) r[j] = j == 0 ? p1[0] + (p2[0] - p1[0]) * t
				: 0.5 * (2 * p1[j] + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t2 + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t3);
			out.push_back(r);
		}
	}
	out.push_back(st.back());
	return out;
}

struct Station { C3 c, a, b; bool foil = false; };

// a closed tube through the stations; ring point = c + a cos t + b sin t (foil: tapered toward the trailing edge)
ColGeo Loft(const std::vector<Station>& stations, int seg, const ColorFn& colorFn) {
	std::vector<double> pos;
	std::vector<uint32_t> idx;
	const int n = (int)stations.size();
	for (const Station& s : stations) for (int k = 0; k < seg; k++) {
		const double t = (double)k / seg * kPi * 2, ca = std::cos(t);
		double sa = std::sin(t);
		if (s.foil) sa *= 0.3 + 0.7 * std::pow((1 + ca) / 2, 0.6);
		for (int q = 0; q < 3; q++) pos.push_back(s.c[q] + s.a[q] * ca + s.b[q] * sa);
	}
	for (int i = 0; i < n - 1; i++) for (int k = 0; k < seg; k++) {
		const uint32_t a = i * seg + k, b = i * seg + (k + 1) % seg, c = (i + 1) * seg + (k + 1) % seg, d = (i + 1) * seg + k;
		idx.insert(idx.end(), { a, b, c, a, c, d });
	}
	for (int pass = 0; pass < 2; pass++) {
		const int i = pass == 0 ? 0 : n - 1;
		const bool first = pass == 0;
		const uint32_t ci = (uint32_t)(pos.size() / 3);
		for (int q = 0; q < 3; q++) pos.push_back(stations[i].c[q]);
		for (int k = 0; k < seg; k++) {
			const uint32_t a = i * seg + k, b = i * seg + (k + 1) % seg;
			if (first) idx.insert(idx.end(), { ci, b, a }); else idx.insert(idx.end(), { ci, a, b });
		}
	}
	ColGeo out;
	MeshBuf& g = out.g;
	for (size_t v = 0; v < pos.size() / 3; v++) g.V(pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2], 0, 0, 0);
	g.C[0].clear(); // (no uvs)
	g.I = idx;
	SmoothNormals(g);
	// make sure faces point outward (the ring direction depends on the station axes)
	const int mi = n / 2, vi = mi * seg + seg / 4;
	const Station& m = stations[mi];
	if (g.N[vi * 3] * (pos[vi * 3] - m.c[0]) + g.N[vi * 3 + 1] * (pos[vi * 3 + 1] - m.c[1]) + g.N[vi * 3 + 2] * (pos[vi * 3 + 2] - m.c[2]) < 0) {
		for (size_t i = 0; i < g.I.size(); i += 3) std::swap(g.I[i + 1], g.I[i + 2]);
		SmoothNormals(g);
	}
	if (colorFn) {
		out.col.resize(pos.size());
		for (size_t i = 0; i < pos.size(); i += 3) { const C3 c = colorFn(pos[i], pos[i + 1], pos[i + 2], g.N[i + 1]); out.col[i] = (float)c[0]; out.col[i + 1] = (float)c[1]; out.col[i + 2] = (float)c[2]; }
	}
	return out;
}

// fuselage along z: [z, cy, rx, ry]
ColGeo Fuselage(const std::vector<Row>& st, int seg = 22, double step = 0.3, const ColorFn& col = nullptr) {
	std::vector<Station> s;
	for (const Row& r : Smooth(st, step)) s.push_back({ { 0, r[1], r[0] }, { std::max(0.001, r[2]), 0, 0 }, { 0, std::max(0.001, r[3]), 0 } });
	return Loft(s, seg, col);
}
// pod along z at an x offset: [z, cy, r]
ColGeo Pod(double x, const std::vector<Row>& st, int seg = 14, const ColorFn& col = nullptr) {
	std::vector<Station> s;
	for (const Row& r : Smooth(st, 0.3)) s.push_back({ { x, r[1], r[0] }, { std::max(0.001, r[2]), 0, 0 }, { 0, std::max(0.001, r[2]), 0 } });
	return Loft(s, seg, col);
}
// wing along x (side = +1 left / -1 right): [x, y, zc, chord, thick]
ColGeo Wing(const std::vector<Row>& st, double side, const ColorFn& col) {
	std::vector<Station> s;
	for (const Row& r : Smooth(st, 0.5)) s.push_back({ { r[0] * side, r[1], r[2] }, { 0, 0, r[3] / 2 }, { 0, r[4] / 2, 0 }, true });
	return Loft(s, 14, col);
}
// fin along y: [y, x, zc, chord, thick]
ColGeo Fin(const std::vector<Row>& st, const ColorFn& col) {
	std::vector<Station> s;
	for (const Row& r : Smooth(st, 0.4)) s.push_back({ { r[1], r[0], r[2] }, { 0, 0, r[3] / 2 }, { r[4] / 2, 0, 0 }, true });
	return Loft(s, 12, col);
}

// along +z, r0 at -z end
MeshBuf Cyl(double r0, double r1, double len, int seg = 12) { MeshBuf g = Geo::Cylinder(r1, r0, len, seg); TransformBuf(g, AirRotX(kPi / 2)); return g; }
void Strut(MeshBuf& gb, const C3& a, const C3& b, double t) {
	const double dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
	const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
	const double nx = dx / len, ny = dy / len, nz = dz / len;
	const AirQuat q = AirQuatFromUnitVectors(0, 1, 0, nx, ny, nz);
	AddGeo(gb, Geo::Cylinder(t, t, len, 6), AirCompose(a[0] + dx * 0.5, a[1] + dy * 0.5, a[2] + dz * 0.5, q));
}
void Wheel(MeshBuf& gb, double x, double y, double z, double r, double w) {
	gb.Color(0.06, 0.06, 0.06);
	AddGeo(gb, Geo::Cylinder(r, r, w, 16), AirM(x, y, z, 0, 0, kPi / 2));
	gb.Color(0.55, 0.56, 0.58);
	AddGeo(gb, Geo::Cylinder(r * 0.55, r * 0.55, w + 0.02, 12), AirM(x, y, z, 0, 0, kPi / 2));
}
// n blades radiating from the hub, spinning about the axis; a little pitch and optional painted tips
MeshBuf Blades(int n, double len, double chord, double thick, char axis = 'y', uint32_t color = 0x1c1c1c, int64_t tip = -1) {
	MeshBuf gb;
	gb.Color(1, 1, 1);
	const C3 c = AirHex(color);
	for (int k = 0; k < n; k++) {
		const double a = (double)k / n * kPi * 2;
		Mat4 rot; MeshBuf geo, tipGeo; Mat4 off, tipOff;
		if (axis == 'x') {
			rot = AirRotX(a);
			geo = Geo::Box(thick, len, chord); off = AirM(0, len / 2, 0, 0, 0.3, 0);
			tipGeo = Geo::Box(thick * 1.2, len * 0.12, chord * 1.04); tipOff = AirM(0, len * 0.94, 0, 0, 0.3, 0);
		} else if (axis == 'y') {
			rot = AirRotY(a);
			geo = Geo::Box(len, thick, chord); off = AirM(len / 2, 0, 0, 0.08, 0, 0);
			tipGeo = Geo::Box(len * 0.08, thick * 1.2, chord * 1.04); tipOff = AirM(len * 0.96, 0, 0, 0.08, 0, 0);
		} else {
			rot = AirRotZ(a);
			geo = Geo::Box(len, chord, thick); off = AirM(len / 2, 0, 0, 0.45, 0, 0);
			tipGeo = Geo::Box(len * 0.12, chord * 1.04, thick * 1.2); tipOff = AirM(len * 0.94, 0, 0, 0.45, 0, 0);
		}
		SetColor(gb, c);
		AddGeo(gb, geo, rot * off);
		if (tip >= 0) { SetColor(gb, AirHex((uint32_t)tip)); AddGeo(gb, tipGeo, rot * tipOff); }
	}
	return gb;
}

void Light(AircraftModel& m, int kind, double x, double y, double z, double r = 0.08) { m.lights.push_back({ { x, y, z }, r, kind }); }

void RotorAssembly(AircraftModel& m, double y, double z, double r, int n, double chord, double tailX, double tailY, double tailZ, double tailR, int tailN) {
	m.hasRotor = true;
	m.rotor.pos = { 0, y, z }; m.rotor.disc = r + 0.1; m.rotor.blade = Blades(n, r, chord, 0.07, 'y', 0x1c1c1c);
	m.rotor.hub = Geo::Cylinder(0.22, 0.28, 0.3, 10); m.rotor.hasHub = true;
	m.tailRotor.pos = { tailX, tailY, tailZ }; m.tailRotor.disc = tailR + 0.05; m.tailRotor.blade = Blades(tailN, tailR, 0.2, 0.04, 'x', 0x1c1c1c, 0xffffff);
}

// ------------------------------------------------------------------ Skipper: high-wing light plane
AircraftModel Skipper(uint32_t color) {
	AircraftModel model; model.cgY = 1.45; model.matte = false;
	const C3 paint = AirHex(color), cream = AirHex(0xf4f1de), win = { 0.05, 0.07, 0.09 };
	MeshBuf body; body.Color(1, 1, 1);
	const ColorFn fcol = [&](double x, double y, double z, double) -> C3 {
		if (z > 0.05 && z < 2.25 && y > 1.62 && y < 2.12) return win;
		if (z > 2.25 && z < 2.7 && y > 1.72 && std::fabs(x) < 0.5) return win;
		if (y > 1.18 && y < 1.34 && z < 2.3) return paint;
		if (z > 3.75) return paint;
		return cream;
	};
	AddGeo(body, Fuselage({ { 4.12, 1.3, 0.02, 0.02 }, { 4.02, 1.3, 0.24, 0.24 }, { 3.75, 1.3, 0.48, 0.5 }, { 3.1, 1.33, 0.58, 0.62 }, { 2.2, 1.4, 0.6, 0.68 }, { 1.0, 1.45, 0.6, 0.72 }, { 0.0, 1.5, 0.56, 0.65 }, { -1.2, 1.6, 0.42, 0.48 }, { -2.6, 1.72, 0.26, 0.3 }, { -3.7, 1.8, 0.13, 0.16 }, { -4.12, 1.82, 0.04, 0.05 } }, 22, 0.3, fcol), Mat4::Identity());
	const ColorFn wcol = [&](double x, double, double, double) -> C3 { return std::fabs(x) > 4.7 ? paint : cream; };
	for (const double s : { 1.0, -1.0 }) {
		AddGeo(body, Wing({ { 0, 2.22, 1.15, 1.55, 0.22 }, { 2.6, 2.25, 1.15, 1.5, 0.19 }, { 5.2, 2.28, 1.12, 1.3, 0.13 }, { 5.5, 2.28, 1.1, 0.9, 0.06 } }, s, wcol), Mat4::Identity());
		AddGeo(body, Wing({ { 0, 1.8, -3.7, 0.95, 0.09 }, { 1.75, 1.8, -3.8, 0.55, 0.05 } }, s, [&](double, double, double, double) { return cream; }), Mat4::Identity());
		SetColor(body, paint);
		AddGeo(body, Geo::Sphere(0.3, 12, 8), AirM(s * 1.18, 0.34, 0.8, 0, 0, 0, 0.55, 0.75, 1.5));
		body.Color(1, 1, 1);
	}
	AddGeo(body, Fin({ { 1.7, 0, -3.35, 1.75, 0.12 }, { 2.2, 0, -3.6, 1.3, 0.1 }, { 2.95, 0, -3.95, 0.72, 0.06 } }, [&](double, double y, double, double) { return y > 2.5 ? paint : cream; }), Mat4::Identity());
	MeshBuf trim; trim.Color(1, 1, 1);
	trim.Color(0.72, 0.72, 0.74);
	for (const double s : { 1.0, -1.0 }) Strut(trim, { s * 0.52, 0.95, 1.25 }, { s * 2.7, 2.18, 1.2 }, 0.035);
	trim.Color(0.2, 0.2, 0.22);
	Strut(trim, { 0, 0.85, 3.3 }, { 0, 0.28, 3.35 }, 0.04);
	for (const double s : { 1.0, -1.0 }) Strut(trim, { s * 0.4, 0.85, 0.85 }, { s * 1.12, 0.32, 0.8 }, 0.045);
	Wheel(trim, 0, 0.26, 3.35, 0.26, 0.12);
	for (const double s : { 1.0, -1.0 }) Wheel(trim, s * 1.18, 0.3, 0.8, 0.3, 0.14);
	model.props.push_back({ { 0, 1.3, 4.18 }, 'z', 1, 1.0, Blades(2, 0.95, 0.14, 0.04, 'z', 0x1a1a1a, 0xffe14d) });
	Light(model, 0, 5.52, 2.28, 1.1);
	Light(model, 1, -5.52, 2.28, 1.1);
	Light(model, 2, 0, 2.98, -4.0, 0.07);
	model.seats = { { 0.3, 1.02, 1.25 }, { -0.3, 1.02, 1.25 }, { 0.3, 1.02, 0.3 }, { -0.3, 1.02, 0.3 } };
	model.visibleSeats = 0;
	model.doorPos = { 1.35, 0, 1.2 };
	model.parts = { body, trim };
	return model;
}

// ------------------------------------------------------------------ Raptor: twin-tail fighter jet
AircraftModel Raptor(uint32_t color) {
	AircraftModel model; model.cgY = 2.3;
	const C3 base = AirHex(color), under = AirMix(base, { 0.8, 0.82, 0.84 }, 0.35), dark = { 0.12, 0.13, 0.14 }, radome = { 0.32, 0.34, 0.36 };
	const ColorFn col = [&](double x, double y, double z, double ny) -> C3 {
		if (z > 7.3) return radome;
		if (z > 6.0 && z < 7.4 && y > 2.45 && std::fabs(x) < 0.35) return dark;
		return ny < -0.3 ? under : base;
	};
	MeshBuf body; body.Color(1, 1, 1);
	AddGeo(body, Fuselage({ { 8.25, 2.1, 0.02, 0.02 }, { 7.9, 2.12, 0.17, 0.15 }, { 7.2, 2.16, 0.38, 0.32 }, { 6.2, 2.22, 0.56, 0.46 }, { 5.0, 2.28, 0.72, 0.5 }, { 3.8, 2.3, 0.95, 0.54 }, { 2.4, 2.3, 1.38, 0.64 }, { 0.5, 2.3, 1.5, 0.66 }, { -2, 2.3, 1.46, 0.62 }, { -4.5, 2.3, 1.3, 0.58 }, { -6.5, 2.3, 1.08, 0.52 }, { -7.7, 2.3, 0.98, 0.48 }, { -8.1, 2.3, 0.92, 0.44 } }, 24, 0.3, col), Mat4::Identity());
	for (const double s : { 1.0, -1.0 }) {
		AddGeo(body, Wing({ { 1.2, 2.16, -0.7, 7.0, 0.3 }, { 3.4, 2.13, -2.1, 5.0, 0.2 }, { 5.55, 2.1, -3.7, 2.4, 0.09 }, { 5.8, 2.1, -3.8, 1.6, 0.05 } }, s, col), Mat4::Identity());
		AddGeo(body, Wing({ { 0.8, 2.22, -6.8, 2.7, 0.14 }, { 3.3, 2.2, -7.85, 1.1, 0.05 } }, s, col), Mat4::Identity());
		const double cant = 0.42;
		AddGeo(body, Fin({ { 2.72, s * 1.05, -5.9, 3.3, 0.16 }, { 3.6, s * (1.05 + 0.88 * cant), -6.6, 2.3, 0.1 }, { 4.4, s * (1.05 + 1.68 * cant), -7.25, 1.3, 0.06 } }, col), Mat4::Identity());
		SetColor(body, base);
		body.Box(s > 0 ? 1.1 : -1.62, 1.78, 0.2, s > 0 ? 1.62 : -1.1, 2.48, 3.2, true, true, true);
		body.Color(1, 1, 1);
	}
	MeshBuf trim; trim.Color(1, 1, 1);
	for (const double s : { 1.0, -1.0 }) {
		trim.Color(0.03, 0.03, 0.035);
		const double q0[3] = { s > 0 ? 1.62 : -1.1, 1.8, 3.21 }, q1[3] = { s > 0 ? 1.1 : -1.62, 1.8, 3.21 }, q2[3] = { s > 0 ? 1.1 : -1.62, 2.46, 3.21 }, q3[3] = { s > 0 ? 1.62 : -1.1, 2.46, 3.21 }, qn[3] = { 0, 0, 1 };
		trim.Quad(q0, q1, q2, q3, qn);
		trim.Color(0.28, 0.27, 0.26);
		AddGeo(trim, Cyl(0.46, 0.4, 1.1, 16), AirM(s * 0.52, 2.22, -8.2));
		trim.Color(0.05, 0.05, 0.05);
		{ MeshBuf c = Geo::Circle(0.36, 16); TransformBuf(c, AirRotY(kPi)); AddGeo(trim, c, AirM(s * 0.52, 2.22, -8.76)); }
		for (const auto& mxz : std::vector<std::array<double, 2>>{ { 2.6, -1.1 }, { 4.1, -2.5 } }) {
			const double mx = mxz[0], mz = mxz[1];
			trim.Color(0.4, 0.42, 0.44);
			trim.Box(s * mx - 0.04, 1.98, mz - 0.6, s * mx + 0.04, 2.1, mz + 0.6);
			trim.Color(0.92, 0.92, 0.9);
			AddGeo(trim, Cyl(0.09, 0.09, 3.0, 8), AirM(s * mx, 1.88, mz + 0.2));
			trim.Color(0.8, 0.15, 0.1);
			{ MeshBuf c = Geo::Cone(0.09, 0.3, 8); TransformBuf(c, AirRotX(kPi / 2)); AddGeo(trim, c, AirM(s * mx, 1.88, mz + 1.85)); }
		}
	}
	trim.Color(0.1, 0.1, 0.11);
	trim.Box(-0.42, 2.45, 2.4, 0.42, 2.78, 5.6);
	MeshBuf gear; gear.Color(1, 1, 1);
	gear.Color(0.75, 0.76, 0.78);
	Strut(gear, { 0, 1.75, 5.3 }, { 0, 0.34, 5.3 }, 0.07);
	for (const double s : { 1.0, -1.0 }) Strut(gear, { s * 1.1, 1.9, -1.4 }, { s * 1.3, 0.4, -1.6 }, 0.09);
	Wheel(gear, 0, 0.32, 5.3, 0.32, 0.18);
	for (const double s : { 1.0, -1.0 }) Wheel(gear, s * 1.32, 0.4, -1.6, 0.4, 0.28);
	model.hasGear = true; model.gear = gear;
	// the canopy opens as the door
	model.doorAxis = 'x'; model.doorMax = -0.75; model.doorPivot = { 0, 2.86, 1.95 };
	model.hasGlass = true; model.glassInDoor = true; model.glassAt = { 0, -2.86, -1.95 };
	model.glass = Fuselage({ { 6.1, 2.72, 0.04, 0.03 }, { 5.6, 2.76, 0.34, 0.3 }, { 4.6, 2.8, 0.47, 0.45 }, { 3.4, 2.8, 0.46, 0.44 }, { 2.4, 2.75, 0.34, 0.33 }, { 1.95, 2.7, 0.08, 0.1 } }, 18, 0.25).g;
	// afterburners
	for (const double s : { 1.0, -1.0 }) {
		MeshBuf f = Geo::Cylinder(0, 0.36, 3.2, 14, 1, true); TransformBuf(f, AirRotX(-kPi / 2)); TransformBuf(f, AirTranslate(0, 0, -1.6));
		MeshBuf core = Geo::Cylinder(0, 0.22, 1.6, 12, 1, true); TransformBuf(core, AirRotX(-kPi / 2)); TransformBuf(core, AirTranslate(0, 0, -0.8));
		model.flames.push_back({ { s * 0.52, 2.22, -8.7 }, f, false });
		model.flames.push_back({ { s * 0.52, 2.22, -8.7 }, core, true });
	}
	Light(model, 0, 5.8, 2.1, -3.85);
	Light(model, 1, -5.8, 2.1, -3.85);
	Light(model, 2, 0, 1.64, -2.0, 0.07);
	model.seats = { { 0, 2.12, 4.05 }, { 0, 1.6, 0.6 }, { 0, 1.6, -0.4 }, { 0, 1.6, -1.4 } };
	model.visibleSeats = 1;
	model.doorPos = { 2.05, 0, 3.8 };
	model.muzzles = { { 0.9, 2.55, 3.0 } };
	model.pylons = { { 2.6, 1.88, 1.2 }, { -2.6, 1.88, 1.2 }, { 4.1, 1.88, -0.2 }, { -4.1, 1.88, -0.2 } };
	model.parts = { body, trim };
	return model;
}

// ------------------------------------------------------------------ Hercules: four-prop cargo plane
AircraftModel Hercules(uint32_t color) {
	AircraftModel model; model.cgY = 3.4;
	const C3 base = AirHex(color), under = AirMix(base, { 0.55, 0.57, 0.55 }, 0.4), win = { 0.04, 0.05, 0.06 }, nose = { 0.16, 0.17, 0.16 };
	const ColorFn col = [&](double, double y, double z, double ny) -> C3 {
		if (z > 13.9) return nose;
		if (z > 11.7 && z < 13.7 && y > 4.05 && y < 4.95) return win;
		return ny < -0.5 ? under : base;
	};
	MeshBuf body; body.Color(1, 1, 1);
	AddGeo(body, Fuselage({ { 14.5, 3.0, 0.05, 0.05 }, { 14.2, 3.05, 0.75, 0.8 }, { 13.5, 3.2, 1.4, 1.55 }, { 12.3, 3.35, 1.88, 2.02 }, { 10.5, 3.4, 2.1, 2.2 }, { -5, 3.4, 2.1, 2.2 }, { -7.5, 3.7, 1.95, 1.95 }, { -10, 4.4, 1.5, 1.35 }, { -12.5, 5.1, 0.9, 0.75 }, { -14.5, 5.6, 0.3, 0.3 } }, 26, 0.5, col), Mat4::Identity());
	for (const double s : { 1.0, -1.0 }) {
		AddGeo(body, Wing({ { 0, 5.75, 1.2, 4.6, 0.72 }, { 2.2, 5.78, 1.2, 4.6, 0.7 }, { 11, 5.85, 1.0, 3.8, 0.5 }, { 19.5, 5.9, 0.8, 2.4, 0.26 }, { 20, 5.9, 0.8, 1.7, 0.1 } }, s, col), Mat4::Identity());
		AddGeo(body, Wing({ { 0, 5.95, -12.5, 3.6, 0.3 }, { 7.8, 5.95, -13.0, 1.8, 0.12 } }, s, col), Mat4::Identity());
		for (const double ex : { 5.6, 10.6 }) AddGeo(body, Pod(s * ex, { { 5.25, 5.45, 0.2 }, { 4.95, 5.45, 0.55 }, { 4.0, 5.4, 0.68 }, { 1.0, 5.35, 0.62 }, { -1.8, 5.5, 0.36 }, { -2.4, 5.6, 0.1 } }, 14, [&](double, double, double, double) { return base; }), Mat4::Identity());
		SetColor(body, base);
		AddGeo(body, Pod(s * 1.95, { { 3.2, 1.55, 0.1 }, { 2.6, 1.5, 0.62 }, { -2.6, 1.5, 0.62 }, { -3.4, 1.7, 0.1 } }, 12), AirM(0, 0, 0, 0, 0, 0, 1, 1, 1));
		body.Color(1, 1, 1);
	}
	AddGeo(body, Fin({ { 5.2, 0, -10.8, 6.5, 0.55 }, { 8, 0, -12.2, 4.3, 0.36 }, { 10.95, 0, -13.3, 2.4, 0.2 } }, col), Mat4::Identity());
	MeshBuf trim; trim.Color(1, 1, 1);
	MeshBuf gear; gear.Color(1, 1, 1);
	for (const double s : { 1.0, -1.0 }) for (const double z : { -1.1, 1.1 }) Wheel(gear, s * 1.95, 0.6, z, 0.6, 0.42);
	for (const double s : { 1.0, -1.0 }) Wheel(gear, s * 0.28, 0.5, 11.2, 0.5, 0.3);
	gear.Color(0.6, 0.6, 0.62);
	Strut(gear, { 0, 1.4, 11.2 }, { 0, 0.5, 11.2 }, 0.1);
	model.hasGear = true; model.gear = gear;
	for (const double s : { 1.0, -1.0 }) for (const double ex : { 5.6, 10.6 }) {
		trim.Color(0.15, 0.15, 0.16);
		{ MeshBuf c = Geo::Cone(0.28, 0.6, 12); TransformBuf(c, AirRotX(kPi / 2)); AddGeo(trim, c, AirM(s * ex, 5.45, 5.55)); }
		model.props.push_back({ { s * ex, 5.45, 5.3 }, 'z', 0.7 * (s > 0 ? 1 : -1), 2.1, Blades(4, 2.0, 0.34, 0.06, 'z', 0x1a1a1a, 0xe8d23a) });
	}
	Light(model, 0, 20, 5.9, 0.8, 0.12);
	Light(model, 1, -20, 5.9, 0.8, 0.12);
	Light(model, 2, 0, 11.1, -13.6, 0.12);
	model.seats = { { 0.7, 3.75, 12.3 }, { -0.7, 3.75, 12.3 }, { 0.6, 2.2, 4 }, { -0.6, 2.2, 4 } };
	model.visibleSeats = 2;
	model.doorPos = { 2.8, 0, 10.3 };
	model.parts = { body, trim };
	return model;
}

// ------------------------------------------------------------------ Warhawk: attack helicopter
AircraftModel Warhawk(uint32_t color) {
	AircraftModel model; model.cgY = 1.8;
	const C3 base = AirHex(color), under = AirMix(base, { 0.3, 0.32, 0.3 }, 0.3);
	const ColorFn col = [&](double, double, double, double ny) -> C3 { return ny < -0.5 ? under : base; };
	MeshBuf body; body.Color(1, 1, 1);
	AddGeo(body, Fuselage({ { 5.7, 1.25, 0.05, 0.05 }, { 5.3, 1.28, 0.3, 0.36 }, { 4.4, 1.36, 0.55, 0.62 }, { 3.0, 1.45, 0.62, 0.62 }, { 1.4, 1.62, 0.72, 0.9 }, { -0.6, 1.85, 0.72, 0.95 }, { -2.0, 2.08, 0.45, 0.6 }, { -3.5, 2.25, 0.26, 0.32 }, { -8.2, 2.4, 0.18, 0.22 }, { -9.35, 2.45, 0.12, 0.15 } }, 20, 0.3, col), Mat4::Identity());
	for (const double s : { 1.0, -1.0 }) {
		AddGeo(body, Pod(s * 0.74, { { 0.9, 2.55, 0.05 }, { 0.6, 2.55, 0.3 }, { -1.2, 2.58, 0.33 }, { -2.0, 2.6, 0.15 } }, 12, col), Mat4::Identity());
		AddGeo(body, Wing({ { 0.55, 1.9, 0.15, 1.3, 0.16 }, { 1.95, 1.95, 0.15, 1.0, 0.1 } }, s, col), Mat4::Identity());
		AddGeo(body, Wing({ { 0, 2.36, -8.3, 0.95, 0.09 }, { 1.7, 2.36, -8.4, 0.7, 0.05 } }, s, col), Mat4::Identity());
	}
	AddGeo(body, Fin({ { 2.2, 0, -8.7, 1.7, 0.16 }, { 3.2, 0, -9.0, 1.3, 0.12 }, { 4.15, 0, -9.3, 0.9, 0.08 } }, col), Mat4::Identity());
	MeshBuf trim; trim.Color(1, 1, 1);
	trim.Color(0.12, 0.13, 0.12);
	AddGeo(trim, Geo::Cylinder(0.16, 0.2, 0.7, 10), AirM(0, 3.0, 0.3));
	for (const double s : { 1.0, -1.0 }) {
		trim.Color(0.25, 0.27, 0.24);
		AddGeo(trim, Cyl(0.25, 0.25, 1.5, 12), AirM(s * 1.3, 1.6, 0.25));
		trim.Color(0.05, 0.05, 0.05);
		AddGeo(trim, Geo::Circle(0.22, 12), AirM(s * 1.3, 1.6, 1.01));
		trim.Color(0.3, 0.3, 0.3);
		trim.Box(s * 1.85 - 0.18, 1.62, -0.4, s * 1.85 + 0.18, 1.72, 0.7);
		trim.Color(0.85, 0.85, 0.82);
		for (const double dx : { -0.1, 0.1 }) AddGeo(trim, Cyl(0.07, 0.07, 1.2, 8), AirM(s * 1.85 + dx, 1.53, 0.15));
		trim.Color(0.08, 0.08, 0.08);
		AddGeo(trim, Cyl(0.16, 0.18, 0.4, 10), AirM(s * 0.95, 2.6, -2.1, 0, s * 0.5, 0));
	}
	trim.Color(0.5, 0.5, 0.5);
	for (const double s : { 1.0, -1.0 }) Strut(trim, { s * 0.55, 1.1, 2.4 }, { s * 1.15, 0.36, 2.45 }, 0.06);
	Strut(trim, { 0, 2.2, -8.4 }, { 0, 0.26, -8.7 }, 0.05);
	for (const double s : { 1.0, -1.0 }) Wheel(trim, s * 1.2, 0.36, 2.45, 0.36, 0.22);
	Wheel(trim, 0, 0.24, -8.7, 0.24, 0.12);
	model.hasGlass = true; model.glassAt = { 0, 0, 0 };
	model.glass = Fuselage({ { 5.05, 1.68, 0.05, 0.04 }, { 4.6, 1.85, 0.42, 0.33 }, { 3.7, 2.0, 0.55, 0.52 }, { 2.4, 2.12, 0.58, 0.56 }, { 1.5, 2.12, 0.46, 0.44 }, { 1.05, 2.05, 0.1, 0.1 } }, 18, 0.25).g;
	// chin gun turret
	MeshBuf gt; gt.Color(1, 1, 1);
	gt.Color(0.2, 0.21, 0.2);
	AddGeo(gt, Geo::Sphere(0.3, 12, 8), Mat4::Identity());
	gt.Color(0.1, 0.1, 0.1);
	AddGeo(gt, Cyl(0.06, 0.05, 1.5, 8), AirM(0, -0.05, 0.85));
	model.hasGun = true; model.gunAt = { 0, 0.86, 4.35 }; model.gun = gt;
	RotorAssembly(model, 3.42, 0.3, 7.3, 4, 0.55, 0.26, 3.6, -9.1, 1.4, 4);
	Light(model, 0, 1.98, 1.95, 0.15);
	Light(model, 1, -1.98, 1.95, 0.15);
	Light(model, 2, 0, 4.2, -9.35, 0.07);
	model.seats = { { 0, 1.28, 2.25 }, { 0, 1.08, 3.55 }, { 0, 1.3, 0 }, { 0, 1.3, -0.8 } };
	model.visibleSeats = 2;
	model.doorPos = { 1.75, 0, 2.6 };
	model.muzzles = { { 0, 0.82, 5.8 } };
	model.pods = { { 1.3, 1.6, 1.1 }, { -1.3, 1.6, 1.1 } };
	model.parts = { body, trim };
	return model;
}

// ------------------------------------------------------------------ Skylark: light helicopter
AircraftModel Skylark(uint32_t color) {
	AircraftModel model; model.cgY = 1.5; model.matte = false;
	const C3 paint = AirHex(color), white = AirHex(0xf2f2f2), win = { 0.04, 0.06, 0.08 };
	const ColorFn col = [&](double x, double y, double z, double) -> C3 {
		if (z > 1.05 && y > 1.12) return win;
		if (z > 0.1 && z < 1.05 && y > 1.62 && std::fabs(x) > 0.3) return win;
		if (y < 1.15 && z > -1.2) return paint;
		if (z < -1.6) return ((long long)std::floor((z + 20) / 1.4) % 2) ? paint : white;
		return white;
	};
	MeshBuf body; body.Color(1, 1, 1);
	AddGeo(body, Fuselage({ { 2.55, 1.22, 0.05, 0.05 }, { 2.35, 1.24, 0.46, 0.5 }, { 1.8, 1.3, 0.76, 0.82 }, { 0.8, 1.42, 0.86, 0.9 }, { -0.3, 1.5, 0.82, 0.86 }, { -1.2, 1.72, 0.55, 0.6 }, { -1.8, 1.96, 0.25, 0.3 }, { -7.2, 2.18, 0.12, 0.14 }, { -8.3, 2.25, 0.08, 0.1 } }, 22, 0.25, col), Mat4::Identity());
	AddGeo(body, Pod(0, { { 0.5, 2.25, 0.05 }, { 0.2, 2.28, 0.45 }, { -1.2, 2.3, 0.42 }, { -1.8, 2.25, 0.1 } }, 14, [&](double, double, double, double) { return white; }), AirM(0, 0, 0, 0, 0, 0, 1.2, 0.8, 1));
	for (const double s : { 1.0, -1.0 }) AddGeo(body, Wing({ { 0, 2.1, -6.0, 0.6, 0.06 }, { 1.0, 2.1, -6.05, 0.45, 0.04 } }, s, [&](double, double, double, double) { return paint; }), Mat4::Identity());
	AddGeo(body, Fin({ { 1.85, 0, -7.9, 0.95, 0.09 }, { 2.95, 0, -8.25, 0.55, 0.05 } }, [&](double, double, double, double) { return paint; }), Mat4::Identity());
	MeshBuf trim; trim.Color(1, 1, 1);
	trim.Color(0.14, 0.14, 0.15);
	AddGeo(trim, Geo::Cylinder(0.12, 0.15, 0.5, 10), AirM(0, 2.62, -0.1));
	trim.Color(0.62, 0.63, 0.65);
	for (const double s : { 1.0, -1.0 }) {
		AddGeo(trim, Cyl(0.05, 0.05, 3.1, 8), AirM(s * 0.98, 0.06, 0.25));
		Strut(trim, { s * 0.98, 0.06, 1.8 }, { s * 0.98, 0.28, 2.15 }, 0.05);
		for (const double z : { 1.0, -0.7 }) Strut(trim, { s * 0.98, 0.06, z }, { s * 0.5, 0.78, z }, 0.045);
	}
	RotorAssembly(model, 2.9, -0.1, 5.4, 2, 0.36, 0.18, 2.4, -8.05, 0.72, 2);
	Light(model, 0, 1.0, 2.1, -6.05, 0.06);
	Light(model, 1, -1.0, 2.1, -6.05, 0.06);
	Light(model, 2, 0, 2.95, -8.3, 0.06);
	model.seats = { { 0.36, 0.78, 1.15 }, { -0.36, 0.78, 1.15 }, { 0.36, 0.78, 0.1 }, { -0.36, 0.78, 0.1 } };
	model.visibleSeats = 0;
	model.doorPos = { 1.45, 0, 0.8 };
	model.parts = { body, trim };
	return model;
}

// ------------------------------------------------------------------ Mammoth: main battle tank
AircraftModel Mammoth(uint32_t color) {
	AircraftModel model; model.cgY = 1.2; model.tank = true;
	const C3 base = AirHex(color), dirt = AirMix(base, { 0.35, 0.3, 0.22 }, 0.45);
	MeshBuf body; body.Color(1, 1, 1);
	auto extrudeSide = [&](const std::vector<Pt2>& pts, double w, const C3& colr) {
		ExtrudeOpts o; o.depth = w * 2; o.bevelEnabled = true; o.bevelThickness = 0.04; o.bevelSize = 0.04; o.bevelSegments = 1;
		MeshBuf g = ExtrudeShape(pts, o);
		TransformBuf(g, AirRotY(-kPi / 2));
		TransformBuf(g, AirTranslate(w, 0, 0));
		SetColor(body, colr);
		AddGeo(body, g, Mat4::Identity());
	};
	extrudeSide({ { 4.0, 1.05 }, { 2.85, 1.74 }, { -3.8, 1.78 }, { -4.0, 1.05 } }, 1.85, base);
	extrudeSide({ { 3.7, 0.5 }, { 4.0, 1.06 }, { -4.0, 1.06 }, { -3.75, 0.5 } }, 1.2, dirt);
	for (const double s : { 1.0, -1.0 }) {
		SetColor(body, base);
		body.Box(s > 0 ? 1.84 : -1.92, 0.6, -3.95, s > 0 ? 1.92 : -1.84, 1.08, 3.85);
		SetColor(body, dirt);
		body.Box(s > 0 ? 1.84 : -1.92, 0.55, -3.95, s > 0 ? 1.93 : -1.83, 0.62, 3.85);
	}
	MeshBuf trim; trim.Color(1, 1, 1);
	trim.Color(0.07, 0.07, 0.07);
	trim.Box(-1.5, 1.79, -3.7, 1.5, 1.82, -2.4);
	for (int k = 0; k < 8; k++) { trim.Color(0.18, 0.18, 0.17); trim.Box(-1.45, 1.82, -3.65 + k * 0.16, 1.45, 1.85, -3.6 + k * 0.16); }
	for (const double s : { 1.0, -1.0 }) {
		const double x0 = s * 1.22, x1 = s * 1.82;
		trim.Color(0.09, 0.09, 0.085);
		trim.Box(std::min(x0, x1), 0.92, -3.6, std::max(x0, x1), 1.02, 3.6);
		trim.Box(std::min(x0, x1), 0.02, -3.2, std::max(x0, x1), 0.1, 3.2);
		for (const double z : { 3.55, -3.55 }) AddGeo(trim, Geo::Cylinder(0.48, 0.48, 0.6, 16), AirM(s * 1.52, 0.52, z, 0, 0, kPi / 2));
	}
	// road wheels per side (one geometry, seven meshes)
	{
		MeshBuf gb; gb.Color(1, 1, 1);
		gb.Color(0.16, 0.17, 0.15);
		AddGeo(gb, Geo::Cylinder(0.36, 0.36, 0.52, 14), AirM(0, 0, 0, 0, 0, kPi / 2));
		gb.Color(0.3, 0.32, 0.27);
		AddGeo(gb, Geo::Cylinder(0.2, 0.2, 0.56, 10), AirM(0, 0, 0, 0, 0, kPi / 2));
		model.roadWheel = gb;
		for (const double s : { 1.0, -1.0 }) { std::vector<Pt3> list; for (int k = 0; k < 7; k++) list.push_back({ s * 1.52, 0.4, -2.85 + k * 0.95 }); model.wheelSets.push_back(list); }
	}
	// turret
	model.turretAt = { 0, 1.78, -0.35 };
	MeshBuf tgb; tgb.Color(1, 1, 1);
	const std::vector<Pt2> outline = { { 0.95, 2.45 }, { 1.75, 1.2 }, { 1.78, -1.7 }, { 1.35, -2.55 }, { -1.35, -2.55 }, { -1.78, -1.7 }, { -1.75, 1.2 }, { -0.95, 2.45 } };
	std::vector<Pt2> ts;
	for (const Pt2& p : outline) ts.push_back({ p[0], -p[1] });
	{
		ExtrudeOpts o; o.depth = 0.8; o.bevelEnabled = true; o.bevelThickness = 0.1; o.bevelSize = 0.1; o.bevelSegments = 2;
		MeshBuf tg = ExtrudeShape(ts, o);
		TransformBuf(tg, AirRotX(-kPi / 2));
		TransformBuf(tg, AirTranslate(0, 0.08, 0));
		SetColor(tgb, base);
		AddGeo(tgb, tg, Mat4::Identity());
	}
	SetColor(tgb, dirt);
	tgb.Box(-1.3, 0.3, -3.1, 1.3, 0.75, -2.62, true);
	SetColor(tgb, base);
	AddGeo(tgb, Geo::Cylinder(0.38, 0.42, 0.3, 12), AirM(0.8, 1.1, -0.7));
	AddGeo(tgb, Geo::Cylinder(0.3, 0.32, 0.2, 10), AirM(-0.8, 1.05, -0.4));
	for (const double s : { 1.0, -1.0 }) for (int k = 0; k < 3; k++) { tgb.Color(0.2, 0.22, 0.18); AddGeo(tgb, Cyl(0.07, 0.07, 0.35, 8), AirM(s * (1.55 - k * 0.14), 0.75, 1.3 + k * 0.05, -0.4, 0, 0)); }
	tgb.Color(0.1, 0.1, 0.1);
	for (const double s : { 1.0, -1.0 }) AddGeo(tgb, Geo::Cylinder(0.015, 0.02, 2.2, 4), AirM(s * 1.2, 1.9, -2.3, 0.15, 0, 0));
	AddGeo(tgb, Cyl(0.035, 0.035, 1.0, 6), AirM(0.8, 1.45, -0.3));
	tgb.Box(0.7, 1.28, -0.9, 0.9, 1.44, -0.35);
	model.turret = tgb;
	// gun: mantlet + barrel on an elevation pivot
	model.gunPivot = { 0, 0.46, 2.35 };
	MeshBuf ggb; ggb.Color(1, 1, 1);
	SetColor(ggb, base);
	ggb.Box(-0.5, -0.3, -0.25, 0.5, 0.3, 0.35);
	SetColor(ggb, AirMix(base, { 0.2, 0.2, 0.2 }, 0.25));
	AddGeo(ggb, Cyl(0.13, 0.11, 5.3, 14), AirM(0, 0, 3.0));
	AddGeo(ggb, Cyl(0.17, 0.17, 0.8, 14), AirM(0, 0, 2.6));
	ggb.Color(0.08, 0.08, 0.08);
	AddGeo(ggb, Cyl(0.13, 0.13, 0.25, 14), AirM(0, 0, 5.55));
	model.tankGun = ggb;
	// the commander's hatch is the door
	model.doorAxis = 'z'; model.doorMax = -1.9; model.doorPivot = { 0.8 + 0.36, 1.26, -0.7 };
	{ MeshBuf h = Geo::Cylinder(0.34, 0.34, 0.06, 12); TransformBuf(h, AirTranslate(-0.36, 0, 0)); model.hatch = h; }
	model.seats = { { 0, 0.9, 0 }, { 0.4, 0.9, 1.5 }, { -0.4, 0.9, -1 }, { 0.4, 0.9, -1 } };
	model.visibleSeats = 0;
	model.doorPos = { 2.45, 0, 0.4 };
	model.parts = { body, trim };
	return model;
}
}

const AircraftModel& BuildAircraftModel(const VehicleDef& def, uint32_t color, const std::string& type) {
	static std::mutex lock;
	static std::map<std::string, std::unique_ptr<AircraftModel>> cache;
	char key[96];
	snprintf(key, sizeof key, "%s/%06x", type.c_str(), color);
	std::lock_guard<std::mutex> g(lock);
	auto it = cache.find(key);
	if (it != cache.end()) return *it->second;
	AircraftModel m;
	if (type == "skipper") m = Skipper(color);
	else if (type == "raptor") m = Raptor(color);
	else if (type == "hercules") m = Hercules(color);
	else if (type == "warhawk") m = Warhawk(color);
	else if (type == "skylark") m = Skylark(color);
	else if (type == "mammoth") m = Mammoth(color);
	else m = def.kind == "heli" ? Skylark(color) : def.kind == "tank" ? Mammoth(color) : Skipper(color);
	auto& slot = cache[key];
	slot = std::make_unique<AircraftModel>(std::move(m));
	return *slot;
}

} // namespace atg
