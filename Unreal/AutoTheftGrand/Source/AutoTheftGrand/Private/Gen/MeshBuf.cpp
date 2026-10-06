// See MeshBuf.h.
#include "MeshBuf.h"

namespace atg {

// ------------------------------------------------------------------ Mat4
Mat4 Mat4::Identity() { Mat4 r; for (int i = 0; i < 16; i++) r.m[i] = (i % 5 == 0) ? 1 : 0; return r; }

Mat4 Mat4::Compose(double px, double py, double pz, double rx, double ry, double rz, double sx, double sy, double sz, const char* order) {
	const double c1 = std::cos(rx / 2), c2 = std::cos(ry / 2), c3 = std::cos(rz / 2);
	const double s1 = std::sin(rx / 2), s2 = std::sin(ry / 2), s3 = std::sin(rz / 2);
	double x, y, z, w;
	if (order[0] == 'Y' && order[1] == 'X') { // YXZ
		x = s1 * c2 * c3 + c1 * s2 * s3; y = c1 * s2 * c3 - s1 * c2 * s3; z = c1 * c2 * s3 - s1 * s2 * c3; w = c1 * c2 * c3 + s1 * s2 * s3;
	} else { // XYZ
		x = s1 * c2 * c3 + c1 * s2 * s3; y = c1 * s2 * c3 - s1 * c2 * s3; z = c1 * c2 * s3 + s1 * s2 * c3; w = c1 * c2 * c3 - s1 * s2 * s3;
	}
	const double x2 = x + x, y2 = y + y, z2 = z + z;
	const double xx = x * x2, xy = x * y2, xz = x * z2, yy = y * y2, yz = y * z2, zz = z * z2, wx = w * x2, wy = w * y2, wz = w * z2;
	Mat4 r;
	r.m[0] = (1 - (yy + zz)) * sx; r.m[1] = (xy + wz) * sx; r.m[2] = (xz - wy) * sx; r.m[3] = 0;
	r.m[4] = (xy - wz) * sy; r.m[5] = (1 - (xx + zz)) * sy; r.m[6] = (yz + wx) * sy; r.m[7] = 0;
	r.m[8] = (xz + wy) * sz; r.m[9] = (yz - wx) * sz; r.m[10] = (1 - (xx + yy)) * sz; r.m[11] = 0;
	r.m[12] = px; r.m[13] = py; r.m[14] = pz; r.m[15] = 1;
	return r;
}

Mat4 Mat4::operator*(const Mat4& b) const {
	Mat4 r;
	for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) {
		double s = 0;
		for (int k = 0; k < 4; k++) s += m[k * 4 + rr] * b.m[c * 4 + k];
		r.m[c * 4 + rr] = s;
	}
	return r;
}

void Mat4::Apply(double& x, double& y, double& z) const {
	const double X = m[0] * x + m[4] * y + m[8] * z + m[12];
	const double Y = m[1] * x + m[5] * y + m[9] * z + m[13];
	const double Z = m[2] * x + m[6] * y + m[10] * z + m[14];
	x = X; y = Y; z = Z;
}

void Mat4::ApplyNormal(double& x, double& y, double& z) const {
	// inverse transpose of the upper 3x3
	const double a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
	const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
	const double D = -(b * i - c * h), E = a * i - c * g, F = -(a * h - b * g);
	const double G = b * f - c * e, H = -(a * f - c * d), I = a * e - b * d;
	// (cofactor matrix = inverse transpose * det; the scale drops out on normalising)
	double X = A * x + B * y + C * z, Y = D * x + E * y + F * z, Z = G * x + H * y + I * z;
	const double det = a * A + b * B + c * C;
	if (det < 0) { X = -X; Y = -Y; Z = -Z; }
	const double l = Hypot3(X, Y, Z);
	if (l > 0) { X /= l; Y /= l; Z /= l; }
	x = X; y = Y; z = Z;
}

