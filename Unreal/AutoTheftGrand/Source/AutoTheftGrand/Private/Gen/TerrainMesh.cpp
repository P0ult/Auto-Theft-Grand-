// Terrain chunks (port of src/world/terrainmesh.js): the heightfield in 256 m chunks at three levels of
// detail (4 / 16 / 64 m) with skirts hiding the seams between levels, and per-vertex biome weights the
// terrain material paints with (grass, crop fields, forest floor, desert, beach, urban paving).
#include "WorldMeshes.h"

namespace atg {

int TerrainChunksX() { return (int)std::ceil((WORLD.maxX - WORLD.minX) / TERRAIN_CH); }
int TerrainChunksZ() { return (int)std::ceil((WORLD.maxZ - WORLD.minZ) / TERRAIN_CH); }
bool TerrainChunkInCity(int i, int j) {
	const double x0 = WORLD.minX + i * TERRAIN_CH, z0 = WORLD.minZ + j * TERRAIN_CH;
	return x0 > CITY_RECT.minX + 2 && x0 + TERRAIN_CH < CITY_RECT.maxX - 2 && z0 > CITY_RECT.minZ + 2 && z0 + TERRAIN_CH < CITY_RECT.maxZ - 2;
}

namespace {
struct TAttr { double col[3]; double t[4]; };

TAttr Attrs(double x, double z, double h) {
	const Regions w = RegionWeights(x, z);
	const double farm = FarmMask(x, z);
	const double dC = CityDist(x, z);
	const double ne = z < -1900 && x > -1300 ? NcEdgeDist(x, z) : 1e9;
	const bool auBeach = z < -2000 && x > NCITY.x + 300 && x > CoastX(z) - 200 && h < 7;
	const bool beachZone = (z > CITY_RECT.maxZ && x < 480 && x > -1100 && h < 4.5) || (h < 2.2 && dC > 300 && RiverDist(x, z) > 60) || auBeach;
	const double beach = beachZone ? (auBeach ? Smooth(CoastX(z) - 200, CoastX(z) - 150, x) : Smooth(4.5, 1.5, h)) : 0;
	const double dry = Clamp(0.5 + 0.5 * std::sin(x * 0.0021) * std::cos(z * 0.0017), 0, 1);
	TAttr a;
	a.col[0] = 0.36 + dry * 0.12; a.col[1] = 0.38 + dry * 0.05; a.col[2] = 0.17 + dry * 0.02;
	const double urb = ne < 200 ? 1 - Smooth(-15, 45, ne) : 0;
	if (urb > 0.001) { a.t[0] = -Min(1, urb * 1.6); a.t[1] = w.desert; a.t[2] = w.mountain * (h < 520 ? 1 : 0.6); a.t[3] = beach * (1 - urb); }
	else { a.t[0] = farm; a.t[1] = w.desert; a.t[2] = w.mountain * (h < 520 ? 1 : 0.6); a.t[3] = beach; }
	return a;
}
} // namespace

MeshBuf BuildTerrainChunk(const CityMap& map, int ci, int cj, int lod) {
	const Heightfield& hf = map.hf;
	const double x0 = WORLD.minX + ci * TERRAIN_CH, z0 = WORLD.minZ + cj * TERRAIN_CH;
	const double step = TERRAIN_LODS[lod];
	const int n = (int)(TERRAIN_CH / step) + 1;
	auto cityDrop = [](double x, double z) { return x > CITY_RECT.minX + 0.5 && x < CITY_RECT.maxX - 0.5 && z > CITY_RECT.minZ + 0.5 && z < CITY_RECT.maxZ - 0.5 ? 0.7 : 0; };
	// biome attributes on the 16 m grid, interpolated for the fine level (much cheaper)
	std::vector<TAttr> A1;
	const int n1 = (int)(TERRAIN_CH / TERRAIN_LODS[1]) + 1;
	if (lod == 0) {
		A1.resize((size_t)n1 * n1);
		for (int j = 0; j < n1; j++) for (int i = 0; i < n1; i++) {
			const double x = x0 + i * TERRAIN_LODS[1], z = z0 + j * TERRAIN_LODS[1];
			A1[(size_t)j * n1 + i] = Attrs(x, z, hf.Sample(x, z) - cityDrop(x, z));
		}
	}
	auto interp = [&](int i, int j) {
		const double f = step / TERRAIN_LODS[1];
		const double fi = i * f, fj = j * f;
		const int i0 = (int)Min(n1 - 2, std::floor(fi)), j0 = (int)Min(n1 - 2, std::floor(fj));
		const double ti = fi - i0, tj = fj - j0;
		const double w00 = (1 - ti) * (1 - tj), w10 = ti * (1 - tj), w01 = (1 - ti) * tj, w11 = ti * tj;
		const TAttr &a = A1[(size_t)j0 * n1 + i0], &b = A1[(size_t)j0 * n1 + i0 + 1], &c = A1[(size_t)(j0 + 1) * n1 + i0], &d = A1[(size_t)(j0 + 1) * n1 + i0 + 1];
		TAttr r;
		for (int q = 0; q < 3; q++) r.col[q] = a.col[q] * w00 + b.col[q] * w10 + c.col[q] * w01 + d.col[q] * w11;
		for (int q = 0; q < 4; q++) r.t[q] = a.t[q] * w00 + b.t[q] * w10 + c.t[q] * w01 + d.t[q] * w11;
		return r;
	};
	MeshBuf g;
	const double e = Max(step, 4);
	for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
		const double x = x0 + i * step, z = z0 + j * step;
		const double y = hf.Sample(x, z) - cityDrop(x, z);
		const double dx = hf.Sample(x + e, z) - hf.Sample(x - e, z), dz = hf.Sample(x, z + e) - hf.Sample(x, z - e);
		const double l = Hypot3(dx, 2 * e, dz);
		const TAttr a = lod == 0 ? interp(i, j) : Attrs(x, z, y);
		g.Set(1, a.col[2], a.t[0]); g.Set(2, a.t[1], a.t[2]); g.Set(3, a.t[3], 0);
		g.V(x, y, z, -dx / l, 2 * e / l, -dz / l, a.col[0], a.col[1]);
	}
	for (int j = 0; j < n - 1; j++) for (int i = 0; i < n - 1; i++) {
		const uint32_t a = j * n + i, b = a + 1, c = a + n, d = c + 1;
		if ((i + j) % 2) { g.Tri(a, c, b); g.Tri(b, c, d); } else { g.Tri(a, c, d); g.Tri(a, d, b); }
	}
	// skirts hanging down along the four edges
	const double skirt = lod == 0 ? 3 : lod == 1 ? 10 : 30;
	std::vector<uint32_t> edges[4];
	for (int i = 0; i < n; i++) { edges[0].push_back(i); edges[1].push_back((n - 1) * n + i); edges[2].push_back(i * n); edges[3].push_back(i * n + n - 1); }
	const bool flips[4] = { false, true, true, false };
	for (int e4 = 0; e4 < 4; e4++) {
		const uint32_t base = (uint32_t)g.Count();
		for (uint32_t vi : edges[e4]) {
			for (int k = 1; k < 4; k++) g.Set(k, g.C[k][vi * 2], g.C[k][vi * 2 + 1]);
			g.V(g.P[vi * 3], g.P[vi * 3 + 1] - skirt, g.P[vi * 3 + 2], g.N[vi * 3], g.N[vi * 3 + 1], g.N[vi * 3 + 2], g.C[0][vi * 2], g.C[0][vi * 2 + 1]);
		}
		const std::vector<uint32_t>& list = edges[e4];
		for (size_t q = 0; q + 1 < list.size(); q++) {
			const uint32_t a = list[q], b = list[q + 1], a2 = base + (uint32_t)q, b2 = base + (uint32_t)q + 1;
			if (flips[e4]) { g.Tri(a, a2, b); g.Tri(b, a2, b2); } else { g.Tri(a, b, a2); g.Tri(b, b2, a2); }
		}
	}
	return g;
}

} // namespace atg
