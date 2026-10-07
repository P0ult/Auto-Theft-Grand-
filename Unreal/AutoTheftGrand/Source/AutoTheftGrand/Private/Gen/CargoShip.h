// The MV Pacific Star at Port Morena (port of src/world/cargoship.js): a container ship you can board. A lofted
// hull, a walkable main deck with container stacks in five bays, a raised forecastle up a ramp, and a stepped
// superstructure whose balconies zig-zag up by ramps to the bridge (the captain's safe is in there). A gangway
// climbs from the quay. Geometry plus the collision the JavaScript adds, in the order it adds it (the game
// applies it: this layer knows no collision world). Identical to the JavaScript: Tools/dumpship.mjs against
// Tools/shiptest.cpp.
#pragma once

#include "Loft.h"

#include <string>
#include <vector>

namespace atg {

// SHIP: where it lies and its layout (local x across, local z from the bow (-) to the stern (+))
struct ShipSpec {
	const char* name;
	double x, z, L, W, deckY;
	double stern, bow, fc;           // local z of the transom, the stem and the forecastle break
	double bays[5]; double bayHalf;
	double levels[4];                // main deck, A, B, bridge
	struct Gangway { double xFoot, x, z0, z1, land[2]; } gangway;
};
extern const ShipSpec SHIP;

// half beam at local z (a long parallel body, a fine bow)
double ShipHalfBeam(double lz);

// one collision call, as buildCargoShip makes it: box (min / max, type), obox (cx, cz, hx, hz, yaw, minY,
// maxY, type), deck (a, b, hl, hr), circle (x, z, r, h, y0)
struct ShipCollider {
	std::string kind, type;
	double minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
	double cx = 0, cz = 0, hx = 0, hz = 0, yaw = 0;
	double ax = 0, az = 0, ay = 0, bx = 0, bz = 0, by = 0, hl = 0, hr = 0;
	double x = 0, z = 0, r = 0, h = 0, y0 = 0;
};
struct ShipPoint { double x = 0, y = 0, z = 0; };
struct ShipStack { double lx, lz; int n; double bay; int row; };

struct CargoShip {
	MeshBuf hull, deck, steel, white, cont, glass, lamp; // (vertex coloured: Part's layout)
	std::vector<ShipCollider> colliders;
	ShipPoint safe, bridgeDoor, gangwayFoot, gangwayTop;
	std::vector<Pt3> route;          // (local x, local z, y) up the ramps to the bridge
	std::vector<ShipStack> stacks;
	// on board: inside the hull's footprint and up on a deck
	static bool Aboard(double x, double y, double z);
	static void ToWorld(double lx, double lz, double& x, double& z) { x = SHIP.x + lx; z = SHIP.z + lz; }
};

CargoShip BuildCargoShip();

} // namespace atg
