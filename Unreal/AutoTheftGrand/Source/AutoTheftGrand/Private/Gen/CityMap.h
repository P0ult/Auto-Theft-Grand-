// Deterministic layout of the whole world (port of src/world/citymap.js + countryside.js): terrain, road
// network, the Los Soles blocks, lots and buildings, props, fences, parking, walk graph, landmarks,
// San Aurelio's frontage, the towns, farms, base, airfield, stations and the vegetation scatter.
// Pure data (no rendering); the Unreal side turns it into meshes and colliders.
#pragma once

#include "RoadLayout.h"

namespace atg {

struct ShopRef { std::string key, front; bool valid() const { return !key.empty(); } };

struct Building {
	double x0 = 0, z0 = 0, x1 = 0, z1 = 0, y0 = 0, y1 = 0;
	int style = 0;
	double rot = 0;
	double base = NaN();
	double tint[3] = { 1, 1, 1 };
	double seed = 0;
	std::string roof = "flat", district = "midtown", kind = "building";
	double floorH = 3.4, cell = 3.2;
	std::string name, sign;
	bool noCollide = false;
	ShopRef shop;
	bool hasFront = false; double front[2] = { 0, 0 };
	double foundation = 0;
};
struct LotSurface { double x0, z0, x1, z1; std::string type; };
struct PadSurface { double cx, cz, hx, hz, yaw, y; std::string type; bool skate = false; };
struct Prop {
	std::string type;
	double x = 0, z = 0, rot = 0, scale = 1;
	double y = NaN(), h = NaN();
	bool street = false;
	bool hasCorner = false; int corner[2] = { 0, 0 };
};
struct Fence {
	double x0 = 0, z0 = 0, x1 = 0, z1 = 0, h = 1;
	std::string type;
	double y = NaN();
	double rot = 0, cx = 0, cz = 0, hx = 0, hz = 0; // (rot != 0: an oriented fence)
	bool tall = false;
};
struct ParkingSpot {
	double x = 0, z = 0, rot = 0;
	std::string district;
	int edge = -1;
	bool curb = false, lot = false, driveway = false, fancy = false, police = false, rural = false;
};
struct Pool { double x0, z0, x1, z1; };
struct Container { double x, z, rot; int level, color; double y = NaN(); };
struct Collider {
	bool oriented = false;
	double minX = 0, minZ = 0, maxX = 0, maxZ = 0;
	double cx = 0, cz = 0, hx = 0, hz = 0, yaw = 0;
	double minY = 0, maxY = 0;
	std::string type;
	bool soft = false;
};
struct Block {
	int i = 0, j = 0;
	double x0 = 0, z0 = 0, x1 = 0, z1 = 0, cx = 0, cz = 0;
	std::string district, special;
	double ix0 = 0, iz0 = 0, ix1 = 0, iz1 = 0;
	std::string ground = "concrete";
	V2 corners[4];
	uint32_t seed = 0;
	bool park = false, merged = false;
	std::string superKind, superName;
	std::vector<int> nodeIds;
};
struct WalkNode {
	int id = 0;
	double x = 0, z = 0, y = NaN();
	std::vector<int> links, cross;
	int block = -1;
	std::string town;
};
struct TownArea { std::string district, name; std::vector<int> nodeIds; double x = 0, z = 0, r = 0; bool town = true; };
struct Landmark {
	double x = 0, z = 0, y = NaN();
	std::string name;
	std::map<std::string, P3> pts;      // named sub-points (door, entry, respawn, gate, top, ...)
	std::map<std::string, double> vals; // named values (rot, s, runway x0 / x1 / z, ...)
};
struct FixedVehicle { std::string type; double x, z, yaw, y, respawn; bool roof = false; };
struct Footprint { double cx = 0, cz = 0, hx = 0, hz = 0, yaw = 0, y0 = 0, yTop = 0; };
// vegetation instances: x, y, z, rotation, scale per instance
struct Vegetation { std::vector<float> pine, oak, bush, cactus, rock, deadtree, palm; };
// a walk-in shop (interiors.js): its frame (u across the front with 0 at the doorway, w into the room from the
// front wall), its layout and furniture, the colliders, and the spots the shop system and the renderer use
struct InteriorShell {
	std::string key, name;
	int building = -1;
	// the frame
	double fy = 0, f[2] = { 0, 1 }, r[2] = { -1, 0 }, ox = 0, oz = 0, W = 0, D = 0;
	double X(double u, double w) const { return ox + r[0] * u + f[0] * w; }
	double Z(double u, double w) const { return oz + r[1] * u + f[1] * w; }
	// the world bounds of a local rectangle: x0, z0, x1, z1
	void Rect(double u0, double w0, double u1, double w1, double out[4]) const;
	double YawIn() const;
	double YawR() const;
	// the layout (interiors.js LAYOUTS)
	struct Spot { double u = 0, w = 0; };
	struct Layout {
		std::string name, extra;
		double ceil = 3.4, H = 0;
		double floor[2][3] = {}, wall[3] = {}, dado[3] = {};
		struct Counter { double u0 = 0, w0 = 0, u1 = 0, w1 = 0, top[3] = {}, body[3] = {}; } counter;
		Spot clerk, service, till;
		bool clerkFacesR = false, hostile = false;
		std::vector<std::array<double, 2>> lights;
	} L;
	// the furniture the renderer dresses (kind, and its spot or extent in the frame)
	struct Furniture { std::string kind; double u = 0, w = 0, u0 = 0, w0 = 0, u1 = 0, w1 = 0, h = 0; int face = 0, i = 0; };
	std::vector<Furniture> furniture;
	std::vector<Collider> colliders;
	P3 door, center, clerk, service, till, light;
	double clerkYaw = 0;
	bool Inside(double x, double z, double pad = 0) const;
};

struct BldOpts {
	double y0 = NaN(), rot = 0, base = NaN();
	bool hasTint = false; double tint[3] = { 1, 1, 1 };
	double seed = NaN();
	std::string roof, kind;
	double floorH = 0, cell = 0;
	std::string name, sign;
	bool noCollide = false;
	ShopRef shop;
	BldOpts& Tint(double r, double g, double b) { hasTint = true; tint[0] = r; tint[1] = g; tint[2] = b; return *this; }
	BldOpts& Tint(const std::array<double, 3>& t) { return Tint(t[0], t[1], t[2]); }
};

class CityMap {
public:
	explicit CityMap(uint32_t seed = 1337);

