// The actual road network of the world (port of src/world/roadlayout.js, northcity.js's roads and
// railway.js): the Los Soles street grid (with a few roundabouts and merged super-blocks), the elevated Sol
// Freeway with diamond interchanges, rural highways, winding hill roads, town streets with roundabouts,
// dirt tracks, San Aurelio's rings and avenues, the Sol Line railway and the base access road.
#pragma once

#include "RoadNet.h"
#include "WorldGen.h"
#include <deque>
#include <map>
#include <set>

namespace atg {

// Los Soles road centrelines (non-uniform spacing for a more organic grid)
extern const std::vector<double> XS;
extern const std::vector<double> ZS;
constexpr double ROAD_W = 20, HALF_ROAD = 10, SIDEWALK_W = 4, CURB_H = 0.15, PARK_OFF = 8.5, WATER_Y = -0.6;
inline double CellPhase(int i, int j) { return ((i * 7 + j * 13) % 17) * 2.0; }

// grid segments removed to form super-blocks ("h:i,j" = E-W street ZS[j] between XS[i] and XS[i+1];
// "v:i,j" = N-S street XS[i] between ZS[j] and ZS[j+1])
extern const std::set<std::string> REMOVED_SEGMENTS;
struct SuperBlockDef { const char* key; int cells[2][2]; const char* kind; const char* name; };
extern const std::vector<SuperBlockDef> SUPERBLOCKS;
extern const std::vector<std::pair<int, int>> CITY_ROUNDABOUTS;
struct FreewayDef { double y, carriage, adj; };
constexpr FreewayDef FW{ 9, 6.7, 15.4 };

// planned routes (control points) in the JavaScript object's order
const std::vector<RouteDef>& ROUTES();

// a road built by the layout: its dense profile and the edges / nodes it became
struct RoadBuilt {
	Line pts;
	std::vector<double> cum;
	std::vector<int> edges;
	std::vector<int> nodes;
};
struct TownRoads { int center = -1; std::vector<RoadBuilt*> roads; };

struct RailStation { std::string key, name; double s = 0, y = 0, x = 0, z = 0, tx = 0, tz = 0, yaw = 0; };
struct RailCrossing { double s, x, z, y, roadY; std::string kind; int edge; double halfW; std::string name; };
struct RailLoop { double s0 = 0, s1 = 0, taper = 55, off = -4.6, m0 = 0, m1 = 0; int edge = -1; };
struct RailInfo {
	Line pts; // x, z, y
	std::vector<double> cum;
	double length = 0;
	std::vector<RailStation> stations;
	std::vector<RailCrossing> crossings;
	std::vector<int> edges, nodes;
	RailLoop loop;
};
struct NCityInfo {
	int center = -1;
	std::vector<int> edges;
	std::map<std::string, int> ends;
	RoadBuilt *hwy = nullptr, *ridge = nullptr, *timber = nullptr, *harborN = nullptr, *harborS = nullptr;
};
struct Interchange { std::string name; double x, z; };

struct RoadInfo {
	std::deque<RoadBuilt> store; // (stable addresses)
	std::vector<Interchange> interchanges;
	std::vector<std::pair<std::string, TownRoads>> towns; // insertion order
	std::vector<int> rbs;
	std::set<std::string> cityRb;
	std::vector<int> freewayEB, freewayWB;
	Line fwPts; std::vector<double> fwCum; std::vector<float> fwY;
	bool hasRail = false; RailInfo rail;
	bool hasNcity = false; NCityInfo ncity;
	int baseGate = -1; RoadBuilt* baseRoad = nullptr; double baseY = 0;
	TownRoads& Town(const std::string& key);
};

// ---- San Aurelio (northcity.js)
struct NcMainDef { double th, A, ph; const char* name; const char* out; };
extern const std::vector<NcMainDef> NC_MAIN;
double NcWalkWidth(ERoad t);
double MainTh(int k, double r);
V2 NcSpokeEnd(const std::string& key);
struct NcSuper { double x, z, th, r; std::string kind; double span, depth; };
std::vector<NcSuper> NcSuperblocks();
struct NcDistrict { const char* key; const char* name; };
// nullptr key: not in the city
NcDistrict NcDistrictAt(double x, double z);
std::vector<RouteDef> NcRoutes();
NCityInfo& BuildNorthCity(RoadNet& net, RoadInfo& info);

// ---- the Sol Line (railway.js)
struct RailDef { double gauge, offset, grade, cruise; };
constexpr RailDef RAIL{ 1.435, 150, 0.026, 24 };
RailInfo& BuildRailway(RoadNet& net, RoadInfo& info, const Line& fwC, const std::vector<double>& fwCum, const std::function<double(double, double)>& terrain, Heightfield& hf);
double LoopOffsetAt(const RailLoop& loop, double s);
// position along the line: x, y, z, tx, tz, grade
struct RailPoint { double x, y, z, tx, tz, grade; };
RailPoint RailAt(const RailInfo& rail, double s);
RailPoint RailAtTrack(const RailInfo& rail, double s, int track);

// ---- skateparks (skatepark.js)
struct SkateparkDef { const char* key; const char* name; double x, z, yaw, hx, hz, y; };
extern const std::vector<SkateparkDef> SKATEPARKS;
std::vector<Pad> SkateparkPads();

void BuildRoadNetwork(Heightfield& hf, RoadNet& net, RoadInfo& info);

} // namespace atg
