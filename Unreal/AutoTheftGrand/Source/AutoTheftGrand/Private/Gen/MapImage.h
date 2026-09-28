// The map used by the radar and the full-screen map (port of src/ui/mapimage.js): a whole-state layer
// (terrain, water, fields, highways, towns, the base) and a sharper Los Soles layer drawn over it. Drawn by
// a tiny software rasteriser into RGBA buffers; the Unreal side turns them into textures.
#pragma once

#include "CityMap.h"

namespace atg {

struct MapLayer {
	int w = 0, h = 0;
	std::vector<uint8_t> rgba; // row 0 = the north edge (minZ)
	double minX = 0, minZ = 0, maxX = 0, maxZ = 0;
};
struct MapLabel { std::string name; double x, z; bool big; };
struct MapImages { MapLayer world, city; std::vector<MapLabel> labels; };

MapImages BuildMapImages(const CityMap& map, int worldSize = 2048, int citySize = 2048);

} // namespace atg
