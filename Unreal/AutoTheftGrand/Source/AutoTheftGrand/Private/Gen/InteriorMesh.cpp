#include "InteriorMesh.h"
#include "ShapeGeo.h"
#include <functional>

namespace atg {

namespace {
const double IntDoorW = 2.4, IntDoorH = 2.7;
using IntC = std::array<double, 3>;

// GeoBuilder.addGeometry: the primitive de-indexed, each vertex then indexed in order
MeshBuf IntFlat(const MeshBuf& g) {
	MeshBuf r = NonIndexed(g);
	r.I.resize(r.Count());
	for (size_t i = 0; i < r.I.size(); i++) r.I[i] = (uint32_t)i;
	return r;
}

// interiors.js rnd(seed): a little LCG
struct IntRnd {
	uint32_t s;
	explicit IntRnd(uint32_t seed) : s(seed) {}
	double operator()() { s = (uint32_t)((uint64_t)s * 1664525u + 1013904223u); return s / 4294967296.0; }
};
}

InteriorMesh BuildInteriorMesh(const InteriorShell& it) {
	InteriorMesh out;
	const InteriorShell::Layout& L = it.L;
	MeshBuf& gb = out.solid;
	MeshBuf& em = out.glow;
	const double H = it.W / 2, D = it.D, C = L.ceil;
	auto col = [](MeshBuf& g, const double* c) { g.Color(c[0], c[1], c[2]); };
	auto colC = [](MeshBuf& g, const IntC& c) { g.Color(c[0], c[1], c[2]); };
	auto P = [&](double u, double w, double h) { return std::array<double, 3>{ it.X(u, w), it.fy + h, it.Z(u, w) }; };
	// a quad in local coordinates ([u, h, w] corners), wound to face n (a local [u, h, w] direction)
	auto face = [&](MeshBuf& g, IntC a, IntC b, IntC c, IntC d, IntC n) {
		std::array<std::array<double, 3>, 4> p = { P(a[0], a[2], a[1]), P(b[0], b[2], b[1]), P(c[0], c[2], c[1]), P(d[0], d[2], d[1]) };
		const double nw[3] = { it.r[0] * n[0] + it.f[0] * n[2], n[1], it.r[1] * n[0] + it.f[1] * n[2] };
		const double e1[3] = { p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2] }, e2[3] = { p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2] };
		const double cr[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
		if (cr[0] * nw[0] + cr[1] * nw[1] + cr[2] * nw[2] < 0) std::reverse(p.begin(), p.end());
		g.Quad(p[0].data(), p[1].data(), p[2].data(), p[3].data(), nw);
	};
	// a local box as a world box
	auto lbox = [&](MeshBuf& g, double u0, double w0, double u1, double w1, double h0, double h1, const IntC& c, bool top = true, bool bottom = false) {
		double q[4]; it.Rect(u0, w0, u1, w1, q);
		colC(g, c); g.Box(q[0], it.fy + h0, q[1], q[2], it.fy + h1, q[3], top, true, bottom);
	};
	auto wallQuad = [&](double u0, double u1, double w, double h0, double h1, IntC n) { face(gb, { u0, h0, w }, { u1, h0, w }, { u1, h1, w }, { u0, h1, w }, n); };
	auto sideQuad = [&](double u, double w0, double w1, double h0, double h1, IntC n) { face(gb, { u, h0, w0 }, { u, h0, w1 }, { u, h1, w1 }, { u, h1, w0 }, n); };
	auto geo = [&](const MeshBuf& g, const Mat4& m) { gb.Add(IntFlat(g), m); };
	auto weapon = [&](const std::string& id, std::array<double, 3> p, double yaw, double roll) { out.weapons.push_back({ id, p[0], p[1], p[2], yaw, roll }); };
	auto glassBox = [&](double u0, double w0, double u1, double w1, double y, double sy, double minW = 0) {
		double q[4]; it.Rect(u0, w0, u1, w1, q);
		out.glass.push_back({ (q[0] + q[2]) / 2, it.fy + y, (q[1] + q[3]) / 2, Max(minW, q[2] - q[0]), sy, Max(minW, q[3] - q[1]) });
	};
	// a flat poster on a side wall (at u = uWall), facing r ('r') or the other way
	auto poster = [&](const char* pic, double uWall, double w, double h, double width, double height, bool facingR) {
		const auto p = P(uWall, w, h);
		out.panels.push_back({ pic, p[0], p[1], p[2], width, height, facingR ? it.YawR() : it.YawR() + kPi, true });
	};
	const double dado = 1.0, e = 0.004;

