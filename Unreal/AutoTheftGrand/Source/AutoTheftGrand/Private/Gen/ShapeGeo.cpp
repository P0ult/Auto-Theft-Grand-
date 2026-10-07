#include "ShapeGeo.h"
#include <algorithm>
#include <deque>

namespace atg {

// ------------------------------------------------------------------ earcut (mapbox earcut 3.0.2)
namespace {
struct ENode {
	uint32_t i; double x, y;
	ENode* prev = nullptr; ENode* next = nullptr;
	bool steiner = false;
};
struct Earcutter {
	std::deque<ENode> pool;
	std::vector<uint32_t>& triangles;
	explicit Earcutter(std::vector<uint32_t>& t) : triangles(t) {}

	ENode* createNode(uint32_t i, double x, double y) { pool.push_back({ i, x, y }); return &pool.back(); }
	ENode* insertNode(uint32_t i, double x, double y, ENode* last) {
		ENode* p = createNode(i, x, y);
		if (!last) { p->prev = p; p->next = p; }
		else { p->next = last->next; p->prev = last; last->next->prev = p; last->next = p; }
		return p;
	}
	static void removeNode(ENode* p) { p->next->prev = p->prev; p->prev->next = p->next; }
	static double area(const ENode* p, const ENode* q, const ENode* r) { return (q->y - p->y) * (r->x - q->x) - (q->x - p->x) * (r->y - q->y); }
	static bool equals(const ENode* a, const ENode* b) { return a->x == b->x && a->y == b->y; }
	static int sign(double n) { return n > 0 ? 1 : n < 0 ? -1 : 0; }
	static bool onSegment(const ENode* p, const ENode* q, const ENode* r) { return q->x <= std::max(p->x, r->x) && q->x >= std::min(p->x, r->x) && q->y <= std::max(p->y, r->y) && q->y >= std::min(p->y, r->y); }
	static bool intersects(const ENode* p1, const ENode* q1, const ENode* p2, const ENode* q2) {
		const int o1 = sign(area(p1, q1, p2)), o2 = sign(area(p1, q1, q2)), o3 = sign(area(p2, q2, p1)), o4 = sign(area(p2, q2, q1));
		if (o1 != o2 && o3 != o4) return true;
		if (o1 == 0 && onSegment(p1, p2, q1)) return true;
		if (o2 == 0 && onSegment(p1, q2, q1)) return true;
		if (o3 == 0 && onSegment(p2, p1, q2)) return true;
		if (o4 == 0 && onSegment(p2, q1, q2)) return true;
		return false;
	}
	static bool intersectsPolygon(const ENode* a, const ENode* b) {
		const ENode* p = a;
		do {
			if (p->i != a->i && p->next->i != a->i && p->i != b->i && p->next->i != b->i && intersects(p, p->next, a, b)) return true;
			p = p->next;
		} while (p != a);
		return false;
	}
	static bool locallyInside(const ENode* a, const ENode* b) {
		return area(a->prev, a, a->next) < 0 ? area(a, b, a->next) >= 0 && area(a, a->prev, b) >= 0 : area(a, b, a->prev) < 0 || area(a, a->next, b) < 0;
	}
	static bool middleInside(const ENode* a, const ENode* b) {
		const ENode* p = a;
		bool inside = false;
		const double px = (a->x + b->x) / 2, py = (a->y + b->y) / 2;
		do {
			if (((p->y > py) != (p->next->y > py)) && p->next->y != p->y && (px < (p->next->x - p->x) * (py - p->y) / (p->next->y - p->y) + p->x)) inside = !inside;
			p = p->next;
		} while (p != a);
		return inside;
	}
	static bool isValidDiagonal(const ENode* a, const ENode* b) {
		return a->next->i != b->i && a->prev->i != b->i && !intersectsPolygon(a, b) &&
			((locallyInside(a, b) && locallyInside(b, a) && middleInside(a, b) && (area(a->prev, a, b->prev) != 0 || area(a, b->prev, b) != 0)) ||
			 (equals(a, b) && area(a->prev, a, a->next) > 0 && area(b->prev, b, b->next) > 0));
	}
	static bool pointInTriangle(double ax, double ay, double bx, double by, double cx, double cy, double px, double py) {
		return (cx - px) * (ay - py) >= (ax - px) * (cy - py) && (ax - px) * (by - py) >= (bx - px) * (ay - py) && (bx - px) * (cy - py) >= (cx - px) * (by - py);
	}
	static bool pointInTriangleExceptFirst(double ax, double ay, double bx, double by, double cx, double cy, double px, double py) {
		return !(ax == px && ay == py) && pointInTriangle(ax, ay, bx, by, cx, cy, px, py);
	}
	static double signedArea(const std::vector<double>& d, size_t start, size_t end) {
		double sum = 0;
		for (size_t i = start, j = end - 2; i < end; i += 2) { sum += (d[j] - d[i]) * (d[i + 1] + d[j + 1]); j = i; }
		return sum;
	}
	ENode* linkedList(const std::vector<double>& d, size_t start, size_t end, bool clockwise) {
		ENode* last = nullptr;
		if (clockwise == (signedArea(d, start, end) > 0)) { for (size_t i = start; i < end; i += 2) last = insertNode((uint32_t)(i / 2), d[i], d[i + 1], last); }
		else { for (long long i = (long long)end - 2; i >= (long long)start; i -= 2) last = insertNode((uint32_t)(i / 2), d[i], d[i + 1], last); }
		if (last && equals(last, last->next)) { removeNode(last); last = last->next; }
		return last;
	}
	ENode* filterPoints(ENode* start, ENode* end = nullptr) {
		if (!start) return start;
		if (!end) end = start;
		ENode* p = start;
		bool again;
		do {
			again = false;
			if (!p->steiner && (equals(p, p->next) || area(p->prev, p, p->next) == 0)) {
				removeNode(p);
				p = end = p->prev;
				if (p == p->next) break;
				again = true;
			} else p = p->next;
		} while (again || p != end);
		return end;
	}
	bool isEar(const ENode* ear) const {
		const ENode* a = ear->prev; const ENode* b = ear; const ENode* c = ear->next;
		if (area(a, b, c) >= 0) return false;
		const double ax = a->x, bx = b->x, cx = c->x, ay = a->y, by = b->y, cy = c->y;
		const double x0 = std::min({ ax, bx, cx }), y0 = std::min({ ay, by, cy }), x1 = std::max({ ax, bx, cx }), y1 = std::max({ ay, by, cy });
		const ENode* p = c->next;
		while (p != a) {
			if (p->x >= x0 && p->x <= x1 && p->y >= y0 && p->y <= y1 && pointInTriangleExceptFirst(ax, ay, bx, by, cx, cy, p->x, p->y) && area(p->prev, p, p->next) >= 0) return false;
			p = p->next;
		}
		return true;
	}
	ENode* cureLocalIntersections(ENode* start) {
		ENode* p = start;
		do {
			ENode* a = p->prev; ENode* b = p->next->next;
			if (!equals(a, b) && intersects(a, p, p->next, b) && locallyInside(a, b) && locallyInside(b, a)) {
				triangles.push_back(a->i); triangles.push_back(p->i); triangles.push_back(b->i);
				removeNode(p); removeNode(p->next);
				p = start = b;
			}
			p = p->next;
		} while (p != start);
		return filterPoints(p);
	}
	ENode* splitPolygon(ENode* a, ENode* b) {
		ENode* a2 = createNode(a->i, a->x, a->y); ENode* b2 = createNode(b->i, b->x, b->y);
		ENode* an = a->next; ENode* bp = b->prev;
		a->next = b; b->prev = a; a2->next = an; an->prev = a2; b2->next = a2; a2->prev = b2; bp->next = b2; b2->prev = bp;
		return b2;
	}
	void splitEarcut(ENode* start) {
		ENode* a = start;
		do {
			ENode* b = a->next->next;
			while (b != a->prev) {
				if (a->i != b->i && isValidDiagonal(a, b)) {
					ENode* c = splitPolygon(a, b);
					a = filterPoints(a, a->next);
					c = filterPoints(c, c->next);
					earcutLinked(a, 0);
					earcutLinked(c, 0);
					return;
				}
				b = b->next;
			}
			a = a->next;
		} while (a != start);
	}
	void earcutLinked(ENode* ear, int pass) {
		if (!ear) return;
		ENode* stop = ear;
		while (ear->prev != ear->next) {
			ENode* prev = ear->prev; ENode* next = ear->next;
			if (isEar(ear)) {
				triangles.push_back(prev->i); triangles.push_back(ear->i); triangles.push_back(next->i);
				removeNode(ear);
				ear = next->next;
				stop = next->next;
				continue;
			}
			ear = next;
			if (ear == stop) {
				if (!pass) earcutLinked(filterPoints(ear), 1);
				else if (pass == 1) { ear = cureLocalIntersections(filterPoints(ear)); earcutLinked(ear, 2); }
				else if (pass == 2) splitEarcut(ear);
				break;
			}
		}
	}
};
}

std::vector<uint32_t> Earcut(const std::vector<double>& data) {
	std::vector<uint32_t> tris;
	Earcutter e(tris);
	ENode* outer = e.linkedList(data, 0, data.size(), true);
	if (!outer || outer->next == outer->prev) return tris;
	// (z-order hashing only above 80 points: not needed here)
	e.earcutLinked(outer, 0);
	return tris;
}

// ------------------------------------------------------------------ ExtrudeGeometry
namespace {
double ShapeArea(const std::vector<Pt2>& c) {
	const size_t n = c.size();
	double a = 0;
	for (size_t p = n - 1, q = 0; q < n; p = q++) a += c[p][0] * c[q][1] - c[q][0] * c[p][1];
	return a * 0.5;
}

Pt2 BevelVec(const Pt2& inPt, const Pt2& inPrev, const Pt2& inNext) {
	const double EPS = 2.220446049250313e-16;
	double vtx, vty, shrink;
	const double vpx = inPt[0] - inPrev[0], vpy = inPt[1] - inPrev[1];
	const double vnx = inNext[0] - inPt[0], vny = inNext[1] - inPt[1];
	const double vpl2 = vpx * vpx + vpy * vpy;
	const double col = vpx * vny - vpy * vnx;
	if (std::fabs(col) > EPS) {
		const double vpl = std::sqrt(vpl2), vnl = std::sqrt(vnx * vnx + vny * vny);
		const double ppx = inPrev[0] - vpy / vpl, ppy = inPrev[1] + vpx / vpl;
		const double pnx = inNext[0] - vny / vnl, pny = inNext[1] + vnx / vnl;
		const double sf = ((pnx - ppx) * vny - (pny - ppy) * vnx) / (vpx * vny - vpy * vnx);
		vtx = ppx + vpx * sf - inPt[0];
		vty = ppy + vpy * sf - inPt[1];
		const double vtl2 = vtx * vtx + vty * vty;
		if (vtl2 <= 2) return { vtx, vty };
		shrink = std::sqrt(vtl2 / 2);
	} else {
		bool eq = false;
		if (vpx > EPS) { if (vnx > EPS) eq = true; }
		else if (vpx < -EPS) { if (vnx < -EPS) eq = true; }
		else if ((vpy > 0) - (vpy < 0) == (vny > 0) - (vny < 0)) eq = true;
		if (eq) { vtx = -vpy; vty = vpx; shrink = std::sqrt(vpl2); }
		else { vtx = vpx; vty = vpy; shrink = std::sqrt(vpl2 / 2); }
	}
	return { vtx / shrink, vty / shrink };
}
}

MeshBuf ExtrudeShape(std::vector<Pt2> vertices, const ExtrudeOpts& o) {
	double bevelThickness = o.bevelThickness, bevelSize = o.bevelSize, bevelOffset = o.bevelOffset;
	int bevelSegments = o.bevelSegments;
	const bool bevelEnabled = o.bevelEnabled;
	const int steps = o.steps;
	const double depth = o.depth;
	if (!bevelEnabled) { bevelSegments = 0; bevelThickness = 0; bevelSize = 0; bevelOffset = 0; }
	if (!(ShapeArea(vertices) < 0)) std::reverse(vertices.begin(), vertices.end());
	// mergeOverlappingPoints
	{
		const double TH2 = 1e-10 * 1e-10;
		Pt2 prev = vertices[0];
		for (size_t i = 1; i <= vertices.size(); i++) {
			const size_t ci = i % vertices.size();
			const Pt2 cur = vertices[ci];
			const double dx = cur[0] - prev[0], dy = cur[1] - prev[1];
			const double s = std::max({ std::fabs(cur[0]), std::fabs(cur[1]), std::fabs(prev[0]), std::fabs(prev[1]) });
			if (dx * dx + dy * dy <= TH2 * s * s) { vertices.erase(vertices.begin() + ci); i--; continue; }
			prev = cur;
		}
	}
	const std::vector<Pt2> contour = vertices;
	const size_t vlen = vertices.size();
	std::vector<Pt2> moves(contour.size());
	for (size_t i = 0, il = contour.size(), j = il - 1, k = 1; i < il; i++, j++, k++) {
		if (j == il) j = 0;
		if (k == il) k = 0;
		moves[i] = BevelVec(contour[i], contour[j], contour[k]);
	}
	std::vector<float> ph;
	auto v = [&](double x, double y, double z) { ph.push_back((float)x); ph.push_back((float)y); ph.push_back((float)z); };
	auto scalePt = [](const Pt2& p, const Pt2& d, double s) { return Pt2{ p[0] + d[0] * s, p[1] + d[1] * s }; };
	auto triangulate = [](std::vector<Pt2> c) {
		if (c.size() > 2 && c.back() == c.front()) c.pop_back();
		std::vector<double> flat;
		for (const Pt2& p : c) { flat.push_back(p[0]); flat.push_back(p[1]); }
		return Earcut(flat);
	};
	std::vector<uint32_t> faces;
	if (bevelSegments == 0) faces = triangulate(contour);
	else {
		std::vector<Pt2> contracted;
		for (int b = 0; b < bevelSegments; b++) {
			const double t = (double)b / bevelSegments;
			const double z = bevelThickness * std::cos(t * kPi / 2);
			const double bs = bevelSize * std::sin(t * kPi / 2) + bevelOffset;
			for (size_t i = 0; i < contour.size(); i++) {
				const Pt2 vert = scalePt(contour[i], moves[i], bs);
				v(vert[0], vert[1], -z);
				if (t == 0) contracted.push_back(vert);
			}
		}
		faces = triangulate(contracted);
	}
	const size_t flen = faces.size() / 3;
	const double bsAll = bevelSize + bevelOffset;
	for (size_t i = 0; i < vlen; i++) { const Pt2 vert = bevelEnabled ? scalePt(vertices[i], moves[i], bsAll) : vertices[i]; v(vert[0], vert[1], 0); }
	for (int s = 1; s <= steps; s++) for (size_t i = 0; i < vlen; i++) { const Pt2 vert = bevelEnabled ? scalePt(vertices[i], moves[i], bsAll) : vertices[i]; v(vert[0], vert[1], depth / steps * s); }
	for (int b = bevelSegments - 1; b >= 0; b--) {
		const double t = (double)b / bevelSegments;
		const double z = bevelThickness * std::cos(t * kPi / 2);
		const double bs = bevelSize * std::sin(t * kPi / 2) + bevelOffset;
		for (size_t i = 0; i < contour.size(); i++) { const Pt2 vert = scalePt(contour[i], moves[i], bs); v(vert[0], vert[1], depth + z); }
	}
	// faces
	std::vector<float> out;
	auto addVertex = [&](size_t idx) { out.push_back(ph[idx * 3]); out.push_back(ph[idx * 3 + 1]); out.push_back(ph[idx * 3 + 2]); };
	auto f3 = [&](size_t a, size_t b, size_t c) { addVertex(a); addVertex(b); addVertex(c); };
	auto f4 = [&](size_t a, size_t b, size_t c, size_t d) { addVertex(a); addVertex(b); addVertex(d); addVertex(b); addVertex(c); addVertex(d); };
	if (bevelEnabled) {
		size_t offset = 0;
		for (size_t i = 0; i < flen; i++) f3(faces[i * 3 + 2] + offset, faces[i * 3 + 1] + offset, faces[i * 3] + offset);
		offset = vlen * (steps + bevelSegments * 2);
		for (size_t i = 0; i < flen; i++) f3(faces[i * 3] + offset, faces[i * 3 + 1] + offset, faces[i * 3 + 2] + offset);
	} else {
		for (size_t i = 0; i < flen; i++) f3(faces[i * 3 + 2], faces[i * 3 + 1], faces[i * 3]);
		for (size_t i = 0; i < flen; i++) f3(faces[i * 3] + vlen * steps, faces[i * 3 + 1] + vlen * steps, faces[i * 3 + 2] + vlen * steps);
	}
	// side walls
	for (long long i = (long long)contour.size() - 1; i >= 0; i--) {
		const size_t j = (size_t)i;
		size_t k = i - 1 < 0 ? contour.size() - 1 : (size_t)(i - 1);
		for (int s = 0, sl = steps + bevelSegments * 2; s < sl; s++) {
			const size_t s1 = vlen * s, s2 = vlen * (s + 1);
			f4(j + s1, k + s1, k + s2, j + s2);
		}
	}
	// computeVertexNormals (non-indexed: each triangle's (c - b) x (a - b), normalised)
	MeshBuf g;
	for (size_t t = 0; t + 8 < out.size() + 0 && t + 9 <= out.size(); t += 9) {
		const double ax = out[t], ay = out[t + 1], az = out[t + 2], bx = out[t + 3], by = out[t + 4], bz = out[t + 5], cx = out[t + 6], cy = out[t + 7], cz = out[t + 8];
		const double cbx = cx - bx, cby = cy - by, cbz = cz - bz, abx = ax - bx, aby = ay - by, abz = az - bz;
		double nx = cby * abz - cbz * aby, ny = cbz * abx - cbx * abz, nz = cbx * aby - cby * abx;
		const float fx = (float)nx, fy = (float)ny, fz = (float)nz;
		double l = std::sqrt((double)fx * fx + (double)fy * fy + (double)fz * fz);
		if (!l) l = 1;
		for (int q = 0; q < 3; q++) g.V(out[t + q * 3], out[t + q * 3 + 1], out[t + q * 3 + 2], fx / l, fy / l, fz / l);
	}
	return g;
}

void TransformBuf(MeshBuf& g, const Mat4& m) {
	for (size_t i = 0; i < g.Count(); i++) {
		double x = g.P[i * 3], y = g.P[i * 3 + 1], z = g.P[i * 3 + 2];
		m.Apply(x, y, z);
		g.P[i * 3] = (float)x; g.P[i * 3 + 1] = (float)y; g.P[i * 3 + 2] = (float)z;
		double nx = g.N[i * 3], ny = g.N[i * 3 + 1], nz = g.N[i * 3 + 2];
		m.ApplyNormal(nx, ny, nz);
		g.N[i * 3] = (float)nx; g.N[i * 3 + 1] = (float)ny; g.N[i * 3 + 2] = (float)nz;
	}
}

MeshBuf NonIndexed(const MeshBuf& g) {
	if (g.I.empty()) return g;
	MeshBuf r;
	for (uint32_t idx : g.I) {
		for (int q = 0; q < 3; q++) { r.P.push_back(g.P[idx * 3 + q]); r.N.push_back(g.N[idx * 3 + q]); }
		for (int c = 0; c < 4; c++) if (!g.C[c].empty()) { r.C[c].push_back(g.C[c][idx * 2]); r.C[c].push_back(g.C[c][idx * 2 + 1]); }
	}
	return r;
}

void SmoothNormals(MeshBuf& g) {
	std::fill(g.N.begin(), g.N.end(), 0.f);
	for (size_t t = 0; t + 2 < g.I.size(); t += 3) {
		const uint32_t a = g.I[t], b = g.I[t + 1], c = g.I[t + 2];
		const double cbx = (double)g.P[c * 3] - g.P[b * 3], cby = (double)g.P[c * 3 + 1] - g.P[b * 3 + 1], cbz = (double)g.P[c * 3 + 2] - g.P[b * 3 + 2];
		const double abx = (double)g.P[a * 3] - g.P[b * 3], aby = (double)g.P[a * 3 + 1] - g.P[b * 3 + 1], abz = (double)g.P[a * 3 + 2] - g.P[b * 3 + 2];
		const double nx = cby * abz - cbz * aby, ny = cbz * abx - cbx * abz, nz = cbx * aby - cby * abx;
		for (uint32_t v : { a, b, c }) { g.N[v * 3] = (float)(g.N[v * 3] + nx); g.N[v * 3 + 1] = (float)(g.N[v * 3 + 1] + ny); g.N[v * 3 + 2] = (float)(g.N[v * 3 + 2] + nz); }
	}
	for (size_t i = 0; i < g.Count(); i++) {
		const double x = g.N[i * 3], y = g.N[i * 3 + 1], z = g.N[i * 3 + 2];
		double l = std::sqrt(x * x + y * y + z * z);
		if (!l) l = 1;
		g.N[i * 3] = (float)(x / l); g.N[i * 3 + 1] = (float)(y / l); g.N[i * 3 + 2] = (float)(z / l);
	}
}

} // namespace atg
