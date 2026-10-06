// Surface helpers for smooth procedural bodywork (port of src/entities/loft.js): lofted grids with smooth
// normals that can be split into several parts (paint, glass, panels that open or break off), swept tubes
// and rounded boxes. A Part is a MeshBuf whose vertex colour is the vertex-lit layout's (ch1 = r, g; ch2.x = b).
#pragma once

#include "MeshBuf.h"

namespace atg {

using Pt3 = std::array<double, 3>;
using Pt2 = std::array<double, 2>;

struct Part {
	MeshBuf m;
	double col[3] = { 1, 1, 1 };
	Part& color(double r, double g, double b) { col[0] = r; col[1] = g; col[2] = b; return *this; }
	Part& hex(uint32_t h); // three.js Color(hex): sRGB to linear
	uint32_t v(double x, double y, double z, double nx, double ny, double nz) { return v(x, y, z, nx, ny, nz, col); }
	uint32_t v(double x, double y, double z, double nx, double ny, double nz, const double c[3]);
	void tri(uint32_t a, uint32_t b, uint32_t c) { m.Tri(a, b, c); }
	// flat convex polygon (in order) with a given normal
	void poly(const std::vector<Pt3>& pts, const Pt3& n);
	// axis-aligned box (flat shaded)
	void box(double x0, double y0, double z0, double x1, double y1, double z1, bool bottom = false);
	// merge a geometry (keeps its smooth normals) through a matrix, in the current colour
	void geo(const MeshBuf& g, const Mat4& matrix);
	bool empty() const { return m.Empty(); }
	size_t n() const { return m.Count(); }
	// the normal stored on a vertex
	Pt3 normalOf(uint32_t i) const { return { m.N[i * 3], m.N[i * 3 + 1], m.N[i * 3 + 2] }; }
};

// monotone cubic through (x, y) points (no overshoot), clamped at the ends
std::function<double(double)> SmoothLine(std::vector<Pt2> pts);
// sorted, de-duplicated stations from a to b every `step`, always including the given feature positions
std::vector<double> Stations(double a, double b, double step, const std::vector<double>& extra = {});

struct GridOpts {
	Pt3 ref = { 0, 0.8, 0 };
	bool inward = false;
	std::function<Pt3(double, double, double, Part*)> colorAt;
	std::function<double(Part*)> offset;
};
// emit a grid rows[i][j] (i along the body, j round the section) with smooth normals; pick(i, j) chooses
// the part for each quad (or nullptr to leave it out)
void EmitGrid(const std::vector<std::vector<Pt3>>& rows, const std::function<Part*(int, int)>& pick, const GridOpts& o = GridOpts());

// rounded rectangle outline (half width hw, from y0 to y1, corner radius r), counter-clockwise, as (x, y)
std::vector<Pt2> RoundRect(double hw, double y0, double y1, double r, int seg = 4);
// sweep a closed section (u sideways / outward, v up) along a path; returns the first vertex index
uint32_t Sweep(Part& part, const std::vector<Pt3>& path, const std::vector<Pt2>& section, bool closeEnds = true, bool left = false);
// a box with rounded vertical edges along z (a lofted rounded rectangle with flat ends)
void RoundBox(Part& part, double x0, double x1, double y0, double y1, double z0, double z1, double r, int seg = 3);

} // namespace atg
