// The big picture (port of src/world/worldgen.js): world bounds, regions (countryside, forest mountains,
// desert), towns, San Aurelio's footprint and valleys, the military base, the river, the terrain height
// function and the Heightfield (4 m grid) that physics and rendering both sample.
#pragma once

#include "GenMath.h"

namespace atg {

struct Rect { double minX, maxX, minZ, maxZ; };
constexpr Rect WORLD{ -5600, 1600, -5400, 1400 };
constexpr double HF_STEP = 4;
// the Los Soles city rectangle (citymap XS/ZS extremes +- half a road)
constexpr Rect CITY_RECT{ -840, 880, -780, 635 };

struct Town { const char* key; const char* name; double x, z, r, padR, padY; };
// in the JavaScript object's key order (it matters: pads, zone names and towns are visited in it)
extern const std::vector<Town> TOWNS;
const Town& TownByKey(const char* key);

struct NCityDef { const char* key; const char* name; double x, z, y; double rings[3]; };
extern const NCityDef NCITY;
double NcRingR(int k, double th);
double NcEdgeDist(double x, double z);
double NcUrban(double x, double z);
double NeValleyFloor(double x, double z);

struct BaseDef { const char* name; double minX, maxX, minZ, maxZ, gateZ; };
extern const BaseDef BASE;
struct AirfieldDef { const char* name; double x, z, len, yaw; };
extern const AirfieldDef AIRFIELD;
struct LakeDef { double x, z, r, y; };
extern const LakeDef LAKE;
extern const std::vector<V2> RIVER;

double PNoise(double x, double y);
double FbmN(double x, double y, int oct);
double Ridged(double x, double y, int oct);
double CityDist(double x, double z);
double RiverDist(double x, double z);
double CoastZ(double x);
double CoastX(double z);
struct Regions { double desert, mountain, country; };
Regions RegionWeights(double x, double z);
double LandHeight(double x, double z);

// a flat pad pressed into the terrain: round (r > 0) or a rectangle
struct Pad {
	std::string key;
	double x = 0, z = 0, r = 0;
	double minX = 0, maxX = 0, minZ = 0, maxZ = 0;
	double y = NaN();
	double blend = 60;
	bool keepSea = false;
};

struct RouteDef;

class Heightfield {
public:
	double minX, minZ, step;
	int nx, nz;
	std::vector<float> h;

	Heightfield(const Rect& b = WORLD, double stepM = HF_STEP);
	void Generate(double (*fn)(double, double), int coarse, std::vector<Pad>& pads);
	void CarveRoutes(const std::vector<RouteDef>& routes, const std::function<double(const std::string&)>& endY);
	void ForRect(double x0, double z0, double x1, double z1, const std::function<void(int, double, double)>& cb);
	double PadIt(Pad& p);
	double Sample(double x, double z) const;
	void Normal(double x, double z, double out[3]) const;
};

} // namespace atg