	// floor tiles (above any car-park surface under the building)
	for (double u = -H; u < H - 1e-3; u += 1) for (double w = 0; w < D - 1e-3; w += 1) {
		col(gb, L.floor[((int)std::floor(u + H) + (int)std::floor(w)) & 1]);
		const double u1 = Min(H, u + 1), w1 = Min(D, w + 1);
		face(gb, { u, 0.06, w }, { u1, 0.06, w }, { u1, 0.06, w1 }, { u, 0.06, w1 }, { 0, 1, 0 });
	}
	// the ceiling
	colC(gb, { 0.8, 0.8, 0.78 });
	face(gb, { -H, C, 0 }, { H, C, 0 }, { H, C, D }, { -H, C, D }, { 0, -1, 0 });
	// the walls: the dado band and the upper wall, facing in; the front has the doorway
	const struct { double h0, h1; const double* c; } bands[2] = { { 0, dado, L.dado }, { dado, C, L.wall } };
	for (const auto& bd : bands) {
		col(gb, bd.c);
		wallQuad(-H, H, D - e, bd.h0, bd.h1, { 0, 0, -1 });
		sideQuad(-H + e, 0, D, bd.h0, bd.h1, { 1, 0, 0 });
		sideQuad(H - e, 0, D, bd.h0, bd.h1, { -1, 0, 0 });
		wallQuad(-H, -IntDoorW / 2, e, bd.h0, bd.h1, { 0, 0, 1 });
		wallQuad(IntDoorW / 2, H, e, bd.h0, bd.h1, { 0, 0, 1 });
		if (bd.h1 > IntDoorH) wallQuad(-IntDoorW / 2, IntDoorW / 2, e, Max(bd.h0, IntDoorH), bd.h1, { 0, 0, 1 });
	}
	// the door frame and a mat
	lbox(gb, -IntDoorW / 2 - 0.12, -0.12, -IntDoorW / 2, 0.14, 0, IntDoorH, { 0.15, 0.15, 0.16 });
	lbox(gb, IntDoorW / 2, -0.12, IntDoorW / 2 + 0.12, 0.14, 0, IntDoorH, { 0.15, 0.15, 0.16 });
	lbox(gb, -IntDoorW / 2 - 0.12, -0.12, IntDoorW / 2 + 0.12, 0.14, IntDoorH, IntDoorH + 0.14, { 0.15, 0.15, 0.16 });
	lbox(gb, -1.1, 0.4, 1.1, 1.7, 0.06, 0.075, { 0.12, 0.12, 0.13 });
	// ceiling light panels
	for (const auto& l : L.lights) {
		const double u = l[0], w = l[1];
		lbox(gb, u - 0.9, w - 0.35, u + 0.9, w + 0.35, C - 0.06, C, { 0.6, 0.6, 0.6 }, false, true);
		colC(em, { 1.0, 0.97, 0.9 });
		face(em, { u - 0.8, C - 0.065, w - 0.28 }, { u + 0.8, C - 0.065, w - 0.28 }, { u + 0.8, C - 0.065, w + 0.28 }, { u - 0.8, C - 0.065, w + 0.28 }, { 0, -1, 0 });
	}
	// the counter
	const auto& c = L.counter;
	lbox(gb, c.u0, c.w0, c.u1, c.w1, 0, 0.98, { c.body[0], c.body[1], c.body[2] });
	lbox(gb, c.u0 - 0.05, c.w0 - 0.05, c.u1 + 0.05, c.w1 + 0.05, 0.98, 1.05, { c.top[0], c.top[1], c.top[2] });
	// the till
	const double tu = L.till.u, tw = L.till.w;
	lbox(gb, tu - 0.22, tw - 0.18, tu + 0.22, tw + 0.18, 1.05, 1.2, { 0.15, 0.15, 0.16 });
	lbox(gb, tu - 0.18, tw - 0.02, tu + 0.18, tw + 0.14, 1.2, 1.42, { 0.2, 0.2, 0.22 });
	colC(em, { 0.3, 1.0, 0.5 });
	face(em, { tu - 0.14, 1.24, tw - 0.03 }, { tu + 0.14, 1.24, tw - 0.03 }, { tu + 0.14, 1.38, tw - 0.03 }, { tu - 0.14, 1.38, tw - 0.03 }, { 0, 0, -1 });

