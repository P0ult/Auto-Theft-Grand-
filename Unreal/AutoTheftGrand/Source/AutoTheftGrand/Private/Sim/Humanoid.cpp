#include "Humanoid.h"
#include "MeshBuf.h"

namespace atg {

// ------------------------------------------------------------------ colours (three.js Color)
static double SrgbToLin(double c) { return c < 0.04045 ? c * 0.0773993808 : std::pow(c * 0.9478672986 + 0.0521327014, 2.4); }
static double LinToSrgb(double c) { return c < 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 0.41666) - 0.055; }

void HexToLinear(uint32_t hex, double out[3]) {
	out[0] = SrgbToLin(((hex >> 16) & 255) / 255.0);
	out[1] = SrgbToLin(((hex >> 8) & 255) / 255.0);
	out[2] = SrgbToLin((hex & 255) / 255.0);
}
uint32_t LinearToHex(const double c[3]) {
	auto ch = [](double v) { return (uint32_t)std::round(Clamp(LinToSrgb(v) * 255, 0, 255)); };
	return ch(c[0]) * 65536 + ch(c[1]) * 256 + ch(c[2]);
}

namespace {

uint32_t Shade(uint32_t hex, double k) { double c[3]; HexToLinear(hex, c); for (double& v : c) v *= k; return LinearToHex(c); }
uint32_t MixHex(uint32_t h1, uint32_t h2, double t) {
	double a[3], b[3]; HexToLinear(h1, a); HexToLinear(h2, b);
	for (int i = 0; i < 3; i++) a[i] += (b[i] - a[i]) * t;
	return LinearToHex(a);
}

using W = std::vector<std::pair<int, double>>;
struct Ring { double y, x, z, rx, rz; W w; uint32_t c; int m; double sq = 0, front = 0; bool cut = false; };

class SkinBuilder {
public:
	HumanoidMesh& o;
	uint32_t n = 0;
	std::vector<std::pair<uint32_t, uint32_t>> smooth;
	explicit SkinBuilder(HumanoidMesh& out) : o(out) {}