// ------------------------------------------------------------------ MeshBuf
static double SrgbToLinear(double c) { return c < 0.04045 ? c * 0.0773993808 : std::pow(c * 0.9478672986 + 0.0521327014, 2.4); }
void MeshBuf::ColorHex(uint32_t hex) {
	Color(SrgbToLinear(((hex >> 16) & 255) / 255.0), SrgbToLinear(((hex >> 8) & 255) / 255.0), SrgbToLinear((hex & 255) / 255.0));
}

uint32_t MeshBuf::V(double x, double y, double z, double nx, double ny, double nz, double u, double v) {
	P.push_back((float)x); P.push_back((float)y); P.push_back((float)z);
	N.push_back((float)nx); N.push_back((float)ny); N.push_back((float)nz);
	C[0].push_back((float)u); C[0].push_back((float)v);
	for (int k = 1; k < 4; k++) { C[k].push_back(cur[k][0]); C[k].push_back(cur[k][1]); }
	return (uint32_t)(Count() - 1);
}

void MeshBuf::Quad(const double p0[3], const double p1[3], const double p2[3], const double p3[3], const double n[3], const double uv0[2], const double uv1[2], const double uv2[2], const double uv3[2]) {
	static const double d0[2] = { 0, 0 }, d1[2] = { 1, 0 }, d2[2] = { 1, 1 }, d3[2] = { 0, 1 };
	if (!uv0) uv0 = d0;
	if (!uv1) uv1 = d1;
	if (!uv2) uv2 = d2;
	if (!uv3) uv3 = d3;
	const uint32_t a = V(p0[0], p0[1], p0[2], n[0], n[1], n[2], uv0[0], uv0[1]);
	const uint32_t b = V(p1[0], p1[1], p1[2], n[0], n[1], n[2], uv1[0], uv1[1]);
	const uint32_t c = V(p2[0], p2[1], p2[2], n[0], n[1], n[2], uv2[0], uv2[1]);
	const uint32_t d = V(p3[0], p3[1], p3[2], n[0], n[1], n[2], uv3[0], uv3[1]);
	Tri(a, b, c); Tri(a, c, d);
}

void MeshBuf::QuadAuto(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
	const float* p = P.data();
	const double ax = p[a * 3], ay = p[a * 3 + 1], az = p[a * 3 + 2];
	const double ux = p[b * 3] - ax, uy = p[b * 3 + 1] - ay, uz = p[b * 3 + 2] - az;
	double vx = p[c * 3] - ax, vy = p[c * 3 + 1] - ay, vz = p[c * 3 + 2] - az;
	if (vx * vx + vy * vy + vz * vz < 1e-10) { vx = p[d * 3] - ax; vy = p[d * 3 + 1] - ay; vz = p[d * 3 + 2] - az; }
	const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
	if (nx * N[a * 3] + ny * N[a * 3 + 1] + nz * N[a * 3 + 2] >= 0) { Tri(a, b, c); Tri(a, c, d); }
	else { Tri(a, c, b); Tri(a, d, c); }
}

