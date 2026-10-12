// The billboards on the city's flat roofs and the shops' name signs (port of city.js _billboards and _signs):
// where they go, the billboards' frames and faces (the faces' uvs pick an ad from textures.js adAtlas, 2 x 4,
// with three.js's flipped v) and their collision boxes. The pictures themselves are painted by the renderer.
// Identical to the JavaScript: Tools/dumpsigns.mjs against Tools/signstest.cpp.
#pragma once

#include "CityMap.h"
#include "MeshBuf.h"

namespace atg {

struct CityBillboards {
	MeshBuf frame;   // (vertex coloured: the city's detail material)
	MeshBuf faces;   // (uv0 into the ad atlas)
	struct Box { double minX, maxX, minZ, maxZ, minY, maxY; };
	std::vector<Box> colliders;
};
CityBillboards BuildBillboards(const CityMap& map);

// a name sign on a building's face: its text, centre, size and the way it faces (a yaw: it faces its local +z)
struct CitySign { std::string text; double x, y, z, w, h, yaw; };
std::vector<CitySign> PlaceSigns(const CityMap& map);

} // namespace atg
