#include "TrainModels.h"
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>

namespace atg {

namespace {

double SrgbToLinear(double c) { return c < 0.04045 ? c * 0.0773993808 : std::pow(c * 0.9478672986 + 0.0521327014, 2.4); }
struct Lin { double r, g, b; };
Lin HexLin(uint32_t h) { return { SrgbToLinear(((h >> 16) & 255) / 255.0), SrgbToLinear(((h >> 8) & 255) / 255.0), SrgbToLinear((h & 255) / 255.0) }; }

// GeoBuilder.box (top on by default, no bottom)
void Bx(MeshBuf& g, double x0, double y0, double z0, double x1, double y1, double z1, bool top = true) { g.Box(x0, y0, z0, x1, y1, z1, top, true, false); }

void Bogie(MeshBuf& gb, double z, Lin color = { 0.12, 0.12, 0.13 }) {
	gb.Color(color.r, color.g, color.b);
	Bx(gb, -1.25, 0.35, z - 1.5, 1.25, 0.95, z + 1.5);
	gb.Color(0.08, 0.08, 0.08);
	const MeshBuf wheel = Geo::Cylinder(0.46, 0.46, 0.14, 16);
	for (double dz : { -1.05, 1.05 }) for (double x : { -0.72, 0.72 }) gb.Add(wheel, Mat4::Compose(x, 0.46, z + dz, 0, 0, kPi / 2));
	gb.Color(0.5, 0.5, 0.52);
	const MeshBuf axle = Geo::Cylinder(0.07, 0.07, 1.7, 8);
	for (double dz : { -1.05, 1.05 }) gb.Add(axle, Mat4::Compose(0, 0.46, z + dz, 0, 0, kPi / 2));
}

void Put(TrainModel& M, const char* name, MeshBuf&& m, EVMat mat) {
	if (m.Empty()) return;
	VPart p; p.name = name; p.mesh = std::move(m); p.mat = mat;
	M.parts.push_back(std::move(p));
}

TrainModel MakeLoco(uint32_t color) {
	TrainModel M;
	const Lin paint = HexLin(color);
	MeshBuf body, trim;
	const double W = 1.5, zf = LOCO_L / 2, zr = -LOCO_L / 2;
	// frame & fuel tank
	trim.Color(0.14, 0.14, 0.15);
	Bx(trim, -1.45, 0.95, zr, 1.45, 1.35, zf);
	Bx(trim, -1.1, 0.55, -3.5, 1.1, 1.0, 3.5);
	Bogie(trim, zr + 3.6); Bogie(trim, zf - 3.6);
	// long hood (narrow) + cab near the front + short nose
	body.Color(paint.r, paint.g, paint.b);
	Bx(body, -1.05, 1.35, zr + 0.3, 1.05, 3.9, zf - 5.2);
	Bx(body, -W, 1.35, zf - 5.2, W, 4.35, zf - 1.9);
	Bx(body, -1.2, 1.35, zf - 1.9, 1.2, 2.9, zf - 0.2);
	// yellow stripe & grey roof on the cab
	body.Color(0.95, 0.75, 0.12);
	Bx(body, -1.07, 1.9, zr + 0.3, 1.07, 2.15, zf - 5.2);
	Bx(body, -W - 0.01, 1.9, zf - 5.2, W + 0.01, 2.15, zf - 1.9);
	Bx(body, -1.21, 1.9, zf - 1.9, 1.21, 2.15, zf - 0.2);
	body.Color(0.35, 0.36, 0.37);
	Bx(body, -W - 0.02, 4.35, zf - 5.3, W + 0.02, 4.5, zf - 1.8);
	// radiator fans & horn on the hood roof
	trim.Color(0.2, 0.2, 0.2);
	const MeshBuf fan = Geo::Cylinder(0.6, 0.6, 0.12, 16);
	for (double z : { zr + 2, zr + 4.2 }) trim.Add(fan, Mat4::Compose(0, 3.96, z));
	trim.Add(Geo::Cylinder(0.08, 0.12, 0.5, 8), Mat4::Compose(0.4, 4.7, zf - 3, kPi / 2, 0, 0));
	// handrails along the walkways
	trim.Color(0.85, 0.8, 0.2);
	for (double x : { -1.38, 1.38 }) Bx(trim, x - 0.03, 2.3, zr + 0.4, x + 0.03, 2.36, zf - 5.4);
	// cab windows
	MeshBuf glass; glass.Color(1, 1, 1);
	Bx(glass, -1.2, 3.1, zf - 1.88, 1.2, 4.05, zf - 1.86);
	Bx(glass, -W - 0.02, 3.1, zf - 4.8, -W, 4.05, zf - 2.4);
	Bx(glass, W, 3.1, zf - 4.8, W + 0.02, 4.05, zf - 2.4);
	// headlights + ditch lights
	MeshBuf lights; lights.Color(1, 1, 1);
	Bx(lights, -0.25, 2.55, zf - 0.21, 0.25, 2.8, zf - 0.17);
	for (double x : { -0.8, 0.8 }) Bx(lights, x - 0.14, 1.5, zf - 0.21, x + 0.14, 1.72, zf - 0.17);
	MeshBuf tail; tail.Color(1, 1, 1);
	for (double x : { -0.8, 0.8 }) Bx(tail, x - 0.14, 3.3, zr + 0.26, x + 0.14, 3.5, zr + 0.3);
	// plough
	trim.Color(0.95, 0.75, 0.12);
	Bx(trim, -1.4, 0.3, zf - 0.2, 1.4, 0.9, zf + 0.25);
	Put(M, "body", std::move(body), EVMat::Body);
	Put(M, "trim", std::move(trim), EVMat::Trim);
	Put(M, "glass", std::move(glass), EVMat::Glass);
	Put(M, "head", std::move(lights), EVMat::Head);
	Put(M, "tail", std::move(tail), EVMat::Tail);
	M.seat = { 0.6, 2.6, zf - 3.4 };
	M.doorPos = { W + 0.6, 0, zf - 3.4 };
	return M;
}

TrainModel MakeCarriage(uint32_t color) {
	TrainModel M;
	const Lin paint = HexLin(color);
	MeshBuf body, trim, glass;
	glass.Color(1, 1, 1);
	const double W = 1.48, z0 = -CAR_L / 2, z1 = CAR_L / 2;
	trim.Color(0.14, 0.14, 0.15);
	Bx(trim, -1.4, 0.95, z0 + 0.4, 1.4, 1.3, z1 - 0.4);
	Bogie(trim, z0 + 3.2); Bogie(trim, z1 - 3.2);
	// body shell: lower (paint), window band (light), roof (grey, rounded by a tapered box)
	body.Color(paint.r, paint.g, paint.b);
	Bx(body, -W, 1.3, z0, W, 2.55, z1, false);
	body.Color(0.9, 0.9, 0.88);
	Bx(body, -W, 2.55, z0, W, 3.55, z1, false);
	body.Color(paint.r, paint.g, paint.b);
	Bx(body, -W, 3.55, z0, W, 3.8, z1, false);
	body.Color(0.42, 0.43, 0.45);
	body.Add(Geo::Cylinder(W, W, CAR_L, 20, 1, false, -kPi / 2, kPi), Mat4::Compose(0, 3.8, 0, kPi / 2, 0, 0, 1, 1, 0.28));
	// doors (two per side) in a darker paint with windows
	body.Color(paint.r * 0.7, paint.g * 0.7, paint.b * 0.7);
	for (double z : { z0 + 2.6, z1 - 2.6 }) for (double x : { -W - 0.01, W + 0.01 }) Bx(body, x - 0.005, 1.35, z - 0.65, x + 0.005, 3.45, z + 0.65);
	// windows
	for (int k = 0; k < 6; k++) {
		const double z = z0 + 5 + k * 2.1;
		for (double x : { -W - 0.012, W + 0.012 }) Bx(glass, x - 0.004, 2.7, z - 0.8, x + 0.004, 3.4, z + 0.8);
	}
	for (double z : { z0 + 2.6, z1 - 2.6 }) for (double x : { -W - 0.016, W + 0.016 }) Bx(glass, x - 0.004, 2.75, z - 0.35, x + 0.004, 3.3, z + 0.35);
	// gangway bellows
	trim.Color(0.08, 0.08, 0.08);
	Bx(trim, -0.8, 1.4, z1 - 0.05, 0.8, 3.5, z1 + 0.55);
	Put(M, "body", std::move(body), EVMat::Body);
	Put(M, "trim", std::move(trim), EVMat::Trim);
	Put(M, "glass", std::move(glass), EVMat::Glass);
	return M;
}

const double CONTAINER[6][3] = { { 0.72, 0.16, 0.12 }, { 0.12, 0.3, 0.55 }, { 0.15, 0.45, 0.22 }, { 0.85, 0.5, 0.1 }, { 0.55, 0.56, 0.58 }, { 0.6, 0.1, 0.35 } };

TrainModel MakeWagon(const std::string& type, double seed) {
	TrainModel M;
	MeshBuf body, trim;
	const double z0 = -WAG_L / 2, z1 = WAG_L / 2, W = 1.45;
	trim.Color(0.13, 0.13, 0.14);
	Bx(trim, -1.35, 0.95, z0 + 0.3, 1.35, 1.25, z1 - 0.3);
	Bogie(trim, z0 + 2.4); Bogie(trim, z1 - 2.4);
	// couplers
	Bx(trim, -0.15, 0.95, z0 - 0.6, 0.15, 1.15, z0 + 0.3); Bx(trim, -0.15, 0.95, z1 - 0.3, 0.15, 1.15, z1 + 0.6);
	auto rnd = [&](double k) { const double x = std::sin(seed * 12.9898 + k * 78.233) * 43758.5453; return x - std::floor(x); };
	if (type == "box") {
		const double C3[3][3] = { { 0.45, 0.2, 0.12 }, { 0.3, 0.32, 0.35 }, { 0.55, 0.42, 0.2 } };
		const double* c = C3[(int)std::floor(rnd(1) * 3)];
		body.Color(c[0], c[1], c[2]);
		Bx(body, -W, 1.25, z0 + 0.2, W, 4.0, z1 - 0.2);
		body.Color(c[0] * 0.75, c[1] * 0.75, c[2] * 0.75);
		for (double z = z0 + 1.2; z < z1 - 0.8; z += 1.1) for (double x : { -W - 0.02, W + 0.02 }) Bx(body, x - 0.02, 1.3, z - 0.05, x + 0.02, 3.95, z + 0.05);
		body.Color(c[0] * 0.6, c[1] * 0.6, c[2] * 0.6);
		for (double x : { -W - 0.03, W + 0.03 }) Bx(body, x - 0.02, 1.35, -1.2, x + 0.02, 3.7, 1.2);
		body.Color(0.28, 0.28, 0.3);
		Bx(body, -W - 0.05, 4.0, z0 + 0.15, W + 0.05, 4.12, z1 - 0.15);
	} else if (type == "tank") {
		const double dark[3] = { 0.1, 0.1, 0.11 }, light[3] = { 0.72, 0.73, 0.75 };
		const double* c = rnd(2) < 0.5 ? dark : light;
		body.Color(c[0], c[1], c[2]);
		body.Add(Geo::Cylinder(1.35, 1.35, WAG_L - 1.6, 20), Mat4::Compose(0, 2.65, 0, kPi / 2, 0, 0));
		const MeshBuf cap = Geo::Sphere(1.35, 16, 8, 0, kTau, 0, kPi / 2);
		for (double z : { z0 + 0.8, z1 - 0.8 }) body.Add(cap, Mat4::Compose(0, 2.65, z, z > 0 ? kPi / 2 : -kPi / 2, 0, 0, 1, 0.35, 1));
		body.Color(c[0] * 0.8 + 0.05, c[1] * 0.8 + 0.05, c[2] * 0.8 + 0.05);
		body.Add(Geo::Cylinder(0.45, 0.45, 0.35, 12), Mat4::Compose(0, 4.05, 0));
		trim.Color(0.85, 0.7, 0.15);
		for (double x : { -1.2, 1.2 }) Bx(trim, x - 0.03, 1.5, z0 + 1, x + 0.03, 1.56, z1 - 1);
	} else if (type == "hopper") {
		const double c[3] = { 0.42, 0.44, 0.45 };
		body.Color(c[0], c[1], c[2]);
		Bx(body, -W, 1.9, z0 + 0.3, W, 3.8, z1 - 0.3, false);
		const MeshBuf cone = Geo::Cone(1.1, 0.8, 4, 1);
		for (double z : { z0 + 3.5, 0.0, z1 - 3.5 }) body.Add(cone, Mat4::Compose(0, 1.55, z, kPi, kPi / 4, 0));
		body.Color(0.08, 0.07, 0.07);
		Bx(body, -W + 0.1, 3.5, z0 + 0.4, W - 0.1, 3.72, z1 - 0.4);
		body.Color(c[0] * 0.8, c[1] * 0.8, c[2] * 0.8);
		for (double z = z0 + 1; z < z1 - 0.6; z += 1.6) for (double x : { -W - 0.02, W + 0.02 }) Bx(body, x - 0.03, 1.95, z - 0.06, x + 0.03, 3.75, z + 0.06);
	} else {
		// flat car with a 40 ft container
		trim.Color(0.2, 0.2, 0.21);
		Bx(trim, -1.4, 1.25, z0 + 0.2, 1.4, 1.4, z1 - 0.2);
		const double* c = CONTAINER[(int)std::floor(rnd(3) * 6)];
		body.Color(c[0], c[1], c[2]);
		Bx(body, -1.22, 1.4, -6.1, 1.22, 4.0, 6.1);
		body.Color(c[0] * 0.78, c[1] * 0.78, c[2] * 0.78);
		for (double z = -5.7; z < 5.8; z += 0.55) for (double x : { -1.24, 1.24 }) Bx(body, x - 0.02, 1.5, z - 0.05, x + 0.02, 3.9, z + 0.05);
		body.Color(0.9, 0.9, 0.9);
		for (double x : { -1.26, 1.26 }) Bx(body, x - 0.01, 3.3, -4, x + 0.01, 3.6, -1.5);
	}
	Put(M, "body", std::move(body), EVMat::Body);
	Put(M, "trim", std::move(trim), EVMat::Trim);
	return M;
}

std::mutex GMutex;
std::map<std::string, std::unique_ptr<TrainModel>> GCache;

const TrainModel& Cached(const std::string& key, const std::function<TrainModel()>& make) {
	std::lock_guard<std::mutex> lock(GMutex);
	auto& slot = GCache[key];
	if (!slot) slot = std::make_unique<TrainModel>(make());
	return *slot;
}

} // namespace

const TrainModel& LocoModel(uint32_t color) { return Cached("loco:" + std::to_string(color), [&] { return MakeLoco(color); }); }
const TrainModel& CarriageModel(uint32_t color) { return Cached("coach:" + std::to_string(color), [&] { return MakeCarriage(color); }); }
const TrainModel& WagonModel(const std::string& type, double seed) {
	char key[96];
	std::snprintf(key, sizeof key, "wagon:%s:%.6f", type.c_str(), seed);
	return Cached(key, [&] { return MakeWagon(type, seed); });
}

} // namespace atg