	void vert(double x, double y, double z, double nx, double ny, double nz, const double c[3], int m, const int si[4], const double sw[4]) {
		o.P.push_back((float)x); o.P.push_back((float)y); o.P.push_back((float)z);
		o.N.push_back((float)nx); o.N.push_back((float)ny); o.N.push_back((float)nz);
		o.C.push_back((float)c[0]); o.C.push_back((float)c[1]); o.C.push_back((float)c[2]);
		o.mat.push_back((float)m);
		for (int k = 0; k < 4; k++) { o.si.push_back((uint16_t)si[k]); o.sw.push_back((float)sw[k]); }
		n++;
	}
	// rigid part on one bone (de-indexed like toNonIndexed)
	void add(const MeshBuf& g, const Mat4& m, uint32_t color, int bone, int mat = HumanMat::cloth) {
		double c[3]; HexToLinear(color, c);
		const int si[4] = { bone, 0, 0, 0 };
		const double sw[4] = { 1, 0, 0, 0 };
		for (uint32_t k : g.I) {
			double x = g.P[k * 3], y = g.P[k * 3 + 1], z = g.P[k * 3 + 2];
			double nx = g.N[k * 3], ny = g.N[k * 3 + 1], nz = g.N[k * 3 + 2];
			m.Apply(x, y, z); m.ApplyNormal(nx, ny, nz);
			o.I.push_back(n);
			vert(x, y, z, nx, ny, nz, c, mat, si, sw);
		}
	}
	// smooth-skinned tube through horizontal elliptical rings
	void loft(const std::vector<Ring>& rings, int seg = 12, bool cap0 = true, bool cap1 = true) {
		const uint32_t base = n;
		std::vector<std::vector<uint32_t>> rows;
		for (const Ring& r : rings) {
			std::vector<uint32_t> row;
			double c[3]; HexToLinear(r.c, c);
			int si[4] = { 0, 0, 0, 0 }; double sw[4] = { 0, 0, 0, 0 }, tot = 0;
			const size_t nw = std::min<size_t>(4, r.w.size());
			for (size_t k = 0; k < nw; k++) tot += r.w[k].second;
			if (tot == 0) tot = 1;
			for (size_t k = 0; k < nw; k++) { si[k] = r.w[k].first; sw[k] = r.w[k].second / tot; }
			for (int j = 0; j < seg; j++) {
				const double a = (double)j / seg * kPi * 2;
				const double ca = std::cos(a), sa = std::sin(a);
				const double k = 1 + r.sq * (1 - std::fabs(std::cos(2 * a))) * 0.12;
				row.push_back(n);
				vert(r.x + ca * r.rx * k, r.y, r.z + sa * r.rz * k + (sa > 0 ? r.front : 0) * sa * sa, ca / r.rx, 0, sa / r.rz, c, r.m, si, sw);
			}
			rows.push_back(row);
		}
		for (size_t i = 0; i + 1 < rows.size(); i++) {
			if (rings[i + 1].cut) continue;
			for (int j = 0; j < seg; j++) {
				const uint32_t a = rows[i][j], b = rows[i][(j + 1) % seg], c = rows[i + 1][j], d = rows[i + 1][(j + 1) % seg];
				const bool up = rings[i + 1].y > rings[i].y;
				if (up) { o.I.insert(o.I.end(), { a, c, b, b, c, d }); } else { o.I.insert(o.I.end(), { a, b, c, b, d, c }); }
			}
		}
		// caps: a centre vertex per end
		auto cap = [&](size_t ri, bool flipUp) {
			const Ring& r = rings[ri];
			const auto& row = rows[ri];
			double c[3]; HexToLinear(r.c, c);
			double tot = 0; for (const auto& e : r.w) tot += e.second;
			if (tot == 0) tot = 1;
			const int si[4] = { r.w.size() > 0 ? r.w[0].first : 0, r.w.size() > 1 ? r.w[1].first : 0, 0, 0 };
			const double sw[4] = { r.w.size() > 0 ? r.w[0].second / tot : 0, r.w.size() > 1 ? r.w[1].second / tot : 0, 0, 0 };
			const uint32_t ci = n;
			vert(r.x, r.y + (flipUp ? 1 : -1) * Min(r.rx, r.rz) * 0.55, r.z, 0, flipUp ? 1 : -1, 0, c, r.m, si, sw);
			for (int j = 0; j < seg; j++) {
				const uint32_t a = row[j], b = row[(j + 1) % seg];
				if (flipUp) o.I.insert(o.I.end(), { a, ci, b }); else o.I.insert(o.I.end(), { a, b, ci });
			}
		};
		const bool up = rings.back().y > rings[0].y;
		if (cap0) cap(0, !up);
		if (cap1) cap(rings.size() - 1, up);
		smooth.push_back({ base, n });
	}
	// proper normals for the lofted (smooth) parts
	void finish() {
		if (smooth.empty()) return;
		std::vector<double> acc(n * 3, 0.0);
		std::vector<uint8_t> inLoft(n, 0);
		for (const auto& r : smooth) for (uint32_t i = r.first; i < r.second; i++) inLoft[i] = 1;
		const auto& P = o.P;
		for (size_t t = 0; t + 2 < o.I.size(); t += 3) {
			const uint32_t i0 = o.I[t], i1 = o.I[t + 1], i2 = o.I[t + 2];
			if (!inLoft[i0]) continue;
			const V3 a(P[i0 * 3], P[i0 * 3 + 1], P[i0 * 3 + 2]), b(P[i1 * 3], P[i1 * 3 + 1], P[i1 * 3 + 2]), c(P[i2 * 3], P[i2 * 3 + 1], P[i2 * 3 + 2]);
			const V3 e = (b - a).cross(c - a);
			for (uint32_t k : { i0, i1, i2 }) { acc[k * 3] += e.x; acc[k * 3 + 1] += e.y; acc[k * 3 + 2] += e.z; }
		}
		for (uint32_t i = 0; i < n; i++) {
			if (!inLoft[i]) continue;
			V3 a(acc[i * 3], acc[i * 3 + 1], acc[i * 3 + 2]);
			if (a.lengthSq() < 1e-12) continue;
			a.normalize();
			o.N[i * 3] = (float)a.x; o.N[i * 3 + 1] = (float)a.y; o.N[i * 3 + 2] = (float)a.z;
		}
	}
};

// three.js BoxGeometry with segments (indexed: each face its own grid), as rbox needs
MeshBuf BoxSegs(double width, double height, double depth, int ws, int hs, int ds) {
	MeshBuf g;
	auto plane = [&](int u, int v, int w, double udir, double vdir, double pw, double ph, double pd, int gx, int gy) {
		const double sw = pw / gx, sh = ph / gy, wh = pw / 2, hh = ph / 2, dh = pd / 2;
		const uint32_t base = (uint32_t)g.Count();
		for (int iy = 0; iy <= gy; iy++) {
			const double y = iy * sh - hh;
			for (int ix = 0; ix <= gx; ix++) {
				const double x = ix * sw - wh;
				double p[3], nn[3] = { 0, 0, 0 };
				p[u] = x * udir; p[v] = y * vdir; p[w] = dh;
				nn[w] = pd > 0 ? 1 : -1;
				g.V(p[0], p[1], p[2], nn[0], nn[1], nn[2], (double)ix / gx, 1 - (double)iy / gy);
			}
		}
		const int gx1 = gx + 1;
		for (int iy = 0; iy < gy; iy++) for (int ix = 0; ix < gx; ix++) {
			const uint32_t a = base + ix + gx1 * iy, b = base + ix + gx1 * (iy + 1), c = base + (ix + 1) + gx1 * (iy + 1), d = base + (ix + 1) + gx1 * iy;
			g.Tri(a, b, d); g.Tri(b, c, d);
		}
	};
	plane(2, 1, 0, -1, -1, depth, height, width, ds, hs);
	plane(2, 1, 0, 1, -1, depth, height, -width, ds, hs);
	plane(0, 2, 1, 1, 1, width, depth, height, ws, ds);
	plane(0, 2, 1, 1, -1, width, depth, -height, ws, ds);
	plane(0, 1, 2, 1, -1, width, height, depth, ws, hs);
	plane(0, 1, 2, -1, -1, width, height, -depth, ws, hs);
	return g;
}

// three.js computeVertexNormals on an indexed geometry
void VertexNormals(MeshBuf& g) {
	const size_t n = g.Count();
	std::vector<double> acc(n * 3, 0.0);
	for (size_t t = 0; t + 2 < g.I.size(); t += 3) {
		const uint32_t ia = g.I[t], ib = g.I[t + 1], ic = g.I[t + 2];
		const V3 A(g.P[ia * 3], g.P[ia * 3 + 1], g.P[ia * 3 + 2]), B(g.P[ib * 3], g.P[ib * 3 + 1], g.P[ib * 3 + 2]), C(g.P[ic * 3], g.P[ic * 3 + 1], g.P[ic * 3 + 2]);
		const V3 cb = (C - B).cross(A - B);
		for (uint32_t k : { ia, ib, ic }) { acc[k * 3] += cb.x; acc[k * 3 + 1] += cb.y; acc[k * 3 + 2] += cb.z; }
	}
	for (size_t i = 0; i < n; i++) {
		V3 v(acc[i * 3], acc[i * 3 + 1], acc[i * 3 + 2]);
		const double l = v.length();
		if (l > 0) v = v * (1 / l);
		g.N[i * 3] = (float)v.x; g.N[i * 3 + 1] = (float)v.y; g.N[i * 3 + 2] = (float)v.z;
	}
}

// a rounded box: the top and bottom thirds pulled in at the corners
MeshBuf RBox(double w, double h, double d) {
	MeshBuf g = BoxSegs(w, h, d, 2, 2, 2);
	for (size_t i = 0; i < g.Count(); i++) {
		const double x = g.P[i * 3], y = g.P[i * 3 + 1], z = g.P[i * 3 + 2];
		const double k = 0.82 + 0.18 * (1 - std::pow(Max(std::fabs(x) / (w / 2), std::fabs(z) / (d / 2)), 4));
		const bool pull = std::fabs(y) > h * 0.3;
		g.P[i * 3] = (float)(x * (pull ? k : 1));
		g.P[i * 3 + 2] = (float)(z * (pull ? k : 1));
	}
	VertexNormals(g);
	return g;
}
MeshBuf Cap(double r, double len, int rs = 8) { return Geo::Capsule(r, Max(0.001, len), 3, rs); }
Mat4 M(double x, double y, double z, double rx = 0, double ry = 0, double rz = 0, double sx = 1, double sy = 1, double sz = 1) { return Mat4::Compose(x, y, z, rx, ry, rz, sx, sy, sz); }

} // namespace