void MeshBuf::Box(double x0, double y0, double z0, double x1, double y1, double z1, bool top, bool sides, bool bottom, double uvs) {
	auto u = [uvs](double v) { return v / uvs; };
	if (sides) {
		{ const double a[3] = { x1, y0, z0 }, b[3] = { x0, y0, z0 }, c[3] = { x0, y1, z0 }, d[3] = { x1, y1, z0 }, n[3] = { 0, 0, -1 };
		  const double t0[2] = { 0, u(y0) }, t1[2] = { u(x1 - x0), u(y0) }, t2[2] = { u(x1 - x0), u(y1) }, t3[2] = { 0, u(y1) }; Quad(a, b, c, d, n, t0, t1, t2, t3); }
		{ const double a[3] = { x0, y0, z1 }, b[3] = { x1, y0, z1 }, c[3] = { x1, y1, z1 }, d[3] = { x0, y1, z1 }, n[3] = { 0, 0, 1 };
		  const double t0[2] = { 0, u(y0) }, t1[2] = { u(x1 - x0), u(y0) }, t2[2] = { u(x1 - x0), u(y1) }, t3[2] = { 0, u(y1) }; Quad(a, b, c, d, n, t0, t1, t2, t3); }
		{ const double a[3] = { x0, y0, z0 }, b[3] = { x0, y0, z1 }, c[3] = { x0, y1, z1 }, d[3] = { x0, y1, z0 }, n[3] = { -1, 0, 0 };
		  const double t0[2] = { 0, u(y0) }, t1[2] = { u(z1 - z0), u(y0) }, t2[2] = { u(z1 - z0), u(y1) }, t3[2] = { 0, u(y1) }; Quad(a, b, c, d, n, t0, t1, t2, t3); }
		{ const double a[3] = { x1, y0, z1 }, b[3] = { x1, y0, z0 }, c[3] = { x1, y1, z0 }, d[3] = { x1, y1, z1 }, n[3] = { 1, 0, 0 };
		  const double t0[2] = { 0, u(y0) }, t1[2] = { u(z1 - z0), u(y0) }, t2[2] = { u(z1 - z0), u(y1) }, t3[2] = { 0, u(y1) }; Quad(a, b, c, d, n, t0, t1, t2, t3); }
	}
	if (top) {
		const double a[3] = { x0, y1, z1 }, b[3] = { x1, y1, z1 }, c[3] = { x1, y1, z0 }, d[3] = { x0, y1, z0 }, n[3] = { 0, 1, 0 };
		const double t0[2] = { u(x0), u(z1) }, t1[2] = { u(x1), u(z1) }, t2[2] = { u(x1), u(z0) }, t3[2] = { u(x0), u(z0) };
		Quad(a, b, c, d, n, t0, t1, t2, t3);
	}
	if (bottom) {
		const double a[3] = { x0, y0, z0 }, b[3] = { x1, y0, z0 }, c[3] = { x1, y0, z1 }, d[3] = { x0, y0, z1 }, n[3] = { 0, -1, 0 };
		Quad(a, b, c, d, n);
	}
}

void MeshBuf::Add(const MeshBuf& g, const Mat4& m) {
	const uint32_t base = (uint32_t)Count();
	const size_t n = g.Count();
	for (size_t i = 0; i < n; i++) {
		double x = g.P[i * 3], y = g.P[i * 3 + 1], z = g.P[i * 3 + 2];
		double nx = g.N[i * 3], ny = g.N[i * 3 + 1], nz = g.N[i * 3 + 2];
		m.Apply(x, y, z);
		m.ApplyNormal(nx, ny, nz);
		V(x, y, z, nx, ny, nz, g.C[0][i * 2], g.C[0][i * 2 + 1]);
	}
	for (uint32_t k : g.I) I.push_back(base + k);
}

void MeshBuf::Append(const MeshBuf& g) {
	const uint32_t base = (uint32_t)Count();
	P.insert(P.end(), g.P.begin(), g.P.end());
	N.insert(N.end(), g.N.begin(), g.N.end());
	for (int k = 0; k < 4; k++) C[k].insert(C[k].end(), g.C[k].begin(), g.C[k].end());
	for (uint32_t k : g.I) I.push_back(base + k);
}

void MeshBuf::Append(const MeshBuf& g, const Mat4& m) {
	const size_t base = Count();
	Append(g);
	for (size_t i = base; i < Count(); i++) {
		double x = P[i * 3], y = P[i * 3 + 1], z = P[i * 3 + 2], nx = N[i * 3], ny = N[i * 3 + 1], nz = N[i * 3 + 2];
		m.Apply(x, y, z); m.ApplyNormal(nx, ny, nz);
		P[i * 3] = (float)x; P[i * 3 + 1] = (float)y; P[i * 3 + 2] = (float)z;
		N[i * 3] = (float)nx; N[i * 3 + 1] = (float)ny; N[i * 3 + 2] = (float)nz;
	}
}

