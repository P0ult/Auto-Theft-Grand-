#include "CitySigns.h"
#include "GenMath.h"

namespace atg {

namespace {
// props.js put / billboardGeo
void SignPut(MeshBuf& gb, const MeshBuf& geo, uint32_t color, const Mat4& m) { gb.ColorHex(color); gb.Glow(0); gb.Signal(0); gb.Add(geo, m); }
// GeoBuilder.addGeometry: the geometry made non-indexed (one vertex per index, every channel copied)
MeshBuf SignFlat(const MeshBuf& g) {
	MeshBuf out;
	const size_t n = g.I.empty() ? g.Count() : g.I.size();
	for (size_t k = 0; k < n; k++) {
		const uint32_t i = g.I.empty() ? (uint32_t)k : g.I[k];
		for (int a = 0; a < 3; a++) { out.P.push_back(g.P[i * 3 + a]); out.N.push_back(g.N[i * 3 + a]); }
		for (int c = 0; c < 4; c++) if (!g.C[c].empty()) { out.C[c].push_back(g.C[c][i * 2]); out.C[c].push_back(g.C[c][i * 2 + 1]); }
		out.I.push_back((uint32_t)k);
	}
	return out;
}
MeshBuf SignBillboardGeo() {
	MeshBuf gb;
	const uint32_t m = 0x4a4d52;
	SignPut(gb, Geo::Box(0.3, 5, 0.3), m, Mat4::Compose(-3, 2.5, 0.4));
	SignPut(gb, Geo::Box(0.3, 5, 0.3), m, Mat4::Compose(3, 2.5, 0.4));
	SignPut(gb, Geo::Box(10.4, 5.4, 0.3), 0x2a2c30, Mat4::Compose(0, 7.5, 0.2));
	SignPut(gb, Geo::Box(10.6, 0.15, 1.0), m, Mat4::Compose(0, 4.8, 0.7));
	return SignFlat(gb);
}
} // namespace

CityBillboards BuildBillboards(const CityMap& map) {
	CityBillboards out;
	const int adCount = 8, cols = 2, rows = 4; // (textures.js ADS, adAtlas)
	RNG rng(555);
	const MeshBuf bgeo = SignBillboardGeo();
	int count = 0;
	for (const Building& b : map.buildings) {
		if (!(b.rot == 0 && !IsSet(b.base) && b.roof == "flat" && b.y1 > 10 && b.y1 < 45 && (b.x1 - b.x0) > 14 && (b.z1 - b.z0) > 8 && b.district != "hills")) continue;
		if (!rng.Chance(0.18) || count > 70) continue;
		count++;
		const bool alongX = rng.Chance(0.5);
		const double cx = (b.x0 + b.x1) / 2, cz = (b.z0 + b.z1) / 2;
		const double rot = alongX ? (rng.Chance(0.5) ? 0 : kPi) : (rng.Chance(0.5) ? kPi / 2 : -kPi / 2);
		const Mat4 m = Mat4::Compose(cx, b.y1, cz, 0, rot, 0);
		out.frame.Color(1, 1, 1);
		out.frame.Append(bgeo, m);
		const int ad = rng.Int(0, adCount - 1);
		const double u0 = (double)(ad % cols) / cols, v0 = 1 - (std::floor((double)ad / cols) + 1) / rows;
		const double u1 = u0 + 1.0 / cols, v1 = v0 + 1.0 / rows;
		// face at local z = 0.36 (front), size 10 x 5
		double pts[4][3] = { { -5, 5.0, 0.37 }, { 5, 5.0, 0.37 }, { 5, 10.0, 0.37 }, { -5, 10.0, 0.37 } };
		for (auto& p : pts) m.Apply(p[0], p[1], p[2]);
		const double n[3] = { std::sin(rot), 0, std::cos(rot) };
		const double uv0[2] = { u0, v0 }, uv1[2] = { u1, v0 }, uv2[2] = { u1, v1 }, uv3[2] = { u0, v1 };
		out.faces.Quad(pts[0], pts[1], pts[2], pts[3], n, uv0, uv1, uv2, uv3);
		out.colliders.push_back({ cx - 5, cx + 5, cz - 5, cz + 5, b.y1 + 4.5, b.y1 + 10.2 });
	}
	return out;
}

std::vector<CitySign> PlaceSigns(const CityMap& map) {
	std::vector<CitySign> out;
	for (const Building& b : map.buildings) {
		if (b.sign.empty()) continue;
		if (b.rot != 0) {
			// countryside: sign on the face towards the road (b.front)
			const double hx = (b.x1 - b.x0) / 2, hz = (b.z1 - b.z0) / 2;
			const double w = Min(hz * 2 - 1, 12.0), h = w * 0.19;
			const double f0 = b.hasFront ? b.front[0] : std::cos(b.rot), f1 = b.hasFront ? b.front[1] : -std::sin(b.rot);
			const double cx = (b.x0 + b.x1) / 2, cz = (b.z0 + b.z1) / 2;
			out.push_back({ b.sign, cx + f0 * (hx + 0.08), Min(b.y1 - h * 0.6, b.y0 + b.foundation + 4.2 + h / 2), cz + f1 * (hx + 0.08), w, h, std::atan2(f0, f1) });
			continue;
		}
		const double w = Min(b.x1 - b.x0 - 1, 14.0), h = w * 0.19;
		// sign on the south face if the block's street is south, else north face
		const Block* blk = map.BlockAt((b.x0 + b.x1) / 2, (b.z0 + b.z1) / 2);
		const bool faceSouth = b.shop.valid() ? b.shop.front == "z1" : blk ? (blk->iz1 - b.z1) < (b.z0 - blk->iz0) : true;
		const double y = Min(b.y1 - h * 0.6, b.y0 + 4.2 + h / 2);
		if (faceSouth) out.push_back({ b.sign, (b.x0 + b.x1) / 2, y, b.z1 + 0.06, w, h, 0 });
		else out.push_back({ b.sign, (b.x0 + b.x1) / 2, y, b.z0 - 0.06, w, h, kPi });
	}
	return out;
}

} // namespace atg