HumanoidMesh BuildHumanoidGeometry(const Appearance& a) {
	using namespace Bone;
	using namespace HumanMat;
	HumanoidMesh out;
	RestOffsets(a, out.rest);
	V3* wp = out.world;
	for (int i = 0; i < COUNT; i++) wp[i] = (BONE_PARENT[i] >= 0 ? wp[BONE_PARENT[i]] : V3()) + out.rest[i];
	SkinBuilder sb(out);
	const uint32_t skinC = a.skin;
	const uint32_t shirtC = a.hasUniform ? a.uniformShirt : a.shirt, pantsC = a.hasUniform ? a.uniformPants : a.pants;
	const bool sleeveLong = a.shirtType == "long" || a.shirtType == "jacket" || a.hasUniform;
	const bool tank = a.shirtType == "tank";
	const uint32_t jacket = a.shirtType == "jacket" ? a.jacketColor : 0; // (0: none, as the JavaScript's null)
	const uint32_t torsoCol = jacket ? jacket : shirtC;
	const double bw = a.build;
	const bool fem = a.female;
	const int denimM = !a.hasUniform && (pantsC == 0x1f2a44 || pantsC == 0x1d3b5c || pantsC == 0x2b2b2b) ? denim : cloth;
	const uint32_t belt = a.hasUniform ? 0x111111 : 0x1a1512;
	const uint32_t lip = MixHex(skinC, 0x8a3a3a, 0.35);
	const double hy = wp[hips].y, sy = wp[spine].y, cy = wp[chest].y, ny = wp[neck].y;
	const int H = hips, S = spine, C = chest, N = neck, HD = head;

	// ---------------- torso: one smooth shell from the crotch to the neck
	const double hipW = fem ? 0.19 : 0.172 * bw, waistW = fem ? 0.13 : 0.155 * bw, chestW = fem ? 0.165 : 0.19 * bw, shW = fem ? 0.16 : 0.2 * bw;
	const uint32_t top = tank ? shirtC : torsoCol;
	auto R = [](double y, double rx, double rz, W w, uint32_t c, int m, double front = 0, bool cut = false, double sq = 0.6) {
		Ring r{ y, 0, 0, rx, rz, std::move(w), c, m };
		r.sq = sq; r.front = front; r.cut = cut;
		return r;
	};
	sb.loft({
		R(hy - 0.16, 0.12, 0.09, { { H, 1 } }, pantsC, denimM),
		R(hy - 0.1, hipW * 0.95, 0.105, { { H, 1 } }, pantsC, denimM),
		R(hy - 0.02, hipW, 0.115, { { H, 1 } }, pantsC, denimM),
		R(hy + 0.05, hipW * 0.95, 0.11, { { H, 0.85 }, { S, 0.15 } }, pantsC, denimM),
		R(hy + 0.05, hipW * 0.95, 0.11, { { H, 0.85 }, { S, 0.15 } }, belt, leather, 0, true),
		R(hy + 0.085, hipW * 0.93, 0.108, { { H, 0.7 }, { S, 0.3 } }, belt, leather),
		R(hy + 0.085, hipW * 0.93, 0.108, { { H, 0.7 }, { S, 0.3 } }, top, cloth, 0, true),
		R(sy + 0.02, waistW, 0.1, { { S, 0.8 }, { H, 0.2 } }, top, cloth, 0.01),
		R(sy + 0.1, waistW * 1.04, 0.102, { { S, 0.6 }, { C, 0.4 } }, top, cloth, 0.015),
		R(cy + 0.0, chestW * 0.96, 0.11, { { C, 0.7 }, { S, 0.3 } }, top, cloth, fem ? 0.03 : 0.02),
		R(cy + 0.1, chestW, 0.118, { { C, 1 } }, top, cloth, fem ? 0.045 : 0.025),
		R(cy + 0.18, shW * 0.92, 0.11, { { C, 1 } }, top, cloth, 0.01),
		R(cy + 0.21, shW * 0.64, 0.088, { { C, 1 } }, top, cloth),
		R(ny + 0.005, 0.078, 0.066, { { C, 0.75 }, { N, 0.25 } }, top, cloth, 0, false, 0),
	}, 14, true, false);
	// neck
	{
		std::vector<Ring> rs = {
			{ ny - 0.02, 0, 0, 0.064, 0.058, { { C, 0.7 }, { N, 0.3 } }, skinC, skin },
			{ ny + 0.04, 0, 0.004, 0.056, 0.053, { { N, 1 } }, skinC, skin },
			{ ny + 0.1, 0, 0.008, 0.054, 0.052, { { N, 0.4 }, { HD, 0.6 } }, skinC, skin },
			{ ny + 0.14, 0, 0.01, 0.05, 0.05, { { HD, 1 } }, skinC, skin },
		};
		sb.loft(rs, 10, false, false);
	}
	// collar / neckline trim
	if (!tank) sb.add(Geo::Torus(0.07, 0.011, 5, 16), M(0, ny + 0.012, 0.006, kPi / 2 - 0.25, 0, 0, 1.05, 1.08, 1), jacket ? jacket : shirtC, C);
	if (jacket) {
		sb.add(Geo::Box(0.1, 0.34, 0.012), M(0, cy + 0.04, chestW * 0.62 + 0.008), shirtC, C); // open front
		for (int s : { 1, -1 }) sb.add(Geo::Box(0.05, 0.16, 0.012), M(s * 0.06, cy + 0.14, chestW * 0.6 + 0.012, 0, 0, s * 0.35), Shade(jacket, 0.85), C); // lapels
	}
	// belt buckle & jeans pockets
	sb.add(Geo::Box(0.045, 0.032, 0.012), M(0, hy + 0.068, hipW * 0.62 + 0.004), 0xb8a060, H, metal);
	if (denimM == denim) for (int s : { 1, -1 }) sb.add(Geo::Box(0.075, 0.08, 0.006), M(s * 0.07, hy - 0.02, -0.117), Shade(pantsC, 0.82), H, denim);
	if (a.hasUniform && !a.noBadge) sb.add(Geo::Box(0.05, 0.06, 0.01), M(0.09, cy + 0.14, 0.13), 0xd4af37, C, metal); // badge
	// work / tactical vests over the shirt: a hi-vis vest with reflective bands, or a plate carrier with pouches
	if (!a.vest.empty()) {
		const bool tac = a.vest == "tactical";
		const uint32_t vc = a.vestColor >= 0 ? (uint32_t)a.vestColor : tac ? 0x2a2d26 : 0xd7ff1e;
		const uint32_t band = tac ? Shade(vc, 0.8) : 0xd8d8d8;
		const double k = tac ? 1.12 : 1.07;
		auto ring = [&](double y, double rx, double rz, W w, uint32_t c, bool cut = false, double front = 0.02) {
			Ring r{ y, 0, 0, rx * k, rz * k, std::move(w), c, cloth };
			r.sq = 0.6; r.front = front; r.cut = cut;
			return r;
		};
		std::vector<Ring> rows = {
			ring(sy - 0.02, waistW, 0.1, { { S, 0.8 }, { H, 0.2 } }, vc),
			ring(sy + 0.06, waistW * 1.02, 0.101, { { S, 0.7 }, { C, 0.3 } }, vc),
		};
		if (!tac) {
			rows.push_back(ring(sy + 0.06, waistW * 1.02, 0.101, { { S, 0.7 }, { C, 0.3 } }, band, true));
			rows.push_back(ring(sy + 0.1, waistW * 1.04, 0.102, { { S, 0.6 }, { C, 0.4 } }, band));
			rows.push_back(ring(sy + 0.1, waistW * 1.04, 0.102, { { S, 0.6 }, { C, 0.4 } }, vc, true));
		}
		rows.push_back(ring(cy + 0.0, chestW * 0.96, 0.11, { { C, 0.7 }, { S, 0.3 } }, vc));
		if (!tac) {
			rows.push_back(ring(cy + 0.06, chestW * 0.98, 0.114, { { C, 1 } }, vc));
			rows.push_back(ring(cy + 0.06, chestW * 0.98, 0.114, { { C, 1 } }, band, true));
			rows.push_back(ring(cy + 0.1, chestW, 0.118, { { C, 1 } }, band));
			rows.push_back(ring(cy + 0.1, chestW, 0.118, { { C, 1 } }, vc, true));
		} else rows.push_back(ring(cy + 0.1, chestW, 0.118, { { C, 1 } }, vc));
		rows.push_back(ring(cy + 0.17, shW * 0.8, 0.108, { { C, 1 } }, vc));
		sb.loft(rows, 14, false, false);
		if (tac) {
			for (double xx : { -0.07, 0.0, 0.07 }) sb.add(RBox(0.055, 0.075, 0.035), M(xx, sy + 0.08, chestW * 0.62 + 0.03), Shade(vc, 0.9), S, cloth); // mag pouches
			sb.add(RBox(0.12, 0.05, 0.02), M(0, cy + 0.08, chestW * 0.66 + 0.02), Shade(vc, 0.75), C, cloth);
		}
	}

	// ---------------- arms: shoulder to wrist in one piece, bending smoothly at the elbow
	const double armR = fem ? 0.044 : 0.052 * bw;
	const double sleeveEnd = sleeveLong ? 0 : tank ? 9 : 0.16; // metres below the shoulder
	struct ArmDef { int ua, fa, hd, sgn; };
	for (const ArmDef& ad : { ArmDef{ lUpperArm, lForearm, lHand, 1 }, ArmDef{ rUpperArm, rForearm, rHand, -1 } }) {
		const int ua = ad.ua, fa = ad.fa, hd = ad.hd, sgn = ad.sgn;
		const V3 u = wp[ua], f = wp[fa], h = wp[hd];
		const double x = u.x;
		const uint32_t sleeve = jacket ? jacket : shirtC;
		auto colAt = [&](double y) -> uint32_t { return tank ? skinC : (u.y - y < sleeveEnd || (sleeveLong && y > h.y + 0.02)) ? sleeve : skinC; };
		auto matAt = [&](double y) -> int { return colAt(y) == skinC && !(sleeveLong && y > h.y + 0.02) ? skin : cloth; };
		std::vector<Ring> rings;
		auto pushR = [&](double y, double r, W w, bool cut = false) {
			Ring q{ y, x + sgn * 0.004 * (y > u.y - 0.05 ? 1 : 0), 0, r, r * 0.95, std::move(w), colAt(y), matAt(y) };
			q.cut = cut;
			rings.push_back(q);
		};
		pushR(u.y + 0.035, armR * 0.6, { { C, 0.6 }, { ua, 0.4 } });
		pushR(u.y - 0.005, armR * 1.2, { { C, 0.45 }, { ua, 0.55 } });
		pushR(u.y - 0.06, armR * 1.1, { { ua, 0.9 }, { C, 0.1 } });
		if (!tank && !sleeveLong) {
			// short sleeve hem: a slightly wider ring, then the bare arm
			const double yh = u.y - sleeveEnd;
			pushR(yh + 0.004, armR * 1.12, { { ua, 1 } });
			rings.push_back(Ring{ yh + 0.002, x, 0, armR * 0.97, armR * 0.92, { { ua, 1 } }, sleeve, cloth }); // underside of the hem
			Ring sk{ yh + 0.002, x, 0, armR * 0.97, armR * 0.92, { { ua, 1 } }, skinC, skin }; sk.cut = true;
			rings.push_back(sk);
		}
		pushR(u.y - 0.2, armR * 0.95, { { ua, 1 } });
		pushR(f.y + 0.04, armR * 0.86, { { ua, 0.75 }, { fa, 0.25 } });
		pushR(f.y, armR * 0.82, { { ua, 0.5 }, { fa, 0.5 } });
		pushR(f.y - 0.04, armR * 0.86, { { fa, 0.8 }, { ua, 0.2 } });
		pushR(f.y - 0.12, armR * 0.88, { { fa, 1 } });
		if (sleeveLong) {
			pushR(h.y + 0.05, armR * 0.78, { { fa, 0.95 }, { hd, 0.05 } });
			pushR(h.y + 0.024, armR * 0.8, { { fa, 0.8 }, { hd, 0.2 } }); // cuff
			rings.push_back(Ring{ h.y + 0.022, x, 0, armR * 0.64, armR * 0.6, { { fa, 0.7 }, { hd, 0.3 } }, sleeve, cloth });
			Ring sk{ h.y + 0.022, x, 0, armR * 0.64, armR * 0.6, { { fa, 0.7 }, { hd, 0.3 } }, skinC, skin }; sk.cut = true;
			rings.push_back(sk);
			rings.push_back(Ring{ h.y - 0.005, x, 0, armR * 0.62, armR * 0.58, { { fa, 0.4 }, { hd, 0.6 } }, skinC, skin });
		} else {
			pushR(h.y + 0.03, armR * 0.68, { { fa, 0.85 }, { hd, 0.15 } });
			pushR(h.y - 0.005, armR * 0.62, { { fa, 0.4 }, { hd, 0.6 } });
		}
		std::stable_sort(rings.begin(), rings.end(), [](const Ring& p, const Ring& q) { return q.y < p.y; });
		sb.loft(rings, 10, true, true);
		// hand: palm, four fingers and a thumb (palm faces the thigh)
		const double hx = h.x, hyy = h.y;
		sb.add(RBox(0.026, 0.085, 0.078), M(hx, hyy - 0.05, 0.004), skinC, hd, skin);
		static const double LENS[4] = { 0.068, 0.076, 0.072, 0.058 };
		for (int k = 0; k < 4; k++) {
			const double z = 0.03 - k * 0.02, len = LENS[k];
			sb.add(Cap(0.0085, len - 0.017, 5), M(hx + sgn * 0.004, hyy - 0.095 - len / 2 + 0.006, z, 0, 0, sgn * 0.08), skinC, hd, skin);
		}
		sb.add(Cap(0.01, 0.035, 5), M(hx + sgn * 0.012, hyy - 0.055, 0.045, 0.55, 0, sgn * 0.3), skinC, hd, skin);
	}

	// ---------------- legs: hip to ankle, bending smoothly at the knee
	const double legR = fem ? 0.07 : 0.078 * bw;
	struct LegDef { int th, shn, ft, sgn; };
	for (const LegDef& ld : { LegDef{ lThigh, lShin, lFoot, 1 }, LegDef{ rThigh, rShin, rFoot, -1 } }) {
		const int th = ld.th, shn = ld.shn, ft = ld.ft, sgn = ld.sgn;
		const V3 t = wp[th], s = wp[shn], f = wp[ft];
		const double x = t.x;
		const double shortsHem = s.y + 0.08;
		auto colAt = [&](double y) -> uint32_t { return a.shorts && y < shortsHem ? skinC : pantsC; };
		auto matAt = [&](double y) -> int { return a.shorts && y < shortsHem ? skin : denimM; };
		std::vector<Ring> rings;
		auto L = [&](double y, double rx, double rz, W w, bool cut = false, double xo = 0) {
			Ring q{ y, x + xo, 0, rx, rz, std::move(w), colAt(y), matAt(y) };
			q.cut = cut;
			rings.push_back(q);
		};
		L(t.y + 0.05, legR * 0.95, legR, { { H, 0.6 }, { th, 0.4 } });
		L(t.y - 0.03, legR * 1.12, legR * 1.12, { { H, 0.3 }, { th, 0.7 } }, false, sgn * 0.008);
		L(t.y - 0.15, legR * 1.08, legR * 1.06, { { th, 1 } }, false, sgn * 0.01);
		L(t.y - 0.3, legR * 0.92, legR * 0.92, { { th, 1 } });
		if (a.shorts) {
			L(shortsHem + 0.004, legR * 0.98, legR * 0.98, { { th, 0.9 }, { shn, 0.1 } });
			rings.push_back(Ring{ shortsHem + 0.002, x, 0, legR * 0.8, legR * 0.82, { { th, 0.9 }, { shn, 0.1 } }, pantsC, denimM });
			Ring sk{ shortsHem + 0.002, x, 0, legR * 0.8, legR * 0.82, { { th, 0.9 }, { shn, 0.1 } }, skinC, skin }; sk.cut = true;
			rings.push_back(sk);
		}
		L(s.y + 0.04, legR * 0.78, legR * 0.82, { { th, 0.75 }, { shn, 0.25 } });
		L(s.y, legR * 0.75, legR * 0.8, { { th, 0.5 }, { shn, 0.5 } });
		L(s.y - 0.05, legR * 0.76, legR * 0.8, { { shn, 0.8 }, { th, 0.2 } });
		L(s.y - 0.15, legR * 0.82, legR * 0.88, { { shn, 1 } });
		L(f.y + 0.12, legR * 0.6, legR * 0.62, { { shn, 1 } });
		L(f.y + 0.05, legR * (a.shorts ? 0.52 : 0.66), legR * (a.shorts ? 0.54 : 0.66), { { shn, 0.7 }, { ft, 0.3 } });
		L(f.y + 0.01, legR * (a.shorts ? 0.5 : 0.64), legR * (a.shorts ? 0.52 : 0.64), { { shn, 0.4 }, { ft, 0.6 } });
		std::stable_sort(rings.begin(), rings.end(), [](const Ring& p, const Ring& q) { return q.y < p.y; });
		sb.loft(rings, 10, false, true);
		// shoe: upper, toe cap, sole, laces
		sb.add(RBox(0.098, 0.075, 0.25), M(f.x, f.y - 0.022, f.z + 0.052), a.shoes, ft, leather);
		sb.add(Geo::Sphere(0.05, 8, 5, 0, kTau, 0, kPi / 2), M(f.x, f.y - 0.045, f.z + 0.15, 0, 0, 0, 0.98, 0.9, 0.9), a.shoes, ft, leather);
		sb.add(RBox(0.104, 0.022, 0.262), M(f.x, f.y - 0.058, f.z + 0.053), 0xe8e4dc, ft, leather);
		sb.add(Geo::Box(0.04, 0.008, 0.07), M(f.x, f.y + 0.017, f.z + 0.075, -0.25, 0, 0), Shade(a.shoes, 0.6), ft, cloth);
	}

	// ---------------- head
	const double hdy = wp[head].y + 0.1;
	sb.add(Geo::Sphere(0.105, 18, 14), M(0, hdy, 0.0, 0, 0, 0, 0.9, 1.1, 1.0), skinC, HD, skin);
	sb.add(Geo::Sphere(0.066, 12, 8), M(0, hdy - 0.066, 0.03, 0.2, 0, 0, 1.05, 0.82, 1.02), skinC, HD, skin); // jaw & chin
	for (int s : { 1, -1 }) sb.add(Geo::Sphere(0.024, 8, 6), M(s * 0.048, hdy - 0.02, 0.058, 0, 0, 0, 1, 0.85, 0.8), skinC, HD, skin); // cheekbones
	sb.add(Geo::Box(0.1, 0.022, 0.03), M(0, hdy + 0.045, 0.085, -0.2, 0, 0), skinC, HD, skin); // brow ridge
	// nose: bridge + tip + nostrils
	sb.add(Geo::Box(0.02, 0.05, 0.03), M(0, hdy + 0.01, 0.1, -0.25, 0, 0), skinC, HD, skin);
	sb.add(Geo::Sphere(0.0135, 8, 6), M(0, hdy - 0.016, 0.112, 0, 0, 0, 1.15, 0.9, 1), skinC, HD, skin);
	for (int s : { 1, -1 }) sb.add(Geo::Sphere(0.0085, 6, 4), M(s * 0.012, hdy - 0.021, 0.105), Shade(skinC, 0.92), HD, skin);
	// eyes: white, iris, and a lid line
	static const uint32_t IRIS[4] = { 0x3b2a1e, 0x2e4a6e, 0x3c5a3a, 0x1c1410 };
	const uint32_t iris = IRIS[((a.skin % 97) + (a.hair % 13)) % 4];
	for (int s : { 1, -1 }) {
		sb.add(Geo::Sphere(0.0135, 10, 8), M(s * 0.035, hdy + 0.022, 0.084, 0, 0, 0, 1.15, 0.72, 0.8), 0xe6e0d8, HD, eye);
		sb.add(Geo::Sphere(0.0068, 8, 6), M(s * 0.035, hdy + 0.022, 0.0935, 0, 0, 0, 1, 1, 0.5), iris, HD, eye);
		sb.add(Geo::Sphere(0.003, 6, 4), M(s * 0.035, hdy + 0.022, 0.0968), 0x050505, HD, eye);
		sb.add(Geo::Box(0.034, 0.007, 0.016), M(s * 0.035, hdy + 0.0325, 0.09, -0.35, 0, 0), Shade(skinC, 0.85), HD, skin); // upper lid
		sb.add(Geo::Box(0.038, 0.009, 0.012), M(s * 0.037, hdy + 0.056, 0.098, -0.15, 0, s * -0.12), a.hair, HD, hair); // brows
		sb.add(Geo::Sphere(0.024, 8, 6), M(s * 0.1, hdy + 0.0, -0.005, 0, 0, 0, 0.45, 1, 0.75), skinC, HD, skin); // ears
	}
	// lips
	sb.add(Cap(0.0085, 0.034, 6), M(0, hdy - 0.047, 0.1, 0, 0, kPi / 2, 1, 1, 0.9), lip, HD, skin);
	sb.add(Cap(0.0095, 0.03, 6), M(0, hdy - 0.061, 0.096, 0, 0, kPi / 2, 1, 1, 0.9), lip, HD, skin);
	if (a.beard) {
		sb.add(Geo::Sphere(0.07, 10, 7, 0, kTau, kPi * 0.35, kPi * 0.65), M(0, hdy - 0.045, 0.03, 0.15, 0, 0, 1.06, 1.0, 1.08), a.hair, HD, hair);
		sb.add(Geo::Box(0.05, 0.01, 0.012), M(0, hdy - 0.038, 0.108), a.hair, HD, hair); // moustache
	}
	if (a.glasses) {
		for (int s : { 1, -1 }) sb.add(Geo::Box(0.046, 0.03, 0.006), M(s * 0.037, hdy + 0.022, 0.112), 0x080808, HD, eye);
		sb.add(Geo::Box(0.03, 0.006, 0.006), M(0, hdy + 0.03, 0.112), 0x111111, HD, metal);
		for (int s : { 1, -1 }) sb.add(Geo::Box(0.004, 0.006, 0.1), M(s * 0.062, hdy + 0.03, 0.065), 0x111111, HD, metal);
	}
	// hair
	const uint32_t hairC = a.hair;
	auto scalp = [&](double sz, double cut) { sb.add(Geo::Sphere(0.112 * sz, 16, 10, 0, kTau, 0, kPi * cut), M(0, hdy + 0.008, -0.006, -0.22, 0, 0, 0.94, 1.08, 1.05), hairC, HD, hair); };
	const std::string& hs = a.hairStyle;
	if (hs == "short") {
		scalp(1.005, 0.54);
		for (int s : { 1, -1 }) sb.add(Geo::Box(0.01, 0.04, 0.026), M(s * 0.092, hdy + 0.018, 0.035), hairC, HD, hair); // sideburns
	} else if (hs == "buzz") scalp(0.985, 0.46);
	else if (hs == "afro") sb.add(Geo::Icosahedron(0.15, 2), M(0, hdy + 0.06, -0.025, 0, 0, 0, 1, 0.92, 1), hairC, HD, hair);
	else if (hs == "long") {
		scalp(1.02, 0.56);
		sb.add(RBox(0.22, 0.28, 0.08), M(0, hdy - 0.09, -0.075, 0.08, 0, 0), hairC, HD, hair);
		for (int s : { 1, -1 }) sb.add(RBox(0.04, 0.2, 0.09), M(s * 0.1, hdy - 0.05, -0.01), hairC, HD, hair);
	} else if (hs == "ponytail") {
		scalp(1.01, 0.56);
		sb.add(Geo::Sphere(0.03, 8, 6), M(0, hdy + 0.02, -0.12), hairC, HD, hair);
		sb.add(Cap(0.032, 0.17, 7), M(0, hdy - 0.07, -0.14, 0.35, 0, 0), hairC, HD, hair);
	} else if (hs == "bun") {
		scalp(1.01, 0.56);
		sb.add(Geo::Sphere(0.052, 10, 8), M(0, hdy + 0.105, -0.075), hairC, HD, hair);
	} else if (hs == "cap") {
		const uint32_t hat = a.hat > 0 ? (uint32_t)a.hat : 0x222222; // (a.hat || 0x222222)
		sb.add(Geo::Sphere(0.118, 16, 10, 0, kTau, 0, kPi * 0.5), M(0, hdy + 0.012, -0.004, -0.08, 0, 0, 0.95, 1.05, 1.04), hat, HD, cloth);
		sb.add(Geo::Cylinder(0.085, 0.09, 0.012, 14, 1, false, -kPi / 2, kPi), M(0, hdy + 0.03, 0.09, 0.1, 0, 0, 1, 1, 1.25), hat, HD, cloth);
		sb.add(Geo::Sphere(0.012, 6, 4), M(0, hdy + 0.128, 0), Shade(hat, 0.7), HD, cloth);
	}
	if (a.bandana > 0) sb.add(Geo::Cylinder(0.108, 0.11, 0.045, 16), M(0, hdy + 0.05, -0.005, -0.1, 0, 0, 0.95, 1, 1.02), (uint32_t)a.bandana, HD, cloth);
	// a balaclava: a knitted hood over the head and face, eyes showing
	if (a.mask > 0) {
		sb.add(Geo::Sphere(0.122, 16, 12), M(0, hdy + 0.005, 0.0, 0, 0, 0, 0.93, 1.13, 1.06), (uint32_t)a.mask, HD, cloth);
		sb.add(Geo::Cylinder(0.066, 0.07, 0.1, 12), M(0, ny + 0.06, 0.004), (uint32_t)a.mask, N, cloth);
	}
	// a hard hat: shell, brim all round, a ridge on top
	if (a.hardhat > 0) {
		const uint32_t hh = (uint32_t)a.hardhat;
		sb.add(Geo::Sphere(0.128, 16, 10, 0, kTau, 0, kPi * 0.5), M(0, hdy + 0.03, -0.004, 0, 0, 0, 0.96, 1.08, 1.06), hh, HD, leather);
		sb.add(Geo::Cylinder(0.15, 0.15, 0.012, 18), M(0, hdy + 0.032, 0.012, 0.05, 0, 0, 0.95, 1, 1.12), hh, HD, leather);
		sb.add(Geo::Box(0.03, 0.02, 0.24), M(0, hdy + 0.16, -0.004), Shade(hh, 0.85), HD, leather);
	}
	if (a.hasUniform && a.uniformHat > 0) {
		const uint32_t uh = (uint32_t)a.uniformHat;
		sb.add(Geo::Cylinder(0.108, 0.113, 0.07, 16), M(0, hdy + 0.07, 0), uh, HD, cloth);
		sb.add(Geo::Cylinder(0.125, 0.125, 0.015, 16), M(0, hdy + 0.11, 0), uh, HD, cloth);
		sb.add(Geo::Box(0.16, 0.01, 0.08), M(0, hdy + 0.045, 0.12, 0.1, 0, 0), 0x111111, HD, leather);
		sb.add(Geo::Box(0.03, 0.03, 0.006), M(0, hdy + 0.08, 0.113), 0xd4af37, HD, metal);
		if (a.uniformBand > 0) sb.add(Geo::Cylinder(0.111, 0.115, 0.018, 16, 1, true), M(0, hdy + 0.05, 0), (uint32_t)a.uniformBand, HD, metal); // gold braid
	}
	sb.finish();
	return out;
}

} // namespace atg