void MeshBuf::RotateFrom(size_t start, double cx, double cz, double yaw) {
	if (yaw == 0) return;
	const double s = std::sin(yaw), c = std::cos(yaw);
	for (size_t i = start; i < Count(); i++) {
		const double x = P[i * 3] - cx, z = P[i * 3 + 2] - cz;
		P[i * 3] = (float)(cx + x * c + z * s); P[i * 3 + 2] = (float)(cz - x * s + z * c);
		const double nx = N[i * 3], nz = N[i * 3 + 2];
		N[i * 3] = (float)(nx * c + nz * s); N[i * 3 + 2] = (float)(-nx * s + nz * c);
	}
}

void MeshBuf::Translate(double x, double y, double z) {
	for (size_t i = 0; i < Count(); i++) { P[i * 3] += (float)x; P[i * 3 + 1] += (float)y; P[i * 3 + 2] += (float)z; }
}

void MeshBuf::Bounds(double mn[3], double mx[3]) const {
	for (int k = 0; k < 3; k++) { mn[k] = kInf; mx[k] = -kInf; }
	for (size_t i = 0; i < Count(); i++) for (int k = 0; k < 3; k++) { mn[k] = Min(mn[k], P[i * 3 + k]); mx[k] = Max(mx[k], P[i * 3 + k]); }
}

