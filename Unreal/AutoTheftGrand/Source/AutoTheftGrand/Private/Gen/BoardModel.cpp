#include "BoardModel.h"
#include "ShapeGeo.h"
#include <map>
#include <memory>

namespace atg {

namespace {
using BoardPt = std::array<double, 3>;
using BoardRows = std::vector<std::vector<BoardPt>>;

// GeoBuilder.addGeometry: the primitive de-indexed (toNonIndexed), each vertex then indexed in order
MeshBuf BoardFlat(const MeshBuf& g) {
	MeshBuf r = NonIndexed(g);
	r.I.resize(r.Count());
	for (size_t i = 0; i < r.I.size(); i++) r.I[i] = (uint32_t)i;
	return r;
}

void BoardQuadGrid(MeshBuf& gb, const BoardRows& rows, bool up) {
	const double n[3] = { 0, up ? 1.0 : -1.0, 0 };
	for (size_t j = 0; j + 1 < rows.size(); j++) for (size_t i = 0; i + 1 < rows[0].size(); i++) {
		const BoardPt &a = rows[j][i], &b = rows[j][i + 1], &c = rows[j + 1][i + 1], &d = rows[j + 1][i];
		gb.Quad((up ? a : b).data(), (up ? d : c).data(), (up ? c : d).data(), (up ? b : a).data(), n);
	}
}

BoardModel MakeBoard(const VehicleDef& def) {
	BoardModel m;
	const double L = def.L, hw = def.W / 2;
	const int nx = 10, nz = 26;
	const double t = 0.012;
	// deck outline: a long rounded rectangle with the nose and tail kicked up
	auto shapeW = [&](double z) { const double e = std::fabs(z) / (L / 2); const double k = Max(0, (e - 0.78) / 0.22); return hw * std::sqrt(Max(0, 1 - k * k)); };
	auto kick = [&](double z) { const double e = (std::fabs(z) - L * 0.3) / (L * 0.2); return e > 0 ? 0.055 * e * e : 0; };
	auto concave = [&](double x) { return 0.006 * (x / hw) * (x / hw); };
	BoardRows top, bot;
	for (int j = 0; j <= nz; j++) {
		const double z = -L / 2 + L * j / nz, w = Max(0.02, shapeW(z) * 0.999);
		std::vector<BoardPt> rt, rb;
		for (int i = 0; i <= nx; i++) {
			const double x = -w + 2 * w * i / nx, y = kick(z) + concave(x);
			rt.push_back({ x, y + t, z }); rb.push_back({ x, y, z });
		}
		top.push_back(rt); bot.push_back(rb);
	}
	MeshBuf& paint = m.paint;
	MeshBuf& trim = m.trim;
	// grip tape on top, the painted graphic underneath, the ply edge all round
	trim.Color(0.07, 0.07, 0.075);
	BoardQuadGrid(trim, top, true);
	paint.Color(1, 1, 1);
	BoardQuadGrid(paint, bot, false);
	trim.Color(0.72, 0.6, 0.42);
	for (int side : { 0, nx }) for (int j = 0; j < nz; j++) {
		const BoardPt &a = top[j][side], &b = top[j + 1][side], &c = bot[j + 1][side], &d = bot[j][side];
		const double n[3] = { side == 0 ? -1.0 : 1.0, 0, 0 };
		trim.Quad((side == 0 ? a : b).data(), (side == 0 ? b : a).data(), (side == 0 ? c : d).data(), (side == 0 ? d : c).data(), n);
	}
	for (int j : { 0, nz }) for (int i = 0; i < nx; i++) {
		const BoardPt &a = top[j][i], &b = top[j][i + 1], &c = bot[j][i + 1], &d = bot[j][i];
		const double n[3] = { 0, 0, j == 0 ? -1.0 : 1.0 };
		trim.Quad((j == 0 ? b : a).data(), (j == 0 ? a : b).data(), (j == 0 ? d : c).data(), (j == 0 ? c : d).data(), n);
	}
	// a painted stripe on the graphic side
	paint.Color(0.15, 0.15, 0.15);
	paint.Box(-hw * 0.7, -0.002, -0.05, hw * 0.7, -0.001, 0.05, false, false, true);
	// a wheel and its hub
	{
		MeshBuf& gb = m.wheel;
		gb.Color(1, 1, 1);
		gb.Add(BoardFlat(Geo::Cylinder(def.wheelR, def.wheelR, 0.032, 16)), Mat4::Compose(0, 0, 0, 0, 0, kPi / 2));
		gb.Color(0.55, 0.55, 0.58);
		gb.Add(BoardFlat(Geo::Cylinder(0.011, 0.011, 0.034, 8)), Mat4::Compose(0, 0, 0, 0, 0, kPi / 2));
	}
	// the trucks: baseplate, kingpin, axle, hanger
	for (int zs : { 1, -1 }) {
		const double z = zs * def.wheelbase / 2;
		trim.Color(0.62, 0.63, 0.66);
		trim.Box(-0.03, -0.012, z - 0.03, 0.03, 0, z + 0.03);
		trim.Add(BoardFlat(Geo::Cylinder(0.012, 0.016, 0.03, 8)), Mat4::Compose(0, -0.027, z));
		trim.Add(BoardFlat(Geo::Cylinder(0.009, 0.009, def.track + 0.02, 8)), Mat4::Compose(0, def.wheelR - 0.065, z, 0, 0, kPi / 2));
		trim.Box(-0.075, -0.05, z - 0.015, 0.075, -0.03, z + 0.015);
		for (int xs : { 1, -1 }) m.wheels.push_back({ xs * def.track / 2, def.wheelR - 0.065, z, zs > 0 });
	}
	return m;
}
}

const BoardModel& BuildBoardModel(const VehicleDef& def) {
	static std::map<std::string, std::unique_ptr<BoardModel>> cache;
	auto& slot = cache[def.id];
	if (!slot) slot = std::make_unique<BoardModel>(MakeBoard(def));
	return *slot;
}

} // namespace atg
