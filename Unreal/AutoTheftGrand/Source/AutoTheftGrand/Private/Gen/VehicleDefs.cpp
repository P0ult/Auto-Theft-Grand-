#include "VehicleDefs.h"

namespace atg {

namespace {
struct Builder {
	std::vector<VehicleDef> v;
	// the common road-vehicle numbers
	VehicleDef& add(const char* id, const char* name, const char* body, double L, double W, double H, double wb, double track, double wheelR, double clearance,
		double mass, double force, double top, double grip, EDrive drive, double steer, double brake, int rarity, std::vector<uint32_t> colors) {
		VehicleDef d;
		d.id = id; d.name = name; d.body = body;
		d.L = L; d.W = W; d.H = H; d.wheelbase = wb; d.track = track; d.wheelR = wheelR; d.clearance = clearance;
		d.mass = mass; d.force = force; d.top = top; d.grip = grip; d.drive = drive; d.steer = steer; d.brake = brake; d.rarity = rarity;
		d.colors = std::move(colors);
		v.push_back(std::move(d));
		return v.back();
	}
	// boats, the tank, trains and aircraft (their car-physics numbers are placeholders, as in the JS)
	VehicleDef& other(const char* id, const char* name, const char* kind, double L, double W, double H, double mass, std::vector<uint32_t> colors) {
		VehicleDef d;
		d.id = id; d.name = name; d.kind = kind; d.L = L; d.W = W; d.H = H; d.mass = mass; d.colors = std::move(colors);
		v.push_back(std::move(d));
		return v.back();
	}
};

std::vector<VehicleDef> Make() {
	Builder b;
	const EDrive R = EDrive::RWD, F = EDrive::FWD, A = EDrive::AWD;
	b.add("meridian", "Meridian", "sedan", 4.8, 1.86, 1.45, 2.8, 1.58, 0.34, 0.26, 1450, 8200, 47, 1.0, R, 0.62, 15000, 10,
		{ 0x8c1c13, 0x1d3557, 0xe8e8e8, 0x2b2b2b, 0x6c757d, 0x3a5a40, 0xbc6c25, 0x5e548e });
	b.add("kestrel", "Kestrel GT", "coupe", 4.5, 1.88, 1.3, 2.62, 1.6, 0.34, 0.2, 1350, 12500, 60, 1.12, R, 0.6, 18000, 4,
		{ 0xd00000, 0xffba08, 0x0077b6, 0x111111, 0xf1faee, 0x2dc653 });
	b.add("brawler", "Brawler", "muscle", 5.0, 1.95, 1.35, 2.95, 1.62, 0.36, 0.24, 1600, 13000, 55, 0.9, R, 0.6, 15000, 5,
		{ 0x111111, 0xf77f00, 0x9d0208, 0x3a86ff, 0xffffff, 0x606c38 });
	{ auto& d = b.add("summit", "Summit", "suv", 4.9, 2.0, 1.85, 2.9, 1.7, 0.4, 0.34, 2150, 11500, 44, 0.95, A, 0.58, 17000, 7,
		{ 0x222222, 0xe5e5e5, 0x283618, 0x14213d, 0x7f5539, 0x6d6875 }); d.camDist = 8.2; d.camHeight = 1.9; }
	{ auto& d = b.add("hauler", "Hauler", "pickup", 5.3, 2.0, 1.8, 3.2, 1.7, 0.4, 0.34, 2100, 11000, 42, 0.92, R, 0.56, 16000, 6,
		{ 0x9b2226, 0x005f73, 0xe9d8a6, 0x3d405b, 0xffffff, 0x495057 }); d.camDist = 8.5; d.camHeight = 1.9; }
	{ auto& d = b.add("parcel", "Parcel Van", "van", 5.1, 2.0, 2.2, 3.1, 1.72, 0.38, 0.3, 2400, 9500, 37, 0.88, R, 0.55, 15000, 4,
		{ 0xffffff, 0xd9d9d9, 0x8d99ae, 0x6a994e, 0xbc4749 }); d.camDist = 9; d.camHeight = 2.3; }
	{ auto& d = b.add("taxi", "Cab", "sedan", 4.8, 1.86, 1.45, 2.8, 1.58, 0.34, 0.26, 1450, 8600, 47, 1.0, R, 0.62, 15000, 3, { 0xf4c20d }); d.taxi = true; }
	{ auto& d = b.add("police", "Police Cruiser", "sedan", 4.95, 1.9, 1.5, 2.9, 1.6, 0.35, 0.26, 1650, 12500, 56, 1.08, R, 0.6, 18000, 0, { 0x111111 }); d.police = true; }
	// the law at three stars and up: Sheriff SUVs on rural roadblocks, the SWAT Enforcer at four
	{ auto& d = b.add("sheriff", "Sheriff SUV", "suv", 4.95, 2.0, 1.85, 2.9, 1.7, 0.4, 0.32, 2200, 13500, 50, 1.0, A, 0.58, 18000, 0, { 0xe6e1d3 });
	  d.police = true; d.livery = 0xe6e1d3; d.camDist = 8.2; d.camHeight = 1.9; }
	{ auto& d = b.add("enforcer", "Enforcer", "van", 5.5, 2.1, 2.45, 3.35, 1.8, 0.42, 0.34, 3900, 15500, 42, 0.92, R, 0.54, 21000, 0, { 0x1c2534 });
	  d.armored = true; d.police = true; d.livery = 0x1c2534; d.camDist = 10; d.camHeight = 2.6; d.health = 2000; d.bulletMul = 0.45; }
	// Gruppe-style cash-in-transit van: bullet resistant; shoot the back doors open (heists.js)
	{ auto& d = b.add("stockade", "Stockade", "van", 5.7, 2.15, 2.5, 3.45, 1.82, 0.43, 0.34, 4300, 15000, 36, 0.9, R, 0.52, 22000, 0, { 0xf0f0ec });
	  d.armored = true; d.camDist = 10.5; d.camHeight = 2.7; d.health = 2600; d.bulletMul = 0.3; }
	// more on the streets
	b.add("buffalo", "Buffalo S", "sedan", 4.95, 1.92, 1.42, 2.95, 1.62, 0.36, 0.22, 1650, 13500, 58, 1.08, A, 0.6, 18000, 4,
		{ 0x0b0b0d, 0x2f3d4c, 0xb8bcc2, 0x7a0f14, 0xf2f2f2, 0x1f4e3d });
	{ auto& d = b.add("baller", "Baller", "suv", 5.05, 2.02, 1.82, 3.0, 1.72, 0.42, 0.3, 2350, 13000, 50, 0.98, A, 0.57, 18000, 4,
		{ 0x0b0b0d, 0xf4f4f2, 0x3c3f44, 0x5b4a3a, 0x1b2a41 }); d.camDist = 8.3; d.camHeight = 1.9; }
	b.add("tempest", "Tempest", "super", 4.75, 2.04, 1.12, 2.8, 1.74, 0.36, 0.13, 1450, 18500, 76, 1.28, A, 0.57, 23000, 1,
		{ 0x2dd4bf, 0xff6b00, 0xf8f8f8, 0x7c3aed, 0x111111, 0xd90429 });
	b.add("zenith", "Zenith", "super", 4.6, 2.0, 1.15, 2.7, 1.7, 0.35, 0.14, 1400, 17000, 72, 1.25, A, 0.58, 22000, 1,
		{ 0xffd60a, 0xe63946, 0x00b4d8, 0xffffff, 0x111111, 0x80ed99 });
	b.add("pico", "Pico", "hatch", 4.0, 1.76, 1.5, 2.5, 1.5, 0.31, 0.2, 1100, 6400, 43, 1.02, F, 0.66, 12500, 7,
		{ 0xe63946, 0x2a9d8f, 0xf4a261, 0xe9ecef, 0x457b9d, 0x6a4c93, 0x8ac926 });
	{ auto& d = b.add("bouncer", "Bouncer", "lowrider", 5.3, 1.96, 1.35, 3.05, 1.6, 0.33, 0.16, 1750, 9000, 46, 0.95, R, 0.6, 14000, 3,
		{ 0x5a189a, 0x2a9d8f, 0xe76f51, 0x9d0208, 0x264653, 0xffb703 }); d.hydraulics = true; }
	{ auto& d = b.add("boxer", "Boxer Truck", "truck", 7.4, 2.35, 3.2, 4.4, 1.95, 0.48, 0.38, 5200, 20000, 32, 0.85, R, 0.5, 30000, 2,
		{ 0xffffff, 0x1d3557, 0x9b2226 }); d.camDist = 12; d.camHeight = 3.2; }
	// ------------------------------------------------ two-wheelers (car physics on a narrow track; see bikes.js)
	{ auto& d = b.add("razor", "Razor 600", "sport", 2.1, 0.72, 1.2, 1.42, 0.3, 0.31, 0.14, 330, 3100, 64, 1.15, R, 0.55, 3900, 2,
		{ 0xd00000, 0x111111, 0x0077b6, 0xffffff, 0x2dc653, 0xff9f1c }); d.bike = "moto"; d.camDist = 5.4; d.camHeight = 1.55; d.health = 600; }
	{ auto& d = b.add("trail", "Trailblazer", "dirt", 2.15, 0.8, 1.25, 1.46, 0.3, 0.36, 0.3, 300, 2500, 45, 1.05, R, 0.62, 3300, 1,
		{ 0xff6b00, 0xf7d000, 0x1b4dd8, 0x2e7d32, 0xffffff }); d.bike = "moto"; d.offroad = true; d.camDist = 5.4; d.camHeight = 1.6; d.health = 550; }
	{ auto& d = b.add("bmx", "BMX", "bmx", 1.65, 0.62, 1.1, 0.98, 0.25, 0.3, 0.2, 95, 440, 10.5, 1.0, R, 0.75, 900, 0,
		{ 0x00b4d8, 0xef233c, 0x111111, 0xffd60a, 0x80ed99 }); d.bike = "bicycle"; d.pedal = true; d.camDist = 4.6; d.camHeight = 1.45; d.health = 300; }
	{ auto& d = b.add("roadbike", "Road Bike", "road", 1.75, 0.6, 1.1, 1.02, 0.25, 0.34, 0.22, 100, 480, 14, 1.0, R, 0.62, 950, 0,
		{ 0xe63946, 0x1d3557, 0xf1faee, 0x2a9d8f, 0x111111 }); d.bike = "bicycle"; d.pedal = true; d.camDist = 4.8; d.camHeight = 1.5; d.health = 300; }
	{ auto& d = b.add("skateboard", "Skateboard", "board", 0.82, 0.22, 0.14, 0.48, 0.19, 0.028, 0.06, 80, 300, 9.5, 0.9, R, 0.42, 380, 0,
		{ 0x1d3557, 0xe63946, 0xffb703, 0x2a9d8f, 0x8338ec, 0x111111, 0xf1faee }); d.bike = "board"; d.board = true; d.camDist = 4.2; d.camHeight = 1.75; d.health = 250; }
	// ------------------------------------------------ military ground vehicles (car physics)
	{ auto& d = b.add("ranger", "Ranger", "suv", 4.7, 2.05, 1.95, 2.85, 1.75, 0.44, 0.42, 2300, 13000, 42, 1.05, A, 0.6, 18000, 0,
		{ 0x4b5320, 0x5a5a3c, 0x6b5b3e }); d.camDist = 8.4; d.camHeight = 2.0; d.military = true; }
	{ auto& d = b.add("barracks", "Barracks", "truck", 8.2, 2.5, 3.3, 4.8, 2.0, 0.55, 0.5, 7800, 26000, 30, 0.9, A, 0.5, 36000, 0,
		{ 0x4b5320 }); d.camDist = 13; d.camHeight = 3.4; d.military = true; }
	// ------------------------------------------------ boats (see boat.js)
	auto boat = [&](const char* id, const char* name, const char* kind, double L, double W, double H, double draft, double freeboard, double mass, double force, double top, double turn,
		double camDist, double camHeight, double health, double wb, double track, std::vector<uint32_t> colors) -> VehicleDef& {
		VehicleDef& d = b.other(id, name, "boat", L, W, H, mass, std::move(colors));
		d.boat = kind; d.draft = draft; d.freeboard = freeboard; d.force = force; d.top = top; d.turn = turn;
		d.camDist = camDist; d.camHeight = camHeight; d.health = health; d.wheelbase = wb; d.track = track; d.wheelR = 0.3; d.clearance = 0;
		d.grip = 1; d.drive = R; d.steer = 0.5; d.brake = 1;
		return d;
	};
	boat("dinghy", "Dinghy", "rib", 4.4, 2.0, 1.1, 0.28, 0.55, 520, 4200, 17, 1.25, 7.5, 2.4, 600, 2.6, 1.6, { 0xd62828, 0x2b2d42, 0xf77f00, 0x3a5a40 });
	boat("speedboat", "Squalo", "speed", 6.8, 2.4, 1.5, 0.42, 0.9, 1500, 15500, 32, 1.05, 9.5, 2.8, 900, 4, 2, { 0xffffff, 0xe63946, 0x1d3557, 0xffb703, 0x111111 });
	{ auto& d = boat("jetski", "Wave Rider", "jetski", 3.1, 1.15, 1.1, 0.2, 0.42, 380, 4600, 25, 1.9, 5.8, 2.0, 450, 1.8, 0.8, { 0xffd60a, 0x00b4d8, 0xe63946, 0x80ed99, 0xffffff });
	  d.astride = true; d.wheelR = 0.2; }
	boat("cruiser", "Marquis", "cruiser", 11.5, 3.8, 3.6, 0.9, 1.35, 9500, 30000, 14, 0.5, 17, 5.5, 1600, 7, 3, { 0xffffff, 0xf1faee, 0x264653 });
	{ auto& d = boat("policeboat", "Predator", "police", 8.4, 2.8, 2.7, 0.5, 1.0, 2900, 24000, 30, 0.95, 11, 3.6, 1400, 5, 2.2, { 0xf4f4f4 }); d.police = true; d.weapons = true; }
	// ------------------------------------------------ tank
	{ auto& d = b.other("mammoth", "Mammoth Tank", "tank", 9.2, 3.7, 2.9, 46000, { 0x4f5a36 });
	  d.tank = true; d.top = 13; d.turn = 0.85; d.accel = 3.2; d.wheelbase = 5; d.track = 3; d.wheelR = 0.5; d.clearance = 0.5; d.force = 1; d.grip = 1; d.drive = A; d.steer = 0.5; d.brake = 1;
	  d.camDist = 14; d.camHeight = 4.2; d.military = true; d.weapons = true; d.health = 3000; d.bulletMul = 0.05; d.blastMul = 0.35; }
	// ------------------------------------------------ Sol Line train (locomotive; carriages follow)
	{ auto& d = b.other("train", "Sol Line Express", "train", 17, 3, 4.5, 110000, { 0x1d4e89, 0x8c1c13, 0x2a6041 });
	  d.train = true; d.top = 32; d.wheelbase = 12; d.track = 1.4; d.wheelR = 0.46; d.clearance = 0.9; d.force = 1; d.grip = 1; d.drive = A; d.steer = 0; d.brake = 1; d.camDist = 24; d.camHeight = 6; }
	{ auto& d = b.other("freight", "Sol Line Freight", "train", 17, 3, 4.5, 130000, { 0xc8541a, 0x2b2d30, 0x9b1c1c });
	  d.train = true; d.freight = true; d.top = 26; d.wheelbase = 12; d.track = 1.4; d.wheelR = 0.46; d.clearance = 0.9; d.force = 1; d.grip = 1; d.drive = A; d.steer = 0; d.brake = 1; d.camDist = 26; d.camHeight = 6.5; }
	// ------------------------------------------------ aircraft
	auto plane = [&](const char* id, const char* name, const char* kind, double L, double W, double H, double colW, double mass, double health,
		double thrust, double vStall, double vMax, double vRotate, double pitchRate, double rollRate, double yawRate, double gearH, double drag,
		double camDist, double camHeight, double maxDial, std::vector<uint32_t> colors) -> VehicleDef& {
		VehicleDef& d = b.other(id, name, kind, L, W, H, mass, std::move(colors));
		d.aircraft = true; d.colW = colW; d.health = health; d.thrust = thrust; d.vStall = vStall; d.vMax = vMax; d.vRotate = vRotate;
		d.pitchRate = pitchRate; d.rollRate = rollRate; d.yawRate = yawRate; d.gearH = gearH; d.drag = drag; d.camDist = camDist; d.camHeight = camHeight; d.maxDial = maxDial;
		return d;
	};
	plane("skipper", "Skipper", "plane", 8.4, 11, 2.9, 1.3, 1100, 700, 11, 20, 60, 24, 1.3, 2.2, 0.6, 1.3, 0.0028, 17, 3.4, 200, { 0xd62828, 0xf4f1de, 0x1d3557, 0xf77f00 });
	{ auto& d = plane("raptor", "Raptor", "jet", 16.5, 11.5, 4.4, 3.2, 12000, 1100, 28, 34, 150, 42, 1.6, 3.6, 0.55, 1.9, 0.0011, 25, 5, 400, { 0x6b7178, 0x4d535a });
	  d.weapons = true; d.military = true; }
	{ auto& d = plane("hercules", "Hercules", "plane", 29, 40, 11, 4.4, 40000, 1800, 7.5, 30, 82, 36, 0.55, 0.75, 0.3, 2.1, 0.0009, 46, 12, 250, { 0x5d6a4f });
	  d.cargo = true; d.military = true; }
	auto heli = [&](const char* id, const char* name, double L, double W, double H, double colW, double mass, double health, double vMax, double rotorR,
		double camDist, double camHeight, std::vector<uint32_t> colors) -> VehicleDef& {
		VehicleDef& d = b.other(id, name, "heli", L, W, H, mass, std::move(colors));
		d.aircraft = true; d.colW = colW; d.health = health; d.vMax = vMax; d.rotorR = rotorR; d.skidH = 0.25; d.camDist = camDist; d.camHeight = camHeight; d.maxDial = 200;
		return d;
	};
	{ auto& d = heli("warhawk", "Warhawk", 15.5, 3.2, 4.2, 2.2, 7000, 1300, 72, 7.3, 18, 5.5, { 0x3d4a33 }); d.weapons = true; d.military = true; d.bulletMul = 0.6; }
	heli("skylark", "Skylark", 11.5, 2.4, 3.3, 1.9, 2400, 800, 62, 5.4, 14, 4.2, { 0x1d3557, 0xe63946, 0xf1faee, 0x111111 });
	return std::move(b.v);
}
} // namespace

const std::vector<VehicleDef>& VehicleDefs() { static const std::vector<VehicleDef> D = Make(); return D; }

const VehicleDef* FindVehicle(const std::string& id) {
	for (const VehicleDef& d : VehicleDefs()) if (d.id == id) return &d;
	return nullptr;
}

const std::vector<std::pair<std::string, double>>& TrafficPool() {
	static const std::vector<std::pair<std::string, double>> P = [] {
		std::vector<std::pair<std::string, double>> p;
		for (const VehicleDef& d : VehicleDefs()) if (d.rarity > 0) p.push_back({ d.id, (double)d.rarity });
		return p;
	}();
	return P;
}

const std::vector<std::string>& BikeIds() {
	static const std::vector<std::string> P = [] { std::vector<std::string> p; for (const VehicleDef& d : VehicleDefs()) if (!d.bike.empty() && !d.board) p.push_back(d.id); return p; }();
	return P;
}
const std::vector<std::string>& BoatIds() {
	static const std::vector<std::string> P = [] { std::vector<std::string> p; for (const VehicleDef& d : VehicleDefs()) if (d.kind == "boat") p.push_back(d.id); return p; }();
	return P;
}

} // namespace atg