	uint32_t seed;
	Heightfield hf;
	RoadNet roads;
	RoadInfo roadInfo;
	std::vector<Block> blockStore;         // one per grid cell (merged ones stay, unreferenced)
	std::vector<int> blocks;               // the blocks, in order (a super-block once)
	std::vector<int> cellBlocks;           // grid cell -> index into blockStore
	std::vector<Building> buildings;
	std::vector<LotSurface> lotSurfaces;
	std::vector<PadSurface> padSurfaces;
	std::vector<Prop> props;
	std::vector<Fence> fences;
	std::vector<ParkingSpot> parkingSpots;
	std::vector<Pool> pools;
	std::vector<Container> containers;
	std::vector<Collider> colliders;
	std::vector<WalkNode> walkNodes;
	std::vector<TownArea> townAreas;
	std::vector<FixedVehicle> fixedVehicles;
	std::vector<InteriorShell> interiors;
	std::vector<NcSuper> ncSupers;
	std::map<std::string, Landmark> landmarks;
	std::vector<Landmark> stations;
	Vegetation vegetation;

	static constexpr double CITY_MINX = -840, CITY_MAXX = 880, CITY_MINZ = -780, CITY_MAXZ = 635;

	const Block* BlockAt(double x, double z) const;
	const Block* GetBlock(int i, int j) const;
	int NearestX(double x) const;
	int NearestZ(double z) const;
	bool IsOnRoad(double x, double z) const;
	bool IsOnCityStreet(double x, double z, double margin = 0) const;
	std::string DistrictAt(double x, double z) const;
	std::string ZoneName(double x, double z) const;
	double TerrainHeight(double x, double z) const { return hf.Sample(x, z); }
	double GroundHeight(double x, double z) const;
	bool InCity(double x, double z) const { return CityDist(x, z) < 1; }
	double WaterLevel(double x, double z) const;
	bool IsWater(double x, double z) const { return GroundHeight(x, z) < WATER_Y - 0.3; }
	static const char* DistrictName(const std::string& key);
	static const char* DistrictColor(const std::string& key); // "#rrggbb" (the map)

	// used by the builders (and Countryside.cpp)
	int AddBuilding(const Block* b, double x0, double z0, double x1, double z1, double height, int style, const BldOpts& o = BldOpts());
	Block* BlockMut(int i, int j);

private:
	void Build();
	static int Idx(const std::vector<double>& arr, double v);
	double CityGround(double x, double z) const;
	void FillBlock(Block& b);
	void Tower(Block& b, RNG& rng, double x0, double z0, double x1, double z1, double H, int style, const std::array<double, 3>& tint);
	struct PerimOpts { double depth[2], width[2]; int floors[2]; std::vector<int> styles; std::function<std::array<double, 3>(RNG&)> tint; double gapChance = 0.15; };
	void Perimeter(Block& b, RNG& rng, const PerimOpts& o);
	void Houses(Block& b, RNG& rng);
	void SuperBlock(Block& b, RNG& rng);
	struct MansionInfo { double cx, cz, w, d; };
	MansionInfo Mansion(Block& b, RNG& rng, bool boss = false);
	void Park(Block& b, RNG& rng, const std::string& kind);
	void SpecialBlock(Block& b, RNG& rng, const std::string& sp);
	void ClearUnderFreeway();
	void StreetProps();
	void BuildSidewalkGraph();
	void RetrofitShops();
	void BuildLandmarks();
	void PlanInteriors();
	void PlanSkateparks();
};

// countryside.js
void PopulateCountryside(CityMap& map);
double FarmMask(double x, double z);
// deterministic scatter: jittered grid points inside a rect, filtered by a density callback
struct ScatterPt { double x, z, r1, r2, r3; };
std::vector<ScatterPt> Scatter(double x0, double z0, double x1, double z1, double spacing, int seed, const std::function<double(double, double)>& density);

} // namespace atg
