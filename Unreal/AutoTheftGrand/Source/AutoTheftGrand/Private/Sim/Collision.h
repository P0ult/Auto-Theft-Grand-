// The static collision world (port of src/world/collision.js): a spatial hash of axis-aligned boxes
// (buildings, fences, containers), oriented boxes (barriers, pillars, rotated buildings), circles (props,
// trees) and road decks (bridges and viaducts: drivable sloped surfaces that are also walls from the side).
// Characters resolve against it as circles, vehicles as oriented boxes; rays, floor and surface heights and
// line of sight come from it too. Nothing here uses Unreal's physics.
#pragma once

#include "Core.h"
#include "CityMap.h"

namespace atg {

struct ColPrim;

struct CollObj {
	enum Kind : uint8_t { Box, OBox, Circle, Deck } kind = Box;
	// box / obox bounds (an obox's are its world AABB)
	double minX = 0, minZ = 0, maxX = 0, maxZ = 0, minY = 0, maxY = 0;
	// obox
	double cx = 0, cz = 0, hx = 0, hz = 0, yaw = 0, s = 0, c = 1;
	// circle
	double x = 0, z = 0, r = 0, h = NaN(), y0 = NaN();
	int prop = -1; bool breakable = false, broken = false, gone = false; // (gone: taken for good, never restored)
	// deck
	double ax = 0, az = 0, ay = 0, bx = 0, bz = 0, by = 0, hl = 0, hr = 0, len = 1, dx = 0, dz = 0;
	int edge = -1; bool pavement = false, skate = false;
	std::string type;
	bool soft = false, low = false, rayOnly = false, removed = false;
	// bookkeeping
	std::vector<int64_t> cells;
	uint32_t stamp = 0;
	double top() const { return IsSet(h) && h != 0 ? h : 3; }   // circles: (o.h || 3)
};

struct Contact { double nx, nz, depth, px, pz; CollObj* obj; };
struct RayHit { double t, x, y, z, nx, ny, nz; CollObj* obj; bool ground; };
struct RayOpts { bool ignoreProps = false, ignoreSoft = false; };

class CollisionWorld {
public:
	explicit CollisionWorld(const CityMap& map);
	const CityMap& map;

	CollObj* addBox(double minX, double minY, double minZ, double maxX, double maxY, double maxZ, const std::string& type, bool soft = false);
	CollObj* addOBox(double cx, double cz, double hx, double hz, double yaw, double minY, double maxY, const std::string& type, bool soft = false, bool low = false);
	CollObj* addCircle(double x, double z, double r, double h, double y0, const std::string& type, int prop = -1, bool breakable = false);
	CollObj* addDeck(double ax, double az, double ay, double bx, double bz, double by, double hl, double hr, int edge = -1, bool pavement = false, bool skate = false);
	CollObj* add(const ColPrim& p);
	void remove(CollObj* o);

	std::vector<CollObj*>& query(double minX, double minZ, double maxX, double maxZ, std::vector<CollObj*>& out);
	// a circle (character) against static geometry at height range [y, y + h]: where it ends up
	struct CircleRes { double x, z; CollObj* hit; };
	CircleRes resolveCircle(double x, double z, double r, double y = 0, double h = 1.8);
	// an oriented box (vehicle) against statics
	std::vector<Contact>& obbContacts(double cx, double cz, double yaw, double hx, double hz, double y, std::vector<Contact>& out);
	bool raycast(double ox, double oy, double oz, double dx, double dy, double dz, double maxDist, RayHit& hit, const RayOpts& opts = RayOpts());
	// highest walkable surface under (x, z) that is not above y + step
	double floorHeight(double x, double z, double y, double step = 0.55);
	// road surface for vehicles: terrain / curbs, or a deck the vehicle is on
	double surfaceHeight(double x, double z, double y, double step = 1.4);
	bool lineOfSight(double ax, double ay, double az, double bx, double by, double bz);

	double deckAt(const CollObj* d, double x, double z) const;
	double deckAtClamped(const CollObj* d, double t) const;

	const std::vector<std::unique_ptr<CollObj>>& all() const { return objects; }

private:
	std::vector<std::unique_ptr<CollObj>> objects;
	std::unordered_map<int64_t, std::vector<CollObj*>> cells;
	uint32_t stampN = 0;
	std::vector<CollObj*> tmp;
	CollObj* insert(std::unique_ptr<CollObj> o, double minX, double minZ, double maxX, double maxZ);
};

// 2D separating-axis test between a vehicle's box (axes f, r; half extents hx along r, hz along f) and a box
// with axes a1 (half bhx) and a2 (half bhz): the push-out normal for the vehicle, the depth and a contact point
bool SatOBB(double cx, double cz, double fx, double fz, double rx, double rz, double hx, double hz,
	double bx, double bz, double a1x, double a1z, double bhx, double bhz, double a2x, double a2z, Contact& out);

} // namespace atg
