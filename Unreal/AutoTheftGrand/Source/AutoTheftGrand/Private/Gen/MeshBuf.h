// A plain vertex / index accumulator (the counterpart of the JavaScript GeoBuilder / Acc), plus the three.js
// primitive shapes (box, cylinder, sphere, cone, icosahedron, torus, capsule, circle, ring) and transforms
// the prop and vehicle models are built from. Coordinates are the JavaScript ones (metres, y up, right-
// handed); triangles wind counter-clockwise seen from the front. The Unreal side maps x, y, z to X, Z, Y
// (and centimetres), which keeps that winding front-facing there.
//
// Every vertex carries four 2-float channels; what they hold depends on the material (see
// ATGMaterials.cpp): the generic "vertex-lit" layout is ch1 = (r, g), ch2 = (b, glow), ch3 = (signal,
// roughness), with ch0 the shape's own uv.
#pragma once

#include "GenMath.h"

namespace atg {

struct Mat4 {
	double m[16]; // column-major like three.js (m[12..14] = translation)
	static Mat4 Identity();
	// three.js Matrix4.compose(position, quaternion from Euler(rx, ry, rz, order), scale)
	static Mat4 Compose(double x, double y, double z, double rx = 0, double ry = 0, double rz = 0, double sx = 1, double sy = 1, double sz = 1, const char* order = "XYZ");
	Mat4 operator*(const Mat4& b) const;
	void Apply(double& x, double& y, double& z) const;          // point
	void ApplyNormal(double& x, double& y, double& z) const;    // normal (inverse transpose, normalised)
};

struct MeshBuf {
	std::vector<float> P, N;
	std::vector<float> C[4];
	std::vector<uint32_t> I;
	float cur[4][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };

	size_t Count() const { return P.size() / 3; }
	bool Empty() const { return P.empty(); }
	void Set(int ch, double a, double b) { cur[ch][0] = (float)a; cur[ch][1] = (float)b; }
	// vertex-lit helpers: colour (linear), glow and signal / roughness
	void Color(double r, double g, double b) { Set(1, r, g); cur[2][0] = (float)b; }
	void ColorHex(uint32_t hex); // three.js Color(hex): sRGB hex to linear
	void Glow(double k) { cur[2][1] = (float)k; }
	void Signal(double code) { cur[3][0] = (float)code; }
	void Rough(double r) { cur[3][1] = (float)r; }

	uint32_t V(double x, double y, double z, double nx, double ny, double nz, double u = 0, double v = 0);
	void Tri(uint32_t a, uint32_t b, uint32_t c) { I.push_back(a); I.push_back(b); I.push_back(c); }
	// GeoBuilder.quad: corners counter-clockwise seen from the front (a, b, c + a, c, d)
	void Quad(const double p0[3], const double p1[3], const double p2[3], const double p3[3], const double n[3],
		const double uv0[2] = nullptr, const double uv1[2] = nullptr, const double uv2[2] = nullptr, const double uv3[2] = nullptr);
	// Acc.quad: winding picked from the normal stored on vertex a
	void QuadAuto(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
	// GeoBuilder.box: axis-aligned box (sides, top, optional bottom)
	void Box(double x0, double y0, double z0, double x1, double y1, double z1, bool top = true, bool sides = true, bool bottom = false, double uvs = 1);
	// merge another buffer transformed by m; its channel 0 is kept, the other channels take the current values
	void Add(const MeshBuf& g, const Mat4& m);
	// append another buffer as-is (all channels), optionally transformed
	void Append(const MeshBuf& g);
	void Append(const MeshBuf& g, const Mat4& m);
	void RotateFrom(size_t start, double cx, double cz, double yaw);
	void Translate(double x, double y, double z);
	void Bounds(double mn[3], double mx[3]) const;
};

// three.js primitives (non-indexed like toNonIndexed(), with uvs), centred like the originals
namespace Geo {
MeshBuf Box(double w, double h, double d);
MeshBuf Cylinder(double rTop, double rBottom, double h, int radial = 8, int heightSegs = 1, bool open = false, double thetaStart = 0, double thetaLen = kTau);
// three.js LatheGeometry: a profile of (x, y) points turned about y
MeshBuf Lathe(const std::vector<std::array<double, 2>>& pts, int segments = 12, double phiStart = 0, double phiLen = kTau);
MeshBuf Cone(double r, double h, int radial = 8, int heightSegs = 1);
MeshBuf Sphere(double r, int wSegs = 8, int hSegs = 6, double phiStart = 0, double phiLen = kTau, double thetaStart = 0, double thetaLen = kPi);
MeshBuf Icosahedron(double r, int detail = 0);
MeshBuf Torus(double r, double tube, int radial = 8, int tubular = 16);
MeshBuf Capsule(double r, double length, int capSegs = 4, int radial = 8);
MeshBuf Circle(double r, int segs = 8);
MeshBuf Ring(double inner, double outer, int segs = 32);
// flat-shade: recompute face normals (three.js computeVertexNormals on a non-indexed geometry)
void ComputeFlatNormals(MeshBuf& g);
} // namespace Geo

} // namespace atg
