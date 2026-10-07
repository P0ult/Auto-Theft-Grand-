// three.js shape geometry (r186): Earcut.triangulate (mapbox earcut 3.0.2) and ExtrudeGeometry, for a single
// straight-sided shape without holes (what the browser game's models use). Ported line for line; the z-order
// hashing earcut switches on above 80 points and the hole handling are left out.
#pragma once

#include "MeshBuf.h"
#include <array>

namespace atg {

using Pt2 = std::array<double, 2>;

// earcut: triangle indices into the flat [x0, y0, x1, y1, ...] list
std::vector<uint32_t> Earcut(const std::vector<double>& data);

struct ExtrudeOpts {
	double depth = 1;
	int steps = 1;
	bool bevelEnabled = true;
	double bevelThickness = 0.2, bevelSize = 0.1, bevelOffset = 0;
	int bevelSegments = 3;
};

// ExtrudeGeometry(new Shape(pts), opts): non-indexed, with flat normals (computeVertexNormals) and zero uvs
MeshBuf ExtrudeShape(std::vector<Pt2> pts, const ExtrudeOpts& o);

// BufferGeometry.applyMatrix4 on a buffer (positions, and normals by the normal matrix, normalised)
void TransformBuf(MeshBuf& g, const Mat4& m);
// toNonIndexed(): one vertex per index (all channels); a non-indexed buffer comes back unchanged
MeshBuf NonIndexed(const MeshBuf& g);
// computeVertexNormals on an indexed buffer: area-weighted face normals summed per vertex, normalised
void SmoothNormals(MeshBuf& g);

} // namespace atg