// ------------------------------------------------------------------ primitives
namespace Geo {

static void Vert(MeshBuf& g, double x, double y, double z, double nx, double ny, double nz, double u, double v) { g.V(x, y, z, nx, ny, nz, u, v); }

MeshBuf Box(double w, double h, double d) {
	MeshBuf g;
	g.Box(-w / 2, -h / 2, -d / 2, w / 2, h / 2, d / 2, true, true, true);
	// (the box's uvs are in metres here; three.js uses 0..1 per face, which nothing downstream reads)
	return g;
}

MeshBuf Cylinder(double rTop, double rBottom, double h, int radial, int heightSegs, bool open, double thetaStart, double thetaLen) {
	MeshBuf g;
	const double half = h / 2, slope = (rBottom - rTop) / h;
	std::vector<std::vector<uint32_t>> idx(heightSegs + 1);
	for (int y = 0; y <= heightSegs; y++) {
		const double v = (double)y / heightSegs, r = v * (rBottom - rTop) + rTop;
		for (int x = 0; x <= radial; x++) {
			const double u = (double)x / radial, th = thetaStart + u * thetaLen, s = std::sin(th), c = std::cos(th);
			const double nl = Hypot3(s, slope, c);
			Vert(g, r * s, -v * h + half, r * c, s / nl, slope / nl, c / nl, u, 1 - v);
			idx[y].push_back((uint32_t)g.Count() - 1);
		}
	}
	for (int x = 0; x < radial; x++) for (int y = 0; y < heightSegs; y++) {
		const uint32_t a = idx[y][x], b = idx[y + 1][x], c = idx[y + 1][x + 1], d = idx[y][x + 1];
		if (rTop > 0 || y != 0) g.Tri(a, b, d);
		if (rBottom > 0 || y != heightSegs - 1) g.Tri(b, c, d);
	}
	if (!open) {
		for (int top = 1; top >= 0; top--) {
			const double r = top ? rTop : rBottom;
			if (r <= 0) continue;
			const double sign = top ? 1 : -1, yy = half * sign;
			const uint32_t c0 = (uint32_t)g.Count();
			for (int x = 1; x <= radial; x++) Vert(g, 0, yy, 0, 0, sign, 0, 0.5, 0.5);
			const uint32_t r0 = (uint32_t)g.Count();
			for (int x = 0; x <= radial; x++) { const double th = thetaStart + (double)x / radial * thetaLen; Vert(g, r * std::sin(th), yy, r * std::cos(th), 0, sign, 0, std::cos(th) * 0.5 + 0.5, std::sin(th) * 0.5 * sign + 0.5); }
			for (int x = 0; x < radial; x++) {
				const uint32_t c = c0 + x, i = r0 + x;
				if (top) g.Tri(i, i + 1, c); else g.Tri(i + 1, i, c);
			}
		}
	}
	return g;
}

MeshBuf Lathe(const std::vector<std::array<double, 2>>& pts, int segments, double phiStart, double phiLen) {
	MeshBuf g;
	const int n = (int)pts.size();
	std::vector<double> init((size_t)n * 3);
	double px = 0, py = 0, pz = 0;
	for (int j = 0; j < n; j++) {
		if (j == 0) {
			const double dx = pts[1][0] - pts[0][0], dy = pts[1][1] - pts[0][1];
			px = dy; py = -dx; pz = 0;
			const double l = Hypot3(px, py, pz);
			init[0] = l > 0 ? px / l : 0; init[1] = l > 0 ? py / l : 0; init[2] = 0;
		} else if (j == n - 1) {
			init[j * 3] = px; init[j * 3 + 1] = py; init[j * 3 + 2] = pz;
		} else {
			const double dx = pts[j + 1][0] - pts[j][0], dy = pts[j + 1][1] - pts[j][1];
			const double cx = dy, cy = -dx;
			double nx = cx + px, ny = cy + py;
			const double l = Hypot(nx, ny);
			if (l > 0) { nx /= l; ny /= l; }
			init[j * 3] = nx; init[j * 3 + 1] = ny; init[j * 3 + 2] = 0;
			px = cx; py = cy; pz = 0;
		}
	}
	for (int i = 0; i <= segments; i++) {
		const double phi = phiStart + (double)i / segments * phiLen, s = std::sin(phi), c = std::cos(phi);
		for (int j = 0; j < n; j++) Vert(g, pts[j][0] * s, pts[j][1], pts[j][0] * c, init[j * 3] * s, init[j * 3 + 1], init[j * 3] * c, (double)i / segments, (double)j / (n - 1));
	}
	for (int i = 0; i < segments; i++) for (int j = 0; j < n - 1; j++) {
		const uint32_t base = j + i * n, a = base, b = base + n, c = base + n + 1, d = base + 1;
		g.Tri(a, b, d); g.Tri(c, d, b);
	}
	return g;
}

MeshBuf Cone(double r, double h, int radial, int heightSegs) { return Cylinder(0, r, h, radial, heightSegs, false); }

MeshBuf Sphere(double r, int wSegs, int hSegs, double phiStart, double phiLen, double thetaStart, double thetaLen) {
	MeshBuf g;
	const double thetaEnd = Min(thetaStart + thetaLen, kPi);
	std::vector<std::vector<uint32_t>> grid(hSegs + 1);
	for (int iy = 0; iy <= hSegs; iy++) {
		const double v = (double)iy / hSegs;
		for (int ix = 0; ix <= wSegs; ix++) {
			const double u = (double)ix / wSegs;
			const double x = -r * std::cos(phiStart + u * phiLen) * std::sin(thetaStart + v * thetaLen);
			const double y = r * std::cos(thetaStart + v * thetaLen);
			const double z = r * std::sin(phiStart + u * phiLen) * std::sin(thetaStart + v * thetaLen);
			const double l = Hypot3(x, y, z);
			Vert(g, x, y, z, l > 0 ? x / l : 0, l > 0 ? y / l : 1, l > 0 ? z / l : 0, u, 1 - v);
			grid[iy].push_back((uint32_t)g.Count() - 1);
		}
	}
	for (int iy = 0; iy < hSegs; iy++) for (int ix = 0; ix < wSegs; ix++) {
		const uint32_t a = grid[iy][ix + 1], b = grid[iy][ix], c = grid[iy + 1][ix], d = grid[iy + 1][ix + 1];
		if (iy != 0 || thetaStart > 0) g.Tri(a, b, d);
		if (iy != hSegs - 1 || thetaEnd < kPi) g.Tri(b, c, d);
	}
	return g;
}

void ComputeFlatNormals(MeshBuf& g) {
	// make every triangle own its vertices, then give each its face normal
	MeshBuf o;
	for (size_t t = 0; t + 2 < g.I.size(); t += 3) {
		const uint32_t id[3] = { g.I[t], g.I[t + 1], g.I[t + 2] };
		double p[3][3];
		for (int k = 0; k < 3; k++) for (int q = 0; q < 3; q++) p[k][q] = g.P[id[k] * 3 + q];
		const double ux = p[1][0] - p[0][0], uy = p[1][1] - p[0][1], uz = p[1][2] - p[0][2];
		const double vx = p[2][0] - p[0][0], vy = p[2][1] - p[0][1], vz = p[2][2] - p[0][2];
		double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
		const double l = Hypot3(nx, ny, nz);
		if (l > 0) { nx /= l; ny /= l; nz /= l; }
		for (int k = 0; k < 3; k++) {
			for (int c = 1; c < 4; c++) o.cur[c][0] = g.C[c][id[k] * 2], o.cur[c][1] = g.C[c][id[k] * 2 + 1];
			o.V(p[k][0], p[k][1], p[k][2], nx, ny, nz, g.C[0][id[k] * 2], g.C[0][id[k] * 2 + 1]);
		}
		o.Tri((uint32_t)o.Count() - 3, (uint32_t)o.Count() - 2, (uint32_t)o.Count() - 1);
	}
	g = o;
}

MeshBuf Icosahedron(double r, int detail) {
	const double t = (1 + std::sqrt(5.0)) / 2;
	const double vs[12][3] = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
	const int fs[20][3] = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
		{ 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
	MeshBuf g;
	auto push = [&](const double a[3]) {
		const double l = Hypot3(a[0], a[1], a[2]);
		const double x = a[0] / l * r, y = a[1] / l * r, z = a[2] / l * r;
		g.V(x, y, z, x / r, y / r, z / r, 0, 0);
	};
	for (const auto& f : fs) {
		const double* A = vs[f[0]]; const double* B = vs[f[1]]; const double* C = vs[f[2]];
		const int cols = detail + 1;
		// subdivide like PolyhedronGeometry: rows of points between A->C and B->C
		std::vector<std::vector<std::array<double, 3>>> v(cols + 1);
		for (int i = 0; i <= cols; i++) {
			double aj[3], bj[3];
			for (int k = 0; k < 3; k++) { aj[k] = A[k] + (C[k] - A[k]) * i / cols; bj[k] = B[k] + (C[k] - B[k]) * i / cols; }
			const int rows = cols - i;
			for (int j = 0; j <= rows; j++) {
				std::array<double, 3> p;
				if (j == 0 && i == cols) p = { aj[0], aj[1], aj[2] };
				else for (int k = 0; k < 3; k++) p[k] = aj[k] + (bj[k] - aj[k]) * j / rows;
				v[i].push_back(p);
			}
		}
		for (int i = 0; i < cols; i++) for (int j = 0; j < 2 * (cols - i) - 1; j++) {
			const int k = j / 2;
			if (j % 2 == 0) { push(v[i][k + 1].data()); push(v[i + 1][k].data()); push(v[i][k].data()); }
			else { push(v[i][k + 1].data()); push(v[i + 1][k + 1].data()); push(v[i + 1][k].data()); }
			g.Tri((uint32_t)g.Count() - 3, (uint32_t)g.Count() - 2, (uint32_t)g.Count() - 1);
		}
	}
	if (detail == 0) ComputeFlatNormals(g);
	return g;
}

MeshBuf Torus(double r, double tube, int radial, int tubular) {
	MeshBuf g;
	for (int j = 0; j <= radial; j++) for (int i = 0; i <= tubular; i++) {
		const double u = (double)i / tubular * kTau, v = (double)j / radial * kTau;
		const double x = (r + tube * std::cos(v)) * std::cos(u), y = (r + tube * std::cos(v)) * std::sin(u), z = tube * std::sin(v);
		double nx = x - r * std::cos(u), ny = y - r * std::sin(u), nz = z;
		const double l = Hypot3(nx, ny, nz); nx /= l; ny /= l; nz /= l;
		g.V(x, y, z, nx, ny, nz, (double)i / tubular, (double)j / radial);
	}
	for (int j = 1; j <= radial; j++) for (int i = 1; i <= tubular; i++) {
		const uint32_t a = (tubular + 1) * j + i - 1, b = (tubular + 1) * (j - 1) + i - 1, c = (tubular + 1) * (j - 1) + i, d = (tubular + 1) * j + i;
		g.Tri(a, b, d); g.Tri(b, c, d);
	}
	return g;
}

MeshBuf Capsule(double r, double length, int capSegs, int radial) {
	// lathe of the profile: bottom pole -> bottom hemisphere -> top hemisphere -> top pole
	struct PP { double x, y; };
	std::vector<PP> prof;
	for (int k = 0; k <= capSegs; k++) { const double a = -kPi / 2 + (double)k / capSegs * kPi / 2; prof.push_back({ r * std::cos(a), -length / 2 + r * std::sin(a) }); }
	for (int k = 0; k <= capSegs; k++) { const double a = (double)k / capSegs * kPi / 2; prof.push_back({ r * std::cos(a), length / 2 + r * std::sin(a) }); }
	MeshBuf g;
	const int np = (int)prof.size();
	for (int i = 0; i <= radial; i++) {
		const double phi = (double)i / radial * kTau, s = std::sin(phi), c = std::cos(phi);
		for (int j = 0; j < np; j++) {
			const double x = prof[j].x * s, y = prof[j].y, z = prof[j].x * c;
			const double cy = y > length / 2 ? length / 2 : y < -length / 2 ? -length / 2 : y;
			double nx = x, ny = y - cy, nz = z; const double l = Hypot3(nx, ny, nz);
			if (l > 0) { nx /= l; ny /= l; nz /= l; } else { ny = y > 0 ? 1 : -1; }
			g.V(x, y, z, nx, ny, nz, (double)i / radial, (double)j / (np - 1));
		}
	}
	for (int i = 0; i < radial; i++) for (int j = 0; j < np - 1; j++) {
		const uint32_t a = i * np + j, b = a + np, c = b + 1, d = a + 1;
		g.Tri(a, b, d); g.Tri(b, c, d);
	}
	return g;
}

MeshBuf Circle(double r, int segs) {
	MeshBuf g;
	g.V(0, 0, 0, 0, 0, 1, 0.5, 0.5);
	for (int s = 0; s <= segs; s++) { const double th = (double)s / segs * kTau; g.V(r * std::cos(th), r * std::sin(th), 0, 0, 0, 1, (std::cos(th) + 1) / 2, (std::sin(th) + 1) / 2); }
	for (int i = 1; i <= segs; i++) g.Tri(i, i + 1, 0);
	return g;
}

MeshBuf Ring(double inner, double outer, int segs) {
	MeshBuf g;
	for (int j = 0; j <= 1; j++) for (int i = 0; i <= segs; i++) {
		const double r = j ? outer : inner, th = (double)i / segs * kTau;
		g.V(r * std::cos(th), r * std::sin(th), 0, 0, 0, 1, 0, 0);
	}
	for (int i = 0; i < segs; i++) {
		const uint32_t a = i, b = i + 1, c = (segs + 1) + i + 1, d = (segs + 1) + i;
		g.Tri(a, b, d); g.Tri(b, c, d);
	}
	return g;
}

} // namespace Geo
} // namespace atg
