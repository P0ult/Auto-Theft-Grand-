// Road network (port of src/world/roadnet.js): a graph of nodes (junctions, roundabouts, merges) and edges
// (3D polylines with lane layouts). Used by the road renderer, traffic / police / mission drivers, GPS
// routing, the map and terrain shaping (roads cut and fill the heightfield; high sections become bridges).
#pragma once

#include "GenMath.h"
#include <memory>
#include <optional>
#include <unordered_map>

namespace atg {

class Heightfield;

// road types. lanes: forward, backward; off0: lateral offset of lane 0's left edge (right-positive,
// relative to the direction of travel); wL / wR: paved half widths left / right of the centreline
enum class ERoad : uint8_t { Street, Freeway, Ramp, Highway, Road, Avenue, Dirt, Rail };
struct RoadType {
	const char* name;
	int lanesF, lanesB;
	double laneW, off0, wL, wR, speed;
	int mark, cls;
	bool barrier;
};
const RoadType& RT(ERoad t);
const char* RoadTypeName(ERoad t);
constexpr double DECK_H = 1.3;   // deck thickness below the road surface
constexpr double DECK_MIN = 3.2; // road this far above the ground is a bridge / viaduct

// ------------------------------------------------------------------ polyline helpers
Line Catmull(const std::vector<V2>& ctrl, double spacing = 6);
Line Catmull(const Line& ctrl, double spacing = 6);
std::vector<double> CumLen(const Line& pts);
Line Resample(const Line& pts, double spacing = 6);
P3 PointAt(const Line& pts, const std::vector<double>& cum, double s);
V2 TangentAt(const Line& pts, int i);
Line OffsetLine(const Line& pts, double off);
struct ProjectHit { double s = 0, d = 0, lat = 0; int i = 0; };
ProjectHit Project(const Line& pts, const std::vector<double>& cum, double x, double z);
struct CrossHit { double sa, sb, x, z; };
std::optional<CrossHit> Intersect(const Line& A, const std::vector<double>& cumA, const Line& B, const std::vector<double>& cumB);
int NearestIdx(const std::vector<double>& cum, double s);
int NearestIdx(const std::vector<float>& cum, double s);

// ------------------------------------------------------------------ profile solver
struct ProfilePin { double s, y, r = NaN(); };
struct ProfileOpts {
	double window = 120, maxGrade = 0.07, maxCut = 14;
	double y0 = NaN(), y1 = NaN();
	double minY = 1.2, waterY = 5.5;
	std::vector<ProfilePin> pins;
	// returns NaN for "not fixed"
	std::function<double(double, double, double)> fixed;
};
struct Profile { std::vector<float> y; std::vector<double> cum; };
Profile SolveProfile(const Line& pts, const std::function<double(double, double)>& terrain, const ProfileOpts& o);

// ------------------------------------------------------------------ graph
enum class ENode : uint8_t { X, RB, End, Via, Split, Merge };

struct RNode {
	int id = 0;
	double x = 0, z = 0, y = 0;
	std::vector<int> e;
	ENode kind = ENode::X;
	double r = 0, rbR = 0;
	double sig = NaN(); // traffic-light phase (NaN: none)
	bool hasGrid = false; int gi = 0, gj = 0;
	std::string name;
	bool noStop = false, city = false, dead = false, ncity = false, rail = false;
	int maxCls = 0;
};
struct NodeOpts {
	ENode kind = ENode::X;
	double r = 0, rbR = 0, sig = NaN();
	bool hasGrid = false; int gi = 0, gj = 0;
	std::string name;
	bool noStop = false, rail = false;
};

struct GridRef { int i = 0, j = 0, di = 0, dj = 0; };
struct RailCrossZone { double s, halfW; };

struct REdge {
	int id = 0, a = 0, b = 0;
	ERoad type = ERoad::Road;
	const RoadType* T = nullptr;
	std::vector<float> p;   // x, y, z per point
	std::vector<float> cum; // arc length per point
	double len = 0;
	int n = 0;
	int lanesF = 0, lanesB = 0;
	bool render = true;
	bool hasGrid = false; GridRef grid;
	std::string name;
	std::vector<uint8_t> deck;
	double speed = 0, wL = 0, wR = 0;
	int8_t barrierL = -1, barrierR = -1; // -1: unset (use the type's), 0 / 1
	double under = 0;
	bool city = false, base = false, removed = false, ncity = false, ext = false, rail = false, loop = false;
	double noBarrierA = 0, noBarrierB = 0;
	double trimA = NaN(), trimB = NaN();
	double walk = 0;
	std::vector<RailCrossZone> crossings;