	IntRnd R((uint32_t)(it.key.size() * 977 + (int64_t)std::floor(it.ox + 0.5)));
	// rows of small coloured boxes on shelves
	auto products = [&](double u0, double w0, double u1, double w1, int levels, double h0, double dh, int faceDir, double depth = 0.35) {
		static const IntC Cols[7] = { { 0.8, 0.15, 0.1 }, { 0.1, 0.4, 0.8 }, { 0.95, 0.75, 0.1 }, { 0.2, 0.6, 0.25 }, { 0.9, 0.9, 0.85 }, { 0.55, 0.2, 0.6 }, { 0.95, 0.45, 0.1 } };
		for (int k = 0; k < levels; k++) {
			const double y = h0 + k * dh;
			lbox(gb, u0, w0, u1, w1, y - 0.03, y, { 0.45, 0.42, 0.38 });
			const bool alongU = std::fabs(u1 - u0) > std::fabs(w1 - w0);
			const double len = alongU ? u1 - u0 : w1 - w0;
			for (double s = 0.08; s < len - 0.12;) {
				const double wdt = 0.08 + R() * 0.18, hgt = 0.12 + R() * dh * 0.55;
				const IntC& cc = Cols[(int)std::floor(R() * 7)];
				if (alongU) { const double a = u0 + s; const double dd = depth * (0.6 + R() * 0.4); lbox(gb, a, w0 + 0.03, a + wdt, w0 + 0.03 + dd, y, y + hgt, cc); }
				else { const double a = w0 + s; const double uu = faceDir > 0 ? u0 + 0.03 : u1 - 0.03 - depth; const double dd = depth * (0.6 + R() * 0.4); lbox(gb, uu, a, uu + dd, a + wdt, y, y + hgt, cc); }
				s += wdt + 0.03 + R() * 0.05;
			}
		}
	};
	auto find = [&](const char* kind) -> const InteriorShell::Furniture* { for (const auto& q : it.furniture) if (q.kind == kind) return &q; return nullptr; };
	auto all = [&](const char* kind) { std::vector<const InteriorShell::Furniture*> v; for (const auto& q : it.furniture) if (q.kind == kind) v.push_back(&q); return v; };

