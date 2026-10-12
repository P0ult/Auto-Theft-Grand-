// The hand-placed landmarks (port of src/world/landmarks.js): the Santa Luz pier (its deck, posts, rails, food
// booths and lamps) with the Ferris wheel, the Vistawood sign, the dock cranes; and the aviation beacons on tall
// roofs and on the cranes (city.js _beacon). Geometry, spots and the collision calls in the JavaScript's order;
// the renderer turns the wheel and blinks the lights. Identical to the JavaScript: Tools/dumplandmarks.mjs
// against Tools/landmarkstest.cpp.
#pragma once

#include "CityMap.h"
#include "MeshBuf.h"

#include <string>
#include <vector>

namespace atg {

struct LmPoint { double x = 0, y = 0, z = 0; };

struct LandmarkCollider {
	std::string kind, type; // kind: box or circle
	bool soft = false;
	double minX = 0, maxX = 0, minZ = 0, maxZ = 0, minY = 0, maxY = 0;
	double x = 0, z = 0, r = 0, h = 0;
};

struct Landmarks {
	// the pier: the deck (its own planked material), posts / rails / booths (vertex coloured), the lamp poles
	// (vertex coloured) and the lamp globes (spheres of radius 0.28 at these points)
	MeshBuf pierDeck, pierPosts, lampPoles;
	std::vector<LmPoint> lamps;
	// the Ferris wheel: the rim, spokes and hub (around the hub, turned about z), the neon bulbs (likewise,
	// vertex coloured), one gondola (hanging from a point on the rim, vertex coloured white) and its sixteen
	// colours, the A-frame (in the world)
	double hubX = 0, hubY = 0, hubZ = 0, wheelR = 20;
	MeshBuf wheel, neon, gondola, support;
	double gondolaColor[16][3] = {};      // (linear rgb: setHSL(k / 16, 0.7, 0.5))
	// the Vistawood sign: a letter 13 x 15 at each spot (tilted back 0.12), and the scaffolding
	struct Letter { char ch; double x, y, z; };
	std::vector<Letter> letters;
	MeshBuf scaffold;
	// the dock cranes (one red steel material)
	MeshBuf cranes;
	std::vector<LmPoint> beacons;              // (the cranes' aviation lights)
	std::vector<LandmarkCollider> colliders;
};

Landmarks BuildLandmarks(const CityMap& map);
// city.js _roofDetails' beacons (antennas, helipads, spires, tall flat roofs), in the buildings' order
std::vector<LmPoint> BuildingBeacons(const CityMap& map);

} // namespace atg