	float X(int i) const { return p[i * 3]; }
	float Y(int i) const { return p[i * 3 + 1]; }
	float Z(int i) const { return p[i * 3 + 2]; }
	bool BarrierL() const { return barrierL < 0 ? T->barrier : barrierL != 0; }
	bool BarrierR() const { return barrierR < 0 ? T->barrier : barrierR != 0; }
};
struct EdgeOpts {
	bool hasLanes = false; int lanesF = 0, lanesB = 0;
	bool render = true;
	bool hasGrid = false; GridRef grid;
	std::string name;
	double speed = NaN(), wL = NaN(), wR = NaN();
	int8_t barrierL = -1, barrierR = -1;
	double under = 0;
	bool city = false, base = false;
	double noBarrierA = 0, noBarrierB = 0;
	double trimA = NaN(), trimB = NaN();
};

struct EdgeHit { int e = -1; double s = 0, d = 0, lat = 0, y = 0, t = 0; int i = 0; bool valid() const { return e >= 0; } };
struct RoadHit { int e = -1; int i = 0; double t = 0, y = 0; bool deck = false; bool valid() const { return e >= 0; } };
// a point on an edge's centreline: x, y, z and the unit tangent
struct EdgePoint { double x = 0, y = 0, z = 0, tx = 0, tz = 0; };
struct RouteStep { int node, edge; };
struct Route { EdgeHit start, goal; std::vector<RouteStep> seq; };

class RoadNet {
public:
	std::vector<RNode> nodes;
	std::vector<REdge> edges;
	double cell = 64;
	struct Seg { int e, i0, i1; };
	std::unordered_map<int64_t, std::vector<Seg>> grid;

	RNode& AddNode(double x, double z, double y, const NodeOpts& o = NodeOpts());
	// pts: dense (x, z, y) points; endpoints may be offset from the node positions
	REdge& AddEdge(int a, int b, const Line& pts, ERoad type, const EdgeOpts& o = EdgeOpts());
	void Index(const REdge& e);
	void RemoveEdge(int eid);

	std::vector<int> EdgesIn(double x0, double z0, double x1, double z1) const;
	EdgeHit Closest(double x, double z, const std::function<bool(const REdge&)>& filter = nullptr, double maxD = 1e9, double y = NaN()) const;
	RoadHit OnRoad(double x, double z, double margin = 0) const;
	const RNode& Other(const REdge& e, int nodeId) const { return e.a == nodeId ? nodes[e.b] : nodes[e.a]; }
	EdgePoint At(const REdge& e, double s) const;
	double LaneOffset(const REdge& e, int dir, int lane) const;
	double Clearance(const RNode& n) const;
	Line LanePath(const REdge& e, int dir, int lane, double trimStart = NaN(), double trimEnd = NaN()) const;
	std::optional<Route> FindRoute(double fromX, double fromZ, double toX, double toZ, const std::function<bool(const REdge&)>& filter = nullptr, bool anyDir = false) const;
	std::vector<V2> RoutePolyline(const Route* r, double fromX, double fromZ, double toX, double toZ) const;

	static int64_t Key(int64_t gx, int64_t gz) { return gx * 100003 + gz; }
};

// Roads cut into hills and sit on embankments; where a road runs high above the ground it becomes a
// bridge / viaduct (deck flag). groundAt(x, z) returns the non-heightfield ground (the city) or NaN.
void ShapeTerrain(RoadNet& net, Heightfield& hf, const std::function<double(double, double)>& groundAt);

// ------------------------------------------------------------------ planned routes (roadlayout.js ROUTES)
struct RouteEnd {
	enum Kind { Null, Num, Key } kind = Null;
	double y = 0;
	std::string key;
	static RouteEnd None() { return RouteEnd(); }
	static RouteEnd AtY(double v) { RouteEnd e; e.kind = Num; e.y = v; return e; }
	static RouteEnd Pad(const std::string& k) { RouteEnd e; e.kind = Key; e.key = k; return e; }
};
struct RouteDef {
	std::string key, type;
	RouteEnd ends[2];
	std::vector<V2> ctrl;
	double grade = NaN(), width = NaN();
};

} // namespace atg
