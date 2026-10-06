#include "Loft.h"
#include <map>

namespace atg {

namespace {
double SrgbToLinear(double c) { return c < 0.04045 ? c * 0.0773993808 : std::pow(c * 0.9478672986 + 0.0521327014, 2.4); }
Pt3 Sub(const Pt3& a, const Pt3& b) { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
Pt3 Cross(const Pt3& a, const Pt3& b) { return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; }
double Len(const Pt3& a) { return Hypot3(a[0], a[1], a[2]); }
double Dot(const Pt3& a, const Pt3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
}

Part& Part::hex(uint32_t h) {
	col[0] = SrgbToLinear(((h >> 16) & 255) / 255.0);
	col[1] = SrgbToLinear(((h >> 8) & 255) / 255.0);
	col[2] = SrgbToLinear((h & 255) / 255.0);
	return *this;
}

uint32_t Part::v(double x, double y, double z, double nx, double ny, double nz, const double c[3]) {
	m.Color(c[0], c[1], c[2]);
	return m.V(x, y, z, nx, ny, nz);
}

void Part::poly(const std::vector<Pt3>& pts, const Pt3& n) {
	if (pts.size() < 3) return;
	const uint32_t i0 = v(pts[0][0], pts[0][1], pts[0][2], n[0], n[1], n[2]);
	uint32_t prev = v(pts[1][0], pts[1][1], pts[1][2], n[0], n[1], n[2]);
	for (size_t k = 2; k < pts.size(); k++) {
		const uint32_t cur = v(pts[k][0], pts[k][1], pts[k][2], n[0], n[1], n[2]);
		const Pt3 a = Sub(pts[k - 1], pts[0]), b = Sub(pts[k], pts[0]);
		const Pt3 c = Cross(a, b);
		if (Dot(c, n) >= 0) tri(i0, prev, cur); else tri(i0, cur, prev);
		prev = cur;
	}
}

void Part::box(double x0, double y0, double z0, double x1, double y1, double z1, bool bottom) {
	poly({ { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } }, { 0, 0, 1 });
	poly({ { x1, y0, z0 }, { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 } }, { 0, 0, -1 });
	poly({ { x1, y0, z1 }, { x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 } }, { 1, 0, 0 });
	poly({ { x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 } }, { -1, 0, 0 });
	poly({ { x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 }, { x0, y1, z0 } }, { 0, 1, 0 });
	if (bottom) poly({ { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 } }, { 0, -1, 0 });
}

void Part::geo(const MeshBuf& g, const Mat4& matrix) {
	const uint32_t base = (uint32_t)m.Count();
	for (size_t i = 0; i < g.Count(); i++) {
		double x = g.P[i * 3], y = g.P[i * 3 + 1], z = g.P[i * 3 + 2];
		double nx = g.N[i * 3], ny = g.N[i * 3 + 1], nz = g.N[i * 3 + 2];
		matrix.Apply(x, y, z);
		matrix.ApplyNormal(nx, ny, nz);
		v(x, y, z, nx, ny, nz);
	}
	for (uint32_t k : g.I) m.I.push_back(base + k);
}

std::function<double(double)> SmoothLine(std::vector<Pt2> P) {
	std::stable_sort(P.begin(), P.end(), [](const Pt2& a, const Pt2& b) { return a[0] - b[0] < 0; });
	const int n = (int)P.size();
	std::vector<double> xs(n), ys(n);
	for (int i = 0; i < n; i++) { xs[i] = P[i][0]; ys[i] = P[i][1]; }
	if (n == 1) { const double y0 = ys[0]; return [y0](double) { return y0; }; }
	std::vector<double> d, m(n, 0.0);
	for (int i = 0; i < n - 1; i++) d.push_back((ys[i + 1] - ys[i]) / Max(1e-6, xs[i + 1] - xs[i]));
	m[0] = d[0]; m[n - 1] = d[n - 2];
	for (int i = 1; i < n - 1; i++) m[i] = d[i - 1] * d[i] <= 0 ? 0 : (d[i - 1] + d[i]) / 2;
	for (int i = 0; i < n - 1; i++) {
		if (d[i] == 0) { m[i] = 0; m[i + 1] = 0; continue; }
		const double a = m[i] / d[i], b = m[i + 1] / d[i], s = a * a + b * b;
		if (s > 9) { const double t = 3 / std::sqrt(s); m[i] = t * a * d[i]; m[i + 1] = t * b * d[i]; }
	}
	return [xs, ys, m, n](double x) {
		if (x <= xs[0]) return ys[0];
		if (x >= xs[n - 1]) return ys[n - 1];
		int i = 0;
		while (x > xs[i + 1]) i++;
		const double h = xs[i + 1] - xs[i], t = (x - xs[i]) / h, t2 = t * t, t3 = t2 * t;
		return (2 * t3 - 3 * t2 + 1) * ys[i] + (t3 - 2 * t2 + t) * h * m[i] + (-2 * t3 + 3 * t2) * ys[i + 1] + (t3 - t2) * h * m[i + 1];
	};
}

std::vector<double> Stations(double a, double b, double step, const std::vector<double>& extra) {
	std::vector<double> out;
	const int n = (int)Max(1, std::ceil((b - a) / step));
	for (int i = 0; i <= n; i++) out.push_back(a + (b - a) * i / n);
	for (double e : extra) if (e > a + 1e-4 && e < b - 1e-4) out.push_back(e);
	std::stable_sort(out.begin(), out.end());
	std::vector<double> res;
	for (double z : out) if (res.empty() || z - res.back() > 0.012) res.push_back(z);
	res.back() = b;
	return res;
}

void EmitGrid(const std::vector<std::vector<Pt3>>& rows, const std::function<Part*(int, int)>& pick, const GridOpts& o) {
	const int ni = (int)rows.size();
	if (ni < 2) return;
	const int nj = (int)rows[0].size();
	if (nj < 2) return;
	std::vector<std::vector<Pt3>> N(ni, std::vector<Pt3>(nj));
	for (int i = 0; i < ni; i++) for (int j = 0; j < nj; j++) {
		Pt3 du = Sub(rows[(std::min)(ni - 1, i + 1)][j], rows[(std::max)(0, i - 1)][j]);
		Pt3 dv = Sub(rows[i][(std::min)(nj - 1, j + 1)], rows[i][(std::max)(0, j - 1)]);
		const Pt3 diag = Sub(rows[(std::min)(ni - 1, i + 1)][(std::min)(nj - 1, j + 1)], rows[(std::max)(0, i - 1)][(std::max)(0, j - 1)]);
		if (Len(du) < 1e-6) du = diag;
		if (Len(dv) < 1e-6) dv = diag;
		const Pt3 c = Cross(du, dv);
		double l = Len(c); if (l == 0) l = 1;
		N[i][j] = { c[0] / l, c[1] / l, c[2] / l };
	}
	double s = 0;
	for (int i = 0; i < ni; i += (std::max)(1, ni >> 2)) for (int j = 0; j < nj; j += (std::max)(1, nj >> 2)) {
		const Pt3& p = rows[i][j]; const Pt3& n = N[i][j];
		s += (p[0] - o.ref[0]) * n[0] + (p[1] - o.ref[1]) * n[1] + (p[2] - o.ref[2]) * n[2];
	}
	const bool flip = (s < 0) != o.inward;
	if (flip) for (auto& row : N) for (Pt3& n : row) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
	std::map<Part*, std::map<int, uint32_t>> maps;
	auto vid = [&](Part* part, int i, int j) {
		std::map<int, uint32_t>& mm = maps[part];
		const int k = i * 4096 + j;
		auto it = mm.find(k);
		if (it != mm.end()) return it->second;
		const Pt3& p = rows[i][j]; const Pt3& n = N[i][j];
		const double off = o.offset ? o.offset(part) : 0;
		const double x = p[0] + n[0] * off, y = p[1] + n[1] * off, z = p[2] + n[2] * off;
		uint32_t id;
		if (o.colorAt) { const Pt3 c = o.colorAt(x, y, z, part); const double cc[3] = { c[0], c[1], c[2] }; id = part->v(x, y, z, n[0], n[1], n[2], cc); }
		else id = part->v(x, y, z, n[0], n[1], n[2]);
		mm[k] = id;
		return id;
	};
	for (int i = 0; i < ni - 1; i++) for (int j = 0; j < nj - 1; j++) {
		Part* part = pick(i, j);
		if (!part) continue;
		const uint32_t a = vid(part, i, j), b = vid(part, i + 1, j), c = vid(part, i + 1, j + 1), d = vid(part, i, j + 1);
		const Pt3& pa = rows[i][j]; const Pt3& pb = rows[i + 1][j]; const Pt3& pc = rows[i + 1][j + 1];
		Pt3 g = Cross(Sub(pb, pa), Sub(pc, pa));
		if (Len(g) < 1e-9) g = Cross(Sub(pc, pa), Sub(rows[i][j + 1], pa));
		if (Dot(g, N[i][j]) >= 0) { part->tri(a, b, c); part->tri(a, c, d); } else { part->tri(a, c, b); part->tri(a, d, c); }
	}
}

std::vector<Pt2> RoundRect(double hw, double y0, double y1, double r, int seg) {
	r = Min(r, Min(hw, (y1 - y0) / 2));
	std::vector<Pt2> out;
	auto arc = [&](double cx, double cy, double a0) { for (int k = 0; k <= seg; k++) { const double a = a0 + ((double)k / seg) * kPi / 2; out.push_back({ cx + std::cos(a) * r, cy + std::sin(a) * r }); } };
	arc(hw - r, y0 + r, -kPi / 2); arc(hw - r, y1 - r, 0); arc(-hw + r, y1 - r, kPi / 2); arc(-hw + r, y0 + r, kPi);
	return out;
}

uint32_t Sweep(Part& part, const std::vector<Pt3>& path, const std::vector<Pt2>& section, bool closeEnds, bool left) {
	const int np = (int)path.size(), ns = (int)section.size();
	std::vector<std::vector<Pt3>> rows;
	auto horiz = [&](int i, double& ox, double& oz) {
		const Pt3& a = path[(std::max)(0, i - 1)]; const Pt3& b = path[(std::min)(np - 1, i + 1)];
		double tx = b[0] - a[0], tz = b[2] - a[2];
		double l = Hypot(tx, tz); if (l == 0) l = 1; tx /= l; tz /= l;
		ox = left ? -tz : tz; oz = left ? tx : -tx;
	};
	for (int i = 0; i < np; i++) {
		double ox, oz; horiz(i, ox, oz);
		std::vector<Pt3> row;
		for (const Pt2& s : section) row.push_back({ path[i][0] + ox * s[0], path[i][1] + s[1], path[i][2] + oz * s[0] });
		row.push_back(row[0]);
		rows.push_back(row);
	}
	double su = 0, sv = 0;
	for (const Pt2& s : section) { su += s[0] / ns; sv += s[1] / ns; }
	const uint32_t base = (uint32_t)part.n();
	std::vector<std::vector<uint32_t>> idx;
	for (int i = 0; i < np; i++) {
		double ox, oz; horiz(i, ox, oz);
		std::vector<uint32_t> ir;
		for (int j = 0; j <= ns; j++) {
			const Pt2& s = section[j % ns];
			const Pt2& p0 = section[(j - 1 + ns) % ns]; const Pt2& p1 = section[(j + 1) % ns];
			double nu = p1[1] - p0[1], nv = -(p1[0] - p0[0]);
			if (nu * (s[0] - su) + nv * (s[1] - sv) < 0) { nu = -nu; nv = -nv; }
			double ll = Hypot(nu, nv); if (ll == 0) ll = 1; nu /= ll; nv /= ll;
			const Pt3& p = rows[i][j];
			ir.push_back(part.v(p[0], p[1], p[2], ox * nu, nv, oz * nu));
		}
		idx.push_back(ir);
	}
	for (int i = 0; i < np - 1; i++) for (int j = 0; j < ns; j++) {
		const uint32_t a = idx[i][j], b = idx[i + 1][j], cc = idx[i + 1][j + 1], d = idx[i][j + 1];
		const Pt3& pa = rows[i][j]; const Pt3& pb = rows[i + 1][j]; const Pt3& pc = rows[i + 1][j + 1];
		const Pt3 g = Cross(Sub(pb, pa), Sub(pc, pa));
		const Pt3 n = part.normalOf(a);
		if (Dot(g, n) >= 0) { part.tri(a, b, cc); part.tri(a, cc, d); } else { part.tri(a, cc, b); part.tri(a, d, cc); }
	}
	if (closeEnds) {
		for (int i : { 0, np - 1 }) {
			std::vector<Pt3> ring(rows[i].begin(), rows[i].begin() + ns);
			const Pt3& a = path[(std::max)(0, i - 1)]; const Pt3& b = path[(std::min)(np - 1, i + 1)];
			const double tx = b[0] - a[0], ty = b[1] - a[1], tz = b[2] - a[2];
			double l = Hypot3(tx, ty, tz); if (l == 0) l = 1;
			const double s = i == 0 ? -1 : 1;
			part.poly(ring, { tx / l * s, ty / l * s, tz / l * s });
		}
	}
	return base;
}

void RoundBox(Part& part, double x0, double x1, double y0, double y1, double z0, double z1, double r, int seg) {
	const double hw = (x1 - x0) / 2, cx = (x0 + x1) / 2;
	std::vector<Pt2> sec = RoundRect(hw, y0, y1, r, seg);
	for (Pt2& p : sec) p[0] += cx;
	std::vector<std::vector<Pt3>> rows;
	for (double z : { z0, z1 }) { std::vector<Pt3> row; for (const Pt2& p : sec) row.push_back({ p[0], p[1], z }); row.push_back(row[0]); rows.push_back(row); }
	GridOpts o; o.ref = { cx, (y0 + y1) / 2, (z0 + z1) / 2 };
	EmitGrid(rows, [&](int, int) { return &part; }, o);
	std::vector<Pt3> e1, e0;
	for (const Pt2& p : sec) e1.push_back({ p[0], p[1], z1 });
	for (auto it = sec.rbegin(); it != sec.rend(); ++it) e0.push_back({ (*it)[0], (*it)[1], z0 });
	part.poly(e1, { 0, 0, 1 });
	part.poly(e0, { 0, 0, -1 });
}

} // namespace atg