	if (L.extra == "gunshop") {
		// a pegboard with guns on the back wall
		lbox(gb, -8, D - 0.1, 8, D - 0.02, 1.0, 3.3, { 0.2, 0.3, 0.22 });
		const std::vector<std::pair<double, std::vector<const char*>>> rows = {
			{ 2.95, { "rifle", "rifle", "rifle", "rifle", "rifle" } }, { 2.35, { "shotgun", "shotgun", "shotgun", "shotgun" } },
			{ 1.75, { "smg", "smg", "pistol", "pistol", "smg", "pistol" } }, { 1.3, { "rpg", "bat", "bat", "knife", "knife" } } };
		for (const auto& row : rows) {
			const int n = (int)row.second.size();
			for (int k = 0; k < n; k++) { const double u = -6.5 + (k + 0.5) * 13 / n; weapon(row.second[k], P(u - 0.25, D - 0.2, row.first), it.YawR(), 0); }
		}
		// counter-top guns
		const std::vector<std::pair<const char*, double>> top = { { "pistol", -4.5 }, { "pistol", -3.7 }, { "smg", -2.4 }, { "grenade", 3 }, { "grenade", 3.2 }, { "knife", 4.3 } };
		for (const auto& g : top) weapon(g.first, P(g.second, c.w0 + 0.4, 1.07), it.YawR(), kPi / 2);
		// ammo shelves
		for (const auto* s : all("shelf")) {
			lbox(gb, s->u0, s->w0, s->u1, s->w1, 0, 0.1, { 0.25, 0.22, 0.2 });
			products(s->u0, s->w0, s->u1, s->w1, 4, 0.45, 0.5, s->face, 0.4);
		}
		// the glass display case with pistols
		const auto* cs = find("case");
		lbox(gb, cs->u0, cs->w0, cs->u1, cs->w1, 0, 0.7, { 0.18, 0.18, 0.2 });
		for (int k = 0; k < 4; k++) weapon(k % 2 ? "pistol" : "smg", P(cs->u0 + 0.6 + k * 1.0, (cs->w0 + cs->w1) / 2, 0.74), it.YawR(), kPi / 2);
		glassBox(cs->u0, cs->w0, cs->u1, cs->w1, 0.85, 0.3);
		// posters: a paper target and the house rules
		poster("target", -H + 0.02, 7.2, 2.2, 1.0, 1.25, true);
		poster("rules", H - 0.02, 6.0, 2.2, 1.6, 1.0, false);
	} else if (L.extra == "burger") {
		// the kitchen: grill, fryers, fridge and extractor hood
		lbox(gb, -9, D - 1.1, 9, D - 0.25, 0, 0.92, { 0.62, 0.63, 0.65 });
		lbox(gb, -8.6, D - 1.05, -4, D - 0.3, 0.92, 0.98, { 0.12, 0.12, 0.12 });
		for (double u : { -2.5, -1.2 }) { lbox(gb, u - 0.5, D - 1.0, u + 0.5, D - 0.35, 0.92, 1.0, { 0.5, 0.5, 0.5 }); lbox(gb, u - 0.42, D - 0.95, u + 0.42, D - 0.4, 1.0, 1.02, { 0.75, 0.55, 0.15 }); }
		lbox(gb, 6.2, D - 1.1, 8.8, D - 0.25, 0, 2.3, { 0.72, 0.73, 0.75 });
		lbox(gb, -9, D - 1.25, 0.5, D - 0.25, 2.3, 2.9, { 0.6, 0.61, 0.63 });
		// tables and stools
		for (const auto* t : all("table")) {
			const auto p = P(t->u, t->w, 0);
			colC(gb, { 0.85, 0.8, 0.72 }); geo(Geo::Cylinder(0.55, 0.55, 0.05, 18), Mat4::Compose(p[0], p[1] + 0.76, p[2]));
			colC(gb, { 0.3, 0.3, 0.32 }); geo(Geo::Cylinder(0.06, 0.18, 0.74, 8), Mat4::Compose(p[0], p[1] + 0.37, p[2]));
			for (double a : { 0.0, kPi }) {
				const double sx = p[0] + std::cos(a) * 0.95, sz = p[2] + std::sin(a) * 0.95;
				colC(gb, { 0.78, 0.12, 0.08 }); geo(Geo::Cylinder(0.22, 0.22, 0.08, 14), Mat4::Compose(sx, p[1] + 0.5, sz));
				colC(gb, { 0.3, 0.3, 0.32 }); geo(Geo::Cylinder(0.04, 0.12, 0.48, 8), Mat4::Compose(sx, p[1] + 0.24, sz));
			}
		}
		// trays and drinks on the counter
		for (double u : { -5.0, 1.5, 4.5 }) { lbox(gb, u - 0.25, c.w0 + 0.15, u + 0.25, c.w0 + 0.55, 1.05, 1.07, { 0.75, 0.2, 0.12 }); lbox(gb, u - 0.12, c.w0 + 0.25, u + 0.02, c.w0 + 0.4, 1.07, 1.25, { 0.95, 0.95, 0.95 }); }
		// the menu board over the counter
		const auto p = P(0, c.w1 + 0.45, 2.7);
		out.panels.push_back({ "burgermenu", p[0], p[1], p[2], 6.4, 1.6, it.YawIn() + kPi, false });
	} else if (L.extra == "petshop") {
		// kennels: a straw floor, back and side walls, a glass front and a water bowl each
		const auto pens = all("pen");
		for (const auto* p : pens) {
			lbox(gb, p->u0, p->w0, p->u1, p->w1, 0.06, 0.09, { 0.78, 0.66, 0.36 });
			lbox(gb, p->u0, p->w0 - 0.05, p->u1, p->w0 + 0.05, 0, 1.2, { 0.85, 0.85, 0.82 });
			lbox(gb, p->u1 - 0.55, p->w0 + 0.3, p->u1 - 0.25, p->w0 + 0.6, 0.09, 0.16, { 0.2, 0.45, 0.8 });
		}
		const auto* last = pens.empty() ? nullptr : pens.back();
		if (last) lbox(gb, last->u0, last->w1 - 0.05, last->u1, last->w1 + 0.05, 0, 1.2, { 0.85, 0.85, 0.82 });
		if (!pens.empty()) glassBox(-H + 2.36, pens[0]->w0, -H + 2.44, last->w1, 0.62, 1.1, 0.05);
		// the aquarium wall: lit blue tanks with little fish
		if (const auto* tk = find("tanks")) {
			lbox(gb, tk->u0, tk->w0, tk->u1, tk->w1, 0, 0.7, { 0.12, 0.12, 0.14 });
			lbox(gb, tk->u0, tk->w0, tk->u1, tk->w1, 1.9, 2.0, { 0.12, 0.12, 0.14 });
			static const IntC Fish[4] = { { 1, 0.5, 0.1 }, { 1, 0.85, 0.2 }, { 0.9, 0.2, 0.5 }, { 0.3, 1, 0.6 } };
			for (double w = tk->w0 + 0.1; w < tk->w1 - 0.9; w += 1.1) {
				colC(em, { 0.2, 0.55, 0.9 });
				face(em, { tk->u0 - 0.005, 0.72, w }, { tk->u0 - 0.005, 0.72, w + 1.0 }, { tk->u0 - 0.005, 1.88, w + 1.0 }, { tk->u0 - 0.005, 1.88, w }, { -1, 0, 0 });
				for (int f = 0; f < 5; f++) {
					const double fw = w + 0.1 + R() * 0.75, fh = 0.85 + R() * 0.9;
					colC(em, Fish[(int)std::floor(R() * 4)]);
					face(em, { tk->u0 - 0.01, fh, fw }, { tk->u0 - 0.01, fh, fw + 0.09 }, { tk->u0 - 0.01, fh + 0.05, fw + 0.09 }, { tk->u0 - 0.01, fh + 0.05, fw }, { -1, 0, 0 });
				}
			}
		}
		// the pet food aisle
		if (const auto* ai = find("aisle")) {
			lbox(gb, ai->u - 0.5, ai->w0, ai->u + 0.5, ai->w1, 0, 0.1, { 0.3, 0.3, 0.32 });
			lbox(gb, ai->u - 0.04, ai->w0, ai->u + 0.04, ai->w1, 0, 1.6, { 0.5, 0.5, 0.52 });
			products(ai->u - 0.48, ai->w0 + 0.05, ai->u - 0.06, ai->w1 - 0.05, 3, 0.4, 0.45, 1, 0.38);
			products(ai->u + 0.06, ai->w0 + 0.05, ai->u + 0.48, ai->w1 - 0.05, 3, 0.4, 0.45, -1, 0.38);
		}
		poster("adopt", H - 0.02, D - 2.4, 2.3, 1.6, 1.0, false);
	} else if (L.extra == "bar") {
		// the backlit bottle wall behind the bar
		lbox(gb, -H + 0.25, 1.6, -H + 0.7, D - 1.2, 0, 1.0, { 0.2, 0.12, 0.07 });
		static const IntC Bottles[5] = { { 0.9, 0.55, 0.15 }, { 0.3, 0.7, 0.3 }, { 0.8, 0.8, 0.9 }, { 0.7, 0.15, 0.1 }, { 0.95, 0.8, 0.3 } };
		for (int k = 0; k < 3; k++) {
			const double y = 1.25 + k * 0.42;
			lbox(gb, -H + 0.25, 1.6, -H + 0.72, D - 1.2, y - 0.03, y, { 0.25, 0.16, 0.1 });
			for (double w = 1.7; w < D - 1.3; w += 0.16 + R() * 0.08) {
				colC(em, Bottles[(int)std::floor(R() * 5)]);
				face(em, { -H + 0.73, y, w }, { -H + 0.73, y, w + 0.08 }, { -H + 0.73, y + 0.3, w + 0.08 }, { -H + 0.73, y + 0.3, w }, { 1, 0, 0 });
			}
		}
		// stools along the bar
		for (const auto* s : all("stool")) {
			const auto p = P(s->u, s->w, 0);
			colC(gb, { 0.55, 0.12, 0.08 }); geo(Geo::Cylinder(0.22, 0.22, 0.08, 12), Mat4::Compose(p[0], p[1] + 0.78, p[2]));
			colC(gb, { 0.3, 0.3, 0.32 }); geo(Geo::Cylinder(0.04, 0.14, 0.76, 8), Mat4::Compose(p[0], p[1] + 0.38, p[2]));
		}
		// the pool table: felt, rails, legs, a few balls
		if (const auto* pt = find("pool")) {
			lbox(gb, pt->u - 1.3, pt->w - 0.75, pt->u + 1.3, pt->w + 0.75, 0.62, 0.82, { 0.3, 0.17, 0.08 });
			lbox(gb, pt->u - 1.18, pt->w - 0.63, pt->u + 1.18, pt->w + 0.63, 0.82, 0.84, { 0.08, 0.4, 0.2 });
			for (const auto& ab : std::vector<std::array<double, 2>>{ { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } }) lbox(gb, pt->u + ab[0] * 1.1 - 0.1, pt->w + ab[1] * 0.6 - 0.1, pt->u + ab[0] * 1.1 + 0.1, pt->w + ab[1] * 0.6 + 0.1, 0, 0.62, { 0.25, 0.14, 0.07 });
			static const IntC Balls[5] = { { 0.9, 0.9, 0.85 }, { 0.9, 0.8, 0.1 }, { 0.1, 0.2, 0.8 }, { 0.8, 0.1, 0.1 }, { 0.1, 0.1, 0.1 } };
			for (int k = 0; k < 7; k++) {
				const double bu = pt->u - 0.8 + R() * 1.6, bw = pt->w - 0.45 + R() * 0.9;
				const auto p = P(bu, bw, 0.87);
				colC(gb, Balls[k % 5]); geo(Geo::Sphere(0.03, 8, 6), Mat4::Compose(p[0], p[1], p[2]));
			}
		}
		// booths
		for (const auto* bo : all("booth")) {
			lbox(gb, H - 1.9, bo->w - 0.45, H - 0.9, bo->w + 0.45, 0.7, 0.75, { 0.3, 0.17, 0.08 });
			lbox(gb, H - 1.9, bo->w - 0.4, H - 1.5, bo->w + 0.4, 0, 0.7, { 0.25, 0.14, 0.07 });
			for (double s : { -1.0, 1.0 }) lbox(gb, H - 2.0, bo->w + s * 0.85 - 0.25, H - 0.3, bo->w + s * 0.85 + 0.25, 0, 0.48, { 0.55, 0.1, 0.08 });
		}
		const auto p = P(0, D - 0.04, 2.4);
		out.panels.push_back({ "neon", p[0], p[1], p[2], 3.2, 1.0, it.YawIn() + kPi, false });
	} else if (L.extra == "cafe") {
		// an espresso machine, a grinder and a pastry case on the counter
		const double cw = (c.w0 + c.w1) / 2;
		lbox(gb, -H + 2, cw - 0.25, -H + 3, cw + 0.25, 1.05, 1.55, { 0.75, 0.75, 0.78 });
		lbox(gb, -H + 3.2, cw - 0.15, -H + 3.5, cw + 0.15, 1.05, 1.45, { 0.15, 0.15, 0.16 });
		lbox(gb, 0, c.w0 - 0.02, 2.2, c.w1 + 0.02, 1.05, 1.1, { 0.8, 0.8, 0.8 });
		static const IntC Pastry[4] = { { 0.85, 0.6, 0.3 }, { 0.55, 0.3, 0.15 }, { 0.95, 0.85, 0.6 }, { 0.9, 0.4, 0.5 } };
		for (int k = 0; k < 8; k++) { const auto p = P(0.2 + (k % 4) * 0.5, c.w0 + 0.2 + (k / 4) * 0.35, 1.13); colC(gb, Pastry[k % 4]); geo(Geo::Sphere(0.09, 8, 6), Mat4::Compose(p[0], p[1], p[2], 0, 0, 0, 1, 0.55, 1)); }
		glassBox(0, c.w0, 2.2, c.w1, 1.27, 0.32);
		// tables and chairs, plants
		for (const auto* t : all("table")) {
			const auto p = P(t->u, t->w, 0);
			colC(gb, { 0.9, 0.9, 0.88 }); geo(Geo::Cylinder(0.42, 0.42, 0.04, 16), Mat4::Compose(p[0], p[1] + 0.76, p[2]));
			colC(gb, { 0.2, 0.2, 0.22 }); geo(Geo::Cylinder(0.04, 0.16, 0.74, 8), Mat4::Compose(p[0], p[1] + 0.37, p[2]));
			for (double a : { 0.8, 0.8 + kPi }) {
				const double sx = p[0] + std::cos(a) * 0.7, sz = p[2] + std::sin(a) * 0.7;
				colC(gb, { 0.45, 0.3, 0.18 }); geo(Geo::Box(0.4, 0.05, 0.4), Mat4::Compose(sx, p[1] + 0.46, sz));
				geo(Geo::Box(0.4, 0.46, 0.05), Mat4::Compose(sx + std::cos(a) * 0.2, p[1] + 0.7, sz + std::sin(a) * 0.2, 0, -a + kPi / 2, 0));
			}
		}
		for (double u : { -H + 0.6, H - 0.6 }) {
			const auto p = P(u, 0.8, 0);
			colC(gb, { 0.6, 0.35, 0.2 }); geo(Geo::Cylinder(0.25, 0.2, 0.45, 10), Mat4::Compose(p[0], p[1] + 0.22, p[2]));
			colC(gb, { 0.2, 0.5, 0.22 }); geo(Geo::Icosahedron(0.45, 0), Mat4::Compose(p[0], p[1] + 0.85, p[2]));
		}
		const auto p = P(-H / 2 + 0.6, D - 0.04, 2.5);
		out.panels.push_back({ "cafemenu", p[0], p[1], p[2], 4.2, 1.4, it.YawIn() + kPi, false });
	} else if (L.extra == "liquor") {
		// aisles of shelves stocked on both sides
		for (const auto* a : all("aisle")) {
			lbox(gb, a->u - 0.5, a->w0, a->u + 0.5, a->w1, 0, 0.12, { 0.3, 0.3, 0.32 });
			lbox(gb, a->u - 0.04, a->w0, a->u + 0.04, a->w1, 0, 1.75, { 0.5, 0.5, 0.52 });
			products(a->u - 0.48, a->w0 + 0.05, a->u - 0.06, a->w1 - 0.05, 4, 0.4, 0.42, 1, 0.38);
			products(a->u + 0.06, a->w0 + 0.05, a->u + 0.48, a->w1 - 0.05, 4, 0.4, 0.42, -1, 0.38);
		}
		// a wall of fridges with lit bottles
		const auto* fr = find("fridges");
		lbox(gb, fr->u0, D - 0.95, fr->u1, D - 0.25, 0, 2.2, { 0.92, 0.92, 0.94 });
		static const IntC Drinks[5] = { { 0.2, 0.55, 0.15 }, { 0.55, 0.35, 0.1 }, { 0.8, 0.1, 0.1 }, { 0.9, 0.8, 0.2 }, { 0.15, 0.3, 0.7 } };
		for (double u = fr->u0 + 0.1; u < fr->u1 - 0.9; u += 1.05) {
			colC(em, { 0.55, 0.7, 0.8 });
			face(em, { u, 0.3, D - 0.955 }, { u + 0.95, 0.3, D - 0.955 }, { u + 0.95, 2.05, D - 0.955 }, { u, 2.05, D - 0.955 }, { 0, 0, -1 });
			for (int k = 0; k < 4; k++) for (int j = 0; j < 6; j++) {
				const IntC& cc = Drinks[(int)std::floor(R() * 5)];
				const double bu = u + 0.1 + j * 0.14, bh = 0.4 + k * 0.42;
				colC(em, cc);
				face(em, { bu, bh, D - 0.96 }, { bu + 0.09, bh, D - 0.96 }, { bu + 0.09, bh + 0.3, D - 0.96 }, { bu, bh + 0.3, D - 0.96 }, { 0, 0, -1 });
			}
			lbox(gb, u + 0.93, D - 0.99, u + 1.03, D - 0.9, 0.2, 2.1, { 0.7, 0.7, 0.72 });
		}
		// cigarettes behind the counter
		products(-H + 0.25, 1.4, -H + 0.7, 5.0, 3, 1.05, 0.42, 1, 0.3);
		// scratch cards and a lottery sign by the till
		poster("lotto", -H + 0.03, 3.2, 2.5, 1.2, 0.6, true);
	}
	return out;
}

} // namespace atg
